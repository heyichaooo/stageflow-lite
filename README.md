# stageflow-lite

轻量级 C++ 分阶段异步流程控制框架，支持上下文传递、顺序执行、取消和失败收口。

`stageflow-lite` 适合把一条复杂异步链路整理成多个明确的 stage。每个 stage 只负责当前阶段的工作，完成后回调 `done(error)`，由 `FlowController` 统一推进下一个阶段。

## 解决的问题

- 异步阶段按顺序执行
- stage 之间通过同一个 context 传递数据
- stage 不直接调用下一个 stage
- cancel / failed / succeeded 只收口一次
- cancel 后晚到的异步 callback 会被丢弃
- stage 内部可以再嵌套一个子 flow

## 核心原则

```text
数据通道：Context
控制通道：FlowController + done(error)

Stage 只能读写 Context
Stage 完成只能调用 done(error)
Stage 不知道下一个 Stage 是谁
FlowController 不理解业务数据，只负责推进顺序
```

## 基本用法

```cpp
#include <stageflow/flow.h>

struct Context {
    int file_id = 0;
    std::string detect_info;
    std::string task_id;
};

using Flow = stageflow::FlowController<Context>;
using Stage = stageflow::FunctionStage<Context>;

auto flow = std::make_shared<Flow>();

flow->add_stage(std::make_shared<Stage>(
    "prepare",
    [](Context& ctx, Stage::Done done) {
        async_prepare([&ctx, done](int error, std::string detect_info) {
            if (error == stageflow::kOk) {
                ctx.detect_info = std::move(detect_info);
            }
            done(error);
        });
        return stageflow::StageRunResult::Pending();
    }));

flow->add_stage(std::make_shared<Stage>(
    "create_task",
    [](Context& ctx, Stage::Done) {
        ctx.task_id = "task-" + std::to_string(ctx.file_id);
        return stageflow::StageRunResult::Succeeded();
    }));

flow->set_finish_callback([](int error) {
    // error == stageflow::kOk means succeeded.
});

flow->start();
```

## 同步和异步 stage

同步成功：

```cpp
return stageflow::StageRunResult::Succeeded();
```

同步失败：

```cpp
return stageflow::StageRunResult::Failed(error);
```

异步执行：

```cpp
start_async_work([done](int error) {
    done(error);
});
return stageflow::StageRunResult::Pending();
```

异步 stage 返回 `Pending` 后，必须最终调用一次 `done(error)`。如果 flow 已经被取消，旧 callback 会自动失效，不会推进后续 stage。

## 嵌套 flow

stage 内部也可以嵌套一个子 flow：

```cpp
auto child_flow = std::make_shared<stageflow::FlowController<Context>>(flow->shared_context());
child_flow->add_stage(...);

flow->add_stage(std::make_shared<stageflow::CompositeStage<Context>>(
    "prepare_retouch", child_flow));
```

父 flow 会把 `CompositeStage` 当成一个普通异步 stage，等子 flow 结束后再继续。

## 构建

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## 适合的场景

- 图片修图、导出、上传等分阶段异步链路
- 初始化流程中的模块依赖编排
- 网络请求 + 本地处理 + 回调收口的组合流程
- 需要明确 cancel / failed / succeeded 边界的业务任务

## License

MIT
