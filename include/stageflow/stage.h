#pragma once

#include <functional>

#include "stageflow/types.h"

namespace stageflow {

template <typename Context>
class IStage {
public:
    using Done = std::function<void(ErrorCode)>;

    virtual ~IStage() = default;
    virtual const char* name() const = 0;
    virtual StageRunResult run(Context& context, Done done) = 0;
    virtual void cancel(Context& context);
};

}  // namespace stageflow

#include "stageflow/detail/stage.inl"
