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
#include <cstdio>
#include <random>
#include <sstream>

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

    // ============ Layout dimensions (pixels, dialog units not used for clarity) ============
    constexpr int kWindowW = 900;
    constexpr int kWindowH = 600;
    constexpr int kToolbarH = 36;
    constexpr int kStatusBarH = 50;
    constexpr int kLeftW = 290;
    constexpr int kRightW = 590;
    constexpr int kPanelPad = 8;

    // 8-character random ID for pair codes (Phase 4 stub; Phase 5 wires the
    // real PSK/pairing flow).
    std::string random_pair_code() {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        std::uniform_int_distribution<int> d(0, 9);
        std::string s;
        s.reserve(9);
        for (int i = 0; i < 8; ++i) {
            if (i == 4) s.push_back('-');
            s.push_back(static_cast<char>('0' + d(rng)));
        }
        return s;
    }

    std::wstring widen(const std::string& s) {
        return std::wstring(s.begin(), s.end());
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
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = const_cast<LPWSTR>(col0.c_str());
        SendMessage(lv, LVM_INSERTITEM, 0, reinterpret_cast<LPARAM>(&item));
    }
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
}

int MainWindow::run() {
    instance_ = this;
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"RemoteToolMain";
    RegisterClassEx(&wc);

    hwnd_ = CreateWindowEx(0, L"RemoteToolMain", L"RemoteTool",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, kWindowW, kWindowH,
                           nullptr, nullptr, GetModuleHandle(nullptr), this);
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
            }
            break;
        }
        case WM_NOTIFY:     on_notify(reinterpret_cast<NMHDR*>(lp)); break;
        case WM_CLOSE:      on_close(); DestroyWindow(hwnd_); break;
        case WM_DESTROY:    PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd_, msg, wp, lp);
    }
    return 0;
}

