# Orbit — external Luau execution for Windows

Orbit has a Windows script editor, a real DLL injection path, an injected Luau
runtime, and an output console. It includes its own empty test-host process so
the whole attach → execute → output workflow can be exercised.

**This is not a working Roblox/Solara replacement.** The injected DLL creates
an independent Luau VM; it does not connect to Roblox's existing VM or expose
`game`, `workspace`, `Instance`, executor-specific APIs, or Roblox permissions.
Attaching to a process does not create that connection. Roblox's client can
reject ordinary DLL loading. No Roblox-specific bridge or protection bypass is
implemented or claimed. Solara's installer, binaries, and branding are not used.

## Build on Windows

Requirements: x64 Windows 10 version 1511 or later, Git, CMake 3.24+, and Visual
Studio 2022 with **Desktop development with C++** and the Windows SDK. The first
configure fetches Luau 0.693, pinned to commit
`0084576e6f8dc9865f27d296f0357794894bd38d`.

From the repository root in a developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
./build/Release/orbit.exe
```

Keep `orbit.exe`, `orbit_runtime.dll`, and `orbit_host.exe` in the same directory.
Use a directory readable by the selected process. No administrator launch is
required for the supplied test host. Error messages include Windows error codes
when a selected process rejects an operation. Windows builds and end-to-end
tests are also defined in `.github/workflows/windows.yml`; the workflow produces
an `orbit-windows-x64` artifact after its tests pass.

## Use

1. Click **Start test host**. Its PID fills the target box.
2. Click **Attach**, then **Run script**. The sample prints the host PID and 55.
3. Edit the script or open a UTF-8 `.luau`/`.lua` file. Output and errors appear
   below the editor. `examples/hello.luau` exercises tables and typed Luau syntax.

The target box also accepts an x64 PID for a process you intend to instrument.
Attachment uses Windows' normal process and DLL-loader APIs. The app reports
success only after the runtime replies over a pipe whose server PID matches
the requested process. It neither elevates privileges nor changes client
protection settings. Closing Orbit stops only its own test host; a DLL loaded
into another target remains loaded until that target exits. Live DLL unloading
is not supported.

## Runtime behavior

- Every script gets a fresh VM with Luau's sandboxed standard library and a
  captured `print` function. Globals do not persist between runs.
- `_HOST_PID` identifies the process running the script. The integration test
  checks it against the independently launched host's PID.
- Source is limited to 256 KiB, VM allocations to 32 MiB, and output to 64 KiB.
  The VM interrupt hook stops execution at a safepoint after two seconds.
  Compilation and long-running native library calls are not covered by this
  execution deadline; these limits are not a process isolation boundary.
- There are no filesystem, HTTP, native module, Roblox game, or executor
  compatibility APIs. Roblox-specific scripts will not run here.
- IPC is local-only, permits the target's Windows user, validates frame sizes,
  and uses timeouts. Scripts and output are not sent to a network service.

## Tests and Linux development

`engine_tests` exercises Luau execution, error reporting, isolation, and resource
limits. `injection_tests` launches `orbit_host.exe`, injects the real DLL, verifies
the remote PID/output, and checks repeat attachment and error recovery.
`ui_tests` launches the actual GUI and exercises Start test host, Attach, Run,
Clear output, and close. The test host has no interpreter of its own.

The core runtime can be built and tested on Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

With the x64 MinGW POSIX toolchain installed, cross-compile Windows binaries:

```sh
cmake -S . -B build-windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-x64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows --parallel 4
```

Cross-compilation alone does not validate Windows execution. Wine can exercise
the binaries and IPC, but does not establish behavior on Windows or Roblox.
The Windows CI workflow must run separately to establish native Windows results.

Luau is distributed under its MIT license; the Windows artifact includes its
upstream `LICENSE.txt`. See `VALIDATION.md` for the results actually observed in
the development workspace.
