#ifndef __UI_VIEW_UI_TASK_SCHEDULER_HPP__
#define __UI_VIEW_UI_TASK_SCHEDULER_HPP__

#include <cstdint>
#include <functional>

/** The id of a scheduled task. A value of 0 or less is no task. */
using TaskId = int64_t;

/**
 * Runs the work of one owner on the frames of its page. A delay counts in the active time of the
 * page, thus no task runs while another page covers the page. A task that is added during a frame
 * runs in a later frame.
 */
class UiTaskScheduler {
public:
    virtual TaskId post(std::function<void()> task) = 0;
    virtual TaskId postDelayed(std::function<void()> task, uint32_t delayMs) = 0;

    /** Runs `task` each `periodMs`. A late task runs one time, and the next period starts at that run. */
    virtual TaskId postRepeating(std::function<void()> task, uint32_t periodMs) = 0;

    /** Runs `callback` in each frame, before the expired tasks of the frame. */
    virtual TaskId runEachFrame(std::function<void()> callback) = 0;

    /** Does nothing for an id of 0 or less, or for a task that ran or was cancelled. */
    virtual void cancelTask(TaskId id) = 0;

    /** Returns true while the task waits to run. */
    virtual bool isTaskPending(TaskId id) const = 0;

protected:
    ~UiTaskScheduler() = default;
};

/** Holds one task, and cancels it when the holder is destroyed. */
class ScheduledTask {
public:
    ScheduledTask() = default;
    ScheduledTask(UiTaskScheduler& scheduler, TaskId id) : scheduler(&scheduler), id(id) {}
    ScheduledTask(ScheduledTask&& other) noexcept : scheduler(other.scheduler), id(other.id) { other.id = 0; }

    ScheduledTask& operator=(ScheduledTask&& other) noexcept {
        if (this != &other) {
            cancel();
            scheduler = other.scheduler;
            id        = other.id;
            other.id  = 0;
        }
        return *this;
    }

    ScheduledTask(const ScheduledTask&)            = delete;
    ScheduledTask& operator=(const ScheduledTask&) = delete;

    ~ScheduledTask() { cancel(); }

    void cancel() {
        if (scheduler) scheduler->cancelTask(id);
        id = 0;
    }

private:
    UiTaskScheduler* scheduler = nullptr;
    TaskId id                  = 0;
};

#endif
