#include "test.hpp"
#include "mini_as/engine.hpp"

namespace {
class HostThing final : public mini_as::RefObject {
public:
    HostThing(const mini_as::TypeInfo* type, int value, int& destroyed)
        : RefObject(type), value(value), destroyed_(destroyed) {}
    int value;
private:
    ~HostThing() override { ++destroyed_; }
    int& destroyed_;
};

struct HostNumber {
    int value = 42;
};
}

TEST_CASE(object_handles_addref_release_deterministically) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    {
        mini_as::ObjectHandle first(new HostThing(type, 7, destroyed));
        CHECK(first.Get()->RefCount() == 1);
        { mini_as::ObjectHandle second = first; CHECK(first.Get()->RefCount() == 2); }
        CHECK(first.Get()->RefCount() == 1);
    }
    CHECK(destroyed == 1);
}

TEST_CASE(weak_object_handles_lock_without_owning_and_expire_after_release) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    int destroyed = 0;
    mini_as::WeakObjectHandle weak("Thing");
    {
        mini_as::ObjectHandle owner(new HostThing(type, 7, destroyed));
        weak = mini_as::WeakObjectHandle(owner, "Thing");
        CHECK(!weak.Expired());
        mini_as::ObjectHandle locked = weak.Lock();
        owner = {};
        CHECK(destroyed == 0);
        CHECK(locked.Get() != nullptr);
    }
    CHECK(destroyed == 1);
    CHECK(weak.Expired());
    CHECK(!weak.Lock());
}

TEST_CASE(object_handles_round_trip_through_script_and_generic_bridge) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    int destroyed = 0;
    CHECK(engine->RegisterGlobalFunction("Thing@ Identity(Thing@ value)", [](mini_as::GenericCall& call) {
        call.SetReturnObject(call.GetArgObject(0));
    }));
    auto* module = engine->GetModule("objects");
    module->AddScriptSection("objects", "Thing@ pass(Thing@ value) { return Identity(value); }");
    CHECK(module->Build());
    mini_as::ObjectHandle object(new HostThing(type, 9, destroyed));
    {
        auto context = engine->CreateContext();
        CHECK(context->Prepare(module->GetFunctionByName("pass")));
        CHECK(context->SetArgObject(0, object));
        CHECK(context->Execute() == mini_as::ExecutionState::Finished);
        auto returned = context->GetReturnValue().As<mini_as::ObjectHandle>();
        CHECK(returned.Get() == object.Get());
    }
    CHECK(destroyed == 0);
    object = {};
    CHECK(destroyed == 1);
}

TEST_CASE(context_rejects_wrong_object_type) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* thing = engine->RegisterObjectType("Thing");
    const auto* other = engine->RegisterObjectType("Other");
    int destroyed = 0;
    auto* module = engine->GetModule("object-types");
    module->AddScriptSection("types", "Thing@ pass(Thing@ value) { return value; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("pass")));
    mini_as::ObjectHandle wrong(new HostThing(other, 0, destroyed));
    CHECK(!context->SetArgObject(0, wrong));
    (void)thing;
}

TEST_CASE(script_classes_allocate_fields_and_cross_context_boundaries) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("script-objects");
    module->AddScriptSection("classes",
        "class Box { int value; string label; }"
        "Box@ makeBox(int value) { Box@ box = Box(); box.value = value; box.label = \"answer\"; return box; }"
        "int readBox(Box@ box) { return box.value; }");
    CHECK(module->Build());
    auto make = engine->CreateContext();
    CHECK(make->Prepare(module->GetFunctionByName("makeBox")));
    CHECK(make->SetArgInt(0, 42));
    CHECK(make->Execute() == mini_as::ExecutionState::Finished);
    mini_as::ObjectHandle box = make->GetReturnValue().As<mini_as::ObjectHandle>();
    auto* scriptBox = dynamic_cast<mini_as::ScriptObject*>(box.Get());
    CHECK(scriptBox != nullptr);
    CHECK(scriptBox->FieldCount() == 2);
    CHECK(scriptBox->GetField(1).As<std::string>() == "answer");

    auto read = engine->CreateContext();
    CHECK(read->Prepare(module->GetFunctionByName("readBox")));
    CHECK(read->SetArgObject(0, box));
    CHECK(read->Execute() == mini_as::ExecutionState::Finished);
    CHECK(read->GetReturnInt() == 42);
}

