#include <chrono>
#include <iostream>
#include <memory>
#include <stageflow/flow.h>
#include <string>
#include <thread>

struct RetouchContext {
    int file_id = 0;
    std::string trace_id;
    std::string detect_info;
    std::string task_id;
};

class PrepareRetouchStage : public stageflow::IStage<RetouchContext> {
public:
    const char* name() const override {
        return "prepare_retouch";
    }

    stageflow::StageRunResult run(RetouchContext& context, Done done) override {
        std::thread([&context, done]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            context.detect_info = "person-basic-detect-ready";
            done(stageflow::kOk);
        }).detach();
        return stageflow::StageRunResult::Pending();
    }
};

class CreateTaskStage : public stageflow::IStage<RetouchContext> {
public:
    const char* name() const override {
        return "create_task";
    }

    stageflow::StageRunResult run(RetouchContext& context, Done) override {
        context.task_id = "preview-task-" + std::to_string(context.file_id);
        return stageflow::StageRunResult::Succeeded();
    }
};

class RunTaskStage : public stageflow::IStage<RetouchContext> {
public:
    const char* name() const override {
        return "run_task";
    }

    stageflow::StageRunResult run(RetouchContext& context, Done done) override {
        std::thread([task_id = context.task_id, done]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::cout << "finished " << task_id << "\n";
            done(stageflow::kOk);
        }).detach();
        return stageflow::StageRunResult::Pending();
    }
};

int main() {
    using Flow = stageflow::FlowController<RetouchContext>;

    auto flow = std::make_shared<Flow>();
    flow->context().file_id = 1001;
    flow->context().trace_id = "trace-preview-1001";

    flow->add_stage(std::make_shared<PrepareRetouchStage>());
    flow->add_stage(std::make_shared<CreateTaskStage>());
    flow->add_stage(std::make_shared<RunTaskStage>());

    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    int final_error = stageflow::kOk;

    flow->set_finish_callback([&](int error) {
        std::lock_guard<std::mutex> lock(mutex);
        final_error = error;
        finished = true;
        cv.notify_one();
    });

    flow->start();

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&]() { return finished; });
    std::cout << "flow finished error=" << final_error
              << " task_id=" << flow->context().task_id << "\n";
    return final_error == stageflow::kOk ? 0 : 1;
}
