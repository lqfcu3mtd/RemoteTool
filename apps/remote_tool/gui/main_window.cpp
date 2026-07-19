#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "main_window.h"
#include "dialogs.h"
#include "resources/resource.h"

#include <algorithm>
#include <random>
#include <string>

namespace rmt::gui {

namespace {
    // ============ Control IDs for the main window controls ============
    constexpr int IDC_TOOLBAR_STATUS   = 301;
    constexpr int IDC_TOOLBAR_SUMMARY  = 302;
    constexpr int IDC_TOOLBAR_SETTINGS = 303;

    constexpr int IDC_LIST_DEVICES     = 310;
    constexpr int IDC_LIST_MAPPINGS    = 311;

    constexpr int IDC_PAIR_LABEL       = 320;
    constexpr int IDC_PAIR_CODE        = 321;
    constexpr int IDC_PAIR_EXPIRES     = 322;
    constexpr int IDC_PAIR_COPY        = 323;
    constexpr int IDC_PAIR_REGEN       = 324;
    constexpr int IDC_PAIR_REVOKE      = 325;

    constexpr int IDC_DEV_ADD          = 330;
    constexpr int IDC_DEV_EDIT         = 331;
    constexpr int IDC_DEV_DELETE       = 332;

    constexpr int IDC_SESS_LABEL       = 340;
    constexpr int IDC_SESS_BODY        = 341;

    constexpr int IDC_MAP_ADD          = 350;
    constexpr int IDC_MAP_EDIT         = 351;
    constexpr int IDC_MAP_START        = 352;
    constexpr int IDC_MAP_STOP         = 353;
    constexpr int IDC_MAP_DELETE       = 354;
    constexpr int IDC_MAP_FILTER_CLEAR = 355;
    constexpr int IDC_MAP_FILTER_LABEL = 356;

    constexpr int IDC_STATUS_BAR       = 360;

    // ============ Layout dimensions (pixels) ============
    constexpr int kWindowW   = 1060;
    constexpr int kWindowH   = 660;
    constexpr int kMinW      = 940;
    constexpr int kMinH      = 600;
    constexpr int kToolbarH  = 40;
    constexpr int kStatusH   = 24;
    constexpr int kLeftW     = 430;
    constexpr int kPanelPad  = 8;
    constexpr int kPairH     = 132;
    constexpr int kSessH     = 120;
    constexpr int kBtnRowH   = 34;

    constexpr wchar_t kAppTitle[] = L"RemoteTool v0.1.0 — Reverse Tunnel Server";

    // 8-digit random pair code (Phase 4 stub; Phase 5+ wires the real
    // PSK/pairing flow).
    std::string random_pair_code() {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        std::uniform_int_distribution<int> d(0, 9);
        std::string s(8, '0');
        for (auto& ch : s) ch = static_cast<char>('0' + d(rng));
        return s;
    }

    // UTF-8 → UTF-16. Config files are JSON/UTF-8, so device names with
    // non-ASCII characters must go through a real conversion, not a
    // byte-wise widening.
    std::wstring widen(const std::string& s) {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                    static_cast<int>(s.size()), nullptr, 0);
        if (n <= 0) {  // fall back to ANSI on invalid UTF-8
            n = MultiByteToWideChar(CP_ACP, 0, s.c_str(),
                                    static_cast<int>(s.size()), nullptr, 0);
            if (n <= 0) return {};
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_ACP, 0, s.c_str(),
                                static_cast<int>(s.size()), w.data(), n);
            return w;
        }
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                            static_cast<int>(s.size()), w.data(), n);
        return w;
    }

    // Format "X seconds ago" / "X min ago" / "X h ago" from a steady_clock
    // time_point.
    std::wstring ago_string(std::chrono::steady_clock::time_point t) {
        if (t.time_since_epoch().count() == 0) return L"—";
        auto now = std::chrono::steady_clock::now();
        auto s = std::chrono::duration_cast<std::chrono::seconds>(now - t).count();
        if (s < 0) s = 0;
        if (s < 60) return std::to_wstring(s) + L"s ago";
        if (s < 3600) return std::to_wstring(s / 60) + L"m ago";
        return std::to_wstring(s / 3600) + L"h ago";
    }

    // Insert / update a ListView subitem.
    void lv_set_text(HWND lv, int row, int col, const std::wstring& text) {
        LVITEM item = {};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = col;
        item.pszText = const_cast<LPWSTR>(text.c_str());
        SendMessage(lv, LVM_SETITEMTEXT, row, reinterpret_cast<LPARAM>(&item));
    }

    void lv_insert_row(HWND lv, int row, const std::wstring& col0) {
        LVITEM item = {};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(col0.c_str());
        SendMessage(lv, LVM_INSERTITEM, 0, reinterpret_cast<LPARAM>(&item));
    }

    int clamp_min(int v, int lo) { return v < lo ? lo : v; }
}  // anonymous namespace

MainWindow* MainWindow::instance_ = nullptr;

MainWindow::MainWindow()
    : io_(std::make_unique<asio::io_context>()),
      devices_(std::make_unique<tunnel::DeviceManager>(*io_)),
      acceptor_(std::make_unique<tunnel::Acceptor>(*io_, *devices_)),
      id_alloc_(std::make_unique<session::SessionIdAllocator>()),
      sessions_(std::make_unique<tunnel::SessionManager>(*io_, *devices_, *id_alloc_)) {}

MainWindow::~MainWindow() {
    if (io_thread_.joinable()) { io_->stop(); io_thread_.join(); }
    // listeners_ is destroyed after the io thread has stopped, so no accept
    // handler can still run against a listener being freed.
}

