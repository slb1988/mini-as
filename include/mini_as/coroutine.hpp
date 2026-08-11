#pragma once

#include "mini_as/engine.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mini_as {

using CoroutineId = std::uint64_t;

struct CoroutineResult {
    CoroutineId id = 0;
    ExecutionState state = ExecutionState::Uninitialized;
    Value returnValue;
    std::string exception;
    SourceLocation location;
    std::vector<StackFrameInfo> callStack;
};

class CoroutineScheduler {
public:
    explicit CoroutineScheduler(ScriptEngine& engine);
    ~CoroutineScheduler();

    CoroutineScheduler(const CoroutineScheduler&) = delete;
    CoroutineScheduler& operator=(const CoroutineScheduler&) = delete;
    CoroutineScheduler(CoroutineScheduler&&) = delete;
    CoroutineScheduler& operator=(CoroutineScheduler&&) = delete;

    bool RegisterYieldFunction(std::string name = "yield");
    CoroutineId Start(const BytecodeFunction* function,
                      std::vector<Value> arguments = {});
    std::size_t ExecuteRound();
    bool Yield();
    bool Abort(CoroutineId id);
    void AbortAll();

    std::size_t GetCoroutineCount() const;
    CoroutineId GetCurrentCoroutine() const;
    const std::vector<CoroutineResult>& GetCompleted() const;
    std::optional<CoroutineResult> TakeCompleted(CoroutineId id);

private:
    struct State;
    void Complete(CoroutineId id, ScriptContext* context);
    std::shared_ptr<State> state_;
};

} // namespace mini_as
