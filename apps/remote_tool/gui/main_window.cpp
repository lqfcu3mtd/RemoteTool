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
#include <algorithm>

namespace rmt::gui {
namespace {
    constexpr int IDC_ADD_DEVICE   = 101;
    constexpr int IDC_DEL_DEVICE   = 102;
    constexpr int IDC_DEVICE_ID    = 103;
    constexpr int IDC_DISP_NAME    = 104;
    constexpr int IDC_ADD_MAPPING  = 105;
    constexpr int IDC_MAP_NAME     = 106;
    constexpr int IDC_MAP_DEVICE   = 107;
    constexpr int IDC_MAP_TARGET   = 108;
    constexpr int IDC_MAP_LPORT    = 109;
    constexpr int IDC_DEL_MAPPING  = 110;
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
                           CW_USEDEFAULT, CW_USEDEFAULT, 640, 400,
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
        self->hwnd_ = hwnd;  // set before WM_CREATE so on_create can use it
    }
    auto* self = reinterpret_cast<MainWindow*>(
        GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (self) return self->handle_message(msg, wp, lp);
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: on_create(); break;
        case WM_TIMER:  on_timer();  break;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_ADD_DEVICE) on_add_device();
            if (LOWORD(wp) == IDC_DEL_DEVICE) on_delete_device();
            if (LOWORD(wp) == IDC_ADD_MAPPING) on_add_mapping();
            if (LOWORD(wp) == IDC_DEL_MAPPING) {
                int s = SendMessage(list_mappings_, LB_GETCURSEL, 0, 0);
                if (s != LB_ERR) SendMessage(list_mappings_, LB_DELETESTRING, s, 0);
            }
            break;
        case WM_CLOSE:  on_close(); DestroyWindow(hwnd_); break;
        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd_, msg, wp, lp);
    }
    return 0;
}

