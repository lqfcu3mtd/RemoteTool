#pragma once
// Win32 main window for RemoteTool (Phase 4 MVP).
// Displays a device list (ListBox) updated from network events via
// a thread-safe EventQueue. Runs the Win32 message loop on the main
// thread while the network io_context runs on a background thread.
#ifdef _WIN32

// Asio must be included before windows.h to avoid WinSock redefinition errors.
// Define UNICODE for wide-char Win32 API support.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <asio.hpp>

#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "rmt/config/config_loader.h"
#include "rmt/tunnel/acceptor.h"
#include "rmt/tunnel/device_manager.h"
#include "rmt/tunnel/session_manager.h"
#include "rmt/session/session_id.h"
#include "event_queue.h"

namespace rmt::gui {

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    // Run the Win32 message loop (blocks until window closes).
    int run();

private:
    // Window procedure.
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

    void on_create();
    void on_timer();
    void on_close();
    void on_add_device();
    void on_delete_device();
    void on_add_mapping();
    void refresh_mappings();

    // Process one EventQueue batch.
    void process_events();
    void add_device_to_list(const std::string& device_id, const std::string& address);
    void remove_device_from_list(const std::string& device_id);

    HWND hwnd_ = nullptr;
    HWND list_devices_ = nullptr;
    HWND list_mappings_ = nullptr;
    HWND edit_device_id_ = nullptr;
    HWND edit_display_name_ = nullptr;
    HWND btn_add_device_ = nullptr;
    HWND edit_map_name_ = nullptr;
    HWND edit_map_device_ = nullptr;
    HWND edit_map_target_ = nullptr;
    HWND edit_map_port_ = nullptr;
    HWND edit_map_local_port_ = nullptr;
    HWND btn_add_mapping_ = nullptr;
    HWND status_bar_ = nullptr;

    std::unique_ptr<asio::io_context> io_;
    std::unique_ptr<tunnel::DeviceManager> devices_;
    std::unique_ptr<tunnel::Acceptor> acceptor_;
    std::unique_ptr<tunnel::SessionManager> sessions_;
    std::unique_ptr<session::SessionIdAllocator> id_alloc_;
    std::thread io_thread_;
    rmt::gui::EventQueue events_;  // event sequence number

    // Device list helper: map device_id -> index in ListBox.
    std::unordered_map<std::string, int> device_index_;

    static constexpr int kTimerId = 1;
    static MainWindow* instance_;  // single-instance for WndProc
};

}  // namespace rmt::gui
#endif  // _WIN32
