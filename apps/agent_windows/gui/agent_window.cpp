#ifdef _WIN32
#include "agent_window.h"
#include "resources/resource.h"
#include <cstdio>
#include <commctrl.h>
#include <string>

namespace rmt::gui {

namespace {
std::wstring widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

// ============ Settings dialog ============

struct SettingsDlgContext {
    config::AgentConfigFile* cfg;
};

static INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<SettingsDlgContext*>(
        GetWindowLongPtr(hwnd, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<SettingsDlgContext*>(lp);
            SetWindowLongPtr(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
            SetDlgItemText(hwnd, IDC_AS_DEVICE, widen(ctx->cfg->device_id).c_str());
            SetDlgItemText(hwnd, IDC_AS_HOST,   widen(ctx->cfg->server_host).c_str());
            SetDlgItemText(hwnd, IDC_AS_PORT,
                std::to_wstring(ctx->cfg->server_port).c_str());
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                wchar_t buf[256] = {};
                GetDlgItemText(hwnd, IDC_AS_DEVICE, buf, 255);
                ctx->cfg->device_id = std::string(buf, buf + wcslen(buf));
                GetDlgItemText(hwnd, IDC_AS_HOST, buf, 255);
                ctx->cfg->server_host = std::string(buf, buf + wcslen(buf));
                GetDlgItemText(hwnd, IDC_AS_PORT, buf, 255);
                try {
                    ctx->cfg->server_port = std::stoi(std::wstring(buf));
                } catch (...) {
                    ctx->cfg->server_port = 0;
                }
                if (ctx->cfg->device_id.empty() || ctx->cfg->server_host.empty() ||
                    ctx->cfg->server_port <= 0 || ctx->cfg->server_port > 65535) {
                    MessageBox(hwnd,
                        L"Device ID, Server host and a valid port (1..65535) are required.",
                        L"Validation", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}
}  // anonymous namespace

AgentWindow* AgentWindow::instance_ = nullptr;

AgentWindow::AgentWindow() : io_(std::make_unique<asio::io_context>()) {}

AgentWindow::~AgentWindow() {
    if (io_thread_.joinable()) { io_->stop(); io_thread_.join(); }
}

int AgentWindow::run() {
    instance_ = this;

    // Load config: try agent.json, fall back to defaults.
    auto lr = config::load_agent_config("agent.json");
    if (auto* loaded = config::try_get_loaded(lr)) {
        cfg_ = *loaded;
    } else {
        cfg_.server_host = "127.0.0.1";
        cfg_.server_port = 4433;
        cfg_.device_id   = "AGENT001";
        // Save the stub so the user can edit it from the GUI.
        save_config();
    }

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"AgentWindow";
    wc.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON));
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wc);

    hwnd_ = CreateWindowEx(0, L"AgentWindow", L"RemoteTool Agent",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 460, 220,
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
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        auto* self = reinterpret_cast<AgentWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<AgentWindow*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self) return self->handle_message(msg, wp, lp);
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT AgentWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: on_create(); break;
        case WM_COMMAND: {
            int id = LOWORD(wp);
            switch (id) {
                case IDC_AGENT_SETTINGS:  on_settings(); break;
                case IDC_AGENT_RECONNECT: on_reconnect(); break;
                case IDC_AGENT_ABOUT:     on_about(); break;
                case IDC_AGENT_EXIT:      on_close(); DestroyWindow(hwnd_); break;
            }
            break;
        }
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
    auto hi = GetModuleHandle(nullptr);

    // Read-only labels (config is shown for transparency; edit via Settings).
    auto make_label = [&](int y, const std::wstring& text) -> HWND {
        return CreateWindowEx(0, L"STATIC", text.c_str(),
            WS_CHILD | WS_VISIBLE, 20, y, 400, 20, hwnd_,
            nullptr, hi, nullptr);
    };

    make_label(10,  L"Device ID:");
    lbl_device_ = make_label(30, widen(cfg_.device_id));
    make_label(55,  L"Server:");
    lbl_server_ = make_label(75, widen(cfg_.server_host) + L":" +
                                   std::to_wstring(cfg_.server_port));
    make_label(100, L"Status:");
    lbl_state_ = make_label(120, L"Disconnected");

    // Action row
    int btn_y = 160;
    int x = 20;
    btn_settings_ = CreateWindowEx(0, L"BUTTON", L"Settings...",
        WS_CHILD | WS_VISIBLE, x, btn_y, 80, 26, hwnd_,
        (HMENU)IDC_AGENT_SETTINGS, hi, nullptr);
    x += 90;
    btn_reconnect_ = CreateWindowEx(0, L"BUTTON", L"Reconnect",
        WS_CHILD | WS_VISIBLE, x, btn_y, 90, 26, hwnd_,
        (HMENU)IDC_AGENT_RECONNECT, hi, nullptr);
    x += 100;
    btn_about_ = CreateWindowEx(0, L"BUTTON", L"About",
        WS_CHILD | WS_VISIBLE, x, btn_y, 70, 26, hwnd_,
        (HMENU)IDC_AGENT_ABOUT, hi, nullptr);
    x += 80;
    btn_exit_ = CreateWindowEx(0, L"BUTTON", L"Exit",
        WS_CHILD | WS_VISIBLE, x, btn_y, 70, 26, hwnd_,
        (HMENU)IDC_AGENT_EXIT, hi, nullptr);

    // ---- Network setup ----
    tunnel::AgentConfig ac;
    ac.server_host = cfg_.server_host;
    ac.server_port = static_cast<std::uint16_t>(cfg_.server_port);
    ac.device_id = cfg_.device_id;
    ac.agent_version = "0.1.0";
    ac.platform = "windows-x86_64";

    agent_ = std::make_shared<tunnel::AgentConnection>(*io_, ac);
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

void AgentWindow::on_settings() {
    config::AgentConfigFile copy = cfg_;
    SettingsDlgContext ctx{&copy};
    if (DialogBoxParam(GetModuleHandle(nullptr),
                       MAKEINTRESOURCE(IDD_AGENT_SETTINGS),
                       hwnd_, SettingsDlgProc,
                       reinterpret_cast<LPARAM>(&ctx)) != IDOK) {
        return;
    }
    cfg_ = copy;
    save_config();
    update_labels();
    // Apply the new config: tear down and restart the agent.
    if (agent_) {
        agent_->stop();
        agent_.reset();
    }
    if (io_) io_->stop();
    if (io_thread_.joinable()) io_thread_.join();
    io_ = std::make_unique<asio::io_context>();

    tunnel::AgentConfig ac;
    ac.server_host = cfg_.server_host;
    ac.server_port = static_cast<std::uint16_t>(cfg_.server_port);
    ac.device_id = cfg_.device_id;
    ac.agent_version = "0.1.0";
    ac.platform = "windows-x86_64";
    agent_ = std::make_shared<tunnel::AgentConnection>(*io_, ac);
    agent_->set_on_state_change([this](tunnel::AgentState state) {
        PostMessage(hwnd_, WM_USER, static_cast<WPARAM>(state), 0);
    });
    agent_->start();
    io_thread_ = std::thread([this] { io_->run(); });
}

void AgentWindow::on_reconnect() {
    if (!agent_) return;
    // Force a fresh connect attempt: stop, then re-start. Stops are
    // asynchronous, so a small delay is needed before do_connect can take
    // effect; the existing on_tunnel_closed path will call do_connect.
    agent_->stop();
}

void AgentWindow::on_about() {
    std::wstring text = L"RemoteTool Agent v0.1.0\n\n"
        L"Reverse tunnel client (PSK auth in Phase 5).\n\n"
        L"Config: agent.json (in the same directory as the .exe).";
    MSGBOXPARAMS mp = {};
    mp.cbSize = sizeof(mp);
    mp.hwndOwner = hwnd_;
    mp.hInstance = GetModuleHandle(nullptr);
    mp.lpszText = text.c_str();
    mp.lpszCaption = L"About Agent";
    mp.dwStyle = MB_USERICON | MB_OK;
    mp.lpszIcon = MAKEINTRESOURCE(IDI_APPICON);
    MessageBoxIndirect(&mp);
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
}

void AgentWindow::update_labels() {
    SetWindowText(lbl_device_, widen(cfg_.device_id).c_str());
    SetWindowText(lbl_server_,
        (widen(cfg_.server_host) + L":" + std::to_wstring(cfg_.server_port)).c_str());
}

void AgentWindow::save_config() {
    config::save_agent_config("agent.json", cfg_);
}

}  // namespace rmt::gui
#endif
