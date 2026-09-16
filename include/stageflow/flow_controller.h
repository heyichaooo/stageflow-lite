#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "stageflow/stage.h"
#include "stageflow/types.h"

namespace stageflow {

template <typename Context>
class FlowController {
public:
    using Stage = IStage<Context>;
    using StagePtr = std::shared_ptr<Stage>;
    using FinishCallback = std::function<void(ErrorCode)>;

    FlowController();
    explicit FlowController(std::shared_ptr<Context> context);

    void add_stage(StagePtr stage);
    void set_finish_callback(FinishCallback callback);

    ErrorCode start();
    ErrorCode cancel();

    FlowState state() const;
    bool is_running() const;
    bool is_terminal() const;
    std::string current_stage() const;
    ErrorCode error() const;

    Context& context();
    std::shared_ptr<Context> shared_context() const;

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

    static bool is_terminal_locked(const Impl& impl);
    static ErrorCode start_current_stage(const std::shared_ptr<Impl>& impl);
    static ErrorCode complete_stage(const std::shared_ptr<Impl>& impl,
                                    std::uint64_t generation,
                                    std::size_t index,
                                    ErrorCode error);
    static ErrorCode finish(const std::shared_ptr<Impl>& impl,
                            FlowState state,
                            ErrorCode error);

    std::shared_ptr<Impl> impl_;
};

}  // namespace stageflow

#include "stageflow/detail/flow_controller.inl"
