#include "mini_as/coroutine.hpp"

#include <algorithm>
#include <utility>

namespace mini_as {

struct CoroutineScheduler::State {
    struct Task {
        CoroutineId id = 0;
        ScriptContext* context = nullptr;
    };

    explicit State(ScriptEngine& owner) : engine(&owner) {}

    ScriptEngine* engine = nullptr;
    std::deque<Task> ready;
    std::vector<CoroutineResult> completed;
    ScriptContext* active = nullptr;
    CoroutineId activeId = 0;
    bool activeAbortRequested = false;
    bool executing = false;
    CoroutineId nextId = 1;
};

CoroutineScheduler::CoroutineScheduler(ScriptEngine& engine)
    : state_(std::make_shared<State>(engine)) {}

CoroutineScheduler::~CoroutineScheduler() {
    AbortAll();
    state_.reset();
}

bool CoroutineScheduler::RegisterYieldFunction(std::string name) {
    if (name.empty()) return false;
    std::weak_ptr<State> state = state_;
    return state_->engine->RegisterGlobalFunction("void " + name + "()",
        [state = std::move(state)](GenericCall& call) {
            const auto locked = state.lock();
            if (!locked || !locked->active) {
                call.SetException("yield() requires an active coroutine");
                return;
            }
            locked->active->Suspend();
        });
}

CoroutineId CoroutineScheduler::Start(const BytecodeFunction* function,
                                      std::vector<Value> arguments) {
    if (!function || function->signature.method ||
        arguments.size() != function->signature.parameters.size()) return 0;
    ScriptContext* context = state_->engine->RequestContext();
    if (!context) return 0;
    if (!context->Prepare(function)) {
        state_->engine->ReturnContext(context);
        return 0;
    }
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (context->SetArgValue(index, std::move(arguments[index]))) continue;
        context->Abort();
        state_->engine->ReturnContext(context);
        return 0;
    }
    const CoroutineId id = state_->nextId++;
    state_->ready.push_back({id, context});
    return id;
}

std::size_t CoroutineScheduler::ExecuteRound() {
    if (state_->executing) return state_->ready.size() + (state_->active ? 1 : 0);
    state_->executing = true;
    const std::size_t turns = state_->ready.size();
    for (std::size_t turn = 0; turn < turns && !state_->ready.empty(); ++turn) {
        State::Task task = state_->ready.front();
        state_->ready.pop_front();
        state_->active = task.context;
        state_->activeId = task.id;
        state_->activeAbortRequested = false;
        ExecutionState execution = task.context->Execute();
        const bool abortRequested = state_->activeAbortRequested;
        state_->active = nullptr;
        state_->activeId = 0;
        state_->activeAbortRequested = false;
        if (abortRequested) {
            task.context->Abort();
            execution = ExecutionState::Aborted;
        }
        if (execution == ExecutionState::Suspended)
            state_->ready.push_back(task);
        else
            Complete(task.id, task.context);
    }
    state_->executing = false;
    return state_->ready.size();
}

bool CoroutineScheduler::Yield() {
    if (!state_->active) return false;
    state_->active->Suspend();
    return true;
}

bool CoroutineScheduler::Abort(CoroutineId id) {
    if (id == 0) return false;
    if (state_->activeId == id) {
        state_->activeAbortRequested = true;
        state_->active->Suspend();
        return true;
    }
    const auto found = std::find_if(state_->ready.begin(), state_->ready.end(),
        [id](const State::Task& task) { return task.id == id; });
    if (found == state_->ready.end()) return false;
    State::Task task = *found;
    state_->ready.erase(found);
    task.context->Abort();
    Complete(task.id, task.context);
    return true;
}

void CoroutineScheduler::AbortAll() {
    if (state_->active) {
        state_->activeAbortRequested = true;
        state_->active->Suspend();
    }
    while (!state_->ready.empty()) {
        State::Task task = state_->ready.front();
        state_->ready.pop_front();
        task.context->Abort();
        Complete(task.id, task.context);
    }
}

std::size_t CoroutineScheduler::GetCoroutineCount() const {
    return state_->ready.size() + (state_->active ? 1 : 0);
}

CoroutineId CoroutineScheduler::GetCurrentCoroutine() const {
    return state_->activeId;
}

const std::vector<CoroutineResult>& CoroutineScheduler::GetCompleted() const {
    return state_->completed;
}

std::optional<CoroutineResult> CoroutineScheduler::TakeCompleted(CoroutineId id) {
    const auto found = std::find_if(state_->completed.begin(), state_->completed.end(),
        [id](const CoroutineResult& result) { return result.id == id; });
    if (found == state_->completed.end()) return std::nullopt;
    CoroutineResult result = std::move(*found);
    state_->completed.erase(found);
    return result;
}

void CoroutineScheduler::Complete(CoroutineId id, ScriptContext* context) {
    CoroutineResult result;
    result.id = id;
    result.state = context->GetState();
    if (result.state == ExecutionState::Finished)
        result.returnValue = context->GetReturnValue();
    if (result.state == ExecutionState::Exception) {
        result.exception = context->GetExceptionString();
        result.location = context->GetExceptionLocation();
        result.callStack = context->GetCallStack();
    }
    state_->completed.push_back(std::move(result));
    state_->engine->ReturnContext(context);
}

} // namespace mini_as
