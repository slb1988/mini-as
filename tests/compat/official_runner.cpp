#include <angelscript.h>
#include <scriptany.h>
#include <weakref.h>
#include <scriptarray.h>
#include <scriptdictionary.h>
#include <scripthandle.h>
#include <scriptstdstring.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace {

class CompatReference {
public:
    explicit CompatReference(int value) : value(value) {}
    void AddRef() { ++references_; }
    void Release() { if (--references_ == 0) delete this; }
    int Get() const { return value; }
    int value;

private:
    ~CompatReference() = default;
    int references_ = 1;
};

struct CompatValue {
    int value = 42;
};

class MemoryBytecodeStream final : public asIBinaryStream {
public:
    int Read(void* output, asUINT size) override {
        if (position_ + size > bytes_.size()) return -1;
        std::memcpy(output, bytes_.data() + position_, size);
        position_ += size;
        return 0;
    }

    int Write(const void* input, asUINT size) override {
        const auto* bytes = static_cast<const std::uint8_t*>(input);
        bytes_.insert(bytes_.end(), bytes, bytes + size);
        return 0;
    }

    void Rewind() { position_ = 0; }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
};

void CompatValueConstruct(asIScriptGeneric* call) {
    new(call->GetObject()) CompatValue();
}

void CompatValueDestruct(asIScriptGeneric* call) {
    static_cast<CompatValue*>(call->GetObject())->~CompatValue();
}

void CompatValueCopyConstruct(asIScriptGeneric* call) {
    const auto* source = static_cast<const CompatValue*>(call->GetArgObject(0));
    new(call->GetObject()) CompatValue(*source);
}

void CompatValueAssign(asIScriptGeneric* call) {
    auto* destination = static_cast<CompatValue*>(call->GetObject());
    const auto* source = static_cast<const CompatValue*>(call->GetArgObject(0));
    *destination = *source;
    call->SetReturnAddress(destination);
}

void ReadHostValue(asIScriptGeneric* call) {
    const auto* value = static_cast<const CompatValue*>(call->GetArgObject(0));
    call->SetReturnDWord(static_cast<asDWORD>(value ? value->value : 0));
}

void GetHostValue(asIScriptGeneric* call) {
    const auto* value = static_cast<const CompatValue*>(call->GetObject());
    call->SetReturnDWord(static_cast<asDWORD>(value ? value->value : 0));
}

void Lift(asIScriptGeneric* call) {
    call->SetReturnDWord(call->GetArgDWord(0) + 2);
}

void CompatReferenceFactory(asIScriptGeneric* call) {
    call->SetReturnAddress(new CompatReference(static_cast<int>(call->GetArgDWord(0))));
}

std::string ReadFile(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream source;
    source << input.rdbuf();
    return source.str();
}

void MessageCallback(const asSMessageInfo* message, void*) {
    std::cerr << message->row << ':' << message->col << ':' << message->message << '\n';
}

void Scoped(asIScriptGeneric* call) {
    call->SetReturnDWord(call->GetArgDWord(0) + 2);
}

void Hidden(asIScriptGeneric* call) {
    call->SetReturnDWord(99);
}

struct DebugProbe {
    bool printed = false;
};