int MainWindow::run() {
    instance_ = this;

    // Load remote_tool.json (bind host / port / limits); persist defaults when
    // missing so the Settings dialog always has a file to update.
    auto lr = config::load_remote_tool_config("remote_tool.json");
    if (auto* loaded = config::try_get_loaded(lr)) {
        rt_config_ = *loaded;
    } else {
        config::save_remote_tool_config("remote_tool.json", rt_config_);
    }

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RemoteToolMain";
    wc.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APPICON));
    if (!wc.hIcon) wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wc);

    HMENU menu_bar = LoadMenu(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDR_MAINMENU));

    hwnd_ = CreateWindowEx(0, L"RemoteToolMain", kAppTitle,
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, kWindowW, kWindowH,
                           nullptr, menu_bar, GetModuleHandle(nullptr), this);
    if (!hwnd_) return 1;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        auto* self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    auto* self = reinterpret_cast<MainWindow*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self) return self->handle_message(msg, wp, lp);
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:     on_create(); break;
        case WM_TIMER:      on_timer();  break;
        case WM_SIZE:       on_size();   break;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = kMinW;
            mmi->ptMinTrackSize.y = kMinH;
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            switch (id) {
                case IDC_DEV_ADD:        on_add_device(); break;
                case IDC_DEV_EDIT:       on_edit_device(); break;
                case IDC_DEV_DELETE:     on_delete_device(); break;
                case IDC_PAIR_COPY:      on_copy_pair_code(); break;
                case IDC_PAIR_REGEN:     on_regenerate_pair_code(); break;
                case IDC_PAIR_REVOKE:    on_revoke_pair_code(); break;
                case IDC_MAP_ADD:        on_add_mapping(); break;
                case IDC_MAP_EDIT:       on_edit_mapping(); break;
                case IDC_MAP_START:      on_start_mapping(); break;
                case IDC_MAP_STOP:       on_stop_mapping(); break;
                case IDC_MAP_DELETE:     on_delete_mapping(); break;
                case IDC_MAP_FILTER_CLEAR: on_clear_mapping_filter(); break;
                case IDC_TOOLBAR_SETTINGS: on_settings(); break;
                // Menu commands.
                case IDM_FILE_EXIT:        on_close(); DestroyWindow(hwnd_); break;
                case IDM_EDIT_ADD_DEVICE:  on_add_device(); break;
                case IDM_EDIT_ADD_MAPPING: on_add_mapping(); break;
                case IDM_FILE_SETTINGS:    on_settings(); break;
                case IDM_VIEW_REFRESH:     refresh_devices_list();
                                            refresh_mappings_list();
                                            update_status_bar(); break;
                case IDM_VIEW_SHOW_ALL:    on_clear_mapping_filter(); break;
                case IDM_HELP_ABOUT: {
                    HICON h = LoadIcon(GetModuleHandle(nullptr),
                                       MAKEINTRESOURCE(IDI_APPICON));
                    std::wstring about = L"RemoteTool v0.1.0\n\n"
                        L"Lightweight reverse tunnel for remote maintenance.\n\n"
                        L"Server GUI: device pairing, port mappings and live\n"
                        L"session monitoring. TLS-PSK client path complete;\n"
                        L"server-side TLS is scheduled for a later phase.";
                    MSGBOXPARAMS mp = {};
                    mp.cbSize = sizeof(mp);
                    mp.hwndOwner = hwnd_;
                    mp.hInstance = GetModuleHandle(nullptr);
                    mp.lpszText = about.c_str();
                    mp.lpszCaption = L"About RemoteTool";
                    mp.dwStyle = MB_USERICON | MB_OK;
                    if (h) mp.lpszIcon = MAKEINTRESOURCE(IDI_APPICON);
                    MessageBoxIndirect(&mp);
                    break;
                }
            }
            break;
        }
        case WM_NOTIFY:     on_notify(reinterpret_cast<NMHDR*>(lp)); break;
        case WM_CLOSE:      on_close(); DestroyWindow(hwnd_); break;
        case WM_DESTROY:    on_destroy(); PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd_, msg, wp, lp);
    }
    return 0;
}

// ============ Fonts / layout ============

void MainWindow::create_fonts() {
    HDC screen = GetDC(nullptr);
    int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen) ReleaseDC(nullptr, screen);

    LOGFONT lf = {};
    lf.lfHeight = -MulDiv(9, dpi, 96);
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    font_ui_ = CreateFontIndirect(&lf);

    lf.lfHeight = -MulDiv(18, dpi, 96);
    lf.lfWeight = FW_BOLD;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    font_code_ = CreateFontIndirect(&lf);
}

void MainWindow::apply_fonts() {
    if (!font_ui_) return;
    const HWND all[] = {
        toolbar_status_, toolbar_summary_, toolbar_settings_,
        panel_devices_, list_devices_, panel_pair_code_,
        static_pair_label_, static_pair_expires_,
        btn_pair_copy_, btn_pair_regen_, btn_pair_revoke_,
        btn_dev_add_, btn_dev_edit_, btn_dev_delete_,
        panel_mappings_, list_mappings_, panel_sessions_,
        static_sessions_label_, static_sessions_body_,
        btn_map_filter_clear_, static_map_filter_,
        btn_map_add_, btn_map_edit_, btn_map_start_, btn_map_stop_,
        btn_map_delete_, status_bar_,
    };
    for (HWND h : all) {
        if (h) SendMessage(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_ui_), TRUE);
    }
    if (font_code_ && static_pair_code_) {
        SendMessage(static_pair_code_, WM_SETFONT,
                    reinterpret_cast<WPARAM>(font_code_), TRUE);
    }
}

void MainWindow::layout_controls(int W, int H) {
    auto place = [](HWND h, int x, int y, int w, int ht) {
        if (h) MoveWindow(h, x, y, w, ht, TRUE);
    };
    const int panels_y = kToolbarH + 4;
    const int panels_h = clamp_min(H - kStatusH - panels_y - kPanelPad, 200);

    // ---- Toolbar ----
    place(toolbar_status_,  12, 10, 320, 20);
    place(toolbar_settings_, W - 100, 8, 88, 24);
    place(toolbar_summary_, W - 100 - 8 - 280, 10, 280, 20);

    // ---- Left panel: Devices ----
    const int lx = kPanelPad;
    const int lw = kLeftW;
    const int ly = panels_y;
    const int lh = panels_h;

    place(panel_devices_, lx, ly, lw, lh);

    const int btn_y   = ly + lh - kBtnRowH;
    const int pair_y  = btn_y - 8 - kPairH;
    const int list_h  = clamp_min(pair_y - 8 - (ly + 20), 60);

    place(list_devices_, lx + 10, ly + 20, lw - 20, list_h);
    place(panel_pair_code_, lx + 10, pair_y, lw - 20, kPairH);
    place(static_pair_label_,   lx + 20, pair_y + 16, lw - 40, 16);
    place(static_pair_code_,    lx + 20, pair_y + 36, lw - 40, 34);
    place(static_pair_expires_, lx + 20, pair_y + 76, lw - 40, 16);
    place(btn_pair_copy_,   lx + 20,  pair_y + 98, 60, 26);
    place(btn_pair_regen_,  lx + 84,  pair_y + 98, 84, 26);
    place(btn_pair_revoke_, lx + 172, pair_y + 98, 70, 26);

    place(btn_dev_add_,    lx + 10,  btn_y + 2, 80, 26);
    place(btn_dev_edit_,   lx + 98,  btn_y + 2, 80, 26);
    place(btn_dev_delete_, lx + 186, btn_y + 2, 80, 26);

    // ---- Right panel: Mappings ----
    const int rx = lx + lw + kPanelPad;
    const int rw = clamp_min(W - rx - kPanelPad, 300);

    place(panel_mappings_, rx, ly, rw, lh);
    place(static_map_filter_,   rx + 10, ly + 4, rw - 120, 18);
    place(btn_map_filter_clear_, rx + rw - 100, ly + 2, 90, 20);

    const int mbtn_y  = ly + lh - kBtnRowH;
    const int sess_y  = mbtn_y - 8 - kSessH;
    const int mlist_h = clamp_min(sess_y - 8 - (ly + 24), 60);

    place(list_mappings_, rx + 10, ly + 24, rw - 20, mlist_h);
    place(panel_sessions_, rx + 10, sess_y, rw - 20, kSessH);
    place(static_sessions_label_, rx + 20, sess_y + 16, rw - 40, 16);
    place(static_sessions_body_,  rx + 20, sess_y + 36, rw - 40, kSessH - 48);

    int bx = rx + 10;
    for (HWND b : {btn_map_add_, btn_map_edit_, btn_map_start_,
                   btn_map_stop_, btn_map_delete_}) {
        place(b, bx, mbtn_y + 2, 72, 26);
        bx += 80;
    }

    // ---- Status bar ----
    SendMessage(status_bar_, WM_SIZE, 0, 0);
    const int parts[] = {150, W / 2, W - 300, -1};
    SendMessage(status_bar_, SB_SETPARTS, 4, reinterpret_cast<LPARAM>(parts));
}