void MainWindow::on_create() {
    // ---- Layout ----
    // Devices section
    CreateWindowEx(0, L"STATIC", L"Devices",
        WS_CHILD | WS_VISIBLE, 10, 5, 300, 16, hwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
    list_devices_ = CreateWindowEx(0, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        10, 24, 300, 100, hwnd_, nullptr, GetModuleHandle(nullptr), nullptr);

    // Add device controls
    CreateWindowEx(0, L"STATIC", L"ID:", WS_CHILD | WS_VISIBLE,
        10, 130, 20, 20, hwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
    edit_device_id_ = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE, 35, 128, 120, 22, hwnd_, (HMENU)IDC_DEVICE_ID,
        GetModuleHandle(nullptr), nullptr);
    CreateWindowEx(0, L"STATIC", L"Name:", WS_CHILD | WS_VISIBLE,
        165, 130, 35, 20, hwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
    edit_display_name_ = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE, 205, 128, 80, 22, hwnd_, (HMENU)IDC_DISP_NAME,
        GetModuleHandle(nullptr), nullptr);
    btn_add_device_ = CreateWindowEx(0, L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE,
        295, 126, 45, 26, hwnd_, (HMENU)IDC_ADD_DEVICE, GetModuleHandle(nullptr), nullptr);
    CreateWindowEx(0, L"BUTTON", L"Del", WS_CHILD | WS_VISIBLE,
        345, 126, 40, 26, hwnd_, (HMENU)IDC_DEL_DEVICE, GetModuleHandle(nullptr), nullptr);

    // Mappings section
    CreateWindowEx(0, L"STATIC", L"Mappings",
        WS_CHILD | WS_VISIBLE, 10, 160, 300, 16, hwnd_, nullptr, GetModuleHandle(nullptr), nullptr);
    list_mappings_ = CreateWindowEx(0, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL,
        10, 178, 300, 40, hwnd_, nullptr, GetModuleHandle(nullptr), nullptr);

    // Mapping add controls (compact row).
    edit_map_name_ = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE, 10, 222, 70, 22, hwnd_, (HMENU)IDC_MAP_NAME,
        GetModuleHandle(nullptr), nullptr);
    edit_map_device_ = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE, 85, 222, 60, 22, hwnd_, (HMENU)IDC_MAP_DEVICE,
        GetModuleHandle(nullptr), nullptr);
    edit_map_target_ = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE, 150, 222, 70, 22, hwnd_, (HMENU)IDC_MAP_TARGET,
        GetModuleHandle(nullptr), nullptr);
    edit_map_port_ = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE, 225, 222, 40, 22, hwnd_, (HMENU)IDC_MAP_LPORT,
        GetModuleHandle(nullptr), nullptr);
    edit_map_local_port_ = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE, 270, 222, 35, 22, hwnd_, nullptr,
        GetModuleHandle(nullptr), nullptr);
    btn_add_mapping_ = CreateWindowEx(0, L"BUTTON", L"+Map", WS_CHILD | WS_VISIBLE,
        310, 220, 50, 26, hwnd_, (HMENU)IDC_ADD_MAPPING, GetModuleHandle(nullptr), nullptr);
    CreateWindowEx(0, L"BUTTON", L"Del", WS_CHILD | WS_VISIBLE,
        365, 220, 40, 26, hwnd_, (HMENU)IDC_DEL_MAPPING, GetModuleHandle(nullptr), nullptr);

    SetWindowText(edit_map_name_, L"name");
    SetWindowText(edit_map_device_, L"device");
    SetWindowText(edit_map_target_, L"host:port");
    SetWindowText(edit_map_port_, L"lport");
    SetWindowText(edit_map_local_port_, L"tport");

    // Status bar
    status_bar_ = CreateWindowEx(0, L"STATIC", L"Ready",
        WS_CHILD | WS_VISIBLE, 10, 268, 600, 20, hwnd_, nullptr,
        GetModuleHandle(nullptr), nullptr);

    SetTimer(hwnd_, kTimerId, 100, nullptr);

    // ---- Load saved mappings ----
    refresh_mappings();

    // ---- Network setup ----
    devices_->set_on_device_online([this](const tunnel::DeviceEntry& e) {
        events_.push([this, id = e.device_id, addr = e.remote_address] {
            add_device_to_list(id, addr);
        });
    });
    devices_->set_on_device_offline([this](const tunnel::DeviceEntry& e) {
        events_.push([this, id = e.device_id] { remove_device_from_list(id); });
    });

    acceptor_->start("0.0.0.0", 4433);
    devices_->start_cleanup_timer();
    io_thread_ = std::thread([this] { io_->run(); });
    SetWindowText(status_bar_, L"Listening on :4433");
}

void MainWindow::on_timer() { process_events(); }

void MainWindow::on_close() {
    KillTimer(hwnd_, kTimerId);
    acceptor_->stop();
    io_->stop();
}

void MainWindow::on_add_device() {
    wchar_t id_buf[128] = {}, name_buf[128] = {};
    GetWindowText(edit_device_id_, id_buf, 127);
    GetWindowText(edit_display_name_, name_buf, 127);
    std::string device_id(id_buf, id_buf + wcslen(id_buf));
    std::string name(name_buf, name_buf + wcslen(name_buf));
    if (device_id.empty()) return;

    // Add to listbox and clear inputs.
    auto w = std::wstring(device_id.begin(), device_id.end()) + L"  " +
             std::wstring(name.begin(), name.end()) + L" (pending)";
    int idx = SendMessage(list_devices_, LB_ADDSTRING, 0, (LPARAM)w.c_str());
    device_index_[device_id] = idx;
    SetWindowText(edit_device_id_, L"");
    SetWindowText(edit_display_name_, L"");

    // Save to devices config.
    config::DevicesConfig cfg;
    auto lr = config::load_devices_config("devices.json");
    if (auto* c = config::try_get_loaded(lr)) cfg = *c;
    config::DeviceRecord rec;
    rec.id = device_id;
    rec.display_name = name;
    rec.enabled = true;
    cfg.devices.push_back(std::move(rec));
    config::save_devices_config("devices.json", cfg);

    SetWindowText(status_bar_, (L"Added device: " + std::wstring(device_id.begin(), device_id.end())).c_str());
}

