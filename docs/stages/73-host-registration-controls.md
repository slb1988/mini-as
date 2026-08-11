# Stage 73: host namespaces, access masks, and configuration groups

## Goal

This stage aligns the remaining AngelScript 2.38.0 host-registration controls:
default namespaces, default and per-module access masks, and removable
configuration groups. The native API and `mini_as::compat` facade expose the
same control flow; compat maps it to the official integer constants.

## Namespaces

`ScriptEngine::SetDefaultNamespace` qualifies subsequently registered global
functions, properties, object types, enums, typedefs, and funcdefs. Object
factories, methods, and properties resolve an unqualified owner and referenced
registered types relative to the active host namespace.

`ScriptModule::SetDefaultNamespace` affects function lookup and dynamic
function compilation. A dynamic function is parsed in that namespace, so
unqualified calls resolve against sibling script and host declarations. Normal
script sections retain their explicit source namespaces.

Namespace names are validated as `identifier(::identifier)*`; the empty string
restores global scope.

## Access masks

Each host registration captures the engine's current 32-bit default access
mask. A module captures that default at creation and may replace it with
`SetAccessMask`. Only registrations whose mask intersects the module mask are
injected into its parser/type-checker/compiler environment.

Filtering applies consistently to global functions/properties, object types
and their registered members, enums, typedefs, and funcdefs. Hidden symbols
therefore fail as ordinary unknown or unavailable declarations instead of
reaching a late VM binding failure.

## Configuration groups

`BeginConfigGroup` opens one non-nested group, registrations capture its name,
and `EndConfigGroup` closes it. `RemoveConfigGroup` first scans live module
environments. Removal is rejected while a module depends on a group.

Successful removal marks registrations inactive instead of erasing entries
from pointer-stable host deques. This is important because bytecode images and
prepared contexts can retain callback descriptors for unrelated groups.
Inactive registrations are excluded from future builds, dynamic compilation,
reflection-oriented type lookup, and bytecode host rebinding.

## Verification

Tests cover:

- qualified host function registration and explicit script lookup;
- module-default namespace lookup and dynamic compilation;
- default-mask inheritance, previous-mask return values, and per-module mask
  filtering with a negative hidden-symbol build;
- invalid namespace syntax and nested/unbalanced configuration group calls;
- successful removal of an unused group and rejection after a module uses it;
- differential execution and in-use removal results against the local official
  AngelScript 2.38.0 tag.

The implementation remains intentionally portable: groups do not introduce
native ABI calling conventions, SDK interface vtables, or platform bridges.
