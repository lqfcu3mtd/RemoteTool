#pragma once
// Win32 main window for RemoteTool (Phase 4 MVP, redesigned layout).
//
// Layout: top status bar, left devices panel (ListView 5 cols + pair code
// panel), right mappings panel (ListView 6 cols + active sessions panel),
// bottom status bar. Add/Edit go through modal dialogs.
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
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

    // Dialog result context. Populated by DeviceDlgProc / MappingDlgProc on OK.
    struct DeviceDialogResult {
        std::string device_id;
        std::string display_name;
        bool accepted = false;
    };
    struct MappingDialogResult {
        std::string name;
        std::string device_id;
        std::uint16_t local_port = 0;
        std::string target_host;
        std::uint16_t target_port = 0;
        bool accepted = false;
    };

private:
    // Local model rows. Persisted config + live state merged here.
    struct DeviceRow {
        std::string device_id;
        std::string display_name;
        bool online = false;
        std::string agent_version;     // empty when offline
        std::string remote_address;    // empty when offline
        std::chrono::steady_clock::time_point last_seen{};
        int session_count = 0;         // 0 for Phase 4 (per-device tracking is Phase 3c+)
        std::string pair_code;         // 8-digit; empty when no active code
        std::chrono::steady_clock::time_point pair_code_expires{};
    };
    struct MappingRow {
        std::string name;
        std::string device_id;
        std::uint16_t local_port = 0;
        std::string target_host;
        std::uint16_t target_port = 0;
        bool running = false;
        int active_conns = 0;
        // Index into persisted config (for start/stop toggling).
        std::string config_id;  // config::MappingRecord::id
    };

    // Window procedure.
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

    void on_create();
    void on_timer();
    void on_close();
    void on_size();
    void on_notify(NMHDR* nm);

    // ListView selection changed.
    void on_device_selection_changed();
    void on_mapping_selection_changed();
    void on_clear_mapping_filter();

    // Button actions.
    void on_add_device();
    void on_edit_device();
    void on_delete_device();
    void on_add_mapping();
    void on_edit_mapping();
    void on_start_mapping();
    void on_stop_mapping();
    void on_delete_mapping();

    // Pair code actions.
    void on_copy_pair_code();
    void on_regenerate_pair_code();
    void on_revoke_pair_code();

    // Refresh helpers (re-read local rows and update ListViews).
    void refresh_devices_list();
    void refresh_mappings_list();
    void update_pair_code_panel();
    void update_active_sessions_panel();
    void update_status_bar();
    void update_toolbar_status();
    void update_filter_label();

    // Lookups.
    int find_device_row(const std::string& device_id) const;
    int find_mapping_row(const std::string& config_id) const;

    // Load persisted config into local rows.
    void load_devices_from_config();
    void load_mappings_from_config();
    void save_devices_config();
    void save_mappings_config();

    // Process one EventQueue batch.
    void process_events();
    void add_device_to_list(const std::string& device_id, const std::string& address,
                            const std::string& agent_version);
    void update_device_online_state(const std::string& device_id, bool online);

    // Main window.
    HWND hwnd_ = nullptr;

    // Top toolbar.
    HWND toolbar_status_ = nullptr;     // "Listening on 0.0.0.0:4433" label
    HWND toolbar_summary_ = nullptr;    // "N online · N sessions" label
    HWND toolbar_settings_ = nullptr;   // Settings button (stub for now)

    // Left panel (Devices).
    HWND panel_devices_ = nullptr;      // outer group box
    HWND list_devices_ = nullptr;       // SysListView32
    HWND panel_pair_code_ = nullptr;    // inner group box
    HWND static_pair_label_ = nullptr;
    HWND static_pair_code_ = nullptr;
    HWND static_pair_expires_ = nullptr;
    HWND btn_pair_copy_ = nullptr;
    HWND btn_pair_regen_ = nullptr;
    HWND btn_pair_revoke_ = nullptr;
    HWND btn_dev_add_ = nullptr;
    HWND btn_dev_edit_ = nullptr;
    HWND btn_dev_delete_ = nullptr;

    // Right panel (Mappings).
    HWND panel_mappings_ = nullptr;
    HWND list_mappings_ = nullptr;
    HWND panel_sessions_ = nullptr;
    HWND static_sessions_label_ = nullptr;
    HWND static_sessions_body_ = nullptr;
    HWND btn_map_filter_clear_ = nullptr;  // "Show all" — clears the device filter
    HWND static_map_filter_ = nullptr;     // "Filter: <device_id>  (2 of 3)" label
    HWND btn_map_add_ = nullptr;
    HWND btn_map_edit_ = nullptr;
    HWND btn_map_start_ = nullptr;
    HWND btn_map_stop_ = nullptr;
    HWND btn_map_delete_ = nullptr;

    // Bottom status bar.
    HWND status_bar_ = nullptr;

    // Backend.
    std::unique_ptr<asio::io_context> io_;
    std::unique_ptr<tunnel::DeviceManager> devices_;
    std::unique_ptr<tunnel::Acceptor> acceptor_;
    std::unique_ptr<session::SessionIdAllocator> id_alloc_;
    std::unique_ptr<tunnel::SessionManager> sessions_;
    std::thread io_thread_;
    rmt::gui::EventQueue events_;

    // Local data model.
    std::vector<DeviceRow> device_rows_;
    std::vector<MappingRow> mapping_rows_;

    // Selection state.
    int selected_device_idx_ = -1;
    int selected_mapping_idx_ = -1;
    std::string filter_device_id_;  // empty = show all mappings

    static constexpr int kTimerId = 1;
    static MainWindow* instance_;
};

// Dialog entry points (defined in dialogs.cpp). Return non-zero on accept.
INT_PTR ShowDeviceDialog(HWND parent,
                         MainWindow::DeviceDialogResult* result,
                         const std::vector<std::string>& existing_ids,
                         bool is_edit);
INT_PTR ShowMappingDialog(HWND parent,
                          MainWindow::MappingDialogResult* result,
                          const std::vector<std::string>& known_device_ids,
                          const std::string& default_device_id,
                          bool is_edit);

}  // namespace rmt::gui
#endif  // _WIN32
