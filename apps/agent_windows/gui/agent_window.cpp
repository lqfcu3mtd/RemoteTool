#ifdef _WIN32
#include "agent_window.h"
#include "resources/resource.h"

#include <commctrl.h>
#include <cstdio>
#include <string>

namespace rmt::gui {

namespace {

constexpr int kWindowW = 520;
constexpr int kWindowH = 420;
constexpr int kMinW    = 440;
constexpr int kMinH    = 360;

constexpr wchar_t kAppTitle[] = L"RemoteTool Agent v0.1.0";

// UTF-8 → UTF-16 (config values come from JSON, i.e. UTF-8).
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, s.c_str(),
                                        static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                            static_cast<int>(s.size()), w.data(), n) == 0) {
        MultiByteToWideChar(CP_ACP, 0, s.c_str(),
                            static_cast<int>(s.size()), w.data(), n);
    }
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                static_cast<int>(w.size()), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring timestamp_now() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[16];
    swprintf_s(buf, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return buf;
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
                std::string device = narrow(buf);
                GetDlgItemText(hwnd, IDC_AS_HOST, buf, 255);
                std::string host = narrow(buf);
                GetDlgItemText(hwnd, IDC_AS_PORT, buf, 255);
                int port = 0;
                try { port = std::stoi(std::wstring(buf)); } catch (...) {}
                if (device.empty()) {
                    MessageBox(hwnd, L"Device ID cannot be empty.",
                               L"Validation", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwnd, IDC_AS_DEVICE));
                    return TRUE;
                }
                if (host.empty()) {
                    MessageBox(hwnd, L"Server host cannot be empty.",
                               L"Validation", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwnd, IDC_AS_HOST));
                    return TRUE;
                }
                if (port <= 0 || port > 65535) {
                    MessageBox(hwnd, L"Server port must be in 1..65535.",
                               L"Validation", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hwnd, IDC_AS_PORT));
                    return TRUE;
                }
                ctx->cfg->device_id = device;
                ctx->cfg->server_host = host;
                ctx->cfg->server_port = port;
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
    if (agent_) { agent_->stop(); }
    if (io_) { io_->stop(); }
    if (io_thread_.joinable()) { io_thread_.join(); }
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
    rebuild_whitelist();

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

    hwnd_ = CreateWindowEx(0, L"AgentWindow", kAppTitle,
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, kWindowW, kWindowH,
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
        case WM_SIZE:   on_size();   break;
        case WM_TIMER:  on_timer();  break;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = kMinW;
            mmi->ptMinTrackSize.y = kMinH;
            break;
        }
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
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lp) == lbl_state_big_) {
                auto* hdc = reinterpret_cast<HDC>(wp);
                SetTextColor(hdc, state_color_);
                SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(
                    GetSysColorBrush(COLOR_BTNFACE));
            }
            return DefWindowProc(hwnd_, msg, wp, lp);
        case WM_USER:
            update_state(static_cast<tunnel::AgentState>(wp));
            break;
        case WM_CLOSE: on_close(); DestroyWindow(hwnd_); break;
        case WM_DESTROY: on_destroy(); PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd_, msg, wp, lp);
    }
    return 0;
}

void AgentWindow::create_fonts() {
    HDC screen = GetDC(nullptr);
    int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen) ReleaseDC(nullptr, screen);

    LOGFONT lf = {};
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;

    lf.lfHeight = -MulDiv(9, dpi, 96);
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    font_ui_ = CreateFontIndirect(&lf);

    lf.lfHeight = -MulDiv(12, dpi, 96);
    lf.lfWeight = FW_BOLD;
    font_title_ = CreateFontIndirect(&lf);

    lf.lfHeight = -MulDiv(24, dpi, 96);
    lf.lfWeight = FW_BOLD;
    font_state_ = CreateFontIndirect(&lf);
}

void AgentWindow::apply_fonts() {
    if (!font_ui_) return;
    const HWND ui[] = {
        lbl_device_, lbl_server_, lbl_attempts_, lbl_log_caption_, edit_log_,
        btn_settings_, btn_reconnect_, btn_about_, btn_exit_,
    };
    for (HWND h : ui) {
        if (h) SendMessage(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui_), TRUE);
    }
    if (font_title_ && static_title_) {
        SendMessage(static_title_, WM_SETFONT,
                    reinterpret_cast<WPARAM>(font_title_), TRUE);
    }
    if (font_state_ && lbl_state_big_) {
        SendMessage(lbl_state_big_, WM_SETFONT,
                    reinterpret_cast<WPARAM>(font_state_), TRUE);
    }
}