TEST_CASE(script_class_interface_table_is_validated_and_resolvable) {
    auto engine = mini_as::CreateScriptEngine();
    auto* module = engine->GetModule("interfaces");
    module->AddScriptSection("interfaces",
        "interface IValue { int get(); }"
        "class Box : IValue { int value; int get() { return value; } }"
        "Box@ make() { return Box(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByName("make")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    auto box = context->GetReturnValue().As<mini_as::ObjectHandle>();
    auto* object = dynamic_cast<mini_as::ScriptObject*>(box.Get());
    CHECK(object != nullptr);
    CHECK(object->Implements("IValue"));
    CHECK(object->ResolveInterfaceMethod("IValue", "int get()") == "Box::int get()");
}

TEST_CASE(script_class_missing_interface_method_is_compile_error) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& message) { diagnostics.push_back(message); });
    auto* module = engine->GetModule("bad-interface");
    module->AddScriptSection("bad-interface",
        "interface IValue { int get(); } class Empty : IValue { int value; }");
    CHECK(!module->Build());
    CHECK(!diagnostics.empty());
}

TEST_CASE(registered_reference_factories_construct_overloads_and_release_objects) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f()",
        [type, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(new HostThing(type, 7, destroyed)));
        }));
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f(int value)",
        [type, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(
                new HostThing(type, call.GetArgInt(0), destroyed)));
        }));
    CHECK(engine->RegisterGlobalFunction("int Read(Thing@ value)",
        [](mini_as::GenericCall& call) {
            const auto* thing = dynamic_cast<HostThing*>(call.GetArgObject(0).Get());
            call.SetReturnInt(thing ? thing->value : -1);
        }));
    auto* module = engine->GetModule("reference-factories");
    module->AddScriptSection("reference-factories",
        "int run() { Thing@ first = Thing(); Thing@ second = Thing(35); "
        "return Read(first) + Read(second); }");
    CHECK(module->Build());
    {
        auto context = engine->CreateContext();
        CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
        CHECK(context->Execute() == mini_as::ExecutionState::Finished);
        CHECK(context->GetReturnInt() == 42);
        CHECK(destroyed == 2);
    }
    CHECK(destroyed == 2);
}

TEST_CASE(reference_factory_registration_rejects_invalid_types_declarations_and_duplicates) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    auto factory = [type](mini_as::GenericCall& call) {
        static int destroyed = 0;
        call.SetReturnObject(mini_as::ObjectHandle(new HostThing(type, 0, destroyed)));
    };
    CHECK(!engine->RegisterObjectFactory("Missing", "Missing@ f()", factory));
    CHECK(!engine->RegisterObjectFactory("Thing", "int f()", factory));
    CHECK(!engine->RegisterObjectFactory("Thing", "Thing@ create()", factory));
    CHECK(!engine->RegisterObjectFactory("Thing", "Thing@ f()", {}));
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f()", factory));
    CHECK(!engine->RegisterObjectFactory("Thing", "Thing@ f()", factory));
    CHECK(diagnostics.size() >= 5);
}

TEST_CASE(registered_reference_types_without_factories_are_not_script_constructible) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(engine->RegisterObjectType("Singleton") != nullptr);
    auto* module = engine->GetModule("missing-reference-factory");
    module->AddScriptSection("missing-reference-factory",
        "int run() { Singleton@ value = Singleton(); return value is null ? 0 : 1; }");
    CHECK(!module->Build());
    bool missing = false;
    for (const auto& diagnostic : diagnostics)
        missing = missing || diagnostic.message.find("no matching factory for 'Singleton'") !=
            std::string::npos;
    CHECK(missing);
}

TEST_CASE(reference_factory_failures_report_the_construction_location) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterObjectType("Thing") != nullptr);
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f(int value)",
        [](mini_as::GenericCall& call) { call.SetException("factory rejected value"); }));
    auto* module = engine->GetModule("reference-factory-error");
    module->AddScriptSection("reference-factory-error",
        "int run() {\n"
        "  Thing@ value = Thing(42);\n"
        "  return 0;\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "factory rejected value");
    CHECK(context->GetExceptionLocation().section == "reference-factory-error");
    CHECK(context->GetExceptionLocation().row == 2);
}

TEST_CASE(reference_factories_reject_null_results_without_exceptions) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterObjectType("Thing") != nullptr);
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f()",
        [](mini_as::GenericCall& call) { call.SetReturnObject({}); }));
    auto* module = engine->GetModule("null-reference-factory");
    module->AddScriptSection("null-reference-factory", "int run() { Thing(); return 0; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("returned null without an exception") !=
        std::string::npos);
}

