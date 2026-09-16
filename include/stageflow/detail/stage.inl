#pragma once

namespace stageflow {

inline StageRunResult StageRunResult::Pending() {
    return {StageResult::Pending, kOk};
}

inline StageRunResult StageRunResult::Succeeded() {
    return {StageResult::Succeeded, kOk};
}

inline StageRunResult StageRunResult::Failed(ErrorCode error) {
    return {StageResult::Failed, error};
}

template <typename Context>
void IStage<Context>::cancel(Context& context) {
    (void)context;
}

}  // namespace stageflow
