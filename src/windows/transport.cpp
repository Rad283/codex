#include "orbit/transport.hpp"

#include <array>
#include <cstring>

namespace orbit {
namespace {
constexpr std::uint32_t magic = 0x5442524f; // ORBT, little endian
constexpr std::uint32_t version = 1;

void transfer(HANDLE pipe, void* buffer, DWORD size, bool writing) {
    auto* cursor = static_cast<char*>(buffer);
    const ULONGLONG deadline = GetTickCount64() + 10000;
    while (size) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) throw win_error("Create I/O event");
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        DWORD count = 0;
        const BOOL completed = writing ? WriteFile(pipe, cursor, size, &count, &operation)
                                       : ReadFile(pipe, cursor, size, &count, &operation);
        if (!completed) {
            const DWORD error = GetLastError();
            if (error != ERROR_IO_PENDING) throw win_error("Pipe I/O", error);
            const auto now = GetTickCount64();
            const auto remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
            if (WaitForSingleObject(event.get(), remaining) != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &operation);
                // OVERLAPPED must remain alive until cancellation has completed.
                GetOverlappedResult(pipe, &operation, &count, TRUE);
                throw std::runtime_error("Pipe I/O timed out.");
            }
            if (!GetOverlappedResult(pipe, &operation, &count, FALSE)) throw win_error("Pipe completion");
        }
        if (!count) throw std::runtime_error("Pipe closed before the frame was complete.");
        cursor += count;
        size -= count;
    }
}
}

std::wstring pipe_name(DWORD pid) {
    return L"\\\\.\\pipe\\Orbit.Luau.v1." + std::to_wstring(pid);
}

void write_frame(HANDLE pipe, const Frame& frame) {
    if (frame.body.size() > max_source) throw std::runtime_error("Frame is too large.");
    std::array<std::uint32_t, 4> header{magic, version, static_cast<std::uint32_t>(frame.kind),
                                     static_cast<std::uint32_t>(frame.body.size())};
    transfer(pipe, header.data(), sizeof(header), true);
    if (!frame.body.empty())
        transfer(pipe, const_cast<char*>(frame.body.data()), static_cast<DWORD>(frame.body.size()), true);
}

Frame read_frame(HANDLE pipe) {
    std::array<std::uint32_t, 4> header{};
    transfer(pipe, header.data(), sizeof(header), false);
    if (header[0] != magic || header[1] != version || header[2] < 1 || header[2] > 4 || header[3] > max_source)
        throw std::runtime_error("Invalid Orbit protocol frame.");
    Frame frame{static_cast<Kind>(header[2]), std::string(header[3], '\0')};
    if (!frame.body.empty()) transfer(pipe, frame.body.data(), header[3], false);
    return frame;
}

Frame request(DWORD pid, const Frame& frame, DWORD connect_timeout_ms) {
    const auto name = pipe_name(pid);
    const auto deadline = GetTickCount64() + connect_timeout_ms;
    Handle pipe;
    do {
        pipe.reset(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (pipe) break;
        const auto error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY)
            throw win_error("Connect to runtime", error);
        Sleep(20);
    } while (GetTickCount64() < deadline);
    if (!pipe) throw std::runtime_error("Runtime pipe not available in the target process.");
    ULONG server_pid = 0;
    if (!GetNamedPipeServerProcessId(pipe.get(), &server_pid)) throw win_error("Verify runtime PID");
    if (server_pid != pid) throw std::runtime_error("Runtime pipe belongs to a different process.");
    write_frame(pipe.get(), frame);
    Frame response = read_frame(pipe.get());
    if ((response.kind != Kind::ok && response.kind != Kind::error) || response.body.size() > max_output)
        throw std::runtime_error("Invalid runtime response.");
    return response;
}
}
