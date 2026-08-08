# Stage 30: Field initializers

Fields may carry assignment-style initializers. Their expressions are checked in
class scope, so earlier fields and instance methods are available through
implicit `this`. Incompatible initializer types are compile errors.

`ScriptObject` first provides typed default values. At each construction site,
the compiler preserves the new object in a hidden local and evaluates explicit
field initializers in declaration order before invoking any selected constructor.
Runtime failures retain the field declaration's source location.
