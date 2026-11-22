#include "ui/page/page_stack.hpp"

#include <cstdio>
#include <utility>

#include "input/input_state.hpp"

using namespace input;

namespace {

void logRefusal(const char* what) {
#ifdef DEBUG
    fprintf(stderr, "[PageStack] %s\n", what);
#else
    (void)what;
#endif
}

}  // namespace

bool PageStack::RawEventFanOut::onRawEvent(const RawEvent& event) {
    stack.manager.onRawEvent(event);
    if (stack.detector) stack.detector->onRawEvent(event);
    return false;
}

PageStack::PageStack(PlaybackControl& playback)
    : commonButtonHandler(playback), manager({}), rawInput(*this) {
    commonButtonHandler.setHelpRequestedCallback([this] {
        UiPageImplBase* top = getTop();
        if (top && top->getHelpId() != HELP_NONE && openHelp) openHelp(top->getHelpId());
    });
    commonButtonHandler.setQuitRequestedCallback([this] {
        if (openQuit) openQuit();
    });
}

PageStack::~PageStack() {
    closeAll();
    snapshot.clear();
}

void PageStack::setDialogOpeners(std::function<void(HelpId)> help, std::function<void()> quit) {
    openHelp = std::move(help);
    openQuit = std::move(quit);
}

void PageStack::start(std::unique_ptr<UiPageImplBase> page, const UiScreenInfo& newScreen) {
    screen = &newScreen;
    push(std::move(page));
}

void PageStack::setScreen(const UiScreenInfo& newScreen) {
    screen = &newScreen;
    for (PageEntry& entry : entries) entry.context->setScreen(newScreen);
    snapshotStale = true;
}

void PageStack::startFrame(uint32_t newFrameTime) {
    frameTime = newFrameTime;
    // A covered page keeps the times of its last frame as the top
    if (entries.empty()) return;
    PageEntry& top = entries.back();
    top.clock.onFrameStart(frameTime);
    top.context->startFrame(frameTime, top.clock.getActiveTime());
}

void PageStack::onActivityGap() {
    if (!entries.empty()) entries.back().clock.onActivityGap();
}

void PageStack::onWakeFromSleep() {
    UiPageImplBase* top = getTop();
    if (!top) return;
    top->pause();
    onActivityGap();
    top->resume();
}

void PageStack::beginPoll() {
    manager.beginPoll();
}

void PageStack::tickInput() {
    if (detector) detector->onTick(frameTime);
}

void PageStack::tickPages() {
    // A covered page is paused, thus only the top page runs
    UiPageImplBase* top = getTop();
    if (!top) return;
    top->onTick();
    entries.back().context->runFrameTasks();
}

void PageStack::layout(UiCompositor& compositor) {
    if (entries.empty()) return;
    const bool dialogOnTop = isDialogOnTop();
    // A change composes the whole screen
    if (compositor.isSurfaceOnly() != dialogOnTop) compositor.setSurfaceOnly(dialogOnTop);
    if (!dialogOnTop) {
        snapshot.clear();
    } else if (snapshotStale) {
        takeSnapshot(compositor);
    }

    PageEntry& top        = entries.back();
    UiLayoutContext& root = compositor.beginLayout(*top.context);
    top.context->startLayout();
    if (dialogOnTop) root.layoutChild(snapshot, root.getMaxRect());
    top.page->layout(root);
    top.context->finishLayout();

    // A view that left the layout loses its tasks. The layout of a snapshot does not count, thus
    // the views of a covered page keep their tasks.
    std::vector<TaskOwner> owners;
    root.collectViewIds(owners);
    owners.push_back(top.page->id);
    top.context->updateActiveOwners(owners);
}

void PageStack::takeSnapshot(UiCompositor& compositor) {
    snapshotStale = false;
    // A dialog cannot open a page, thus only the bottom page can have no page under it
    if (entries.size() < 2) {
        snapshot.clear();
        return;
    }
    PageEntry& under = entries[entries.size() - 2];
    under.page->layout(compositor.beginLayout(*under.context));
    snapshot.setImage(compositor.drawSnapshot(), dimColor);
}

