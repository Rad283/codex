#pragma once
#include <windows.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace orbit {
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { reset(); value_ = std::exchange(other.value_, nullptr); }
        return *this;
    }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE value = nullptr) {
        if (*this) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_;
};

inline std::runtime_error win_error(const char* operation, DWORD code = GetLastError()) {
    return std::runtime_error(std::string(operation) + " failed (Windows error " + std::to_string(code) + ").");
}

inline std::filesystem::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size()) throw win_error("GetModuleFileNameW");
    buffer.resize(count);
    return std::filesystem::path(buffer).parent_path();
}

// Owns only the supplied test host, never the user's selected target.
class TestHost {
public:
    ~TestHost() { stop(); }
    void start() {
        stop();
        const auto path = executable_directory() / L"orbit_host.exe";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, path.parent_path().c_str(), &startup, &process))
            throw win_error("Start test host");
        CloseHandle(process.hThread);
        process_.reset(process.hProcess);
        pid_ = process.dwProcessId;
        const auto event_name = L"Local\\Orbit.Host.Ready." + std::to_wstring(pid_);
        const auto deadline = GetTickCount64() + 5000;
        do {
            Handle ready(OpenEventW(SYNCHRONIZE, FALSE, event_name.c_str()));
            if (ready && WaitForSingleObject(ready.get(), 50) == WAIT_OBJECT_0) return;
            if (WaitForSingleObject(process_.get(), 0) == WAIT_OBJECT_0) break;
            Sleep(20);
        } while (GetTickCount64() < deadline);
        stop();
        throw std::runtime_error("The test host did not signal readiness within 5 seconds.");
    }
    DWORD pid() const { return pid_; }
    void stop() {
        if (process_) {
            TerminateProcess(process_.get(), 0);
            WaitForSingleObject(process_.get(), 3000);
            process_.reset();
        }
        pid_ = 0;
    }
private:
    Handle process_;
    DWORD pid_ = 0;
};

void attach(DWORD pid, const std::filesystem::path& dll);
}
