# nvngx.dll_dlssnr.dll

Neural Rendering's snippet resolves the module that owns its caller's return address and refuses
anything whose path does not contain `nvngx.dll` -- the driver core being `_nvngx.dll`. It returns
`FAIL_PlatformError` before it inspects a single argument. OptiScaler installs as `dxgi.dll` or
`winmm.dll`, so it fails that test like anything else would.

This library exists to be named correctly. It forwards create, evaluate and release, plus a few
parameter-block helpers, so the calls into the snippet originate from a module the snippet accepts.

It builds with the rest of the solution: `dlssnr_forwarder.vcxproj` in `OptiScaler.sln` writes
`x64\Release\a\nvngx.dll_dlssnr.dll`, which `package.ps1` copies into the installer payload.
`CMakeLists.txt` builds the same DLL on its own:

    cmake -S . -B build -A x64
    cmake --build build --config Release

One detail is load-bearing and not obvious. The forwarder must not `return snippetFn(...)` -- that is a
tail call, the compiler emits a `jmp`, this module's frame disappears, and the snippet then resolves its
caller to whoever called the forwarder. The result goes into a `volatile` local first.
