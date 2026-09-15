#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace stageflow {

using ErrorCode = int;

constexpr ErrorCode kOk = 0;
constexpr ErrorCode kCanceled = -1;
constexpr ErrorCode kStaleCallback = -2;
constexpr ErrorCode kInvalidState = -3;
constexpr ErrorCode kStageFailed = -4;

enum class FlowState {
    Idle,
    Running,
    Succeeded,
    Failed,
    Canceled,
};

enum class StageResult {
    Pending,
    Succeeded,
    Failed,
};

struct StageRunResult {
    StageResult result = StageResult::Pending;
    ErrorCode error = kOk;

    static StageRunResult Pending() {
        return {StageResult::Pending, kOk};
    }

    static StageRunResult Succeeded() {
        return {StageResult::Succeeded, kOk};
    }

    static StageRunResult Failed(ErrorCode error = kStageFailed) {
        return {StageResult::Failed, error};
    }
};

template <typename Context>
class IStage {
public:
    using Done = std::function<void(ErrorCode)>;

    virtual ~IStage() = default;
    virtual const char* name() const = 0;
    virtual StageRunResult run(Context& context, Done done) = 0;
    virtual void cancel(Context& context) {
        (void)context;
    }
};

template <typename Context>
class FlowController {
public:
    using Stage = IStage<Context>;
    using StagePtr = std::shared_ptr<Stage>;
    using FinishCallback = std::function<void(ErrorCode)>;

    FlowController()
        : impl_(std::make_shared<Impl>(std::make_shared<Context>())) {}

    explicit FlowController(std::shared_ptr<Context> context)
        : impl_(std::make_shared<Impl>(std::move(context))) {
        if (!impl_->context) {
            impl_->context = std::make_shared<Context>();
        }
    }

    void add_stage(StagePtr stage) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stages.push_back(std::move(stage));
    }

    void set_finish_callback(FinishCallback callback) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->finish_callback = std::move(callback);
    }

    ErrorCode start() {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->state == FlowState::Running) {
                return kInvalidState;
            }
            impl_->state = FlowState::Running;
            impl_->error = kOk;
            impl_->current_index = 0;
            impl_->current_stage.clear();
            ++impl_->generation;
        }
        return start_current_stage(impl_);
    }

    ErrorCode cancel() {
        StagePtr stage;
        FinishCallback callback;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (is_terminal_locked(*impl_)) {
                return kOk;
            }
            impl_->state = FlowState::Canceled;
            impl_->error = kCanceled;
            ++impl_->generation;
            if (impl_->current_index < impl_->stages.size()) {
                stage = impl_->stages[impl_->current_index];
            }
            callback = impl_->finish_callback;
        }
        if (stage) {
            stage->cancel(*impl_->context);
        }
        if (callback) {
            callback(kCanceled);
        }
        return kOk;
    }

    FlowState state() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->state;
    }

    bool is_running() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->state == FlowState::Running;
    }

    bool is_terminal() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return is_terminal_locked(*impl_);
    }

    std::string current_stage() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->current_stage;
    }

    ErrorCode error() const {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->error;
    }

    Context& context() {
        return *impl_->context;
    }

    std::shared_ptr<Context> shared_context() const {
        return impl_->context;
    }

private:
    struct Impl {
        explicit Impl(std::shared_ptr<Context> ctx) : context(std::move(ctx)) {}

        mutable std::mutex mutex;
        std::shared_ptr<Context> context;
        std::vector<StagePtr> stages;
        FinishCallback finish_callback;
        FlowState state = FlowState::Idle;
        ErrorCode error = kOk;
        std::size_t current_index = 0;
        std::string current_stage;
        std::uint64_t generation = 0;
    };

    static bool is_terminal_locked(const Impl& impl) {
        return impl.state == FlowState::Succeeded ||
               impl.state == FlowState::Failed ||
               impl.state == FlowState::Canceled;
    }

    static ErrorCode start_current_stage(const std::shared_ptr<Impl>& impl) {
        StagePtr stage;
        std::size_t index = 0;
        std::uint64_t generation = 0;
        bool reached_end = false;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (impl->state != FlowState::Running) {
                return kInvalidState;
            }
            if (impl->current_index >= impl->stages.size()) {
                reached_end = true;
            } else {
                index = impl->current_index;
                generation = impl->generation;
                stage = impl->stages[index];
                impl->current_stage = stage ? stage->name() : "";
            }
        }

        if (reached_end) {
            return finish(impl, FlowState::Succeeded, kOk);
        }

        if (!stage) {
            return finish(impl, FlowState::Failed, kStageFailed);
        }

        std::weak_ptr<Impl> weak_impl = impl;
        auto done = [weak_impl, generation, index](ErrorCode error) {
            if (auto locked = weak_impl.lock()) {
                complete_stage(locked, generation, index, error);
            }
        };

        StageRunResult result = stage->run(*impl->context, std::move(done));
        if (result.result == StageResult::Pending) {
            return kOk;
        }
        if (result.result == StageResult::Succeeded) {
            return complete_stage(impl, generation, index, kOk);
        }
        return complete_stage(impl, generation, index,
                              result.error == kOk ? kStageFailed : result.error);
    }

    static ErrorCode complete_stage(const std::shared_ptr<Impl>& impl,
                                    std::uint64_t generation,
                                    std::size_t index,
                                    ErrorCode error) {
        bool needs_fail = false;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (generation != impl->generation || index != impl->current_index) {
                return kStaleCallback;
            }
            if (impl->state != FlowState::Running) {
                return kInvalidState;
            }
            if (error != kOk) {
                needs_fail = true;
            } else {
                ++impl->current_index;
            }
        }
        if (needs_fail) {
            return finish(impl, FlowState::Failed, error);
        }
        return start_current_stage(impl);
    }

    static ErrorCode finish(const std::shared_ptr<Impl>& impl,
                            FlowState state,
                            ErrorCode error) {
        FinishCallback callback;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            if (is_terminal_locked(*impl)) {
                return impl->error;
            }
            impl->state = state;
            impl->error = error;
            ++impl->generation;
            impl->current_stage.clear();
            callback = impl->finish_callback;
        }
        if (callback) {
            callback(error);
        }
        return error;
    }

    std::shared_ptr<Impl> impl_;
};

}  // namespace stageflow