TEST_CASE(reference_factories_reject_objects_of_the_wrong_registered_type) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* expected = engine->RegisterObjectType("Thing");
    const auto* other = engine->RegisterObjectType("Other");
    CHECK(expected != nullptr);
    CHECK(other != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f()",
        [other, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(new HostThing(other, 0, destroyed)));
        }));
    auto* module = engine->GetModule("wrong-reference-factory-type");
    module->AddScriptSection("wrong-reference-factory-type",
        "int run() {\n"
        "  Thing();\n"
        "  return 0;\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("object factory returned Other@ but declared Thing@") !=
        std::string::npos);
    CHECK(context->GetExceptionLocation().row == 2);
    CHECK(destroyed == 1);
}

TEST_CASE(registered_object_methods_receive_this_and_support_reference_writeback) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f(int value)",
        [type, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(new HostThing(
                type, call.GetArgInt(0), destroyed)));
        }));
    CHECK(engine->RegisterObjectMethod("Thing", "void add(int value)",
        [](mini_as::GenericCall& call) {
            auto* thing = dynamic_cast<HostThing*>(call.GetObject().Get());
            CHECK(thing != nullptr);
            thing->value += call.GetArgInt(0);
        }));
    CHECK(engine->RegisterObjectMethod("Thing", "int get() const",
        [](mini_as::GenericCall& call) {
            const auto* thing = dynamic_cast<const HostThing*>(call.GetObject().Get());
            CHECK(thing != nullptr);
            call.SetReturnInt(thing->value);
        }));
    CHECK(engine->RegisterObjectMethod("Thing", "void answer(int &out value)",
        [](mini_as::GenericCall& call) {
            CHECK(call.GetObject());
            call.SetArgInt(0, 42);
        }));
    auto* module = engine->GetModule("registered-object-methods");
    module->AddScriptSection("registered-object-methods",
        "int run() { Thing@ value = Thing(40); value.add(2); int output; "
        "value.answer(output); return value.get() + output - 42; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(registered_object_factories_and_methods_accept_variadic_arguments) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory(
        "Thing", "Thing@ f(const int &in ...)",
        [type, &destroyed](mini_as::GenericCall& call) {
            int value = 0;
            for (std::size_t index = 0; index < call.GetArgCount(); ++index)
                value += call.GetArgInt(index);
            call.SetReturnObject(mini_as::ObjectHandle(new HostThing(type, value, destroyed)));
        }));
    CHECK(engine->RegisterObjectMethod(
        "Thing", "int add(const int &in ...) const",
        [](mini_as::GenericCall& call) {
            auto* thing = dynamic_cast<HostThing*>(call.GetObject().Get());
            CHECK(thing != nullptr);
            for (std::size_t index = 0; index < call.GetArgCount(); ++index)
                thing->value += call.GetArgInt(index);
            call.SetReturnInt(thing->value);
        }));
    auto* module = engine->GetModule("object-variadics");
    module->AddScriptSection("object-variadics.as",
        "int run() { Thing@ value = Thing(20, 20); return value.add(1, 1); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(destroyed == 1);
}

TEST_CASE(wildcard_variadic_out_retains_static_object_handle_type) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterGlobalFunction(
        "void Make(? &out ...)", [type, &destroyed](mini_as::GenericCall& call) {
            CHECK(call.GetArgCount() == 1);
            CHECK(call.GetArgType(0) == mini_as::DataType::Object("Thing", true));
            call.SetArgObject(0, mini_as::ObjectHandle(new HostThing(type, 42, destroyed)));
        }));
    auto* module = engine->GetModule("wildcard-object-out");
    module->AddScriptSection("wildcard-object-out.as",
        "int run() { Thing@ value; Make(value); return value is null ? 0 : 42; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(destroyed == 1);
}

