// Host tests of the tasks of a page context: the order of a frame, the frame boundary, the delays,
// the repeats and the cancel of a task.

#include <string>
#include <vector>

#include "test.h"
#include "ui/page/page_context.hpp"
#include "ui/view/unique_id.hpp"

namespace {

std::vector<std::string> runLog;

class FakeScreen final : public UiScreenInfo {
public:
    SDL_PixelFormat* getPixelFormat() const override { return nullptr; }
    UiSize getScreenSize() const override { return UiSize{640, 480}; }
};

FakeScreen screen;

/** A page context with the key-less calls of a page */
class TestScheduler final : public UiTaskScheduler {
public:
    TestScheduler() { pageScheduler.setContext(context); }

    TaskId post(std::function<void()> task) override { return pageScheduler.post(std::move(task)); }
    TaskId postDelayed(std::function<void()> task, uint32_t delayMs) override {
        return pageScheduler.postDelayed(std::move(task), delayMs);
    }
    TaskId postRepeating(std::function<void()> task, uint32_t periodMs) override {
        return pageScheduler.postRepeating(std::move(task), periodMs);
    }
    TaskId runEachFrame(std::function<void()> callback) override {
        return pageScheduler.runEachFrame(std::move(callback));
    }
    void cancelTask(TaskId id) override { pageScheduler.cancelTask(id); }
    bool isTaskPending(TaskId id) const override { return pageScheduler.isTaskPending(id); }

    void beginFrame(uint32_t activeTime) { context.startFrame(activeTime, activeTime); }
    void runFrame() { context.runFrameTasks(); }

    std::unique_ptr<PageContext> contextHolder = PageContext::create(screen);
    PageContext& context                       = *contextHolder;
    OwnerScheduler pageScheduler{ui::generateUniqueId()};
};

/** Starts a frame at activeTime and runs it. */
void runFrameAt(TestScheduler& scheduler, uint32_t activeTime) {
    scheduler.beginFrame(activeTime);
    scheduler.runFrame();
}

}  // namespace

TEST(frame_callbacks_run_before_the_expired_tasks) {
    runLog.clear();
    TestScheduler scheduler;
    scheduler.post([] { runLog.push_back("task"); });
    scheduler.runEachFrame([] { runLog.push_back("frame"); });
    runFrameAt(scheduler, 0);
    CHECK_EQ_SZ(runLog.size(), 2);
    CHECK(runLog[0] == "frame");
    CHECK(runLog[1] == "task");
}

TEST(work_added_during_a_frame_waits_for_the_next_frame) {
    runLog.clear();
    TestScheduler scheduler;
    scheduler.beginFrame(0);
    // As from an input handler or onTick(), after the start of the frame
    scheduler.post([] { runLog.push_back("input"); });
    scheduler.runEachFrame([] { runLog.push_back("frame"); });
    scheduler.runFrame();
    CHECK(runLog.empty());

    runFrameAt(scheduler, 16);
    CHECK_EQ_SZ(runLog.size(), 2);
    CHECK(runLog[0] == "frame");
    CHECK(runLog[1] == "input");
}

TEST(a_task_posted_from_a_task_or_a_callback_waits_for_the_next_frame) {
    runLog.clear();
    TestScheduler scheduler;
    scheduler.post([&scheduler] {
        runLog.push_back("first");
        scheduler.post([] { runLog.push_back("from task"); });
    });
    TaskId callback = 0;
    callback        = scheduler.runEachFrame([&scheduler, &callback] {
        scheduler.post([] { runLog.push_back("from callback"); });
        scheduler.cancelTask(callback);
    });
    runFrameAt(scheduler, 0);
    CHECK_EQ_SZ(runLog.size(), 1);

    runFrameAt(scheduler, 16);
    CHECK_EQ_SZ(runLog.size(), 3);
    CHECK(runLog[1] == "from callback");
    CHECK(runLog[2] == "from task");
}

TEST(a_task_that_posts_itself_runs_one_time_in_each_frame) {
    TestScheduler scheduler;
    int count = 0;
    std::function<void()> repost;
    repost = [&] {
        count++;
        scheduler.post(repost);
    };
    scheduler.post(repost);
    for (uint32_t frame = 0; frame < 5; frame++) runFrameAt(scheduler, frame * 16);
    CHECK_EQ_INT(count, 5);
}

TEST(a_delay_counts_from_the_time_of_the_post) {
    runLog.clear();
    TestScheduler scheduler;
    runFrameAt(scheduler, 1000);
    scheduler.postDelayed([] { runLog.push_back("delayed"); }, 500);
    runFrameAt(scheduler, 1499);
    CHECK(runLog.empty());
    runFrameAt(scheduler, 1500);
    CHECK_EQ_SZ(runLog.size(), 1);
    runFrameAt(scheduler, 3000);
    CHECK_EQ_SZ(runLog.size(), 1);
}

TEST(a_task_cancelled_during_the_run_does_not_run) {
    runLog.clear();
    TestScheduler scheduler;
    TaskId second = 0;
    scheduler.post([&] {
        runLog.push_back("first");
        scheduler.cancelTask(second);
    });
    second = scheduler.post([] { runLog.push_back("second"); });
    runFrameAt(scheduler, 0);
    runFrameAt(scheduler, 16);
    CHECK_EQ_SZ(runLog.size(), 1);
    CHECK(runLog[0] == "first");
}

