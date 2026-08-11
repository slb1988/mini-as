#pragma once

#include "mini_as/addons/array.hpp"

#include <map>
#include <string>
#include <vector>

namespace mini_as::addons {

struct DictionaryValue {
    Value value;
};

class ScriptDictionary final : public RefObject {
public:
    explicit ScriptDictionary(const TypeInfo* type);

    void Set(std::string key, Value value);
    bool Get(std::string_view key, Value& value) const;
    bool Exists(std::string_view key) const;
    bool Empty() const;
    std::size_t Size() const;
    bool Delete(std::string_view key);
    void DeleteAll();
    std::vector<std::string> Keys() const;
    Value ValueAt(std::size_t iterator) const;
    const std::string& KeyAt(std::size_t iterator) const;

    void EnumerateReferences(
        const std::function<void(RefObject*)>& visitor) const override;
    void ClearReferences() override;

private:
    ~ScriptDictionary() override = default;

    std::map<std::string, Value, std::less<>> values_;
};

bool RegisterScriptDictionary(ScriptEngine& engine);

} // namespace mini_as::addons