void PageStack::endFrame() {
    // A command that a result callback gives goes into the queue, and applies after it
    while (pending) {
        Command command = std::move(*pending);
        pending.reset();
        frameHasCommand = false;
        apply(std::move(command));
    }
    frameHasCommand = false;
}

void PageStack::closeAll() {
    while (!entries.empty()) {
        entries.back().page->onExit();
        entries.pop_back();
    }
    pending.reset();
    legacyModule = nullptr;
}

bool PageStack::isDialogOnTop() const {
    return !entries.empty() && entries.back().page->getKind() == PageKind::Dialog;
}

bool PageStack::takeQuitRequest() {
    return std::exchange(quitRequested, false);
}

std::function<ModuleExitReason(DisplayContext*)> PageStack::takeLegacyModule() {
    return std::exchange(legacyModule, nullptr);
}

void PageStack::completeLegacyModule() {
    pop(nullptr);
}

void PageStack::exit() {
    if (!acceptCommand()) return;
    pending = Command{Command::Kind::Exit, nullptr, nullptr};
}

void PageStack::runLegacyModule(std::function<ModuleExitReason(DisplayContext*)> module) {
    legacyModule = std::move(module);
}

void PageStack::queueOpen(std::unique_ptr<UiPageImplBase> page) {
    if (isDialogOnTop()) {
        logRefusal("a dialog cannot open a page");
        return;
    }
    if (!acceptCommand()) return;
    pending = Command{Command::Kind::Open, std::move(page), nullptr};
}

void PageStack::queueClose(UiPageImplBase& page, std::function<void()> deliver) {
    if (&page != getTop()) {
        logRefusal("only the top page can close");
        return;
    }
    if (!acceptCommand()) return;
    pending = Command{Command::Kind::Close, nullptr, std::move(deliver)};
}

bool PageStack::acceptCommand() {
    if (entries.empty()) return false;
    if (frameHasCommand) {
        logRefusal("the frame already has a navigation command");
        return false;
    }
    frameHasCommand = true;
    // The page gets no more events of this frame and of this gesture
    entries.back().page->onRawEvent({frameTime, event::ButtonReset{}});
    manager.discardGesture(frameTime);
    return true;
}

void PageStack::apply(Command command) {
    switch (command.kind) {
        case Command::Kind::Open: {
            const bool fullScreen = command.page->getKind() == PageKind::FullScreen;
            entries.back().page->pause();
            onActivityGap();
            push(std::move(command.page));
            if (fullScreen && onScreenChange) onScreenChange();
            break;
        }
        case Command::Kind::Close: {
            const bool fullScreen = entries.back().page->getKind() == PageKind::FullScreen;
            pop(std::move(command.deliver));
            if (fullScreen && !entries.empty() && onScreenChange) onScreenChange();
            break;
        }
        case Command::Kind::Exit:
            closeAll();
            quitRequested = true;
            break;
    }
}

void PageStack::push(std::unique_ptr<UiPageImplBase> page) {
    UiPageImplBase& added = *page;
    auto context          = PageContext::create(*screen);
    // A new page starts at the elapsed time of the frame
    context->startFrame(frameTime, 0);
    added.navigator = this;
    added.context   = context.get();
    added.scheduler.setContext(*context);
    entries.push_back(PageEntry{
        .context = std::move(context),
        .clock   = {},
        .page    = std::move(page),
    });
    added.onEnter();
    connectTop();
}

void PageStack::pop(std::function<void()> deliver) {
    if (entries.empty()) return;
    entries.back().page->onExit();
    entries.pop_back();
    if (deliver) deliver();
    if (entries.empty()) return;
    entries.back().page->resume();
    connectTop();
    // The status group of the page did not change while it was covered
    input::InputState::getInstance().setStatusChanged();
}

void PageStack::connectTop() {
    snapshotStale       = true;
    UiPageImplBase& top = *entries.back().page;
    manager.setChain({&top, &commonButtonHandler});
    std::vector<ButtonRequest> requests;
    const auto globalRequests = CommonButtonHandler::getButtonRequests();
    requests.assign(globalRequests.begin(), globalRequests.end());
    const auto pageRequests = top.getButtonRequests();
    requests.insert(requests.end(), pageRequests.begin(), pageRequests.end());
    detector = std::make_unique<ButtonDetector>(manager, requests);
}
