#include "ui/page_stack.hpp"

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
}

void PageStack::setDialogOpeners(std::function<void(HelpId)> help, std::function<void()> quit) {
    openHelp = std::move(help);
    openQuit = std::move(quit);
}

void PageStack::start(std::unique_ptr<UiPageImplBase> page, UiContext& newContext) {
    context = &newContext;
    push(std::move(page), 0);
}

void PageStack::setContext(UiContext& newContext) {
    context = &newContext;
    for (const auto& page : pages) page->setContext(newContext);
    snapshotStale = true;
}

void PageStack::beginPoll() {
    manager.beginPoll();
}

void PageStack::tickInput(uint32_t now) {
    frameTime = now;
    if (detector) detector->onTick(now);
}

void PageStack::tickTop(uint32_t now) {
    if (UiPageImplBase* top = getTop()) top->onTick(now);
}

void PageStack::layout(UiCompositor& compositor) {
    if (pages.empty()) return;
    const bool dialogOnTop = isDialogOnTop();
    // A change composes the whole screen
    if (compositor.isSurfaceOnly() != dialogOnTop) compositor.setSurfaceOnly(dialogOnTop);
    if (!dialogOnTop) {
        snapshot.clear();
    } else if (snapshotStale) {
        takeSnapshot(compositor);
    }

    UiLayoutContext& root = compositor.beginLayout();
    if (dialogOnTop) root.layoutChild(snapshot, root.getMaxRect());
    pages.back()->layout(root);
}

void PageStack::takeSnapshot(UiCompositor& compositor) {
    snapshotStale = false;
    // A dialog cannot open a page, thus only the bottom page can have no page under it
    if (pages.size() < 2) {
        snapshot.clear();
        return;
    }
    pages[pages.size() - 2]->layout(compositor.beginLayout());
    snapshot.setImage(compositor.drawSnapshot(), dimColor);
}

void PageStack::endFrame(uint32_t now) {
    // A command that a result callback gives goes into the queue, and applies after it
    while (pending) {
        Command command = std::move(*pending);
        pending.reset();
        frameHasCommand = false;
        apply(std::move(command), now);
    }
    frameHasCommand = false;
}

void PageStack::closeAll() {
    while (!pages.empty()) {
        pages.back()->onExit();
        pages.pop_back();
    }
    pending.reset();
    legacyModule = nullptr;
}

bool PageStack::isDialogOnTop() const {
    return !pages.empty() && pages.back()->getKind() == PageKind::Dialog;
}

bool PageStack::takeQuitRequest() {
    return std::exchange(quitRequested, false);
}

std::function<ModuleExitReason(DisplayContext*)> PageStack::takeLegacyModule() {
    return std::exchange(legacyModule, nullptr);
}

void PageStack::completeLegacyModule(uint32_t now) {
    pop(now, nullptr);
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
    if (pages.empty()) return false;
    if (frameHasCommand) {
        logRefusal("the frame already has a navigation command");
        return false;
    }
    frameHasCommand = true;
    // The page gets no more events of this frame and of this gesture
    pages.back()->onRawEvent({frameTime, event::ButtonReset{}});
    manager.discardGesture(frameTime);
    return true;
}

void PageStack::apply(Command command, uint32_t now) {
    switch (command.kind) {
        case Command::Kind::Open:
            pages.back()->pause(now);
            push(std::move(command.page), now);
            break;
        case Command::Kind::Close:
            pop(now, std::move(command.deliver));
            break;
        case Command::Kind::Exit:
            closeAll();
            quitRequested = true;
            break;
    }
}

void PageStack::push(std::unique_ptr<UiPageImplBase> page, uint32_t) {
    UiPageImplBase& added = *page;
    added.navigator       = this;
    pages.push_back(std::move(page));
    if (context) added.setContext(*context);
    added.onEnter();
    connectTop();
}

void PageStack::pop(uint32_t now, std::function<void()> deliver) {
    if (pages.empty()) return;
    pages.back()->onExit();
    pages.pop_back();
    if (deliver) deliver();
    if (pages.empty()) return;
    pages.back()->resume(now);
    connectTop();
    // The status group of the page did not change while it was covered
    input::InputState::getInstance().setStatusChanged();
}

void PageStack::connectTop() {
    snapshotStale       = true;
    UiPageImplBase& top = *pages.back();
    manager.setChain({&top, &commonButtonHandler});
    std::vector<ButtonRequest> requests;
    const auto globalRequests = CommonButtonHandler::getButtonRequests();
    requests.assign(globalRequests.begin(), globalRequests.end());
    const auto pageRequests = top.getButtonRequests();
    requests.insert(requests.end(), pageRequests.begin(), pageRequests.end());
    detector = std::make_unique<ButtonDetector>(manager, requests);
}
