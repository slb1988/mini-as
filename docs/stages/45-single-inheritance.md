# Stage 45: single inheritance and polymorphism

Script classes can now inherit from one script class while continuing to
implement interfaces, for example `class Derived : Base, IValue`. The type
checker resolves forward-declared hierarchies, rejects cycles and multiple
concrete bases, flattens inherited fields ahead of derived fields, validates
override signatures, and permits implicit handle conversions from a derived
class to any base class.

All ordinary script methods participate in virtual dispatch, matching
AngelScript's default method semantics. Each class receives a stable virtual
layout: inherited slots keep their position, overrides replace the
implementation in place, and new overloads append slots. `CallVirtual` uses the
receiver's static class and slot while the VM selects the implementation from
the object's real `TypeId`. A qualified call such as `Base::method()` bypasses
the virtual slot and invokes that base implementation directly.

Derived constructors may call `super(...)` once. When omitted, the compiler
injects the nearest available default base constructor; classes without their
own constructor receive the same implicit chain at the allocation site. Base
field slots precede derived slots and field initializers run base-to-derived.
The current teaching implementation intentionally does not model every
path-sensitive placement rule for `super()` inside branches.

Finalization retains the Stage 44 engine queue. A derived object's finalizer
binding now stores the complete destructor chain and runs it from most-derived
to base. Failure in one destructor is diagnosed with its source location and
does not prevent the remaining base destructors from running.

This stage does not add explicit downcasts, `final`, `abstract`, `override`, or
member access control. Those remain separate roadmap features.