// ============ Create / destroy ============

void MainWindow::on_create() {
    auto hi = GetModuleHandle(nullptr);
    RECT rc; GetClientRect(hwnd_, &rc);
    const int W = rc.right - rc.left;

    // ---- Top toolbar ----
    toolbar_status_ = CreateWindowEx(0, L"STATIC",
        (L"Listening on " + widen(rt_config_.bind_host) + L":" +
         std::to_wstring(rt_config_.agent_port)).c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, (HMENU)IDC_TOOLBAR_STATUS, hi, nullptr);
    toolbar_summary_ = CreateWindowEx(0, L"STATIC", L"0 online",
        WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 10, 10,
        hwnd_, (HMENU)IDC_TOOLBAR_SUMMARY, hi, nullptr);
    toolbar_settings_ = CreateWindowEx(0, L"BUTTON", L"Settings...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_TOOLBAR_SETTINGS, hi, nullptr);

    // ---- Left panel: Devices ----
    panel_devices_ = CreateWindowEx(0, L"BUTTON", L"Devices",
        BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, hwnd_, nullptr, hi, nullptr);

    list_devices_ = CreateWindowEx(0, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_LIST_DEVICES, hi, nullptr);
    ListView_SetExtendedListViewStyle(list_devices_,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    {
        LVCOLUMN c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        const struct { int w; const wchar_t* t; } cols[] = {
            {85,  L"Name"},   {75,  L"ID"},     {50, L"Status"},
            {40,  L"Ver"},    {110, L"Address"}, {55, L"Last"},
        };
        for (int i = 0; i < 6; ++i) {
            c.cx = cols[i].w;
            c.pszText = const_cast<LPWSTR>(cols[i].t);
            c.iSubItem = i;
            ListView_InsertColumn(list_devices_, i, &c);
        }
    }

    panel_pair_code_ = CreateWindowEx(0, L"BUTTON", L"Pair code",
        BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, hwnd_, nullptr, hi, nullptr);
    static_pair_label_ = CreateWindowEx(0, L"STATIC", L"No device selected",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, (HMENU)IDC_PAIR_LABEL, hi, nullptr);
    static_pair_code_ = CreateWindowEx(0, L"STATIC", L"— — — —   — — — —",
        WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 10, 10,
        hwnd_, (HMENU)IDC_PAIR_CODE, hi, nullptr);
    static_pair_expires_ = CreateWindowEx(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, (HMENU)IDC_PAIR_EXPIRES, hi, nullptr);
    btn_pair_copy_ = CreateWindowEx(0, L"BUTTON", L"Copy",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_PAIR_COPY, hi, nullptr);
    btn_pair_regen_ = CreateWindowEx(0, L"BUTTON", L"Regenerate",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_PAIR_REGEN, hi, nullptr);
    btn_pair_revoke_ = CreateWindowEx(0, L"BUTTON", L"Revoke",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_PAIR_REVOKE, hi, nullptr);
    EnableWindow(btn_pair_copy_,   FALSE);
    EnableWindow(btn_pair_regen_,  FALSE);
    EnableWindow(btn_pair_revoke_, FALSE);

    btn_dev_add_ = CreateWindowEx(0, L"BUTTON", L"+ Add",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_DEV_ADD, hi, nullptr);
    btn_dev_edit_ = CreateWindowEx(0, L"BUTTON", L"Edit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_DEV_EDIT, hi, nullptr);
    btn_dev_delete_ = CreateWindowEx(0, L"BUTTON", L"Delete",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_DEV_DELETE, hi, nullptr);

    // ---- Right panel: Mappings ----
    panel_mappings_ = CreateWindowEx(0, L"BUTTON", L"Port mappings",
        BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, hwnd_, nullptr, hi, nullptr);

    btn_map_filter_clear_ = CreateWindowEx(0, L"BUTTON", L"Show all",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_MAP_FILTER_CLEAR, hi, nullptr);
    static_map_filter_ = CreateWindowEx(0, L"STATIC",
        L"Filter: all devices", WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_MAP_FILTER_LABEL, hi, nullptr);

    list_mappings_ = CreateWindowEx(0, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_LIST_MAPPINGS, hi, nullptr);
    ListView_SetExtendedListViewStyle(list_mappings_,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    {
        LVCOLUMN c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        const struct { int w; const wchar_t* t; } cols[] = {
            {90,  L"Name"},  {100, L"Local"},  {140, L"Target"},
            {60,  L"State"}, {45,  L"Conn"},   {90,  L"Device"},
        };
        for (int i = 0; i < 6; ++i) {
            c.cx = cols[i].w;
            c.pszText = const_cast<LPWSTR>(cols[i].t);
            c.iSubItem = i;
            ListView_InsertColumn(list_mappings_, i, &c);
        }
    }

    panel_sessions_ = CreateWindowEx(0, L"BUTTON", L"Active sessions",
        BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        0, 0, 10, 10, hwnd_, nullptr, hi, nullptr);
    static_sessions_label_ = CreateWindowEx(0, L"STATIC", L"No mapping selected",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10,
        hwnd_, (HMENU)IDC_SESS_LABEL, hi, nullptr);
    static_sessions_body_ = CreateWindowEx(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_READONLY,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_SESS_BODY, hi, nullptr);

    btn_map_add_ = CreateWindowEx(0, L"BUTTON", L"+ Add",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_MAP_ADD, hi, nullptr);
    btn_map_edit_ = CreateWindowEx(0, L"BUTTON", L"Edit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_MAP_EDIT, hi, nullptr);
    btn_map_start_ = CreateWindowEx(0, L"BUTTON", L"Start",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_MAP_START, hi, nullptr);
    btn_map_stop_ = CreateWindowEx(0, L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_MAP_STOP, hi, nullptr);
    btn_map_delete_ = CreateWindowEx(0, L"BUTTON", L"Delete",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_MAP_DELETE, hi, nullptr);

    // ---- Bottom status bar ----
    status_bar_ = CreateWindowEx(0, STATUSCLASSNAME, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 10, 10, hwnd_, (HMENU)IDC_STATUS_BAR, hi, nullptr);

    create_fonts();
    apply_fonts();
    layout_controls(W, rc.bottom - rc.top);

    SetTimer(hwnd_, kTimerId, 500, nullptr);

    // ---- Load saved data ----
    load_devices_from_config();
    load_mappings_from_config();
    refresh_devices_list();
    refresh_mappings_list();
    update_status_bar();
    update_toolbar_status();
    update_filter_label();

    // ---- Network wiring ----
    // Session-scoped frames (SESSION_OPENED / SESSION_DATA / ...) that
    // DeviceManager does not handle are dispatched to SessionManager.
    devices_->set_on_unhandled_frame(
        [this](std::shared_ptr<tunnel::TunnelConnection> /*conn*/,
               const rmt::protocol::Frame& frame) {
            sessions_->on_session_frame(frame.header.session_id, frame);
        });

    devices_->set_on_device_online([this](const tunnel::DeviceEntry& e) {
        events_.push([this, id = e.device_id, addr = e.remote_address,
                      ver = e.agent_version] {
            add_device_to_list(id, addr, ver);
        });
    });
    devices_->set_on_device_offline([this](const tunnel::DeviceEntry& e) {
        const std::string id = e.device_id;
        // Tear down any forwarding sessions for the dropped device. This runs
        // on the io thread like all other SessionManager operations.
        asio::post(*io_, [this, id] {
            sessions_->remove_all_sessions_for_device(id);
        });
        events_.push([this, id] {
            update_device_online_state(id, false);
        });
    });
    sessions_->set_on_session_closed([this](std::uint32_t /*session_id*/) {
        events_.push([this] { update_status_bar(); });
    });

    sessions_->set_max_sessions(rt_config_.max_sessions_per_mapping);
    acceptor_->start(rt_config_.bind_host,
                     static_cast<std::uint16_t>(rt_config_.agent_port));
    devices_->start_cleanup_timer();

    // Start listeners for mappings persisted as enabled.
    for (int i = 0; i < static_cast<int>(mapping_rows_.size()); ++i) {
        if (mapping_rows_[i].running) start_listener_for_mapping(i);
    }

    io_thread_ = std::thread([this] { io_->run(); });
}

void MainWindow::on_destroy() {
    KillTimer(hwnd_, kTimerId);
    if (font_ui_)   { DeleteObject(font_ui_);   font_ui_ = nullptr; }
    if (font_code_) { DeleteObject(font_code_); font_code_ = nullptr; }
}

void MainWindow::on_size() {
    if (!status_bar_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    layout_controls(rc.right - rc.left, rc.bottom - rc.top);
}

void MainWindow::on_timer() {
    process_events();
    query_session_counts();
    // Pair-code countdown / expiry refresh.
    if (selected_device_idx_ >= 0 &&
        selected_device_idx_ < static_cast<int>(device_rows_.size())) {
        auto& d = device_rows_[selected_device_idx_];
        if (!d.pair_code.empty() &&
            std::chrono::steady_clock::now() >= d.pair_code_expires) {
            d.pair_code.clear();  // expired
        }
        update_pair_code_panel();
    }
    update_status_bar();
}

void MainWindow::query_session_counts() {
    // Snapshot the config ids (GUI thread), count on the io thread, apply
    // back on the GUI thread via the event queue.
    std::vector<std::string> ids;
    ids.reserve(mapping_rows_.size());
    for (const auto& m : mapping_rows_) ids.push_back(m.config_id);
    asio::post(*io_, [this, ids = std::move(ids)] {
        std::vector<std::size_t> counts;
        counts.reserve(ids.size());
        for (const auto& id : ids) {
            counts.push_back(sessions_->active_sessions_for_mapping(id));
        }
        const std::size_t total = sessions_->active_session_count();
        events_.push([this, ids = std::move(ids), counts = std::move(counts), total] {
            total_sessions_ui_ = static_cast<int>(total);
            bool changed = false;
            for (std::size_t i = 0; i < ids.size(); ++i) {
                int idx = find_mapping_row(ids[i]);
                if (idx < 0) continue;
                int v = static_cast<int>(counts[i]);
                if (mapping_rows_[idx].active_conns != v) {
                    mapping_rows_[idx].active_conns = v;
                    changed = true;
                }
            }
            if (!changed) return;
            // Update only the Conn column cells (full refresh would drop the
            // user's selection every timer tick).
            int displayed = 0;
            for (std::size_t i = 0; i < mapping_rows_.size(); ++i) {
                const auto& m = mapping_rows_[i];
                if (!filter_device_id_.empty() &&
                    m.device_id != filter_device_id_) continue;
                lv_set_text(list_mappings_, displayed, 4,
                            std::to_wstring(m.active_conns));
                ++displayed;
            }
            update_active_sessions_panel();
        });
    });
}

void MainWindow::on_close() {
    KillTimer(hwnd_, kTimerId);
    for (auto& [id, listener] : listeners_) {
        (void)id;
        if (listener) listener->stop();
    }
    acceptor_->stop();
    io_->stop();
}

void MainWindow::on_notify(NMHDR* nm) {
    if (!nm) return;
    if (nm->idFrom == IDC_LIST_DEVICES && nm->code == LVN_ITEMCHANGED) {
        auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
        if ((lv->uChanged & LVIF_STATE) &&
            (lv->uNewState & LVIS_SELECTED) != (lv->uOldState & LVIS_SELECTED)) {
            selected_device_idx_ = (lv->uNewState & LVIS_SELECTED) ? lv->iItem : -1;
            on_device_selection_changed();
        }
    } else if (nm->idFrom == IDC_LIST_MAPPINGS && nm->code == LVN_ITEMCHANGED) {
        auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
        if ((lv->uChanged & LVIF_STATE) &&
            (lv->uNewState & LVIS_SELECTED) != (lv->uOldState & LVIS_SELECTED)) {
            selected_mapping_idx_ = (lv->uNewState & LVIS_SELECTED) ? lv->iItem : -1;
            on_mapping_selection_changed();
        }
    } else if (nm->code == NM_DBLCLK) {
        if (nm->idFrom == IDC_LIST_DEVICES)  on_edit_device();
        if (nm->idFrom == IDC_LIST_MAPPINGS) on_edit_mapping();
    } else if (nm->code == NM_CUSTOMDRAW) {
        // Subtle color hints: green for online, gray for offline; green for
        // running mappings, gray for stopped.
        auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(nm);
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) {
            SetWindowLongPtr(hwnd_, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
            return;
        }
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            int row = static_cast<int>(cd->nmcd.dwItemSpec);
            if (nm->idFrom == IDC_LIST_DEVICES &&
                row >= 0 && row < static_cast<int>(device_rows_.size())) {
                cd->clrText = device_rows_[row].online
                    ? RGB(0x0f, 0x6e, 0x56)   // online green
                    : RGB(0x88, 0x88, 0x88);  // offline gray
            } else if (nm->idFrom == IDC_LIST_MAPPINGS) {
                int actual = displayed_to_actual_mapping(row);
                if (actual >= 0) {
                    cd->clrText = mapping_rows_[actual].running
                        ? RGB(0x0f, 0x6e, 0x56)
                        : RGB(0x88, 0x88, 0x88);
                }
            }
            SetWindowLongPtr(hwnd_, DWLP_MSGRESULT, CDRF_DODEFAULT);
            return;
        }
    }
}

void MainWindow::on_device_selection_changed() {
    if (selected_device_idx_ >= 0 &&
        selected_device_idx_ < static_cast<int>(device_rows_.size())) {
        filter_device_id_ = device_rows_[selected_device_idx_].device_id;
    } else {
        filter_device_id_.clear();
    }
    refresh_mappings_list();
    update_filter_label();
    update_pair_code_panel();
}

void MainWindow::on_mapping_selection_changed() {
    update_active_sessions_panel();
}

void MainWindow::on_clear_mapping_filter() {
    filter_device_id_.clear();
    selected_device_idx_ = -1;
    ListView_SetItemState(list_devices_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    refresh_mappings_list();
    update_filter_label();
    update_pair_code_panel();
}

void MainWindow::update_filter_label() {
    std::wstring text;
    if (filter_device_id_.empty()) {
        text = L"Filter: all devices";
    } else {
        int total = static_cast<int>(mapping_rows_.size());
        int shown = 0;
        for (const auto& m : mapping_rows_) {
            if (m.device_id == filter_device_id_) ++shown;
        }
        text = L"Filter: " + widen(filter_device_id_) +
               L"  (" + std::to_wstring(shown) + L" of " +
               std::to_wstring(total) + L")";
    }
    SetWindowText(static_map_filter_, text.c_str());
}

int MainWindow::find_device_row(const std::string& device_id) const {
    for (size_t i = 0; i < device_rows_.size(); ++i) {
        if (device_rows_[i].device_id == device_id) return static_cast<int>(i);
    }
    return -1;
}

int MainWindow::find_mapping_row(const std::string& config_id) const {
    for (size_t i = 0; i < mapping_rows_.size(); ++i) {
        if (mapping_rows_[i].config_id == config_id) return static_cast<int>(i);
    }
    return -1;
}

int MainWindow::displayed_to_actual_mapping(int displayed_row) const {
    int displayed = 0;
    for (size_t i = 0; i < mapping_rows_.size(); ++i) {
        if (!filter_device_id_.empty() &&
            mapping_rows_[i].device_id != filter_device_id_) continue;
        if (displayed == displayed_row) return static_cast<int>(i);
        ++displayed;
    }
    return -1;
}

// ============ Refresh helpers ============

void MainWindow::refresh_devices_list() {
    ListView_DeleteAllItems(list_devices_);
    for (size_t i = 0; i < device_rows_.size(); ++i) {
        const auto& d = device_rows_[i];
        const int row = static_cast<int>(i);
        lv_insert_row(list_devices_, row,
                      widen(d.display_name.empty() ? d.device_id : d.display_name));
        lv_set_text(list_devices_, row, 1, widen(d.device_id));
        lv_set_text(list_devices_, row, 2, d.online ? L"Online" : L"Offline");
        lv_set_text(list_devices_, row, 3,
                    d.agent_version.empty() ? L"—" : widen(d.agent_version));
        lv_set_text(list_devices_, row, 4,
                    d.remote_address.empty() ? L"—" : widen(d.remote_address));
        lv_set_text(list_devices_, row, 5,
                    d.online ? ago_string(d.last_seen) : L"—");
    }
    if (selected_device_idx_ >= static_cast<int>(device_rows_.size())) {
        selected_device_idx_ = -1;
    }
    update_pair_code_panel();
}

void MainWindow::refresh_mappings_list() {
    ListView_DeleteAllItems(list_mappings_);
    int row = 0;
    for (size_t i = 0; i < mapping_rows_.size(); ++i) {
        const auto& m = mapping_rows_[i];
        if (!filter_device_id_.empty() && m.device_id != filter_device_id_) continue;
        lv_insert_row(list_mappings_, row, widen(m.name));
        lv_set_text(list_mappings_, row, 1,
                    L"127.0.0.1:" + std::to_wstring(m.local_port));
        lv_set_text(list_mappings_, row, 2,
                    widen(m.target_host) + L":" + std::to_wstring(m.target_port));
        lv_set_text(list_mappings_, row, 3, m.running ? L"Running" : L"Stopped");
        lv_set_text(list_mappings_, row, 4, std::to_wstring(m.active_conns));
        lv_set_text(list_mappings_, row, 5, widen(m.device_id));
        ++row;
    }
    if (selected_mapping_idx_ >= row) {
        selected_mapping_idx_ = -1;
    }
    update_active_sessions_panel();
}

void MainWindow::update_pair_code_panel() {
    if (selected_device_idx_ < 0 ||
        selected_device_idx_ >= static_cast<int>(device_rows_.size())) {
        SetWindowText(static_pair_label_,   L"No device selected");
        SetWindowText(static_pair_code_,    L"— — — —   — — — —");
        SetWindowText(static_pair_expires_, L"");
        EnableWindow(btn_pair_copy_,   FALSE);
        EnableWindow(btn_pair_regen_,  FALSE);
        EnableWindow(btn_pair_revoke_, FALSE);
        return;
    }
    const auto& d = device_rows_[selected_device_idx_];
    std::wstring label = L"Pair code · " + widen(d.device_id);
    SetWindowText(static_pair_label_, label.c_str());

    if (!d.pair_code.empty()) {
        // Format "5 8 3 9   2 1 4 7" for readability.
        std::wstring formatted;
        for (size_t i = 0; i < d.pair_code.size(); ++i) {
            if (i == 4) formatted += L"   ";
            else if (i > 0) formatted += L' ';
            formatted += static_cast<wchar_t>(d.pair_code[i]);
        }
        SetWindowText(static_pair_code_, formatted.c_str());

        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            d.pair_code_expires - std::chrono::steady_clock::now()).count();
        if (remaining < 0) remaining = 0;
        std::wstring exp = L"Valid for " + std::to_wstring(remaining) + L"s";
        SetWindowText(static_pair_expires_, exp.c_str());
        EnableWindow(btn_pair_copy_,   TRUE);
        EnableWindow(btn_pair_regen_,  TRUE);
        EnableWindow(btn_pair_revoke_, TRUE);
    } else {
        SetWindowText(static_pair_code_, L"— — — —   — — — —");
        SetWindowText(static_pair_expires_, L"Click Regenerate to issue a code");
        EnableWindow(btn_pair_copy_,   FALSE);
        EnableWindow(btn_pair_regen_,  TRUE);
        EnableWindow(btn_pair_revoke_, FALSE);
    }
}

void MainWindow::update_active_sessions_panel() {
    int actual = displayed_to_actual_mapping(selected_mapping_idx_);
    if (actual < 0) {
        SetWindowText(static_sessions_label_, L"No mapping selected");
        SetWindowText(static_sessions_body_,  L"");
        return;
    }
    const auto& m = mapping_rows_[actual];
    std::wstring lbl = L"Active sessions · " + widen(m.name) + L" (" +
                       std::to_wstring(m.active_conns) + L")";
    SetWindowText(static_sessions_label_, lbl.c_str());
    if (m.running) {
        SetWindowText(static_sessions_body_,
            (L"Listening on 127.0.0.1:" + std::to_wstring(m.local_port) +
             L"\r\nForwarding to " + widen(m.target_host) + L":" +
             std::to_wstring(m.target_port) + L" via " + widen(m.device_id) +
             L"\r\nActive sessions on this mapping: " +
             std::to_wstring(m.active_conns)).c_str());
    } else {
        SetWindowText(static_sessions_body_,
            L"Mapping is stopped. Start it to accept connections.");
    }
}

void MainWindow::set_last_event(const std::wstring& text) {
    last_event_ = text;
    update_status_bar();
}

void MainWindow::update_status_bar() {
    if (!status_bar_) return;
    int online = 0;
    for (const auto& d : device_rows_) if (d.online) ++online;
    int running = 0;
    for (const auto& m : mapping_rows_) if (m.running) ++running;
    int conns = total_sessions_ui_;

    std::wstring listen = L"Listening on " + widen(rt_config_.bind_host) +
                          L":" + std::to_wstring(rt_config_.agent_port);
    std::wstring counts = std::to_wstring(online) + L" online · " +
                          std::to_wstring(running) + L" mapping(s) running · " +
                          std::to_wstring(conns) + L" active session(s)";

    SendMessage(status_bar_, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(L"Ready"));
    SendMessage(status_bar_, SB_SETTEXT, 1, reinterpret_cast<LPARAM>(listen.c_str()));
    SendMessage(status_bar_, SB_SETTEXT, 2, reinterpret_cast<LPARAM>(last_event_.c_str()));
    SendMessage(status_bar_, SB_SETTEXT, 3, reinterpret_cast<LPARAM>(counts.c_str()));
}

void MainWindow::update_toolbar_status() {
    int online = 0;
    for (const auto& d : device_rows_) if (d.online) ++online;
    int running = 0;
    for (const auto& m : mapping_rows_) if (m.running) ++running;
    std::wstring s = std::to_wstring(online) + L" online · " +
                     std::to_wstring(running) + L" running";
    SetWindowText(toolbar_summary_, s.c_str());
}

// ============ Config load / save ============

void MainWindow::load_devices_from_config() {
    auto lr = config::load_devices_config("devices.json");
    if (auto* cfg = config::try_get_loaded(lr)) {
        for (auto& rec : cfg->devices) {
            DeviceRow row;
            row.device_id    = rec.id;
            row.display_name = rec.display_name;
            device_rows_.push_back(std::move(row));
        }
    }
}

void MainWindow::load_mappings_from_config() {
    auto lr = config::load_mappings_config("mappings.json");
    if (auto* cfg = config::try_get_loaded(lr)) {
        for (auto& rec : cfg->mappings) {
            MappingRow row;
            row.name        = rec.name;
            row.device_id   = rec.device_id;
            row.local_port  = static_cast<std::uint16_t>(rec.local_port);
            row.target_host = rec.target_host;
            row.target_port = static_cast<std::uint16_t>(rec.target_port);
            row.running     = rec.enabled;
            row.config_id   = rec.id;
            mapping_rows_.push_back(std::move(row));
        }
    }
}

void MainWindow::save_devices_config() {
    config::DevicesConfig cfg;
    for (const auto& d : device_rows_) {
        config::DeviceRecord rec;
        rec.id = d.device_id;
        rec.display_name = d.display_name;
        rec.enabled = true;  // persisted flag, not the transient online state
        cfg.devices.push_back(std::move(rec));
    }
    config::save_devices_config("devices.json", cfg);
}

void MainWindow::save_mappings_config() {
    config::MappingsConfig cfg;
    for (const auto& m : mapping_rows_) {
        config::MappingRecord rec;
        rec.id          = m.config_id.empty() ? ("map-" + m.name) : m.config_id;
        rec.name        = m.name;
        rec.device_id   = m.device_id;
        rec.local_port  = m.local_port;
        rec.target_host = m.target_host;
        rec.target_port = m.target_port;
        rec.enabled     = m.running;
        cfg.mappings.push_back(std::move(rec));
    }
    config::save_mappings_config("mappings.json", cfg);
}

// ============ Event handlers (network) ============

void MainWindow::process_events() {
    events_.process();
}

void MainWindow::add_device_to_list(const std::string& device_id,
                                    const std::string& address,
                                    const std::string& agent_version) {
    int idx = find_device_row(device_id);
    if (idx < 0) {
        DeviceRow row;
        row.device_id      = device_id;
        row.display_name   = device_id;
        row.online         = true;
        row.agent_version  = agent_version;
        row.remote_address = address;
        row.last_seen      = std::chrono::steady_clock::now();
        device_rows_.push_back(std::move(row));
        save_devices_config();
    } else {
        device_rows_[idx].online         = true;
        device_rows_[idx].agent_version  = agent_version;
        device_rows_[idx].remote_address = address;
        device_rows_[idx].last_seen      = std::chrono::steady_clock::now();
    }
    set_last_event(L"Device online: " + widen(device_id));
    refresh_devices_list();
    update_toolbar_status();
}

void MainWindow::update_device_online_state(const std::string& device_id, bool online) {
    int idx = find_device_row(device_id);
    if (idx < 0) return;
    device_rows_[idx].online = online;
    if (online) {
        device_rows_[idx].last_seen = std::chrono::steady_clock::now();
    }
    set_last_event(std::wstring(L"Device ") + (online ? L"online: " : L"offline: ") +
                   widen(device_id));
    refresh_devices_list();
    update_toolbar_status();
}

// ============ Mapping listener lifecycle ============

void MainWindow::start_listener_for_mapping(int actual_idx) {
    if (actual_idx < 0 || actual_idx >= static_cast<int>(mapping_rows_.size())) return;
    const auto& m = mapping_rows_[actual_idx];
    if (listeners_.count(m.config_id) != 0) return;  // already running

    auto listener = std::make_unique<tunnel::MappingListener>(*io_, *sessions_, *devices_);
    // start() binds synchronously and posts the accept loop to io_; only this
    // listener object is touched, so calling it from the GUI thread while the
    // io thread runs is safe (distinct-object rule).
    const rmt::ErrorCode started = listener->start(
        rt_config_.mapping_bind_host, m.local_port,
        m.device_id, m.config_id, m.target_host, m.target_port);
    if (started != rmt::ErrorCode::Ok) {
        // Bind failed (e.g. port already in use) — roll back to Stopped.
        MessageBox(hwnd_,
            (L"Failed to listen on port " + std::to_wstring(m.local_port) +
             L" (" + widen(std::string(rmt::to_string(started))) +
             L"). The port may already be in use.").c_str(),
            L"Mapping", MB_OK | MB_ICONERROR);
        mapping_rows_[actual_idx].running = false;
        save_mappings_config();
        set_last_event(L"Listener failed: " + widen(m.name));
        return;
    }
    listeners_.emplace(m.config_id, std::move(listener));
    set_last_event(L"Mapping started: " + widen(m.name));
}

void MainWindow::stop_listener_for_mapping(const std::string& config_id) {
    auto it = listeners_.find(config_id);
    if (it == listeners_.end()) return;
    it->second->stop();
    // Defer destruction onto the io thread: a pending accept handler holds a
    // raw `this` and is delivered (as operation_aborted) before any handler
    // posted afterwards, so this ordering is safe.
    auto* raw = it->second.release();
    listeners_.erase(it);
    asio::post(*io_, [raw] { delete raw; });
    set_last_event(L"Mapping stopped: " + widen(config_id));
}

void MainWindow::stop_all_listeners() {
    for (auto& [id, listener] : listeners_) {
        (void)id;
        if (listener) listener->stop();
    }
    // Destruction happens in ~MainWindow after the io thread has stopped.
}

// ============ Device actions ============

void MainWindow::on_add_device() {
    std::vector<std::string> existing;
    for (const auto& d : device_rows_) existing.push_back(d.device_id);

    DeviceDialogResult r;
    if (ShowDeviceDialog(hwnd_, &r, existing, false) != IDOK || !r.accepted) return;

    DeviceRow row;
    row.device_id     = r.device_id;
    row.display_name  = r.display_name.empty() ? r.device_id : r.display_name;
    row.pair_code     = random_pair_code();
    row.pair_code_expires = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    device_rows_.push_back(std::move(row));
    save_devices_config();
    refresh_devices_list();
    update_status_bar();

    // Auto-select the new device so the user immediately sees its pair code.
    selected_device_idx_ = static_cast<int>(device_rows_.size()) - 1;
    ListView_SetItemState(list_devices_, selected_device_idx_,
                          LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    update_pair_code_panel();
    set_last_event(L"Device added: " + widen(r.device_id));
}

void MainWindow::on_edit_device() {
    if (selected_device_idx_ < 0 ||
        selected_device_idx_ >= static_cast<int>(device_rows_.size())) return;
    std::vector<std::string> existing;
    for (const auto& d : device_rows_) existing.push_back(d.device_id);

    DeviceDialogResult r;
    r.device_id    = device_rows_[selected_device_idx_].device_id;
    r.display_name = device_rows_[selected_device_idx_].display_name;
    if (ShowDeviceDialog(hwnd_, &r, existing, true) != IDOK || !r.accepted) return;

    device_rows_[selected_device_idx_].display_name = r.display_name;
    save_devices_config();
    refresh_devices_list();
}

void MainWindow::on_delete_device() {
    if (selected_device_idx_ < 0 ||
        selected_device_idx_ >= static_cast<int>(device_rows_.size())) return;
    auto& d = device_rows_[selected_device_idx_];
    if (MessageBox(hwnd_,
            (L"Delete device \"" + widen(d.device_id) + L"\"? This will also remove its mappings.").c_str(),
            L"Confirm", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    std::string removed_id = d.device_id;
    device_rows_.erase(device_rows_.begin() + selected_device_idx_);
    // Stop listeners of mappings owned by this device, then drop the mappings.
    for (const auto& m : mapping_rows_) {
        if (m.device_id == removed_id) stop_listener_for_mapping(m.config_id);
    }
    mapping_rows_.erase(
        std::remove_if(mapping_rows_.begin(), mapping_rows_.end(),
            [&](const MappingRow& m) { return m.device_id == removed_id; }),
        mapping_rows_.end());
    if (filter_device_id_ == removed_id) filter_device_id_.clear();
    selected_device_idx_ = -1;
    save_devices_config();
    save_mappings_config();
    refresh_devices_list();
    refresh_mappings_list();
    update_status_bar();
    update_toolbar_status();
    set_last_event(L"Device deleted: " + widen(removed_id));
}

void MainWindow::on_copy_pair_code() {
    if (selected_device_idx_ < 0 ||
        selected_device_idx_ >= static_cast<int>(device_rows_.size())) return;
    const auto& d = device_rows_[selected_device_idx_];
    if (d.pair_code.empty()) return;
    if (!OpenClipboard(hwnd_)) return;
    EmptyClipboard();
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (d.pair_code.size() + 1) * sizeof(wchar_t));
    if (!hg) { CloseClipboard(); return; }
    auto* dst = static_cast<wchar_t*>(GlobalLock(hg));
    if (!dst) { GlobalFree(hg); CloseClipboard(); return; }
    for (size_t i = 0; i < d.pair_code.size(); ++i) {
        dst[i] = static_cast<wchar_t>(d.pair_code[i]);
    }
    dst[d.pair_code.size()] = L'\0';
    GlobalUnlock(hg);
    if (!SetClipboardData(CF_UNICODETEXT, hg)) {
        GlobalFree(hg);  // ownership was not transferred — avoid a leak
    }
    CloseClipboard();
}

void MainWindow::on_regenerate_pair_code() {
    if (selected_device_idx_ < 0 ||
        selected_device_idx_ >= static_cast<int>(device_rows_.size())) return;
    device_rows_[selected_device_idx_].pair_code = random_pair_code();
    device_rows_[selected_device_idx_].pair_code_expires =
        std::chrono::steady_clock::now() + std::chrono::minutes(10);
    update_pair_code_panel();
}

void MainWindow::on_revoke_pair_code() {
    if (selected_device_idx_ < 0 ||
        selected_device_idx_ >= static_cast<int>(device_rows_.size())) return;
    device_rows_[selected_device_idx_].pair_code.clear();
    update_pair_code_panel();
}

// ============ Mapping actions ============

void MainWindow::on_add_mapping() {
    if (device_rows_.empty()) {
        MessageBox(hwnd_, L"Add a device first.", L"Mapping", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<std::string> ids;
    for (const auto& d : device_rows_) ids.push_back(d.device_id);

    MappingDialogResult r;
    if (ShowMappingDialog(hwnd_, &r, ids, filter_device_id_, false) != IDOK || !r.accepted) return;

    // Reject duplicate local ports up front (bind would fail anyway).
    for (const auto& m : mapping_rows_) {
        if (m.local_port == r.local_port) {
            MessageBox(hwnd_,
                (L"Local port " + std::to_wstring(r.local_port) +
                 L" is already used by mapping \"" + widen(m.name) + L"\".").c_str(),
                L"Mapping", MB_OK | MB_ICONWARNING);
            return;
        }
    }

    MappingRow row;
    row.name        = r.name;
    row.device_id   = r.device_id;
    row.local_port  = r.local_port;
    row.target_host = r.target_host;
    row.target_port = r.target_port;
    row.running     = true;
    row.config_id   = "map-" + r.name;
    mapping_rows_.push_back(std::move(row));
    save_mappings_config();
    start_listener_for_mapping(static_cast<int>(mapping_rows_.size()) - 1);
    refresh_mappings_list();
    update_status_bar();
    update_toolbar_status();
}

void MainWindow::on_edit_mapping() {
    int actual = displayed_to_actual_mapping(selected_mapping_idx_);
    if (actual < 0) return;

    std::vector<std::string> ids;
    for (const auto& d : device_rows_) ids.push_back(d.device_id);

    MappingDialogResult r;
    r.name         = mapping_rows_[actual].name;
    r.device_id    = mapping_rows_[actual].device_id;
    r.local_port   = mapping_rows_[actual].local_port;
    r.target_host  = mapping_rows_[actual].target_host;
    r.target_port  = mapping_rows_[actual].target_port;
    if (ShowMappingDialog(hwnd_, &r, ids, r.device_id, true) != IDOK || !r.accepted) return;

    auto& m = mapping_rows_[actual];
    const bool endpoint_changed =
        m.local_port != r.local_port || m.device_id != r.device_id ||
        m.target_host != r.target_host || m.target_port != r.target_port;
    m.device_id   = r.device_id;
    m.local_port  = r.local_port;
    m.target_host = r.target_host;
    m.target_port = r.target_port;
    save_mappings_config();

    // Restart the listener when the endpoint changed while running.
    if (m.running && endpoint_changed) {
        stop_listener_for_mapping(m.config_id);
        start_listener_for_mapping(actual);
    }
    refresh_mappings_list();
    update_status_bar();
}

void MainWindow::on_start_mapping() {
    int actual = displayed_to_actual_mapping(selected_mapping_idx_);
    if (actual < 0) return;
    if (mapping_rows_[actual].running) return;
    mapping_rows_[actual].running = true;
    start_listener_for_mapping(actual);
    save_mappings_config();
    refresh_mappings_list();
    update_status_bar();
    update_toolbar_status();
}

void MainWindow::on_stop_mapping() {
    int actual = displayed_to_actual_mapping(selected_mapping_idx_);
    if (actual < 0) return;
    if (!mapping_rows_[actual].running) return;
    mapping_rows_[actual].running = false;
    stop_listener_for_mapping(mapping_rows_[actual].config_id);
    save_mappings_config();
    refresh_mappings_list();
    update_status_bar();
    update_toolbar_status();
}

void MainWindow::on_delete_mapping() {
    int actual = displayed_to_actual_mapping(selected_mapping_idx_);
    if (actual < 0) return;
    auto& m = mapping_rows_[actual];
    if (MessageBox(hwnd_,
            (L"Delete mapping \"" + widen(m.name) + L"\"?").c_str(),
            L"Confirm", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    stop_listener_for_mapping(m.config_id);
    const std::string name = m.name;
    mapping_rows_.erase(mapping_rows_.begin() + actual);
    selected_mapping_idx_ = -1;
    save_mappings_config();
    refresh_mappings_list();
    update_status_bar();
    update_toolbar_status();
    set_last_event(L"Mapping deleted: " + widen(name));
}

void MainWindow::on_settings() {
    config::RemoteToolConfig copy = rt_config_;
    if (ShowSettingsDialog(hwnd_, &copy) != IDOK) return;
    const bool listen_changed =
        copy.bind_host != rt_config_.bind_host ||
        copy.agent_port != rt_config_.agent_port;
    rt_config_ = copy;
    config::save_remote_tool_config("remote_tool.json", rt_config_);
    sessions_->set_max_sessions(rt_config_.max_sessions_per_mapping);
    if (listen_changed) {
        MessageBox(hwnd_,
            L"Listen address/port changes take effect after restarting RemoteTool.",
            L"Settings", MB_OK | MB_ICONINFORMATION);
    }
    update_status_bar();
}

}  // namespace rmt::gui
#endif
