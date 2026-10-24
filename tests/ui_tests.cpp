#include "orbit/windows.hpp"

#include <iostream>
#include <vector>

namespace {
struct WindowSearch { DWORD pid; HWND result = nullptr; };
BOOL CALLBACK find_window(HWND window, LPARAM userdata) {
    auto& search = *reinterpret_cast<WindowSearch*>(userdata);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == search.pid && IsWindowVisible(window)) {
        wchar_t type[128]{};
        GetClassNameW(window, type, 128);
        if (std::wstring(type) == L"Orbit.ExternalLuau") {
            search.result = window;
            return FALSE;
        }
    }
    return TRUE;
}

std::wstring output(HWND window) {
    HWND control = GetDlgItem(window, 202);
    const auto size = SendMessageW(control, WM_GETTEXTLENGTH, 0, 0);
    std::vector<wchar_t> value(static_cast<std::size_t>(size) + 1);
    SendMessageW(control, WM_GETTEXT, value.size(), reinterpret_cast<LPARAM>(value.data()));
    return value.data();
}

template<class Predicate> void wait_until(Predicate predicate, const char* error) {
    const auto deadline = GetTickCount64() + 15000;
    do {
        if (predicate()) return;
        Sleep(50);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error(error);
}

void click(HWND window, int id) {
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(window, id)));
}
}

int main() {
    orbit::Handle process;
    HWND window = nullptr;
    try {
        const auto path = orbit::executable_directory() / L"orbit.exe";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(path.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                            path.parent_path().c_str(), &startup, &info)) throw orbit::win_error("Start GUI");
        process.reset(info.hProcess);
        CloseHandle(info.hThread);
        WindowSearch search{info.dwProcessId};
        wait_until([&] { EnumWindows(find_window, reinterpret_cast<LPARAM>(&search)); return search.result != nullptr; },
                   "GUI did not open.");
        window = search.result;
        wait_until([&] { return GetDlgItem(window, 203) != nullptr; }, "GUI controls did not initialize.");
        if (IsWindowEnabled(GetDlgItem(window, 103))) throw std::runtime_error("Run is enabled before attachment.");
        std::cout << "PASS: GUI starts with execution disabled\n";
        click(window, 101);
        click(window, 102);
        wait_until([&] { return IsWindowEnabled(GetDlgItem(window, 103)) != FALSE; }, "GUI did not attach to its host.");
        std::cout << "PASS: Start test host and Attach controls work\n";
        click(window, 103);
        wait_until([&] { return output(window).find(L"Total:\t55") != std::wstring::npos; }, "GUI did not display script output.");
        std::cout << "PASS: Run displays the injected VM's output\n";
        click(window, 105);
        if (!output(window).empty()) throw std::runtime_error("Clear output failed.");
        std::cout << "PASS: Clear output works\n";
        PostMessageW(window, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.get(), 15000) != WAIT_OBJECT_0) throw std::runtime_error("GUI did not close.");
        DWORD code = 1;
        if (!GetExitCodeProcess(process.get(), &code) || code != 0) throw std::runtime_error("GUI exited with failure.");
        std::cout << "PASS: GUI closes cleanly\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        if (window) {
            const auto value = output(window);
            std::wcerr << value << '\n';
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        if (process && WaitForSingleObject(process.get(), 5000) != WAIT_OBJECT_0)
            TerminateProcess(process.get(), 1);
        return 1;
    }
}
