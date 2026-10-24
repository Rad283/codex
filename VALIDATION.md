# Observed validation

Development machine: Debian 13, x86-64. Dependency: official Luau 0.693 at
`0084576e6f8dc9865f27d296f0357794894bd38d`.

| Check | Observed result |
| --- | --- |
| Native Linux Release build | Passed with GCC 14.2 and CMake 4.4.4 |
| Native runtime tests | Passed: 16 checks, one CTest suite |
| Windows x64 cross-build | Passed using GCC 14 MinGW POSIX |
| Windows runtime tests under Wine 10 | Passed: 16 checks |
| Windows injection tests under Wine 10 | Passed: all five checks, including injected execution and host-PID verification |
| Windows GUI tests under Wine 10 and Xvfb | Passed: all five checks, including attach, run, output, clear, and clean close |
| Combined Windows-binary CTest run under Wine | Passed: 3/3 suites, 26.05 seconds |
| Native Windows build/execution | Not run here; a Windows CI workflow is supplied |
| Roblox attachment, game APIs, or executor compatibility | Not verified; the Roblox VM/game-API bridge is not implemented |

The final native command was `ctest --test-dir build --output-on-failure`.
The final Windows-binary command was
`ctest --test-dir build-windows --output-on-failure`, after configuring
`CMAKE_CROSSCOMPILING_EMULATOR` to the workspace's Wine launcher. Wine and Xvfb
are development test tools, not dependencies of the Windows application.

Wine testing found and resolved an empty-stack error when reporting interrupted
or yielded scripts, and a test-host startup race. The final tests exercise those
paths. Cross-compilation and Wine execution do not establish native Windows or
Roblox compatibility. The supplied Windows workflow has not been run or
published from this workspace.
