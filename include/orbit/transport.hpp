#pragma once
#include "orbit/engine.hpp"
#include "orbit/windows.hpp"

namespace orbit {
enum class Kind : std::uint32_t { ping = 1, execute = 2, ok = 3, error = 4 };
struct Frame {
    Kind kind;
    std::string body;
};

std::wstring pipe_name(DWORD pid);
void write_frame(HANDLE pipe, const Frame& frame);
Frame read_frame(HANDLE pipe);
Frame request(DWORD pid, const Frame& frame, DWORD connect_timeout_ms = 3000);
}
