#ifndef __UI_VIEW_UI_CONTEXT_HPP__
#define __UI_VIEW_UI_CONTEXT_HPP__

#include <cstdint>
#include <functional>

#include <SDL2/SDL.h>

#include "ui/view/ui_size.hpp"
#include "ui/view/ui_task_scheduler.hpp"

/** The facts of the screen that a view draws for. */
class UiScreenInfo {
public:
    virtual SDL_PixelFormat* getPixelFormat() const = 0;
    virtual UiSize getScreenSize() const = 0;

protected:
    ~UiScreenInfo() = default;
};

/**
 * The owner of a task: the id of a view or of a page. After each layout of the page, the stack
 * cancels each task of an owner that is not the page and not a view of the layout.
 */
using TaskOwner = int;

/**
 * What a page and its views get from the stack. Each page has its own context. A page and a view
 * schedule through an OwnerScheduler, which gives the owner.
 */
class UiContext : public UiScreenInfo {
public:
    /**
     * Returns the time of the last frame in which the page was the top, in milliseconds from the
     * start of the app.
     */
    virtual uint32_t getElapsedTime() const = 0;

    /** Returns the time that the page was the top page, in milliseconds from its open. */
    virtual uint32_t getActiveTime() const = 0;

    virtual TaskId post(TaskOwner owner, std::function<void()> task, uint32_t delayMs) = 0;
    virtual TaskId postRepeating(TaskOwner owner, std::function<void()> task, uint32_t periodMs) = 0;
    virtual TaskId runEachFrame(TaskOwner owner, std::function<void()> callback) = 0;
    virtual void cancelTask(TaskId id) = 0;
    virtual bool isTaskPending(TaskId id) const = 0;

protected:
    ~UiContext() = default;
};

/** The scheduler of one owner on a context. It schedules nothing before it has a context. */
class OwnerScheduler final : public UiTaskScheduler {
public:
    explicit OwnerScheduler(TaskOwner owner) : owner(owner) {}

    void setContext(UiContext& newContext) { context = &newContext; }

    TaskId post(std::function<void()> task) override { return postDelayed(std::move(task), 0); }
    TaskId postDelayed(std::function<void()> task, uint32_t delayMs) override {
        return context ? context->post(owner, std::move(task), delayMs) : 0;
    }
    TaskId postRepeating(std::function<void()> task, uint32_t periodMs) override {
        return context ? context->postRepeating(owner, std::move(task), periodMs) : 0;
    }
    TaskId runEachFrame(std::function<void()> callback) override {
        return context ? context->runEachFrame(owner, std::move(callback)) : 0;
    }
    void cancelTask(TaskId id) override {
        if (context) context->cancelTask(id);
    }
    bool isTaskPending(TaskId id) const override { return context && context->isTaskPending(id); }

private:
    TaskOwner owner;
    UiContext* context = nullptr;
};

#endif
