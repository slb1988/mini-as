# mini_angelscript

A bottom-up, educational reimplementation of AngelScript's core architecture.
The implementation uses C++17 and CMake and evolves through buildable commits.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

See `docs/stages/` for the design notes attached to each stage.

