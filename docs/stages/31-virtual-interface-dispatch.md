# Stage 31: Virtual interface dispatch

Interface methods receive stable slots in declaration order. Calls through an
interface handle emit `CALL_VIRTUAL` with the interface `TypeId` and slot, not a
concrete function address.

The module image contains dispatch entries keyed by concrete `TypeId`, interface
`TypeId`, and slot. At runtime the VM reads the receiver's actual type, resolves
the implementing `FunctionId`, and enters that ordinary script method frame.
The receiver remains hidden local zero. Missing implementations are compile
errors; null receivers and absent runtime entries are located VM exceptions.
