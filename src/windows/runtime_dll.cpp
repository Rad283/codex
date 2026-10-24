#include "orbit/transport.hpp"
#include <sddl.h>

#include <vector>

namespace {
class PipeSecurity {
public:
    PipeSecurity() {
        orbit::Handle token;
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) throw orbit::win_error("Read process token");
        token.reset(raw);
        DWORD size = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
        std::vector<char> storage(size);
        if (!GetTokenInformation(token.get(), TokenUser, storage.data(), size, &size))
            throw orbit::win_error("Read process user");
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid))
            throw orbit::win_error("Format user SID");
        std::wstring rule = L"D:P(A;;GA;;;";
        rule += sid;
        rule += L")";
        LocalFree(sid);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(rule.c_str(), SDDL_REVISION_1, &descriptor_, nullptr))
            throw orbit::win_error("Create pipe permissions");
        attributes = {sizeof(SECURITY_ATTRIBUTES), descriptor_, FALSE};
    }
    ~PipeSecurity() { if (descriptor_) LocalFree(descriptor_); }
    SECURITY_ATTRIBUTES attributes{};
private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
};

DWORD WINAPI serve(void*) {
    try {
        PipeSecurity security;
        const auto name = orbit::pipe_name(GetCurrentProcessId());
        // Keep one server handle for the lifetime of the runtime: no race to
        // replace the pipe between requests, and no unbounded server threads.
        orbit::Handle pipe(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                            FILE_FLAG_FIRST_PIPE_INSTANCE,
                                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                            1, 65536, 65536, 0, &security.attributes));
        if (!pipe) throw orbit::win_error("Create runtime pipe");
        for (;;) {
            orbit::Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event) throw orbit::win_error("Create connect event");
            OVERLAPPED operation{};
            operation.hEvent = event.get();
            if (!ConnectNamedPipe(pipe.get(), &operation)) {
                const DWORD error = GetLastError();
                if (error == ERROR_IO_PENDING) {
                    if (WaitForSingleObject(event.get(), INFINITE) != WAIT_OBJECT_0) return 1;
                    DWORD count = 0;
                    if (!GetOverlappedResult(pipe.get(), &operation, &count, FALSE)) {
                        DisconnectNamedPipe(pipe.get());
                        continue;
                    }
                } else if (error != ERROR_PIPE_CONNECTED) {
                    DisconnectNamedPipe(pipe.get());
                    continue;
                }
            }
            try {
                const auto frame = orbit::read_frame(pipe.get());
                if (frame.kind == orbit::Kind::ping && frame.body.empty()) {
                    orbit::write_frame(pipe.get(), {orbit::Kind::ok, "Isolated Luau VM. Roblox game APIs are unavailable."});
                } else if (frame.kind == orbit::Kind::execute) {
                    const auto result = orbit::execute(frame.body, GetCurrentProcessId());
                    orbit::write_frame(pipe.get(), {result.ok ? orbit::Kind::ok : orbit::Kind::error, result.output});
                } else {
                    orbit::write_frame(pipe.get(), {orbit::Kind::error, "Unsupported request."});
                }
                // The client waits for a complete response and closes. Avoid
                // FlushFileBuffers: it can block forever on a stalled client.
                try { orbit::Frame extra = orbit::read_frame(pipe.get()); (void)extra; }
                catch (const std::exception&) {}
            } catch (const std::exception&) {
                // A malformed or disconnected client must not kill the host.
            }
            DisconnectNamedPipe(pipe.get());
        }
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        return 1;
    }
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        (void)instance; // Static CRTs need their normal thread notifications.
        // Luau and IPC initialization happen on the worker, outside loader lock.
        HANDLE thread = CreateThread(nullptr, 0, serve, nullptr, 0, nullptr);
        if (!thread) return FALSE;
        CloseHandle(thread);
    }
    return TRUE;
}
