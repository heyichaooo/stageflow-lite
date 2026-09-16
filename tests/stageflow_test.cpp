#include <cassert>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stageflow/stageflow.h>
#include <thread>

struct TestContext {
    int value = 0;
    bool canceled = false;
};

using Flow = stageflow::FlowController<TestContext>;

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

class SyncSetStage : public stageflow::IStage<TestContext> {
public:
    SyncSetStage(const char* name, int expected, int value)
        : name_(name), expected_(expected), value_(value) {}

    const char* name() const override {
        return name_;
    }

    stageflow::StageRunResult run(TestContext& context, Done) override {
        assert(context.value == expected_);
        context.value = value_;
        return stageflow::StageRunResult::Succeeded();
    }

private:
    const char* name_;
    int expected_;
    int value_;
};

class AsyncSetStage : public stageflow::IStage<TestContext> {
public:
    const char* name() const override {
        return "async_2";
    }

    stageflow::StageRunResult run(TestContext& context, Done done) override {
        std::thread([&context, done]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            assert(context.value == 1);
            context.value = 2;
            done(stageflow::kOk);
        }).detach();
        return stageflow::StageRunResult::Pending();
    }
};

class PendingStage : public stageflow::IStage<TestContext> {
public:
    explicit PendingStage(Done& late_done) : late_done_(late_done) {}

    const char* name() const override {
        return "pending";
    }

    stageflow::StageRunResult run(TestContext&, Done done) override {
        late_done_ = done;
        return stageflow::StageRunResult::Pending();
    }

    void cancel(TestContext& context) override {
        context.canceled = true;
    }

private:
    Done& late_done_;
};

static void runs_sync_and_async_stages_in_order() {
    auto flow = std::make_shared<Flow>();
    flow->add_stage(std::make_shared<SyncSetStage>("sync_1", 0, 1));
    flow->add_stage(std::make_shared<AsyncSetStage>());
    flow->add_stage(std::make_shared<SyncSetStage>("sync_3", 2, 3));

    assert(wait_for_finish(flow) == stageflow::kOk);
    assert(flow->context().value == 3);
    assert(flow->state() == stageflow::FlowState::Succeeded);
}

static void cancel_invalidates_late_callbacks() {
    auto flow = std::make_shared<Flow>();
    stageflow::IStage<TestContext>::Done late_done;
    flow->add_stage(std::make_shared<PendingStage>(late_done));
    flow->add_stage(std::make_shared<SyncSetStage>("must_not_run", 0, 99));

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
