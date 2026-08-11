#pragma once

#include "mini_as/engine.hpp"

namespace mini_as::addons {

class ScriptArray final : public RefObject {
public:
    ScriptArray(const TypeInfo* type, DataType elementType, Value defaultElement,
                std::size_t length = 0);

    const DataType& ElementType() const;
    std::size_t Size() const;
    bool Empty() const;
    void Resize(std::size_t length);
    void InsertLast(Value value);
    Value RemoveLast();
    const Value& Get(std::size_t index) const;
    void Set(std::size_t index, Value value);
    void EnumerateReferences(const std::function<void(RefObject*)>& visitor) const override;
    void ClearReferences() override;

private:
    ~ScriptArray() override = default;
    DataType elementType_;
    Value defaultElement_;
    std::vector<Value> elements_;
};

bool RegisterScriptArray(ScriptEngine& engine, std::string name = "array");

} // namespace mini_as::addons
