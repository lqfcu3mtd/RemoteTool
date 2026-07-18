#ifdef _WIN32
#include "agent_window.h"
#include <cstdio>
#include <commctrl.h>

namespace rmt::gui {

AgentWindow* AgentWindow::instance_ = nullptr;

AgentWindow::AgentWindow(const std::string& device_id,
                          const std::string& server_addr)
    : device_id_(device_id), server_addr_(server_addr),
      io_(std::make_unique<asio::io_context>()) {}

AgentWindow::~AgentWindow() {
    if (io_thread_.joinable()) { io_->stop(); io_thread_.join(); }
}

int AgentWindow::run() {
    instance_ = this;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"AgentWindow";
    RegisterClassEx(&wc);

    hwnd_ = CreateWindowEx(0, L"AgentWindow", L"RemoteTool Agent",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 400, 220,
                           nullptr, nullptr, GetModuleHandle(nullptr), this);
    if (!hwnd_) return 1;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK AgentWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (instance_ && instance_->hwnd_ == hwnd)
        return instance_->handle_message(msg, wp, lp);
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT AgentWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: on_create(); break;
        case WM_COMMAND:
            if (LOWORD(wp) == 100) on_close();
            break;
        case WM_USER:
            update_state(static_cast<tunnel::AgentState>(wp));
            break;
        case WM_CLOSE: on_close(); DestroyWindow(hwnd_); break;
        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd_, msg, wp, lp);
    }
    return 0;
}

void AgentWindow::on_create() {
    auto make_label = [&](int y, const std::wstring& text) -> HWND {
        return CreateWindowEx(0, L"STATIC", text.c_str(),
            WS_CHILD | WS_VISIBLE, 20, y, 350, 20, hwnd_,
            nullptr, GetModuleHandle(nullptr), nullptr);
    };

    auto wdev = std::wstring(device_id_.begin(), device_id_.end());
    auto wsrv = std::wstring(server_addr_.begin(), server_addr_.end());

    make_label(10, L"Device ID:");
    lbl_device_ = make_label(30, wdev);

    make_label(55, L"Server:");
    lbl_server_ = make_label(75, wsrv);

    make_label(100, L"Status:");
    lbl_state_ = make_label(120, L"Disconnected");

    btn_exit_ = CreateWindowEx(0, L"BUTTON", L"Exit",
        WS_CHILD | WS_VISIBLE, 300, 160, 70, 25, hwnd_,
        (HMENU)100, GetModuleHandle(nullptr), nullptr);

    // ---- Network setup ----
    auto wp = std::make_shared<std::wstring>(wsrv);
    tunnel::AgentConfig cfg;
    cfg.server_host = server_addr_;
    cfg.server_port = 4433;
    cfg.device_id = device_id_;
    cfg.agent_version = "0.1.0";
    cfg.platform = "windows-x86_64";

    agent_ = std::make_shared<tunnel::AgentConnection>(*io_, cfg);
    agent_->set_on_state_change([this](tunnel::AgentState state) {
        PostMessage(hwnd_, WM_USER, static_cast<WPARAM>(state), 0);
    });

    agent_->start();
    io_thread_ = std::thread([this] { io_->run(); });
}

void AgentWindow::on_close() {
    if (agent_) agent_->stop();
    if (io_) io_->stop();
}

void AgentWindow::update_state(tunnel::AgentState state) {
    const wchar_t* text = L"Unknown";
    switch (state) {
        case tunnel::AgentState::Disconnected: text = L"Disconnected"; break;
        case tunnel::AgentState::Connecting:   text = L"Connecting..."; break;
        case tunnel::AgentState::WaitHelloAck: text = L"Authenticating..."; break;
        case tunnel::AgentState::Online:       text = L"Online"; break;
        case tunnel::AgentState::Error:        text = L"Error"; break;
    }
    SetWindowText(lbl_state_, text);
    // Also update on WM_USER messages from network thread.
}

}  // namespace rmt::gui
#endif
