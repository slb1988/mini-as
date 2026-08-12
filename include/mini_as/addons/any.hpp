#pragma once

#include "mini_as/engine.hpp"

namespace mini_as::addons {

class ScriptAny final : public RefObject {
public:
    explicit ScriptAny(const TypeInfo* type, Value value = {});

    void Store(Value value);
    bool Retrieve(Value& value) const;
    const Value& StoredValue() const;
    bool HasValue() const;
    void Clear();

    void EnumerateReferences(
        const std::function<void(RefObject*)>& visitor) const override;
    void ClearReferences() override;

private:
    ~ScriptAny() override = default;

    Value value_;
};

bool RegisterScriptAny(ScriptEngine& engine);

} // namespace mini_as::addons
