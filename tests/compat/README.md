# AngelScript compatibility tests

Configure with `-DMINI_AS_BUILD_COMPAT_TESTS=ON` to download the official
AngelScript 2.38.0 SDK and build both runners. Each case is executed by the
teaching engine and by the official engine; normalized state and return output
must match.

The SDK archive is pinned by SHA-256. Normal builds keep the option disabled
and never access the network.