void DebugLineCallback(asIScriptContext* context, void* userData) {
    auto* probe = static_cast<DebugProbe*>(userData);
    const int line = context->GetLineNumber(0);
    if (!probe || probe->printed || line != 3) return;
    probe->printed = true;
    const auto* function = context->GetFunction(0);
    const auto* caller = context->GetFunction(1);
    std::cout << "debug=" << (function ? function->GetName() : "<null>")
              << ':' << line << ':' << context->GetCallstackSize() << '\n';
    for (asUINT level = 0; level < 2; ++level) {
        const int count = context->GetVarCount(level);
        for (int index = 0; index < count; ++index) {
            const char* name = nullptr;
            int typeId = 0;
            context->GetVar(static_cast<asUINT>(index), level, &name, &typeId);
            if (!name || typeId != asTYPEID_INT32 ||
                !context->IsVarInScope(static_cast<asUINT>(index), level)) continue;
            const bool selected = level == 0
                ? std::strcmp(name, "value") == 0 || std::strcmp(name, "doubled") == 0
                : std::strcmp(name, "seed") == 0;
            if (!selected) continue;
            const auto* value = static_cast<const int*>(
                context->GetAddressOfVar(static_cast<asUINT>(index), level));
            if (!value) continue;
            std::cout << (level == 0 ? "local=" : "caller-local=")
                      << name << ':' << *value << '\n';
        }
        if (level == 0)
            std::cout << "caller=" << (caller ? caller->GetName() : "<null>")
                      << ':' << context->GetLineNumber(1) << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    asIScriptEngine* engine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
    if (!engine) return 3;
    if (std::string(argv[1]).find("any_ref_objects") != std::string::npos) {
        RegisterScriptHandle(engine);
        RegisterScriptAny(engine);
    }
    int hostCounter = 40;
    const bool hostControls = std::string(argv[1]).find("host_controls") != std::string::npos;
    const bool registeredTemplates =
        std::string(argv[1]).find("registered_template_types") != std::string::npos;
    const bool dictionaryObject =
        std::string(argv[1]).find("dictionary_object") != std::string::npos;
    const bool arrayTemplate =
        std::string(argv[1]).find("array_template_object") != std::string::npos ||
        std::string(argv[1]).find("initialization_lists") != std::string::npos ||
        std::string(argv[1]).find("indexing_expressions") != std::string::npos ||
        std::string(argv[1]).find("foreach_operator_protocol") != std::string::npos ||
        dictionaryObject;
    if (dictionaryObject) RegisterStdString(engine);
    if (arrayTemplate) RegisterScriptArray(engine, false);
    if (dictionaryObject) RegisterScriptDictionary(engine);
    if (registeredTemplates && engine->RegisterObjectType(
            "HostBox<class T>", 0,
            asOBJ_REF | asOBJ_TEMPLATE | asOBJ_NOCOUNT) < 0) {
        engine->ShutDownAndRelease();
        return 3;
    }
    if (hostControls) {
        if (engine->SetDefaultNamespace("HostTools") < 0 ||
            engine->SetDefaultAccessMask(0x1u) != ~asDWORD(0) ||
            engine->BeginConfigGroup("runtime") < 0 ||
            engine->RegisterGlobalFunction("int Scoped(int value)",
                asFUNCTION(Scoped), asCALL_GENERIC) < 0 ||
            engine->EndConfigGroup() < 0) {
            engine->ShutDownAndRelease(); return 3;
        }
        engine->SetDefaultAccessMask(0x2u);
        if (engine->RegisterGlobalFunction("int Hidden()", asFUNCTION(Hidden), asCALL_GENERIC) < 0 ||
            engine->SetDefaultNamespace("") < 0) {
            engine->ShutDownAndRelease(); return 3;
        }
        engine->SetDefaultAccessMask(~asDWORD(0));
    }
    engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, true);
    engine->SetMessageCallback(asFUNCTION(MessageCallback), nullptr, asCALL_CDECL);
    RegisterScriptWeakRef(engine);
    if (engine->RegisterGlobalProperty("int hostCounter", &hostCounter) < 0) {
        engine->ShutDownAndRelease();
        return 4;
    }
    if (engine->RegisterEnum("HostColor") < 0 ||
        engine->RegisterEnumValue("HostColor", "HostRed", 40) < 0 ||
        engine->RegisterEnumValue("HostColor", "HostBlue", 42) < 0 ||
        engine->RegisterTypedef("HostScore", "int") < 0 ||
        engine->RegisterFuncdef("HostScore HostTransform(HostScore value)") < 0 ||
        engine->RegisterGlobalFunction("HostScore Lift(HostScore value)",
            asFUNCTION(Lift), asCALL_GENERIC) < 0) {
        engine->ShutDownAndRelease();
        return 5;
    }
    asITypeInfo* reflectedEnum = engine->GetTypeInfoByName("HostColor");
    asITypeInfo* reflectedAlias = engine->GetTypeInfoByName("HostScore");
    asITypeInfo* reflectedFuncdef = nullptr;
    for (asUINT index = 0; index < engine->GetFuncdefCount(); ++index) {
        asITypeInfo* candidate = engine->GetFuncdefByIndex(index);
        if (candidate && std::strcmp(candidate->GetName(), "HostTransform") == 0) {
            reflectedFuncdef = candidate;
            break;
        }
    }
    asIScriptFunction* reflectedLift =
        engine->GetGlobalFunctionByDecl("HostScore Lift(HostScore)");
    if (!reflectedEnum || reflectedEnum->GetEnumValueCount() != 2 ||
        !reflectedAlias || reflectedAlias->GetTypedefTypeId() < 0 ||
        !reflectedFuncdef || !reflectedFuncdef->GetFuncdefSignature() || !reflectedLift) {
        engine->ShutDownAndRelease();
        return 5;
    }
    if (engine->RegisterObjectType("HostValue", sizeof(CompatValue),
            asOBJ_VALUE | asGetTypeTraits<CompatValue>()) < 0 ||
        engine->RegisterObjectBehaviour("HostValue", asBEHAVE_CONSTRUCT, "void f()",
            asFUNCTION(CompatValueConstruct), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectBehaviour("HostValue", asBEHAVE_DESTRUCT, "void f()",
            asFUNCTION(CompatValueDestruct), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectBehaviour("HostValue", asBEHAVE_CONSTRUCT,
            "void f(const HostValue &in)", asFUNCTION(CompatValueCopyConstruct),
            asCALL_GENERIC) < 0 ||
        engine->RegisterObjectMethod("HostValue",
            "HostValue &opAssign(const HostValue &in)",
            asFUNCTION(CompatValueAssign), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectMethod("HostValue", "int get() const",
            asFUNCTION(GetHostValue), asCALL_GENERIC) < 0 ||
        engine->RegisterGlobalFunction("int ReadHostValue(HostValue value)",
            asFUNCTION(ReadHostValue), asCALL_GENERIC) < 0) {
        engine->ShutDownAndRelease();
        return 6;
    }
    if (engine->RegisterObjectType("HostRef", 0, asOBJ_REF) < 0 ||
        engine->RegisterObjectProperty("HostRef", "int value",
            asOFFSET(CompatReference, value)) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_FACTORY,
            "HostRef@ f(int value)", asFUNCTION(CompatReferenceFactory), asCALL_GENERIC) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_ADDREF,
            "void f()", asMETHOD(CompatReference, AddRef), asCALL_THISCALL) < 0 ||
        engine->RegisterObjectBehaviour("HostRef", asBEHAVE_RELEASE,
            "void f()", asMETHOD(CompatReference, Release), asCALL_THISCALL) < 0 ||
        engine->RegisterObjectMethod("HostRef", "int get() const",
            asMETHOD(CompatReference, Get), asCALL_THISCALL) < 0) {
        engine->ShutDownAndRelease();
        return 7;
    }
    const bool importedFunctions =
        std::string(argv[1]).find("imported_functions") != std::string::npos;
    const bool sharedEntities =
        std::string(argv[1]).find("shared_entities") != std::string::npos;
    const bool externalEntities =
        std::string(argv[1]).find("external_entities") != std::string::npos;
    if (importedFunctions || sharedEntities || externalEntities) {
        asIScriptModule* sourceModule = engine->GetModule(
            importedFunctions ? "math" : "shared-source", asGM_ALWAYS_CREATE);
        const char* sourceCode = importedFunctions
            ? "int total = 40; int Add(int value) { total += value; return total; }"
            : "shared interface ICounter { int read(); } "
              "shared class Counter : ICounter { int value; Counter(int start) { value = start; } "
              "int read() { return value; } } "
              "shared int Twice(int value) { return value * 2; } "
              "Counter@ Make() { return Counter(40); }";
        sourceModule->AddScriptSection("math.as", sourceCode);
        if (sourceModule->Build() < 0) {
            engine->ShutDownAndRelease(); return 8;
        }
    }
    asIScriptModule* module = engine->GetModule("compat", asGM_ALWAYS_CREATE);
    if (hostControls) module->SetAccessMask(0x1u);
    const std::string source = ReadFile(argv[1]);
    module->AddScriptSection("compat.as", source.c_str(), source.size());
    if (module->Build() < 0) { engine->ShutDownAndRelease(); return 8; }
    if ((importedFunctions || sharedEntities) &&
        module->BindAllImportedFunctions() < 0) {
        engine->ShutDownAndRelease(); return 8;
    }
    if (hostControls && engine->RemoveConfigGroup("runtime") != asCONFIG_GROUP_IS_IN_USE) {
        engine->ShutDownAndRelease(); return 8;
    }
    if (std::string(argv[1]).find("registered_named_types") != std::string::npos &&
        (module->GetGlobalVarCount() != 2 ||
         module->GetGlobalVarIndexByDecl("HostColor selected") < 0 ||
         module->GetGlobalVarIndexByDecl("HostTransform@ transform") < 0)) {
        engine->ShutDownAndRelease();
        return 8;
    }
    if (std::string(argv[1]).find("registered_named_types") != std::string::npos) {
        asIScriptFunction* detached = nullptr;
        if (module->CompileFunction("dynamic-detached",
                "int detached_probe() { return main(); }", 0, 0, &detached) < 0 ||
            !detached || module->GetFunctionByDecl("int detached_probe()") ||
            module->CompileFunction("dynamic-added",
                "int dynamic_probe() { return main(); }", 0,
                asCOMP_ADD_TO_MODULE, nullptr) < 0 ||
            !module->GetFunctionByDecl("int dynamic_probe()")) {
            if (detached) detached->Release();
            engine->ShutDownAndRelease();
            return 8;
        }
        detached->Release();
        asIScriptFunction* added = module->GetFunctionByDecl("int dynamic_probe()");
        if (!added || module->CompileFunction("dynamic-caller",
                "int dynamic_caller() { return dynamic_probe(); }", 0,
                asCOMP_ADD_TO_MODULE, nullptr) < 0 ||
            !module->GetFunctionByDecl("int dynamic_caller()")) {
            engine->ShutDownAndRelease();
            return 8;
        }
        MemoryBytecodeStream bytecode;
        if (module->SaveByteCode(&bytecode) < 0) {
            engine->ShutDownAndRelease();
            return 8;
        }
        bytecode.Rewind();
        asIScriptModule* loaded = engine->GetModule("compat-loaded", asGM_ALWAYS_CREATE);
        if (loaded->LoadByteCode(&bytecode) < 0 ||
            !loaded->GetFunctionByDecl("int main()") ||
            !loaded->GetFunctionByDecl("int dynamic_caller()") ||
            !loaded->GetFunctionByDecl("int dynamic_probe()") ||
            module->RemoveFunction(added) < 0 ||
            module->GetFunctionByDecl("int dynamic_probe()") ||
            !module->GetFunctionByDecl("int dynamic_caller()")) {
            engine->ShutDownAndRelease();
            return 8;
        }
    }
    asIScriptFunction* function = module->GetFunctionByDecl("int main()");
    if (!function) { engine->ShutDownAndRelease(); return 9; }
    asIScriptContext* context = engine->CreateContext();
    context->Prepare(function);
    const bool debugIntrospection =
        std::string(argv[1]).find("debug_introspection") != std::string::npos;
    DebugProbe debugProbe;
    if (debugIntrospection)
        context->SetLineCallback(asFUNCTION(DebugLineCallback), &debugProbe, asCALL_CDECL);
    const int state = context->Execute();
    if (state != asEXECUTION_FINISHED) {
        context->Release();
        engine->ShutDownAndRelease();
        return 9;
    }
    if (debugIntrospection && !debugProbe.printed) {
        context->Release();
        engine->ShutDownAndRelease();
        return 9;
    }
    std::cout << "state=finished\nreturn=int:" << context->GetReturnDWord() << '\n';
    context->Release();
    engine->ShutDownAndRelease();
    return 0;
}
