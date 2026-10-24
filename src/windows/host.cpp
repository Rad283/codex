#include <windows.h>
#include <string>

int main() {
    // Intentionally has no interpreter. Integration tests must inject the DLL
    // and receive output over IPC to demonstrate external execution.
    const auto name = L"Local\\Orbit.Host.Ready." + std::to_wstring(GetCurrentProcessId());
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    if (!ready || !SetEvent(ready)) return 1;
    Sleep(INFINITE);
}
