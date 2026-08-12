#pragma once

#include "mini_as/engine.hpp"

namespace mini_as::addons {

struct ScriptRefValue {
    ObjectHandle object;
};

Value MakeScriptRef(ObjectHandle object = {});
const ObjectHandle& GetScriptRef(const Value& value);
bool RegisterScriptRef(ScriptEngine& engine);

} // namespace mini_as::addons
