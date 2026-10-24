#include "orbit/transport.hpp"
#include <commdlg.h>

#include <functional>
#include <fstream>
#include <iterator>
#include <memory>
#include <thread>

namespace {
constexpr UINT completed_message = WM_APP + 1;
constexpr int host_button = 101, attach_button = 102, run_button = 103, open_button = 104, clear_button = 105;
constexpr COLORREF background = RGB(18, 20, 27), surface = RGB(27, 30, 40), ink = RGB(224, 227, 237);

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw orbit::win_error("Decode UTF-8");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (!size) throw orbit::win_error("Encode script as UTF-8");
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring text(HWND control) {
    const int size = GetWindowTextLengthW(control);
    std::wstring value(size + 1, L'\0');
    GetWindowTextW(control, value.data(), size + 1);
    value.resize(size);
    return value;
}

struct Completion {
    bool ok;
    DWORD attached_pid;
    std::string message;
};

class App {
public:
    ~App() {
        if (worker_.joinable()) worker_.join();
        DeleteObject(mono_);
        DeleteObject(font_);
        DeleteObject(title_font_);
        DeleteObject(background_);
        DeleteObject(surface_);
    }

    void initialize(HWND window) {
        window_ = window;
        font_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, 0, L"Segoe UI");
        mono_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                            CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
        title_font_ = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                  CLEARTYPE_QUALITY, 0, L"Segoe UI");
        title_ = control(L"STATIC", L"ORBIT  /  EXTERNAL LUAU", 0);
        SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(title_font_), TRUE);
        subtitle_ = control(L"STATIC", L"Independent Luau VM. Roblox game APIs are not implemented.", 0);
        host_ = control(L"BUTTON", L"Start test host", BS_PUSHBUTTON, host_button);
        pid_label_ = control(L"STATIC", L"Target PID", 0);
        pid_ = control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL);
        SendMessageW(pid_, EM_SETLIMITTEXT, 10, 0);
        attach_ = control(L"BUTTON", L"Attach", BS_PUSHBUTTON, attach_button);
        run_ = control(L"BUTTON", L"Run script", BS_PUSHBUTTON, run_button);
        open_ = control(L"BUTTON", L"Open file", BS_PUSHBUTTON, open_button);
        clear_ = control(L"BUTTON", L"Clear output", BS_PUSHBUTTON, clear_button);
        editor_label_ = control(L"STATIC", L"SCRIPT  /  Luau", 0);
        editor_ = control(L"EDIT", L"-- Runs in the attached process, in an isolated VM.\r\n"
                          L"local total: number = 0\r\nfor i = 1, 10 do\r\n    total += i\r\nend\r\n"
                          L"print(\"Host PID:\", _HOST_PID)\r\nprint(\"Total:\", total)\r\n",
                          WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN |
                          WS_VSCROLL | WS_HSCROLL, 201);
        SendMessageW(editor_, EM_SETLIMITTEXT, orbit::max_source, 0);
        output_label_ = control(L"STATIC", L"OUTPUT", 0);
        output_ = control(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                           ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL, 202);
        SendMessageW(output_, EM_SETLIMITTEXT, 1024 * 1024, 0);
        SendMessageW(editor_, WM_SETFONT, reinterpret_cast<WPARAM>(mono_), TRUE);
        SendMessageW(output_, WM_SETFONT, reinterpret_cast<WPARAM>(mono_), TRUE);
        status_ = control(L"STATIC", L"Not attached", 0, 203);
        EnableWindow(run_, FALSE);
        log(L"Ready. Start a test host, attach, then run a script.\r\n"
             L"Attaching to a process does not expose Roblox's game/workspace objects.");
        RECT bounds{};
        GetClientRect(window_, &bounds);
        resize(bounds.right, bounds.bottom);
    }

    void resize(int width, int height) {
        if (!editor_) return;
        const int inner = width - 40;
        MoveWindow(title_, 20, 14, inner, 36, TRUE);
        MoveWindow(subtitle_, 20, 54, inner, 24, TRUE);
        MoveWindow(host_, 20, 90, 128, 32, TRUE);
        MoveWindow(pid_label_, 164, 97, 78, 24, TRUE);
        MoveWindow(pid_, 244, 90, 102, 32, TRUE);
        MoveWindow(attach_, 356, 90, 80, 32, TRUE);
        MoveWindow(run_, 446, 90, 102, 32, TRUE);
        MoveWindow(open_, 558, 90, 94, 32, TRUE);
        MoveWindow(clear_, 662, 90, 114, 32, TRUE);
        MoveWindow(editor_label_, 20, 140, inner, 24, TRUE);
        const int editor_height = (height - 260) * 2 / 3;
        MoveWindow(editor_, 20, 168, inner, editor_height, TRUE);
        const int output_top = 188 + editor_height;
        MoveWindow(output_label_, 20, output_top, inner, 24, TRUE);
        MoveWindow(output_, 20, output_top + 28, inner, height - output_top - 78, TRUE);
        MoveWindow(status_, 20, height - 36, inner, 26, TRUE);
    }

    void command(int id) {
        if (busy_) return;
        try {
            if (id == host_button) {
                host_process_.start();
                SetWindowTextW(pid_, std::to_wstring(host_process_.pid()).c_str());
                attached_pid_ = 0;
                EnableWindow(run_, FALSE);
                SetWindowTextW(status_, L"Test host started. Click Attach.");
                log(L"Started test host PID " + std::to_wstring(host_process_.pid()) + L".");
            } else if (id == attach_button) {
                const auto value = text(pid_);
                std::size_t end = 0;
                const auto parsed = std::stoull(value, &end);
                if (end != value.size() || !parsed || parsed > MAXDWORD)
                    throw std::runtime_error("Enter a valid process ID.");
                const auto pid = static_cast<DWORD>(parsed);
                const auto dll = orbit::executable_directory() / L"orbit_runtime.dll";
                job([pid, dll] {
                    orbit::attach(pid, dll);
                    return Completion{true, pid, "Attached to PID " + std::to_string(pid) +
                        ". Scripts use an isolated VM; Roblox game APIs are unavailable."};
                }, L"Attaching...");
            } else if (id == run_button) {
                const auto source = utf8(text(editor_));
                if (source.size() > orbit::max_source) throw std::runtime_error("Script exceeds 256 KiB in UTF-8.");
                const auto pid = attached_pid_;
                job([pid, source] {
                    const auto response = orbit::request(pid, {orbit::Kind::execute, source});
                    const bool ok = response.kind == orbit::Kind::ok;
                    return Completion{ok, pid, (ok ? "[completed]\n" : "[script failed]\n") + response.body};
                }, L"Executing...");
            } else if (id == open_button) {
                open_script();
            } else if (id == clear_button) {
                SetWindowTextW(output_, L"");
            }
        } catch (const std::exception& error) {
            log(wide(error.what()));
            SetWindowTextW(status_, L"Operation failed. See output.");
        }
    }

    void complete(Completion* raw) {
        std::unique_ptr<Completion> result(raw);
        if (worker_.joinable()) worker_.join();
        busy_ = false;
        attached_pid_ = result->attached_pid;
        enable_controls();
        log(wide(result->message));
        SetWindowTextW(status_, result->ok ? L"Ready — isolated Luau VM" : L"Operation failed. See output.");
    }

    HBRUSH color(HDC dc, HWND control) {
        SetTextColor(dc, ink);
        const bool edit = control == editor_ || control == output_ || control == pid_;
        SetBkColor(dc, edit ? surface : background);
        return edit ? surface_ : background_;
    }

    void close() {
        // Finite loader/IPC timeouts let an in-flight operation complete before
        // destroying the window that receives its completion message.
        if (worker_.joinable()) worker_.join();
        MSG message{};
        while (PeekMessageW(&message, window_, completed_message, completed_message, PM_REMOVE))
            delete reinterpret_cast<Completion*>(message.lParam);
    }

