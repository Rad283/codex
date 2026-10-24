#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace orbit {
inline constexpr std::size_t max_source = 256 * 1024;
inline constexpr std::size_t max_output = 64 * 1024;

struct Limits {
    std::chrono::milliseconds time{2000};
    std::size_t memory = 32 * 1024 * 1024;
    std::size_t output = max_output;
};

struct Result {
    bool ok;
    std::string output;
};

// Each execution gets a fresh, sandboxed VM. No Roblox objects are exposed.
Result execute(std::string_view source, std::uint32_t host_pid = 0, Limits limits = {});
}
