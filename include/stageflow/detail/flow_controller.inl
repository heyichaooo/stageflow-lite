#pragma once

namespace stageflow {

template <typename Context>
FlowController<Context>::FlowController()
    : impl_(std::make_shared<Impl>(std::make_shared<Context>())) {}

template <typename Context>
FlowController<Context>::FlowController(std::shared_ptr<Context> context)
    : impl_(std::make_shared<Impl>(std::move(context))) {
    if (!impl_->context) {
        impl_->context = std::make_shared<Context>();
    }
}

template <typename Context>
void FlowController<Context>::add_stage(StagePtr stage) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stages.push_back(std::move(stage));
}

template <typename Context>
void FlowController<Context>::set_finish_callback(FinishCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->finish_callback = std::move(callback);
}

template <typename Context>
ErrorCode FlowController<Context>::start() {
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

template <typename Context>
ErrorCode FlowController<Context>::cancel() {
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

template <typename Context>
FlowState FlowController<Context>::state() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state;
}

template <typename Context>
bool FlowController<Context>::is_running() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state == FlowState::Running;
}

template <typename Context>
bool FlowController<Context>::is_terminal() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return is_terminal_locked(*impl_);
}

template <typename Context>
std::string FlowController<Context>::current_stage() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current_stage;
}

template <typename Context>
ErrorCode FlowController<Context>::error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

template <typename Context>
Context& FlowController<Context>::context() {
    return *impl_->context;
}

template <typename Context>
std::shared_ptr<Context> FlowController<Context>::shared_context() const {
    return impl_->context;
}

template <typename Context>
bool FlowController<Context>::is_terminal_locked(const Impl& impl) {
    return impl.state == FlowState::Succeeded ||
           impl.state == FlowState::Failed ||
           impl.state == FlowState::Canceled;
}

template <typename Context>
ErrorCode FlowController<Context>::start_current_stage(const std::shared_ptr<Impl>& impl) {
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

template <typename Context>
ErrorCode FlowController<Context>::complete_stage(const std::shared_ptr<Impl>& impl,
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

template <typename Context>
ErrorCode FlowController<Context>::finish(const std::shared_ptr<Impl>& impl,
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

}  // namespace stageflow
