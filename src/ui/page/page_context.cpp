#include "ui/page/page_context.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace {

// One count for the whole app, thus an id is never given again, also by another page
TaskId lastTaskId = 0;

class PageContextImpl final : public PageContext {
public:
    explicit PageContextImpl(const UiScreenInfo& screen) : screen(&screen) {}

    ~PageContextImpl() override;

    PageContextImpl(const PageContextImpl&)            = delete;
    PageContextImpl& operator=(const PageContextImpl&) = delete;

    void setScreen(const UiScreenInfo& newScreen) override {
        screen = &newScreen;
        screenCount++;
    }
    void startFrame(uint32_t newElapsedTime, uint32_t newActiveTime) override;
    void runFrameTasks() override;
    void updateActiveOwners(const std::vector<TaskOwner>& owners) override;
    void startLayout() override { layoutNumber++; }
    void finishLayout() override;

    SDL_PixelFormat* getPixelFormat() const override { return screen->getPixelFormat(); }
    UiSize getScreenSize() const override { return screen->getScreenSize(); }
    uint32_t getElapsedTime() const override { return elapsedTime; }
    uint32_t getActiveTime() const override { return activeTime; }

    TaskId post(TaskOwner owner, std::function<void()> task, uint32_t delayMs) override;
    TaskId postRepeating(TaskOwner owner, std::function<void()> task, uint32_t periodMs) override;
    TaskId runEachFrame(TaskOwner owner, std::function<void()> callback) override;
    void cancelTask(TaskId id) override;
    bool isTaskPending(TaskId id) const override;
    void cancelOwnerTasks(TaskOwner owner) override;

protected:
    void addView(UiView& view) override { views.push_back(&view); }
    void removeView(UiView& view) override { std::erase(views, &view); }
    uint32_t getScreenCount() const override { return screenCount; }
    uint32_t getLayoutNumber() const override { return layoutNumber; }

private:
    struct Task {
        TaskId id;
        TaskOwner owner;
        std::function<void()> run;
        uint32_t dueTime;
        uint32_t periodMs;
        bool eachFrame;
        /** The task was added before the start of the frame */
        bool ready;
        bool done;
    };

    TaskId add(TaskOwner owner, std::function<void()> run, uint32_t delayMs, uint32_t periodMs, bool eachFrame);
    Task* find(TaskId id) const;
    void removeDone();

    const UiScreenInfo* screen;
    uint32_t elapsedTime = 0;
    uint32_t activeTime  = 0;
    std::vector<std::unique_ptr<Task>> tasks;
    bool running = false;
    /** The views that attached to this context */
    std::vector<UiView*> views;
    uint32_t screenCount  = 0;
    uint32_t layoutNumber = 0;
};

PageContextImpl::~PageContextImpl() {
    // A view that outlives the context, such as the snapshot of the stack, keeps no pointer to it
    while (!views.empty()) detachView(*views.back());
}

void PageContextImpl::finishLayout() {
    // A detach removes the view from the list, thus the loop reads a copy
    const std::vector<UiView*> attached = views;
    for (UiView* view : attached) {
        if (!isLaidOut(*view, layoutNumber)) detachView(*view);
    }
}

void PageContextImpl::cancelOwnerTasks(TaskOwner owner) {
    for (const auto& task : tasks) {
        if (task->owner == owner) task->done = true;
    }
    if (!running) removeDone();
}

TaskId PageContextImpl::post(TaskOwner owner, std::function<void()> task, uint32_t delayMs) {
    return add(owner, std::move(task), delayMs, 0, false);
}

TaskId PageContextImpl::postRepeating(TaskOwner owner, std::function<void()> task, uint32_t periodMs) {
    return add(owner, std::move(task), periodMs, periodMs, false);
}

TaskId PageContextImpl::runEachFrame(TaskOwner owner, std::function<void()> callback) {
    return add(owner, std::move(callback), 0, 0, true);
}

void PageContextImpl::cancelTask(TaskId id) {
    Task* task = find(id);
    if (!task) return;
    task->done = true;
    // A task that runs now is removed after the run
    if (!running) removeDone();
}

void PageContextImpl::updateActiveOwners(const std::vector<TaskOwner>& owners) {
    for (const auto& task : tasks) {
        if (std::find(owners.begin(), owners.end(), task->owner) == owners.end()) task->done = true;
    }
    if (!running) removeDone();
}

void PageContextImpl::startFrame(uint32_t newElapsedTime, uint32_t newActiveTime) {
    elapsedTime = newElapsedTime;
    activeTime  = newActiveTime;
    for (const auto& task : tasks) task->ready = true;
}

void PageContextImpl::runFrameTasks() {
    running = true;
    std::vector<Task*> callbacks;
    std::vector<Task*> expired;
    for (const auto& task : tasks) {
        if (!task->ready || task->done) continue;
        if (task->eachFrame) {
            callbacks.push_back(task.get());
        } else if (static_cast<int32_t>(activeTime - task->dueTime) >= 0) {
            expired.push_back(task.get());
        }
    }
    std::stable_sort(expired.begin(), expired.end(), [this](const Task* a, const Task* b) {
        return static_cast<int32_t>(a->dueTime - activeTime) < static_cast<int32_t>(b->dueTime - activeTime);
    });

    // A task that is added during the run is not in the lists, and a task that is cancelled is done
    for (Task* task : callbacks) {
        if (!task->done) task->run();
    }
    for (Task* task : expired) {
        if (task->done) continue;
        if (task->periodMs > 0) {
            task->dueTime = activeTime + task->periodMs;
        } else {
            task->done = true;
        }
        task->run();
    }
    running = false;
    removeDone();
}

bool PageContextImpl::isTaskPending(TaskId id) const {
    return find(id) != nullptr;
}

TaskId PageContextImpl::add(TaskOwner owner, std::function<void()> run, uint32_t delayMs, uint32_t periodMs,
                            bool eachFrame) {
    const TaskId id = ++lastTaskId;
    tasks.push_back(std::make_unique<Task>(Task{
        .id        = id,
        .owner     = owner,
        .run       = std::move(run),
        .dueTime   = activeTime + delayMs,
        .periodMs  = periodMs,
        .eachFrame = eachFrame,
        .ready     = false,
        .done      = false,
    }));
    return id;
}

PageContextImpl::Task* PageContextImpl::find(TaskId id) const {
    if (id <= 0) return nullptr;
    for (const auto& task : tasks) {
        if (task->id == id) return task->done ? nullptr : task.get();
    }
    return nullptr;
}

void PageContextImpl::removeDone() {
    std::erase_if(tasks, [](const std::unique_ptr<Task>& task) { return task->done; });
}

}  // namespace

std::unique_ptr<PageContext> PageContext::create(const UiScreenInfo& screen) {
    return std::make_unique<PageContextImpl>(screen);
}
