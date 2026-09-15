#include <cassert>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stageflow/flow.h>
#include <thread>

struct TestContext {
    int value = 0;
    bool canceled = false;
};

using Flow = stageflow::FlowController<TestContext>;
using Stage = stageflow::FunctionStage<TestContext>;

static int wait_for_finish(const std::shared_ptr<Flow>& flow) {
    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    int final_error = 999;

    flow->set_finish_callback([&](int error) {
        std::lock_guard<std::mutex> lock(mutex);
        final_error = error;
        finished = true;
        cv.notify_one();
    });

    assert(flow->start() == stageflow::kOk);
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(2), [&]() { return finished; });
    assert(finished);
    return final_error;
}

static void runs_sync_and_async_stages_in_order() {
    auto flow = std::make_shared<Flow>();
    flow->add_stage(std::make_shared<Stage>(
        "sync_1",
        [](TestContext& ctx, Stage::Done) {
            ctx.value = 1;
            return stageflow::StageRunResult::Succeeded();
        }));
    flow->add_stage(std::make_shared<Stage>(
        "async_2",
        [](TestContext& ctx, Stage::Done done) {
            std::thread([&ctx, done]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                assert(ctx.value == 1);
                ctx.value = 2;
                done(stageflow::kOk);
            }).detach();
            return stageflow::StageRunResult::Pending();
        }));
    flow->add_stage(std::make_shared<Stage>(
        "sync_3",
        [](TestContext& ctx, Stage::Done) {
            assert(ctx.value == 2);
            ctx.value = 3;
            return stageflow::StageRunResult::Succeeded();
        }));

    assert(wait_for_finish(flow) == stageflow::kOk);
    assert(flow->context().value == 3);
    assert(flow->state() == stageflow::FlowState::Succeeded);
}

static void cancel_invalidates_late_callbacks() {
    auto flow = std::make_shared<Flow>();
    stageflow::IStage<TestContext>::Done late_done;
    flow->add_stage(std::make_shared<Stage>(
        "pending",
        [&](TestContext&, Stage::Done done) {
            late_done = done;
            return stageflow::StageRunResult::Pending();
        },
        [](TestContext& ctx) {
            ctx.canceled = true;
        }));
    flow->add_stage(std::make_shared<Stage>(
        "must_not_run",
        [](TestContext& ctx, Stage::Done) {
            ctx.value = 99;
            return stageflow::StageRunResult::Succeeded();
        }));

    int callback_error = 999;
    flow->set_finish_callback([&](int error) {
        callback_error = error;
    });
    assert(flow->start() == stageflow::kOk);
    assert(flow->cancel() == stageflow::kOk);
    assert(flow->context().canceled);
    assert(flow->state() == stageflow::FlowState::Canceled);
    assert(callback_error == stageflow::kCanceled);

    late_done(stageflow::kOk);
    assert(flow->context().value == 0);
    assert(flow->state() == stageflow::FlowState::Canceled);
}

int main() {
    runs_sync_and_async_stages_in_order();
    cancel_invalidates_late_callbacks();
    return 0;
}
