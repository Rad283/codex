#include "orbit/transport.hpp"
#include <tlhelp32.h>

#include <cwchar>

namespace orbit {
namespace {
std::uintptr_t remote_module(DWORD pid, const wchar_t* name) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot) throw win_error("Enumerate target modules");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.get(), &entry)) throw win_error("Read target module list");
    do {
        if (_wcsicmp(entry.szModule, name) == 0)
            return reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
    } while (Module32NextW(snapshot.get(), &entry));
    throw std::runtime_error("Required Windows loader module was not found in the target.");
}

LPTHREAD_START_ROUTINE remote_loader(DWORD pid) {
    // LoadLibraryW can be forwarded into KernelBase. Resolve the containing
    // module and its RVA instead of assuming local and remote ASLR bases match.
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (!kernel) throw win_error("Find kernel32");
    FARPROC loader = GetProcAddress(kernel, "LoadLibraryW");
    if (!loader) throw win_error("Resolve LoadLibraryW");
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(loader), &owner))
        throw win_error("Find loader module");
    wchar_t path[32768]{};
    const auto size = GetModuleFileNameW(owner, path, 32768);
    if (!size || size >= 32768) throw win_error("Read loader module name");
    const auto name = std::filesystem::path(path).filename().wstring();
    const auto offset = reinterpret_cast<std::uintptr_t>(loader) - reinterpret_cast<std::uintptr_t>(owner);
    return reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_module(pid, name.c_str()) + offset);
}

class RemoteAllocation {
public:
    RemoteAllocation(HANDLE process, SIZE_T size) : process_(process) {
        address = VirtualAllocEx(process_, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!address) throw win_error("Allocate DLL path");
    }
    ~RemoteAllocation() { if (address) VirtualFreeEx(process_, address, 0, MEM_RELEASE); }
    void* address = nullptr;
private:
    HANDLE process_;
};
}

void attach(DWORD pid, const std::filesystem::path& dll) {
    if (!pid || pid == GetCurrentProcessId()) throw std::runtime_error("Choose a different, nonzero target PID.");
    const auto path = std::filesystem::absolute(dll);
    if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("orbit_runtime.dll is missing beside orbit.exe.");
    try {
        const auto response = request(pid, {Kind::ping, {}}, 100);
        if (response.kind == Kind::ok) return;
    } catch (const std::exception&) {
        // No existing runtime; proceed with the normal loader path.
    }
    Handle process(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                               PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!process) throw win_error("Open target process");
    USHORT target_machine = 0, native_machine = 0;
    if (!IsWow64Process2(process.get(), &target_machine, &native_machine)) throw win_error("Check target architecture");
    if (target_machine != IMAGE_FILE_MACHINE_UNKNOWN || native_machine != IMAGE_FILE_MACHINE_AMD64)
        throw std::runtime_error("Target must be a native x64 Windows process.");
    const auto loader = remote_loader(pid);
    const auto bytes = (path.native().size() + 1) * sizeof(wchar_t);
    RemoteAllocation allocation(process.get(), bytes);
    SIZE_T written = 0;
    if (!WriteProcessMemory(process.get(), allocation.address, path.c_str(), bytes, &written) || written != bytes)
        throw win_error("Write DLL path");
    Handle thread(CreateRemoteThread(process.get(), nullptr, 0, loader, allocation.address, 0, nullptr));
    if (!thread) throw win_error("Start loader thread");
    if (WaitForSingleObject(thread.get(), 5000) != WAIT_OBJECT_0) {
        // Do not free an argument that a still-running loader thread may read.
        allocation.address = nullptr;
        throw std::runtime_error("Loader did not finish within 5 seconds. The DLL-path allocation was retained for its safety.");
    }
    // A thread exit code truncates a 64-bit HMODULE. The actual runtime
    // handshake, including verification of the pipe's server PID, is decisive.
    const auto response = request(pid, {Kind::ping, {}});
    if (response.kind != Kind::ok) throw std::runtime_error(response.body);
}
}
