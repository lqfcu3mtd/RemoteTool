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

namespace rmt::gui {

MainWindow* MainWindow::instance_ = nullptr;

MainWindow::MainWindow()
    : io_(std::make_unique<asio::io_context>()),
      devices_(std::make_unique<tunnel::DeviceManager>(*io_)),
      acceptor_(std::make_unique<tunnel::Acceptor>(*io_, *devices_)) {
}

MainWindow::~MainWindow() {
    if (io_thread_.joinable()) {
        io_->stop();
        io_thread_.join();
    }
}

// ---- public ----

int MainWindow::run() {
    instance_ = this;

    // Register window class.
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"RemoteToolMain";
    RegisterClassEx(&wc);

    // Create window.
    hwnd_ = CreateWindowEx(0, L"RemoteToolMain", L"RemoteTool",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
                           nullptr, nullptr, GetModuleHandle(nullptr), this);
    if (!hwnd_) return 1;

    // Message loop.
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ---- window procedure ----

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = instance_;
    if (self && self->hwnd_ == hwnd) {
        return self->handle_message(msg, wp, lp);
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:  on_create();  break;
        case WM_TIMER:   on_timer();   break;
        case WM_CLOSE:   on_close();   DestroyWindow(hwnd_); break;
        case WM_DESTROY: PostQuitMessage(0); break;
        default: return DefWindowProc(hwnd_, msg, wp, lp);
    }
    return 0;
}

// ---- event handlers ----

void MainWindow::on_create() {
    // Create device list.
    list_devices_ = CreateWindowEx(0, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
        10, 10, 600, 400, hwnd_, nullptr, GetModuleHandle(nullptr), nullptr);

    // Status bar.
    status_bar_ = CreateWindowEx(0, L"STATIC", L"Ready",
        WS_CHILD | WS_VISIBLE, 10, 420, 600, 20, hwnd_, nullptr,
        GetModuleHandle(nullptr), nullptr);

    // Start timer (100 ms) for EventQueue polling.
    SetTimer(hwnd_, kTimerId, 100, nullptr);

    // ---- Network setup ----
    // Callbacks from DeviceManager go through EventQueue to UI.
    devices_->set_on_device_online([this](const tunnel::DeviceEntry& e) {
        events_.push([this, id = e.device_id, addr = e.remote_address] {
            add_device_to_list(id, addr);
        });
    });
    devices_->set_on_device_offline([this](const tunnel::DeviceEntry& e) {
        events_.push([this, id = e.device_id] {
            remove_device_from_list(id);
        });
    });

    // Start listening.
    acceptor_->start("0.0.0.0", 4433);
    devices_->start_cleanup_timer();

    // Run io_context on background thread.
    io_thread_ = std::thread([this] { io_->run(); });

    SetWindowText(status_bar_, L"Listening on :4433");
}

void MainWindow::on_timer() {
    process_events();
}

void MainWindow::on_close() {
    KillTimer(hwnd_, kTimerId);
    acceptor_->stop();
    io_->stop();
}

// ---- EventQueue processing ----

void MainWindow::process_events() {
    // Temporarily intercept EventQueue processing to execute UI updates.
    // We patch EventQueue::push to go directly for WM_TIMER-driven processing.
    // Simple approach: override the action directly inside our handler.
    //
    // But EventQueue::process executes actions that we pushed. Those actions
    // need access to 'this'. We use lambdas capturing [this] when pushing.
    events_.process();
}

void MainWindow::add_device_to_list(const std::string& device_id,
                                     const std::string& address) {
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
#endif  // _WIN32
