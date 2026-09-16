#pragma once

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

    static StageRunResult Pending();
    static StageRunResult Succeeded();
    static StageRunResult Failed(ErrorCode error = kStageFailed);
};

}  // namespace stageflow