TEST(a_cancel_of_no_task_does_nothing) {
    TestScheduler scheduler;
    int count     = 0;
    const TaskId kept = scheduler.post([&count] { count++; });
    const TaskId ran  = scheduler.post([] {});
    runFrameAt(scheduler, 0);
    const TaskId cancelled = scheduler.postDelayed([] {}, 100);
    scheduler.cancelTask(cancelled);

    scheduler.cancelTask(0);
    scheduler.cancelTask(-1);
    scheduler.cancelTask(-1000);
    scheduler.cancelTask(ran);
    scheduler.cancelTask(cancelled);
    scheduler.cancelTask(kept + 1000);
    CHECK_EQ_INT(count, 1);
    CHECK(!scheduler.isTaskPending(ran));
    CHECK(!scheduler.isTaskPending(cancelled));
}

TEST(a_late_repeating_task_runs_one_time_and_counts_from_that_run) {
    TestScheduler scheduler;
    int count = 0;
    runFrameAt(scheduler, 0);
    scheduler.postRepeating([&count] { count++; }, 100);
    runFrameAt(scheduler, 100);
    CHECK_EQ_INT(count, 1);
    // Three periods late: one run
    runFrameAt(scheduler, 450);
    CHECK_EQ_INT(count, 2);
    // The next period counts from 450
    runFrameAt(scheduler, 549);
    CHECK_EQ_INT(count, 2);
    runFrameAt(scheduler, 550);
    CHECK_EQ_INT(count, 3);
}

TEST(each_expired_task_runs_in_one_frame) {
    TestScheduler scheduler;
    int count = 0;
    for (int index = 0; index < 50; index++) scheduler.postDelayed([&count] { count++; }, static_cast<uint32_t>(index));
    runFrameAt(scheduler, 1000);
    CHECK_EQ_INT(count, 50);
}

TEST(expired_tasks_run_in_the_order_of_their_time) {
    runLog.clear();
    TestScheduler scheduler;
    scheduler.postDelayed([] { runLog.push_back("late"); }, 200);
    scheduler.postDelayed([] { runLog.push_back("early"); }, 100);
    runFrameAt(scheduler, 300);
    CHECK_EQ_SZ(runLog.size(), 2);
    CHECK(runLog[0] == "early");
    CHECK(runLog[1] == "late");
}

TEST(a_scheduled_task_cancels_its_task_when_destroyed) {
    TestScheduler scheduler;
    int count = 0;
    {
        ScheduledTask held(scheduler, scheduler.post([&count] { count++; }));
    }
    ScheduledTask moved;
    {
        ScheduledTask held(scheduler, scheduler.post([&count] { count += 10; }));
        moved = std::move(held);
    }
    runFrameAt(scheduler, 0);
    CHECK_EQ_INT(count, 10);

    // A new task in the holder cancels the old one
    moved = ScheduledTask(scheduler, scheduler.post([&count] { count += 100; }));
    moved = ScheduledTask(scheduler, scheduler.post([&count] { count += 1000; }));
    runFrameAt(scheduler, 16);
    CHECK_EQ_INT(count, 1010);
}

TEST(an_id_is_not_given_again) {
    TestScheduler first;
    TestScheduler second;
    const TaskId a = first.post([] {});
    first.cancelTask(a);
    const TaskId b = first.post([] {});
    const TaskId c = second.post([] {});
    CHECK(a > 0);
    CHECK(b > a);
    CHECK(c > b);
}

TEST(the_tasks_of_an_owner_that_is_not_active_end) {
    std::unique_ptr<PageContext> holder = PageContext::create(screen);
    PageContext& scheduler              = *holder;
    int page  = 0;
    int stays = 0;
    int left  = 0;
    scheduler.runEachFrame(3, [&page] { page++; });
    scheduler.runEachFrame(7, [&stays] { stays++; });
    scheduler.runEachFrame(9, [&left] { left++; });
    scheduler.post(9, [&left] { left += 100; }, 50);
    scheduler.startFrame(0, 0);
    scheduler.runFrameTasks();
    scheduler.updateActiveOwners({3, 7, 8});
    for (uint32_t time = 16; time < 200; time += 16) {
        scheduler.startFrame(time, time);
        scheduler.runFrameTasks();
    }
    CHECK_EQ_INT(page, 13);
    CHECK_EQ_INT(stays, 13);
    CHECK_EQ_INT(left, 1);
}

int main(void) {
    printf("page_context\n");
    RUN(frame_callbacks_run_before_the_expired_tasks);
    RUN(work_added_during_a_frame_waits_for_the_next_frame);
    RUN(a_task_posted_from_a_task_or_a_callback_waits_for_the_next_frame);
    RUN(a_task_that_posts_itself_runs_one_time_in_each_frame);
    RUN(a_delay_counts_from_the_time_of_the_post);
    RUN(a_task_cancelled_during_the_run_does_not_run);
    RUN(a_cancel_of_no_task_does_nothing);
    RUN(a_late_repeating_task_runs_one_time_and_counts_from_that_run);
    RUN(each_expired_task_runs_in_one_frame);
    RUN(expired_tasks_run_in_the_order_of_their_time);
    RUN(a_scheduled_task_cancels_its_task_when_destroyed);
    RUN(an_id_is_not_given_again);
    RUN(the_tasks_of_an_owner_that_is_not_active_end);
    return test_summary();
}