TEST_CASE(registered_object_methods_support_explicit_template_arguments) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory(
        "Thing", "Thing@ f()", [type, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(
                mini_as::ObjectHandle(new HostThing(type, 0, destroyed)));
        }));
    CHECK(engine->RegisterObjectMethod(
        "Thing", "T echo<class T>(T value) const",
        [](mini_as::GenericCall& call) {
            CHECK(call.GetObject());
            CHECK(call.GetTemplateArgCount() == 1);
            CHECK(call.GetTemplateArgType(0) == call.GetArgType(0));
            call.SetReturn(call.GetArg(0));
        }));
    auto* module = engine->GetModule("template-methods");
    module->AddScriptSection("template-methods.as",
        "int run() { Thing@ value = Thing(); return value.echo<int>(40) + "
        "(value.echo<string>(\"ok\") == \"ok\" ? 2 : 0); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
    CHECK(destroyed == 1);
}

TEST_CASE(object_method_registration_rejects_invalid_owners_callbacks_and_duplicates) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(engine->RegisterObjectType("Thing") != nullptr);
    auto method = [](mini_as::GenericCall& call) { call.SetReturnInt(42); };
    CHECK(!engine->RegisterObjectMethod("Missing", "int get() const", method));
    CHECK(!engine->RegisterObjectMethod("Thing", "int get() const", {}));
    CHECK(!engine->RegisterObjectMethod("Thing", "int &get()", method));
    CHECK(engine->RegisterObjectMethod("Thing", "int get() const", method));
    CHECK(!engine->RegisterObjectMethod("Thing", "int get() const", method));
    CHECK(diagnostics.size() >= 4);
}

TEST_CASE(registered_object_methods_are_not_published_as_global_functions) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(engine->RegisterObjectType("Thing") != nullptr);
    CHECK(engine->RegisterObjectMethod("Thing", "int get() const",
        [](mini_as::GenericCall& call) { call.SetReturnInt(42); }));
    auto* module = engine->GetModule("method-global-visibility");
    module->AddScriptSection("method-global-visibility", "int run() { return get(); }");
    CHECK(!module->Build());
    bool hidden = false;
    for (const auto& diagnostic : diagnostics)
        hidden = hidden || diagnostic.message.find("no matching function for 'get'") !=
            std::string::npos;
    CHECK(hidden);
}

TEST_CASE(null_registered_object_method_receivers_report_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterObjectType("Thing") != nullptr);
    CHECK(engine->RegisterObjectMethod("Thing", "int get() const",
        [](mini_as::GenericCall& call) { call.SetReturnInt(42); }));
    auto* module = engine->GetModule("null-host-method");
    module->AddScriptSection("null-host-method",
        "int run() {\n"
        "  Thing@ value;\n"
        "  return value.get();\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "null host method receiver");
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(registered_object_method_exceptions_report_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f()",
        [type, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(new HostThing(type, 0, destroyed)));
        }));
    CHECK(engine->RegisterObjectMethod("Thing", "void fail()",
        [](mini_as::GenericCall& call) { call.SetException("method failed"); }));
    auto* module = engine->GetModule("host-method-error");
    module->AddScriptSection("host-method-error",
        "int run() {\n"
        "  Thing@ value = Thing();\n"
        "  value.fail();\n"
        "  return 0;\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString() == "method failed");
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(registered_object_properties_support_lvalues_and_reference_writeback) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f(int value)",
        [type, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(new HostThing(
                type, call.GetArgInt(0), destroyed)));
        }));
    CHECK(engine->RegisterObjectProperty("Thing", "int value",
        [](const mini_as::ObjectHandle& object) {
            const auto* thing = dynamic_cast<const HostThing*>(object.Get());
            return mini_as::Value(thing ? thing->value : -1);
        },
        [](const mini_as::ObjectHandle& object, mini_as::Value value) {
            auto* thing = dynamic_cast<HostThing*>(object.Get());
            if (!thing) throw std::runtime_error("invalid Thing receiver");
            thing->value = value.As<std::int32_t>();
        }));
    CHECK(engine->RegisterGlobalFunction("void SetAnswer(int &out value)",
        [](mini_as::GenericCall& call) { call.SetArgInt(0, 42); }));
    auto* module = engine->GetModule("registered-object-properties");
    module->AddScriptSection("registered-object-properties",
        "int run() { Thing@ item = Thing(39); item.value += 1; item.value++; "
        "SetAnswer(item.value); return item.value; }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 42);
}