void MainWindow::on_create() {
    auto hi = GetModuleHandle(nullptr);
    RECT rc; GetClientRect(hwnd_, &rc);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;

    int toolbar_y = 0;
    int panels_y = toolbar_y + kToolbarH;
    int status_y = H - kStatusBarH;
    int panels_h = status_y - panels_y;

    // ---- Top toolbar ----
    CreateWindowEx(0, L"BUTTON", L"", BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        0, toolbar_y, W, kToolbarH, hwnd_, nullptr, hi, nullptr);
    toolbar_status_ = CreateWindowEx(0, L"STATIC", L"Listening on 0.0.0.0:4433",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 12, toolbar_y + 10, 280, 20,
        hwnd_, (HMENU)IDC_TOOLBAR_STATUS, hi, nullptr);
    toolbar_settings_ = CreateWindowEx(0, L"BUTTON", L"Settings",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        200, toolbar_y + 7, 80, 22, hwnd_, (HMENU)IDC_TOOLBAR_SETTINGS, hi, nullptr);
    toolbar_summary_ = CreateWindowEx(0, L"STATIC", L"0 online",
        WS_CHILD | WS_VISIBLE | SS_RIGHT, W - 220, toolbar_y + 10, 200, 20,
        hwnd_, (HMENU)IDC_TOOLBAR_SUMMARY, hi, nullptr);

    // ---- Left panel: Devices ----
    int lx = kPanelPad;
    int lw = kLeftW;
    int ly = panels_y + 4;
    int lh = panels_h - 8;

    panel_devices_ = CreateWindowEx(0, L"BUTTON", L"Devices",
        BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        lx, ly, lw, lh, hwnd_, nullptr, hi, nullptr);

    // Devices ListView
    list_devices_ = CreateWindowEx(0, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        lx + 10, ly + 24, lw - 20, 150, hwnd_, (HMENU)IDC_LIST_DEVICES, hi, nullptr);
    ListView_SetExtendedListViewStyle(list_devices_,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    {
        LVCOLUMN c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.cx = 100; c.pszText = const_cast<LPWSTR>(L"Name");     c.iSubItem = 0;
        ListView_InsertColumn(list_devices_, 0, &c);
        c.cx = 70;  c.pszText = const_cast<LPWSTR>(L"Status");   c.iSubItem = 1;
        ListView_InsertColumn(list_devices_, 1, &c);
        c.cx = 40;  c.pszText = const_cast<LPWSTR>(L"Ver");     c.iSubItem = 2;
        ListView_InsertColumn(list_devices_, 2, &c);
        c.cx = 40;  c.pszText = const_cast<LPWSTR>(L"Sess");    c.iSubItem = 3;
        ListView_InsertColumn(list_devices_, 3, &c);
        c.cx = 60;  c.pszText = const_cast<LPWSTR>(L"Last");    c.iSubItem = 4;
        ListView_InsertColumn(list_devices_, 4, &c);
    }

    // Pair code panel
    int pair_y = ly + 180;
    int pair_h = 130;
    panel_pair_code_ = CreateWindowEx(0, L"BUTTON", L"Pair code",
        BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        lx + 10, pair_y, lw - 20, pair_h, hwnd_, nullptr, hi, nullptr);
    static_pair_label_ = CreateWindowEx(0, L"STATIC", L"No device selected",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        lx + 20, pair_y + 18, lw - 40, 16, hwnd_, (HMENU)IDC_PAIR_LABEL, hi, nullptr);
    static_pair_code_ = CreateWindowEx(0, L"STATIC", L"— — — —   — — — —",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        lx + 20, pair_y + 38, lw - 40, 32, hwnd_, (HMENU)IDC_PAIR_CODE, hi, nullptr);
    // Larger font for the code
    {
        HFONT hf = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FF_MODERN, L"Consolas");
        SendMessage(static_pair_code_, WM_SETFONT, (WPARAM)hf, TRUE);
    }
    static_pair_expires_ = CreateWindowEx(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        lx + 20, pair_y + 72, lw - 40, 16, hwnd_, (HMENU)IDC_PAIR_EXPIRES, hi, nullptr);
    btn_pair_copy_ = CreateWindowEx(0, L"BUTTON", L"Copy",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        lx + 20, pair_y + 94, 60, 24, hwnd_, (HMENU)IDC_PAIR_COPY, hi, nullptr);
    btn_pair_regen_ = CreateWindowEx(0, L"BUTTON", L"Regenerate",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        lx + 84, pair_y + 94, 84, 24, hwnd_, (HMENU)IDC_PAIR_REGEN, hi, nullptr);
    btn_pair_revoke_ = CreateWindowEx(0, L"BUTTON", L"Revoke",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        lx + 172, pair_y + 94, 70, 24, hwnd_, (HMENU)IDC_PAIR_REVOKE, hi, nullptr);
    EnableWindow(btn_pair_copy_,   FALSE);
    EnableWindow(btn_pair_regen_,  FALSE);
    EnableWindow(btn_pair_revoke_, FALSE);

    // Device action bar
    int dev_btn_y = ly + lh - 36;
    btn_dev_add_ = CreateWindowEx(0, L"BUTTON", L"+ Add",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        lx + 10, dev_btn_y, 64, 26, hwnd_, (HMENU)IDC_DEV_ADD, hi, nullptr);
    btn_dev_edit_ = CreateWindowEx(0, L"BUTTON", L"Edit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        lx + 78, dev_btn_y, 64, 26, hwnd_, (HMENU)IDC_DEV_EDIT, hi, nullptr);
    btn_dev_delete_ = CreateWindowEx(0, L"BUTTON", L"Delete",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        lx + 146, dev_btn_y, 64, 26, hwnd_, (HMENU)IDC_DEV_DELETE, hi, nullptr);

    // ---- Right panel: Mappings ----
    int rx = lx + lw + 8;
    int rw = W - rx - kPanelPad;

    panel_mappings_ = CreateWindowEx(0, L"BUTTON", L"Port mappings",
        BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        rx, ly, rw, lh, hwnd_, nullptr, hi, nullptr);

    // Filter hint
    btn_map_filter_clear_ = CreateWindowEx(0, L"BUTTON", L"Show all",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        rx + rw - 90, ly + 2, 80, 20, hwnd_, (HMENU)IDC_MAP_FILTER_CLEAR, hi, nullptr);
    static_map_filter_ = CreateWindowEx(0, L"STATIC",
        L"Filter: all devices", WS_CHILD | WS_VISIBLE | SS_LEFT,
        rx + 10, ly + 4, rw - 110, 18, hwnd_, (HMENU)IDC_MAP_FILTER_LABEL, hi, nullptr);

    // Mappings ListView
    list_mappings_ = CreateWindowEx(0, WC_LISTVIEW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        rx + 10, ly + 24, rw - 20, 150, hwnd_, (HMENU)IDC_LIST_MAPPINGS, hi, nullptr);
    ListView_SetExtendedListViewStyle(list_mappings_,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    {
        LVCOLUMN c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.cx = 80;  c.pszText = const_cast<LPWSTR>(L"Name");   c.iSubItem = 0;
        ListView_InsertColumn(list_mappings_, 0, &c);
        c.cx = 110; c.pszText = const_cast<LPWSTR>(L"Local");  c.iSubItem = 1;
        ListView_InsertColumn(list_mappings_, 1, &c);
        c.cx = 110; c.pszText = const_cast<LPWSTR>(L"Target"); c.iSubItem = 2;
        ListView_InsertColumn(list_mappings_, 2, &c);
        c.cx = 70;  c.pszText = const_cast<LPWSTR>(L"State");  c.iSubItem = 3;
        ListView_InsertColumn(list_mappings_, 3, &c);
        c.cx = 50;  c.pszText = const_cast<LPWSTR>(L"Conn");   c.iSubItem = 4;
        ListView_InsertColumn(list_mappings_, 4, &c);
        c.cx = 100; c.pszText = const_cast<LPWSTR>(L"Device"); c.iSubItem = 5;
        ListView_InsertColumn(list_mappings_, 5, &c);
    }

    // Active sessions panel
    int sess_y = ly + 180;
    int sess_h = 130;
    panel_sessions_ = CreateWindowEx(0, L"BUTTON", L"Active sessions",
        BS_GROUPBOX | WS_CHILD | WS_VISIBLE,
        rx + 10, sess_y, rw - 20, sess_h, hwnd_, nullptr, hi, nullptr);
    static_sessions_label_ = CreateWindowEx(0, L"STATIC", L"No mapping selected",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        rx + 20, sess_y + 18, rw - 40, 16, hwnd_, (HMENU)IDC_SESS_LABEL, hi, nullptr);
    static_sessions_body_ = CreateWindowEx(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_READONLY | WS_BORDER,
        rx + 20, sess_y + 38, rw - 40, 86, hwnd_, (HMENU)IDC_SESS_BODY, hi, nullptr);

    // Mapping action bar
    int map_btn_y = ly + lh - 36;
    btn_map_add_ = CreateWindowEx(0, L"BUTTON", L"+ Add",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        rx + 10, map_btn_y, 64, 26, hwnd_, (HMENU)IDC_MAP_ADD, hi, nullptr);
    btn_map_edit_ = CreateWindowEx(0, L"BUTTON", L"Edit",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        rx + 78, map_btn_y, 64, 26, hwnd_, (HMENU)IDC_MAP_EDIT, hi, nullptr);
    btn_map_start_ = CreateWindowEx(0, L"BUTTON", L"Start",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        rx + 146, map_btn_y, 64, 26, hwnd_, (HMENU)IDC_MAP_START, hi, nullptr);
    btn_map_stop_ = CreateWindowEx(0, L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        rx + 214, map_btn_y, 64, 26, hwnd_, (HMENU)IDC_MAP_STOP, hi, nullptr);
    btn_map_delete_ = CreateWindowEx(0, L"BUTTON", L"Delete",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        rx + 282, map_btn_y, 64, 26, hwnd_, (HMENU)IDC_MAP_DELETE, hi, nullptr);

    // ---- Bottom status bar ----
    status_bar_ = CreateWindowEx(0, STATUSCLASSNAME, L"",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, status_y, W, kStatusBarH, hwnd_, (HMENU)IDC_STATUS_BAR, hi, nullptr);
    {
        int parts[] = {220, W - 220, -1};
        SendMessage(status_bar_, SB_SETPARTS, 3, (LPARAM)parts);
    }

    SetTimer(hwnd_, kTimerId, 100, nullptr);

    // ---- Load saved data ----
    load_devices_from_config();
    load_mappings_from_config();
    refresh_devices_list();
    refresh_mappings_list();
    update_status_bar();
    update_toolbar_status();
    update_filter_label();

    // ---- Network setup ----
    devices_->set_on_device_online([this](const tunnel::DeviceEntry& e) {
        events_.push([this, id = e.device_id, addr = e.remote_address,
                      ver = e.agent_version] {
            add_device_to_list(id, addr, ver);
        });
    });
    devices_->set_on_device_offline([this](const tunnel::DeviceEntry& e) {
        events_.push([this, id = e.device_id] {
            update_device_online_state(id, false);
        });
    });

    acceptor_->start("0.0.0.0", 4433);
    devices_->start_cleanup_timer();
    io_thread_ = std::thread([this] { io_->run(); });
}

void MainWindow::on_size() {
    if (!status_bar_) return;
    SendMessage(status_bar_, WM_SIZE, 0, 0);
    RECT rc; GetClientRect(hwnd_, &rc);
    int W = rc.right - rc.left;
    int parts[] = {220, W - 220, -1};
    SendMessage(status_bar_, SB_SETPARTS, 3, (LPARAM)parts);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::on_timer() {
    process_events();
}

void MainWindow::on_close() {
    KillTimer(hwnd_, kTimerId);
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

// ============ Refresh helpers ============

void MainWindow::refresh_devices_list() {
    ListView_DeleteAllItems(list_devices_);
    for (size_t i = 0; i < device_rows_.size(); ++i) {
        const auto& d = device_rows_[i];
        std::wstring name = widen(d.device_id);
        if (!d.display_name.empty()) {
            // We render name and sub-name in two columns: name + agent version
            // placeholder. Display name is shown when offline (no version).
            name = widen(d.device_id);
        }
        lv_insert_row(list_devices_, static_cast<int>(i), name);
        lv_set_text(list_devices_, static_cast<int>(i), 1,
                    d.online ? L"Online" : L"Offline");
        lv_set_text(list_devices_, static_cast<int>(i), 2,
                    d.agent_version.empty() ? L"—" : widen(d.agent_version));
        lv_set_text(list_devices_, static_cast<int>(i), 3,
                    std::to_wstring(d.session_count));
        lv_set_text(list_devices_, static_cast<int>(i), 4,
                    d.online ? ago_string(d.last_seen) : L"—");
    }
    if (selected_device_idx_ >= static_cast<int>(device_rows_.size())) {
        selected_device_idx_ = -1;
    }
    if (selected_device_idx_ < 0 && !device_rows_.empty()) {
        // Keep selection cleared; user picks explicitly.
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
        // Format "5 8 3 9 2 1 4 7" for readability.
        std::wstring formatted;
        for (size_t i = 0; i < d.pair_code.size(); ++i) {
            if (i == 4) formatted += L"   ";
            else if (i > 0) formatted += L' ';
            formatted += d.pair_code[i];
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
    if (selected_mapping_idx_ < 0) {
        SetWindowText(static_sessions_label_, L"No mapping selected");
        SetWindowText(static_sessions_body_,  L"");
        return;
    }
    // Map back from displayed index to the underlying mapping row.
    int displayed = 0;
    int actual = -1;
    for (size_t i = 0; i < mapping_rows_.size(); ++i) {
        if (!filter_device_id_.empty() && mapping_rows_[i].device_id != filter_device_id_) continue;
        if (displayed == selected_mapping_idx_) { actual = static_cast<int>(i); break; }
        ++displayed;
    }
    if (actual < 0) {
        SetWindowText(static_sessions_label_, L"No mapping selected");
        SetWindowText(static_sessions_body_,  L"");
        return;
    }
    const auto& m = mapping_rows_[actual];
    std::wstring lbl = L"Active sessions · " + widen(m.name) + L" ("
                     + std::to_wstring(m.active_conns) + L")";
    SetWindowText(static_sessions_label_, lbl.c_str());
    if (m.active_conns == 0) {
        SetWindowText(static_sessions_body_,
            m.running ? L"Listening. No active connections yet."
                      : L"Mapping is stopped. Start it to accept connections.");
    } else {
        // Per-session detail is wired in Phase 3c. For now show a placeholder.
        SetWindowText(static_sessions_body_,
            L"Per-session detail is wired in Phase 3c (not in this build).");
    }
}

void MainWindow::update_status_bar() {
    int online = 0;
    for (const auto& d : device_rows_) if (d.online) ++online;
    int running = 0;
    for (const auto& m : mapping_rows_) if (m.running) ++running;
    int conns = static_cast<int>(sessions_->active_session_count());

    std::wstring left  = L"Ready";
    std::wstring mid   = L"Last: " + (device_rows_.empty() ? std::wstring(L"no devices")
                                       : widen(device_rows_.back().device_id) + L" selected");
    std::wstring right = std::to_wstring(online) + L" online · " +
                         std::to_wstring(running) + L" mapping(s) running · " +
                         std::to_wstring(conns) + L" active conn(s)";

    SendMessage(status_bar_, SB_SETTEXT, 0, (LPARAM)left.c_str());
    SendMessage(status_bar_, SB_SETTEXT, 1, (LPARAM)mid.c_str());
    SendMessage(status_bar_, SB_SETTEXT, 2, (LPARAM)right.c_str());
}

void MainWindow::update_toolbar_status() {
    int online = 0;
    for (const auto& d : device_rows_) if (d.online) ++online;
    std::wstring s = std::to_wstring(online) + L" online";
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
            row.local_port  = rec.local_port;
            row.target_host = rec.target_host;
            row.target_port = rec.target_port;
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
        rec.enabled = d.online;
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
        idx = static_cast<int>(device_rows_.size()) - 1;
        save_devices_config();
    } else {
        device_rows_[idx].online         = true;
        device_rows_[idx].agent_version  = agent_version;
        device_rows_[idx].remote_address = address;
        device_rows_[idx].last_seen      = std::chrono::steady_clock::now();
    }
    refresh_devices_list();
    update_status_bar();
    update_toolbar_status();
}

void MainWindow::update_device_online_state(const std::string& device_id, bool online) {
    int idx = find_device_row(device_id);
    if (idx < 0) return;
    device_rows_[idx].online = online;
    if (online) {
        device_rows_[idx].last_seen = std::chrono::steady_clock::now();
    }
    refresh_devices_list();
    update_status_bar();
    update_toolbar_status();
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
    // Also drop mappings owned by this device.
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
    for (size_t i = 0; i < d.pair_code.size(); ++i) dst[i] = d.pair_code[i];
    dst[d.pair_code.size()] = L'\0';
    GlobalUnlock(hg);
    SetClipboardData(CF_UNICODETEXT, hg);
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
    r.local_port = 0; r.target_port = 0;
    if (ShowMappingDialog(hwnd_, &r, ids, filter_device_id_, false) != IDOK || !r.accepted) return;

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
    refresh_mappings_list();
    update_status_bar();
}

void MainWindow::on_edit_mapping() {
    int actual = -1;
    int displayed = 0;
    for (size_t i = 0; i < mapping_rows_.size(); ++i) {
        if (!filter_device_id_.empty() && mapping_rows_[i].device_id != filter_device_id_) continue;
        if (displayed == selected_mapping_idx_) { actual = static_cast<int>(i); break; }
        ++displayed;
    }
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

    mapping_rows_[actual].device_id   = r.device_id;
    mapping_rows_[actual].local_port  = r.local_port;
    mapping_rows_[actual].target_host = r.target_host;
    mapping_rows_[actual].target_port = r.target_port;
    save_mappings_config();
    refresh_mappings_list();
}

void MainWindow::on_start_mapping() {
    int actual = -1, displayed = 0;
    for (size_t i = 0; i < mapping_rows_.size(); ++i) {
        if (!filter_device_id_.empty() && mapping_rows_[i].device_id != filter_device_id_) continue;
        if (displayed == selected_mapping_idx_) { actual = static_cast<int>(i); break; }
        ++displayed;
    }
    if (actual < 0) return;
    mapping_rows_[actual].running = true;
    save_mappings_config();
    refresh_mappings_list();
    update_status_bar();
}

void MainWindow::on_stop_mapping() {
    int actual = -1, displayed = 0;
    for (size_t i = 0; i < mapping_rows_.size(); ++i) {
        if (!filter_device_id_.empty() && mapping_rows_[i].device_id != filter_device_id_) continue;
        if (displayed == selected_mapping_idx_) { actual = static_cast<int>(i); break; }
        ++displayed;
    }
    if (actual < 0) return;
    mapping_rows_[actual].running = false;
    save_mappings_config();
    refresh_mappings_list();
    update_status_bar();
}

void MainWindow::on_delete_mapping() {
    int actual = -1, displayed = 0;
    for (size_t i = 0; i < mapping_rows_.size(); ++i) {
        if (!filter_device_id_.empty() && mapping_rows_[i].device_id != filter_device_id_) continue;
        if (displayed == selected_mapping_idx_) { actual = static_cast<int>(i); break; }
        ++displayed;
    }
    if (actual < 0) return;
    auto& m = mapping_rows_[actual];
    if (MessageBox(hwnd_,
            (L"Delete mapping \"" + widen(m.name) + L"\"?").c_str(),
            L"Confirm", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    mapping_rows_.erase(mapping_rows_.begin() + actual);
    selected_mapping_idx_ = -1;
    save_mappings_config();
    refresh_mappings_list();
    update_status_bar();
}

}  // namespace rmt::gui
#endif
