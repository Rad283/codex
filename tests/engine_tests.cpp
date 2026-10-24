#include "orbit/engine.hpp"

#include <cstdlib>
#include <iostream>

namespace {
int checks = 0;
void expect(bool condition, const char* name) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
    std::cout << "PASS: " << name << '\n';
}
}

int main() {
    using orbit::execute;
    auto result = execute("local sum: number = 0; for i = 1, 10 do sum += i end; print(sum)");
    expect(result.ok && result.output == "55\n", "Luau types, iteration, compound assignment and output");
    result = execute("print(_HOST_PID)", 1234);
    expect(result.ok && result.output == "1234\n", "host context");
    result = execute("print('hello', true, nil)");
    expect(result.ok && result.output == "hello\ttrue\tnil\n", "print formats multiple values");
    result = execute("local =");
    expect(!result.ok && result.output.find("Error:") != std::string::npos, "syntax errors are reported");
    result = execute("error('intentional')");
    expect(!result.ok && result.output.find("intentional") != std::string::npos, "runtime errors are reported");
    result = execute("coroutine.yield()");
    expect(!result.ok && result.output.find("yielded") != std::string::npos, "yield without a result is reported safely");
    result = execute("assert(game == nil and workspace == nil); print('isolated')");
    expect(result.ok && result.output == "isolated\n", "does not fabricate Roblox APIs");
    result = execute("assert(io == nil and loadfile == nil and dofile == nil and package == nil)");
    expect(result.ok, "no filesystem or native module access");
    expect(execute("temporary_global = 42").ok && execute("assert(temporary_global == nil)").ok,
           "executions use independent global environments");
    result = execute("math.abs = nil");
    expect(!result.ok, "sandbox protects standard library tables");
    orbit::Limits limits;
    limits.time = std::chrono::milliseconds(30);
    const auto start = std::chrono::steady_clock::now();
    result = execute("while true do end", 0, limits);
    expect(!result.ok && result.output.find("time limit") != std::string::npos &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(3), "infinite loops are interrupted");
    result = execute("while true do pcall(function() while true do end end) end", 0, limits);
    expect(!result.ok && result.output.find("time limit") != std::string::npos,
           "pcall cannot swallow execution interruption");
    result = execute("local t = {}; while true do t[#t + 1] = string.rep('x', 10000) end", 0,
                     {std::chrono::milliseconds(2000), 1024 * 1024, orbit::max_output});
    expect(!result.ok && result.output.find("memory") != std::string::npos, "VM memory is bounded");
    result = execute("for i = 1, 1000 do print('1234567890') end", 0,
                     {std::chrono::milliseconds(2000), 1024 * 1024, 64});
    expect(!result.ok && result.output.find("output limit") != std::string::npos, "output is bounded");
    expect(!execute(std::string(orbit::max_source + 1, ' ')).ok, "oversized source is rejected");
    expect(execute("assert(6 * 7 == 42)").ok, "runtime recovers after failed scripts");
    std::cout << checks << " checks passed.\n";
}