TEST_CASE(const_registered_object_properties_reject_assignment_and_output_references) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterObjectType("Thing") != nullptr);
    CHECK(engine->RegisterObjectProperty("Thing", "const int identity",
        [](const mini_as::ObjectHandle&) { return mini_as::Value(std::int32_t{42}); }));
    CHECK(engine->RegisterGlobalFunction("void Reset(int &out value)",
        [](mini_as::GenericCall& call) { call.SetArgInt(0, 0); }));
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    auto* module = engine->GetModule("const-object-property");
    module->AddScriptSection("const-object-property",
        "int run(Thing@ item) { item.identity = 1; Reset(item.identity); return 0; }");
    CHECK(!module->Build());
    bool readOnly = false, output = false;
    for (const auto& diagnostic : diagnostics) {
        readOnly = readOnly || diagnostic.message.find("field 'identity' is read-only") !=
            std::string::npos;
        output = output || diagnostic.message.find("const value cannot be passed") !=
            std::string::npos;
    }
    CHECK(readOnly);
    CHECK(output);
}

TEST_CASE(object_property_registration_rejects_invalid_accessors_and_duplicates) {
    auto engine = mini_as::CreateScriptEngine();
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(engine->RegisterObjectType("Thing") != nullptr);
    auto getter = [](const mini_as::ObjectHandle&) { return mini_as::Value(std::int32_t{0}); };
    auto setter = [](const mini_as::ObjectHandle&, mini_as::Value) {};
    CHECK(!engine->RegisterObjectProperty("Missing", "int value", getter, setter));
    CHECK(!engine->RegisterObjectProperty("Thing", "not a declaration", getter, setter));
    CHECK(!engine->RegisterObjectProperty("Thing", "int value", {}, setter));
    CHECK(!engine->RegisterObjectProperty("Thing", "int value", getter));
    CHECK(!engine->RegisterObjectProperty("Thing", "const int id", getter, setter));
    CHECK(engine->RegisterObjectProperty("Thing", "int value", getter, setter));
    CHECK(!engine->RegisterObjectProperty("Thing", "int value", getter, setter));
    CHECK(diagnostics.size() >= 6);
}

TEST_CASE(registered_object_property_getter_failures_report_the_access_location) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f()",
        [type, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(new HostThing(type, 0, destroyed)));
        }));
    CHECK(engine->RegisterObjectProperty("Thing", "int value",
        [](const mini_as::ObjectHandle&) { return mini_as::Value("wrong type"); },
        [](const mini_as::ObjectHandle&, mini_as::Value) {}));
    auto* module = engine->GetModule("host-property-getter-error");
    module->AddScriptSection("host-property-getter-error",
        "int run() {\n"
        "  Thing@ item = Thing();\n"
        "  return item.value;\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("getter returned string but declared int") !=
        std::string::npos);
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(registered_object_property_setter_exceptions_report_the_assignment_location) {
    auto engine = mini_as::CreateScriptEngine();
    const auto* type = engine->RegisterObjectType("Thing");
    CHECK(type != nullptr);
    int destroyed = 0;
    CHECK(engine->RegisterObjectFactory("Thing", "Thing@ f()",
        [type, &destroyed](mini_as::GenericCall& call) {
            call.SetReturnObject(mini_as::ObjectHandle(new HostThing(type, 0, destroyed)));
        }));
    CHECK(engine->RegisterObjectProperty("Thing", "int value",
        [](const mini_as::ObjectHandle&) { return mini_as::Value(std::int32_t{0}); },
        [](const mini_as::ObjectHandle&, mini_as::Value) {
            throw std::runtime_error("setter refused value");
        }));
    auto* module = engine->GetModule("host-property-setter-error");
    module->AddScriptSection("host-property-setter-error",
        "int run() {\n"
        "  Thing@ item = Thing();\n"
        "  item.value = 42;\n"
        "  return 0;\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("host property exception: setter refused value") !=
        std::string::npos);
    CHECK(context->GetExceptionLocation().row == 3);
}

TEST_CASE(registered_value_types_default_copy_pass_return_and_write_back_by_value) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterValueType("HostNumber",
        mini_as::Value::HostValue("HostNumber", HostNumber{})) != nullptr);
    CHECK(engine->RegisterObjectMethod("HostNumber", "int get() const",
        [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetObjectValue().AsHostValue<HostNumber>().value);
        }));
    CHECK(engine->RegisterGlobalFunction("int ReadNumber(HostNumber value)",
        [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetArg(0).AsHostValue<HostNumber>().value);
        }));
    CHECK(engine->RegisterGlobalFunction("void IncrementNumber(HostNumber &inout value)",
        [](mini_as::GenericCall& call) {
            mini_as::Value updated = call.GetArg(0);
            ++updated.AsHostValue<HostNumber>().value;
            call.SetArg(0, std::move(updated));
        }));
    auto* module = engine->GetModule("registered-value-types");
    module->AddScriptSection("registered-value-types",
        "HostNumber global; HostNumber identity(HostNumber value) { return value; } "
        "int run() { HostNumber original; HostNumber copied = HostNumber(original); "
        "IncrementNumber(copied); HostNumber returned = identity(copied); "
        "return global.get() * 10000 + original.get() * 100 + returned.get(); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    const auto state = context->Execute();
    if (state != mini_as::ExecutionState::Finished)
        throw std::runtime_error(context->GetExceptionString());
    CHECK(context->GetReturnInt() == 424243);
}