void AgentWindow::layout_controls(int W, int H) {
    auto place = [](HWND h, int x, int y, int w, int ht) {
        if (h) MoveWindow(h, x, y, w, ht, TRUE);
    };
    const int pad = 16;
    const int btn_h = 28;
    const int btn_y = H - pad - btn_h;
    const int log_bottom = btn_y - 10;
    const int log_caption_y = 178;
    const int log_y = log_caption_y + 18;
    const int log_h = log_bottom > log_y ? log_bottom - log_y : 40;

    place(static_title_, pad, 12, W - 2 * pad, 20);
    place(lbl_state_big_, pad, 38, W - 2 * pad, 40);
    place(lbl_device_,   pad, 92,  W - 2 * pad, 18);
    place(lbl_server_,   pad, 114, W - 2 * pad, 18);
    place(lbl_attempts_, pad, 136, W - 2 * pad, 18);
    place(lbl_log_caption_, pad, log_caption_y, 200, 16);
    place(edit_log_, pad, log_y, W - 2 * pad, log_h);

    const int widths[] = {96, 96, 80, 80};
    const HWND btns[] = {btn_settings_, btn_reconnect_, btn_about_, btn_exit_};
    int x = pad;
    for (int i = 0; i < 4; ++i) {
        place(btns[i], x, btn_y, widths[i], btn_h);
        x += widths[i] + 10;
    }
}

void AgentWindow::on_create() {
    auto hi = GetModuleHandle(nullptr);

    static_title_ = CreateWindowEx(0, L"STATIC", L"RemoteTool Agent",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, nullptr, hi, nullptr);
    lbl_state_big_ = CreateWindowEx(0, L"STATIC", L"Offline",
        WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 10, 10,
        hwnd_, nullptr, hi, nullptr);
    lbl_device_ = CreateWindowEx(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, nullptr, hi, nullptr);
    lbl_server_ = CreateWindowEx(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, nullptr, hi, nullptr);
    lbl_attempts_ = CreateWindowEx(0, L"STATIC", L"Reconnects: 0",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, nullptr, hi, nullptr);
    lbl_log_caption_ = CreateWindowEx(0, L"STATIC", L"Recent events",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, nullptr, hi, nullptr);
    edit_log_ = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_READONLY |
        ES_AUTOVSCROLL | WS_VSCROLL,
        0, 0, 10, 10, hwnd_, nullptr, hi, nullptr);

    btn_settings_ = CreateWindowEx(0, L"BUTTON", L"Settings...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_AGENT_SETTINGS, hi, nullptr);
    btn_reconnect_ = CreateWindowEx(0, L"BUTTON", L"Reconnect",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_AGENT_RECONNECT, hi, nullptr);
    btn_about_ = CreateWindowEx(0, L"BUTTON", L"About",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_AGENT_ABOUT, hi, nullptr);
    btn_exit_ = CreateWindowEx(0, L"BUTTON", L"Exit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_AGENT_EXIT, hi, nullptr);

    create_fonts();
    apply_fonts();
    RECT rc; GetClientRect(hwnd_, &rc);
    layout_controls(rc.right - rc.left, rc.bottom - rc.top);
    update_labels();
    append_log(L"Agent starting");
    SetTimer(hwnd_, kTimerId, 500, nullptr);

    // ---- Network setup ----
    restart_agent();
}

void AgentWindow::on_destroy() {
    if (font_ui_)    { DeleteObject(font_ui_);    font_ui_ = nullptr; }
    if (font_title_) { DeleteObject(font_title_); font_title_ = nullptr; }
    if (font_state_) { DeleteObject(font_state_); font_state_ = nullptr; }
}

void AgentWindow::on_size() {
    if (!hwnd_ || !edit_log_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    layout_controls(rc.right - rc.left, rc.bottom - rc.top);
}

void AgentWindow::rebuild_whitelist() {
    rmt::security::TargetPolicy policy;
    policy.allowed_cidrs = cfg_.allowed_cidrs;
    policy.allowed_ports = cfg_.allowed_ports;
    policy.allow_ipv6 = cfg_.allow_ipv6;
    policy.self_listener_port = 0;  // the Agent has no local listener
    auto result = rmt::security::TargetWhitelist::create(std::move(policy));
    if (auto* err = std::get_if<std::string>(&result)) {
        MessageBox(hwnd_,
            (L"Invalid target_policy in agent.json: " + widen(*err) +
             L"\n\nAll session targets will be denied until this is fixed.").c_str(),
            L"Target policy", MB_OK | MB_ICONWARNING);
        rmt::security::TargetPolicy deny_all;  // empty = deny-all
        whitelist_ = std::make_unique<rmt::security::TargetWhitelist>(
            std::get<rmt::security::TargetWhitelist>(
                rmt::security::TargetWhitelist::create(std::move(deny_all))));
        return;
    }
    whitelist_ = std::make_unique<rmt::security::TargetWhitelist>(
        std::get<rmt::security::TargetWhitelist>(std::move(result)));
    if (cfg_.allowed_cidrs.empty() || cfg_.allowed_ports.empty()) {
        append_log(L"target_policy is empty: all session targets will be denied");
    }
}

void AgentWindow::restart_agent() {
    // Tear down the previous instance. AgentConnection::stop() is terminal
    // (stopping_ is latched), so reconnecting requires a fresh object.
    if (agent_) {
        agent_->stop();
        agent_.reset();
    }
    session_mgr_.reset();  // sessions die with the old connection
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
        // Marshalled to the GUI thread; controls are only touched there.
        PostMessage(hwnd_, WM_USER, static_cast<WPARAM>(state), 0);
    });

    // Session dispatch: OPEN_SESSION / SESSION_DATA / ... are routed to
    // per-session AgentSession handlers (whitelist enforced inside).
    session_mgr_ = std::make_unique<tunnel::AgentSessionManager>(*whitelist_);
    session_mgr_->attach(agent_);
    session_mgr_->set_on_event([this](std::string text) {
        std::lock_guard<std::mutex> lk(events_m_);
        gui_events_.push_back(widen(text));
    });

    agent_->start();
    io_thread_ = std::thread([this] { io_->run(); });
}