private:
    HWND control(const wchar_t* kind, const wchar_t* value, DWORD style, int id = 0) {
        HWND result = CreateWindowExW(0, kind, value, WS_CHILD | WS_VISIBLE | style,
                                       0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                       GetModuleHandleW(nullptr), nullptr);
        if (!result) throw orbit::win_error("Create control");
        SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        return result;
    }

    void log(std::wstring message) {
        // Normalize LF for the native multiline edit control.
        std::wstring normalized;
        for (std::size_t i = 0; i < message.size(); ++i) {
            if (message[i] == L'\n' && (!i || message[i - 1] != L'\r')) normalized += L'\r';
            normalized += message[i] ? message[i] : L'\uFFFD';
        }
        normalized += L"\r\n";
        if (GetWindowTextLengthW(output_) + normalized.size() > 900000) SetWindowTextW(output_, L"");
        SendMessageW(output_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(output_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(normalized.c_str()));
        SendMessageW(output_, EM_SCROLLCARET, 0, 0);
    }

    void enable_controls() {
        for (const auto control : {host_, attach_, open_, clear_, pid_}) EnableWindow(control, !busy_);
        EnableWindow(run_, !busy_ && attached_pid_ != 0);
    }

    void job(std::function<Completion()> action, const wchar_t* status) {
        if (worker_.joinable()) worker_.join();
        worker_ = std::thread([window = window_, action = std::move(action)] {
            auto result = std::make_unique<Completion>();
            try { *result = action(); }
            catch (const std::exception& error) { *result = {false, 0, error.what()}; }
            if (PostMessageW(window, completed_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
        });
        busy_ = true;
        enable_controls();
        SetWindowTextW(status_, status);
    }

    void open_script() {
        wchar_t path[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window_;
        dialog.lpstrFilter = L"Luau scripts\0*.luau;*.lua\0Text files\0*.txt\0All files\0*.*\0\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = 32768;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetOpenFileNameW(&dialog)) return;
        const auto file_path = std::filesystem::path(path);
        if (std::filesystem::file_size(file_path) > orbit::max_source)
            throw std::runtime_error("Script exceeds 256 KiB.");
        std::ifstream file(file_path, std::ios::binary);
        if (!file) throw std::runtime_error("Could not read script file.");
        std::string source((std::istreambuf_iterator<char>(file)), {});
        if (source.find('\0') != std::string::npos) throw std::runtime_error("Open a UTF-8 text script without NUL bytes.");
        if (source.compare(0, 3, "\xEF\xBB\xBF") == 0) source.erase(0, 3);
        auto content = wide(source);
        std::wstring normalized;
        for (std::size_t i = 0; i < content.size(); ++i) {
            if (content[i] == L'\n' && (!i || content[i - 1] != L'\r')) normalized += L'\r';
            normalized += content[i];
        }
        SetWindowTextW(editor_, normalized.c_str());
    }

    HWND window_{}, title_{}, subtitle_{}, host_{}, pid_label_{}, pid_{}, attach_{}, run_{}, open_{}, clear_{};
    HWND editor_label_{}, editor_{}, output_label_{}, output_{}, status_{};
    HFONT mono_{}, font_{}, title_font_{};
    HBRUSH background_ = CreateSolidBrush(background), surface_ = CreateSolidBrush(surface);
    orbit::TestHost host_process_;
    std::thread worker_;
    bool busy_ = false;
    DWORD attached_pid_ = 0;
};

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wparam, lparam);
    try {
        switch (message) {
        case WM_CREATE: app->initialize(window); return 0;
        case WM_SIZE: app->resize(LOWORD(lparam), HIWORD(lparam)); return 0;
        case WM_COMMAND:
            if (HIWORD(wparam) == BN_CLICKED) app->command(LOWORD(wparam));
            return 0;
        case completed_message: app->complete(reinterpret_cast<Completion*>(lparam)); return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            return reinterpret_cast<LRESULT>(app->color(reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam)));
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize = {830, 600};
            return 0;
        }
        case WM_CLOSE: app->close(); DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "Orbit", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    SetProcessDPIAware();
    App app;
    WNDCLASSW type{};
    type.lpfnWndProc = procedure;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    type.lpszClassName = L"Orbit.ExternalLuau";
    if (!RegisterClassW(&type)) return 1;
    HWND window = CreateWindowExW(0, type.lpszClassName, L"Orbit — External Luau",
                                   WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 760,
                                   nullptr, nullptr, instance, &app);
    if (!window) return 1;
    ShowWindow(window, show);
    MSG message{};
    int status = 0;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return status < 0 ? 1 : static_cast<int>(message.wParam);
}