TEST_CASE(contexts_accept_registered_value_type_arguments) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterValueType("HostNumber",
        mini_as::Value::HostValue("HostNumber", HostNumber{})) != nullptr);
    CHECK(engine->RegisterGlobalFunction("int ReadNumber(HostNumber value)",
        [](mini_as::GenericCall& call) {
            call.SetReturnInt(call.GetArg(0).AsHostValue<HostNumber>().value);
        }));
    auto* module = engine->GetModule("value-context-arguments");
    module->AddScriptSection("value-context-arguments",
        "int read(HostNumber value) { return ReadNumber(value); }");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int read(HostNumber)")));
    HostNumber number;
    number.value = 77;
    CHECK(context->SetArgValue(0, mini_as::Value::HostValue("HostNumber", number)));
    CHECK(context->Execute() == mini_as::ExecutionState::Finished);
    CHECK(context->GetReturnInt() == 77);
}

TEST_CASE(value_type_registration_and_construction_reject_invalid_uses) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterValueType("HostNumber",
        mini_as::Value::HostValue("WrongName", HostNumber{})) == nullptr);
    CHECK(engine->RegisterValueType("HostNumber",
        mini_as::Value::HostValue("HostNumber", HostNumber{})) != nullptr);
    CHECK(engine->RegisterValueType("HostNumber",
        mini_as::Value::HostValue("HostNumber", HostNumber{})) == nullptr);
    std::vector<mini_as::Diagnostic> diagnostics;
    engine->SetMessageCallback([&](const mini_as::Diagnostic& diagnostic) {
        diagnostics.push_back(diagnostic);
    });
    CHECK(!engine->RegisterObjectMethod("HostNumber", "void mutate()",
        [](mini_as::GenericCall&) {}));
    auto* module = engine->GetModule("invalid-value-construction");
    module->AddScriptSection("invalid-value-construction",
        "int run() { HostNumber value = HostNumber(42); return 0; }");
    CHECK(!module->Build());
    bool invalid = false;
    bool mutableMethod = false;
    for (const auto& diagnostic : diagnostics)
        invalid = invalid || diagnostic.message.find("no matching value constructor for 'HostNumber'") !=
            std::string::npos;
    for (const auto& diagnostic : diagnostics)
        mutableMethod = mutableMethod || diagnostic.message.find("value type methods must be const") !=
            std::string::npos;
    CHECK(invalid);
    CHECK(mutableMethod);
}

TEST_CASE(registered_value_type_return_mismatches_report_the_call_location) {
    auto engine = mini_as::CreateScriptEngine();
    CHECK(engine->RegisterValueType("HostNumber",
        mini_as::Value::HostValue("HostNumber", HostNumber{})) != nullptr);
    CHECK(engine->RegisterValueType("OtherNumber",
        mini_as::Value::HostValue("OtherNumber", HostNumber{})) != nullptr);
    CHECK(engine->RegisterGlobalFunction("HostNumber WrongNumber()",
        [](mini_as::GenericCall& call) {
            call.SetReturn(mini_as::Value::HostValue("OtherNumber", HostNumber{}));
        }));
    auto* module = engine->GetModule("wrong-value-return");
    module->AddScriptSection("wrong-value-return",
        "int run() {\n"
        "  HostNumber value = WrongNumber();\n"
        "  return 0;\n"
        "}\n");
    CHECK(module->Build());
    auto context = engine->CreateContext();
    CHECK(context->Prepare(module->GetFunctionByDecl("int run()")));
    CHECK(context->Execute() == mini_as::ExecutionState::Exception);
    CHECK(context->GetExceptionString().find("returned OtherNumber but declared HostNumber") !=
        std::string::npos);
    CHECK(context->GetExceptionLocation().row == 2);
}

