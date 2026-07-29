# 14 - Trial-deletion cycle collection

Reference counting destroys ordinary object graphs immediately but cannot make
either member of a cycle reach zero. Script objects therefore register as GC
candidates and enumerate handles stored in their fields.

A collection snapshots real reference counts, counts incoming edges from the
candidate set, and treats `real > internal` as an external root. Reachability
from those roots is marked. Unmarked objects are held by temporary references,
their object fields are cleared to break the cycle, and the temporary holds are
released so normal reference-count destruction performs the cleanup.

This is a stop-the-world teaching version of AngelScript's incremental trial
deletion. Tests cover self-cycles, two-object cycles, rooted cycles, collection
counts, and removal from the candidate set after destruction.
