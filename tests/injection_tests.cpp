#include "orbit/transport.hpp"

#include <iostream>

int main() {
    try {
        orbit::TestHost host;
        host.start();
        const auto dll = orbit::executable_directory() / L"orbit_runtime.dll";
        orbit::attach(host.pid(), dll);
        const auto result = orbit::request(host.pid(), {orbit::Kind::execute,
            "local x: number = 40; x += 2; print(_HOST_PID, x)"});
        const auto expected = std::to_string(host.pid()) + "\t42\n";
        if (result.kind != orbit::Kind::ok || result.body != expected)
            throw std::runtime_error("Remote Luau result mismatch: " + result.body);
        std::cout << "PASS: DLL injection and script execution inside test-host PID " << host.pid() << '\n';
        orbit::attach(host.pid(), dll);
        std::cout << "PASS: repeat attachment reuses the runtime\n";
        const auto error = orbit::request(host.pid(), {orbit::Kind::execute, "error('remote failure')"});
        if (error.kind != orbit::Kind::error || error.body.find("remote failure") == std::string::npos)
            throw std::runtime_error("Remote error was not propagated.");
        std::cout << "PASS: runtime errors reach the client\n";
        const auto recovery = orbit::request(host.pid(), {orbit::Kind::execute, "print('recovered')"});
        if (recovery.kind != orbit::Kind::ok || recovery.body != "recovered\n")
            throw std::runtime_error("Runtime did not recover from script failure.");
        std::cout << "PASS: subsequent scripts still execute\n";
        try {
            orbit::attach(0, dll);
            throw std::logic_error("Invalid PID was accepted.");
        } catch (const std::logic_error&) { throw; }
        catch (const std::runtime_error&) { std::cout << "PASS: invalid target is rejected\n"; }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