void MainWindow::refresh_mappings() {
    SendMessage(list_mappings_, LB_RESETCONTENT, 0, 0);
    auto lr = config::load_mappings_config("mappings.json");
    if (auto* cfg = config::try_get_loaded(lr)) {
        for (auto& m : cfg->mappings) {
            auto w = std::wstring(m.name.begin(), m.name.end()) + L"  " +
                     std::wstring(m.device_id.begin(), m.device_id.end()) +
                     L"  -> " + std::to_wstring(m.target_port);
            SendMessage(list_mappings_, LB_ADDSTRING, 0, (LPARAM)w.c_str());
        }
    }
}

void MainWindow::on_delete_device() {
    int sel = SendMessage(list_devices_, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;

    // Get device_id from the list text.
    wchar_t buf[256] = {};
    SendMessage(list_devices_, LB_GETTEXT, sel, (LPARAM)buf);
    std::string text(buf, buf + wcslen(buf));
    auto sp = text.find(' ');
    std::string dev_id = (sp != std::string::npos) ? text.substr(0, sp) : text;

    SendMessage(list_devices_, LB_DELETESTRING, sel, 0);
    for (auto it = device_index_.begin(); it != device_index_.end(); ) {
        if (it->second == sel) it = device_index_.erase(it);
        else if (it->second > sel) { it->second--; ++it; }
        else ++it;
    }

    // Remove from devices.json.
    auto lr = config::load_devices_config("devices.json");
    if (auto* cfg = config::try_get_loaded(lr)) {
        config::DevicesConfig copy = *cfg;
        auto& devs = copy.devices;
        devs.erase(std::remove_if(devs.begin(), devs.end(),
            [&](const config::DeviceRecord& d) { return d.id == dev_id; }), devs.end());
        config::save_devices_config("devices.json", copy);
    }
}

void MainWindow::on_add_mapping() {
    wchar_t buf[128] = {};
    std::string name, dev, host;
    int lport = 0, tport = 0;
    GetWindowText(edit_map_name_, buf, 127);
    name.assign(buf, buf + wcslen(buf));
    GetWindowText(edit_map_device_, buf, 127);
    dev.assign(buf, buf + wcslen(buf));
    GetWindowText(edit_map_target_, buf, 127);
    std::string target(buf, buf + wcslen(buf));
    auto sep = target.find(':');
    if (sep != std::string::npos) {
        host = target.substr(0, sep);
        tport = std::stoi(target.substr(sep + 1));
    }
    GetWindowText(edit_map_port_, buf, 127);
    lport = std::stoi(std::wstring(buf));
    if (name.empty() || dev.empty() || host.empty() || lport == 0 || tport == 0) return;

    // Add to list.
    auto w = std::wstring(name.begin(), name.end()) + L"  " +
             std::wstring(dev.begin(), dev.end()) + L"  -> " + std::to_wstring(tport);
    SendMessage(list_mappings_, LB_ADDSTRING, 0, (LPARAM)w.c_str());

    // Persist.
    config::MappingsConfig cfg;
    auto lr = config::load_mappings_config("mappings.json");
    if (auto* c = config::try_get_loaded(lr)) cfg = *c;
    config::MappingRecord m;
    m.id = "map-" + name;
    m.device_id = dev;
    m.name = name;
    m.local_port = lport;
    m.target_host = host;
    m.target_port = tport;
    m.enabled = true;
    cfg.mappings.push_back(std::move(m));
    config::save_mappings_config("mappings.json", cfg);

    SetWindowText(status_bar_, L"Mapping added");
}

void MainWindow::process_events() { events_.process(); }

void MainWindow::add_device_to_list(const std::string& device_id, const std::string& address) {
    auto w = std::wstring(device_id.begin(), device_id.end()) +
             L"  [" + std::wstring(address.begin(), address.end()) + L"]";
    int idx = SendMessage(list_devices_, LB_ADDSTRING, 0, (LPARAM)w.c_str());
    device_index_[device_id] = idx;
}

void MainWindow::remove_device_from_list(const std::string& device_id) {
    auto it = device_index_.find(device_id);
    if (it != device_index_.end()) {
        SendMessage(list_devices_, LB_DELETESTRING, it->second, 0);
        device_index_.erase(it);
    }
}

}  // namespace rmt::gui
#endif