void AgentWindow::on_close() {
    if (agent_) agent_->stop();
    if (io_) io_->stop();
}

void AgentWindow::on_timer() {
    drain_event_queue();
    // Refresh the active-session counter from the io thread.
    if (io_ && session_mgr_) {
        asio::post(*io_, [this] {
            int n = session_mgr_
                ? static_cast<int>(session_mgr_->active_session_count()) : 0;
            pending_session_count_.store(n);
        });
    }
}

void AgentWindow::drain_event_queue() {
    std::deque<std::wstring> batch;
    {
        std::lock_guard<std::mutex> lk(events_m_);
        batch.swap(gui_events_);
    }
    for (const auto& line : batch) append_log(line);

    int n = pending_session_count_.exchange(-1);
    if (n >= 0 && n != sessions_ui_) {
        sessions_ui_ = n;
        update_labels();
    }
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
    rebuild_whitelist();
    append_log(L"Settings saved; reconnecting");
    restart_agent();
}

void AgentWindow::on_reconnect() {
    if (!agent_) return;
    append_log(L"Manual reconnect requested");
    restart_agent();
}

void AgentWindow::on_about() {
    std::wstring text = L"RemoteTool Agent v0.1.0\n\n"
        L"Reverse tunnel client.\n\n"
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
    // Count completed reconnect cycles: every transition into Connecting
    // after the first connection attempt means the watchdog restarted us.
    if (state == tunnel::AgentState::Connecting &&
        state_ != tunnel::AgentState::Disconnected &&
        state_ != tunnel::AgentState::Connecting) {
        ++reconnect_attempts_;
        update_labels();
    }

    const wchar_t* text = L"Unknown";
    COLORREF color = RGB(0x88, 0x88, 0x88);
    switch (state) {
        case tunnel::AgentState::Disconnected:
            text = L"Offline";       color = RGB(0xb0, 0x2a, 0x2a); break;
        case tunnel::AgentState::Connecting:
            text = L"Connecting..."; color = RGB(0xc0, 0x74, 0x00); break;
        case tunnel::AgentState::WaitHelloAck:
            text = L"Authenticating..."; color = RGB(0x1f, 0x5a, 0xb8); break;
        case tunnel::AgentState::Online:
            text = L"Online";        color = RGB(0x0f, 0x6e, 0x38); break;
        case tunnel::AgentState::Error:
            text = L"Error";         color = RGB(0xb0, 0x2a, 0x2a); break;
    }
    state_ = state;
    state_color_ = color;
    SetWindowText(lbl_state_big_, text);
    InvalidateRect(lbl_state_big_, nullptr, TRUE);
    append_log(text);

    // Leaving the Online state means the tunnel is gone: drop all sessions.
    if (state != tunnel::AgentState::Online && session_mgr_ && io_) {
        auto* mgr = session_mgr_.get();
        asio::post(*io_, [mgr] { mgr->clear_all(); });
        sessions_ui_ = 0;
    }
}

void AgentWindow::update_labels() {
    SetWindowText(lbl_device_,
        (L"Device ID:  " + widen(cfg_.device_id)).c_str());
    SetWindowText(lbl_server_,
        (L"Server:  " + widen(cfg_.server_host) + L":" +
         std::to_wstring(cfg_.server_port)).c_str());
    SetWindowText(lbl_attempts_,
        (L"Reconnects: " + std::to_wstring(reconnect_attempts_) +
         L"   ·   Active sessions: " + std::to_wstring(sessions_ui_)).c_str());
}

void AgentWindow::append_log(const std::wstring& line) {
    if (!edit_log_) return;
    std::wstring entry = timestamp_now() + L"  " + line + L"\r\n";
    int len = GetWindowTextLength(edit_log_);
    // Keep the log bounded (~8 KiB of text).
    if (len > 8000) {
        SendMessage(edit_log_, EM_SETSEL, 0, len / 2);
        SendMessage(edit_log_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        len = GetWindowTextLength(edit_log_);
    }
    SendMessage(edit_log_, EM_SETSEL, len, len);
    SendMessage(edit_log_, EM_REPLACESEL, FALSE,
                reinterpret_cast<LPARAM>(entry.c_str()));
}

void AgentWindow::save_config() {
    config::save_agent_config("agent.json", cfg_);
}

}  // namespace rmt::gui
#endif
