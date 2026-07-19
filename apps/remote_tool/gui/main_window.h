#pragma once
// Win32 main window for RemoteTool — dark-themed.
//
// Layout: header band (title + summary + Settings), left "Devices" card
// (ListView 6 cols + pair-code sub-card), right "Port mappings" card
// (ListView 6 cols + active-sessions sub-card), bottom status band.
// Cards/bands are painted in WM_PAINT (double-buffered); the look comes from
// the shared dark theme (apps/shared/theme). Add/Edit go through modal
// dialogs (dialogs.cpp). The window is resizable; controls are re-laid out on
// WM_SIZE and a minimum size is enforced.
//
// Threading: the GUI thread never touches sockets. Network callbacks run on
// the io_context thread and post closures to EventQueue, which the GUI
// thread drains from a WM_TIMER handler. MappingListener start/stop calls
// are issued from the GUI thread but only touch listener objects that the
// io thread uses; listener destruction is deferred via asio::post so pending
// accept handlers never see a freed object.
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

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "rmt/config/config_loader.h"
#include "rmt/tunnel/acceptor.h"
#include "rmt/tunnel/device_manager.h"
#include "rmt/tunnel/mapping_listener.h"
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
        std::string pair_code;         // 8 digits; empty when no active code
        std::chrono::steady_clock::time_point pair_code_expires{};
    };
    struct MappingRow {
        std::string name;
        std::string device_id;
        std::uint16_t local_port = 0;
        std::string target_host;
        std::uint16_t target_port = 0;
        bool running = false;
        int active_conns = 0;  // refreshed from SessionManager via io thread
        std::string config_id;  // config::MappingRecord::id
    };

    // Window procedure.
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);

    void on_create();
    void on_timer();
    void on_close();
    void on_destroy();
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
    void on_settings();

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
    // Query per-mapping session counts on the io thread and apply to the UI.
    void query_session_counts();

    // Layout / visuals.
    void layout_controls(int client_w, int client_h);
    void create_fonts();
    void apply_fonts();
    void set_last_event(const std::wstring& text);
    // Paint the themed background (bands + cards), double-buffered.
    void paint(HDC hdc, RECT client);
    static void paint_tramp(HDC hdc, RECT client, void* ctx);
    LRESULT handle_ctl_color_static(HDC hdc, HWND control);

    // Lookups.
    int find_device_row(const std::string& device_id) const;
    int find_mapping_row(const std::string& config_id) const;
    // Map a displayed ListView row to a mapping_rows_ index (honors filter).
    int displayed_to_actual_mapping(int displayed_row) const;

    // Load persisted config into local rows.
    void load_devices_from_config();
    void load_mappings_from_config();
    void save_devices_config();
    void save_mappings_config();

    // Mapping listener lifecycle (real MappingListener per running mapping).
    void start_listener_for_mapping(int actual_idx);
    void stop_listener_for_mapping(const std::string& config_id);
    void stop_all_listeners();

    // Process one EventQueue batch.
    void process_events();
    void add_device_to_list(const std::string& device_id, const std::string& address,
                            const std::string& agent_version);
    void update_device_online_state(const std::string& device_id, bool online);

    // Main window.
    HWND hwnd_ = nullptr;

    // Header band.
    HWND static_title_ = nullptr;       // "RemoteTool"
    HWND static_subtitle_ = nullptr;    // "Reverse tunnel server"
    HWND toolbar_summary_ = nullptr;    // "N online · M running"
    HWND toolbar_settings_ = nullptr;   // Settings button

    // Left card (Devices).
    HWND list_devices_ = nullptr;       // SysListView32
    HWND static_pair_label_ = nullptr;
    HWND static_pair_code_ = nullptr;
    HWND static_pair_expires_ = nullptr;
    HWND btn_pair_copy_ = nullptr;
    HWND btn_pair_regen_ = nullptr;
    HWND btn_pair_revoke_ = nullptr;
    HWND btn_dev_add_ = nullptr;
    HWND btn_dev_edit_ = nullptr;
    HWND btn_dev_delete_ = nullptr;

    // Right card (Mappings).
    HWND list_mappings_ = nullptr;
    HWND static_sessions_label_ = nullptr;
    HWND static_sessions_body_ = nullptr;
    HWND btn_map_filter_clear_ = nullptr;  // "Show all" — clears the device filter
    HWND static_map_filter_ = nullptr;     // "Filter: <device_id>  (2 of 3)" label
    HWND btn_map_add_ = nullptr;
    HWND btn_map_edit_ = nullptr;
    HWND btn_map_start_ = nullptr;
    HWND btn_map_stop_ = nullptr;
    HWND btn_map_delete_ = nullptr;

    // Bottom status band.
    HWND status_left_ = nullptr;    // last notable event
    HWND status_right_ = nullptr;   // listen address + counters

    // Card rects computed by layout_controls, painted by paint().
    RECT rc_devices_card_ = {};
    RECT rc_pair_card_ = {};
    RECT rc_mappings_card_ = {};
    RECT rc_sessions_card_ = {};

    // GDI resources (created in on_create, destroyed in on_destroy).
    HFONT font_ui_ = nullptr;       // UI font, 9pt
    HFONT font_title_ = nullptr;    // header title, 12pt bold
    HFONT font_caption_ = nullptr;  // card captions, 9pt semibold
    HFONT font_code_ = nullptr;     // pair-code display font (monospace, bold)

    // Backend.
    std::unique_ptr<asio::io_context> io_;
    std::unique_ptr<tunnel::DeviceManager> devices_;
    std::unique_ptr<tunnel::Acceptor> acceptor_;
    std::unique_ptr<session::SessionIdAllocator> id_alloc_;
    std::unique_ptr<tunnel::SessionManager> sessions_;
    std::thread io_thread_;
    rmt::gui::EventQueue events_;

    // One live listener per running mapping, keyed by config_id. Owned by the
    // GUI thread; destruction of a removed listener is deferred onto the io
    // thread so in-flight accept handlers complete first.
    std::unordered_map<std::string, std::unique_ptr<tunnel::MappingListener>> listeners_;

    // Persisted remote_tool.json settings.
    config::RemoteToolConfig rt_config_;

    // Local data model.
    std::vector<DeviceRow> device_rows_;
    std::vector<MappingRow> mapping_rows_;

    // Selection state.
    int selected_device_idx_ = -1;
    int selected_mapping_idx_ = -1;
    std::string filter_device_id_;  // empty = show all mappings

    // Status band left part: last notable event.
    std::wstring last_event_ = L"Ready";

    // GUI-thread snapshot of the global active session count (updated by
    // query_session_counts; avoids reading SessionManager off the io thread).
    int total_sessions_ui_ = 0;

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
// Settings dialog: edits remote_tool.json fields in place. Returns IDOK when
// the user accepted (caller persists).
INT_PTR ShowSettingsDialog(HWND parent, config::RemoteToolConfig* cfg);

}  // namespace rmt::gui
#endif  // _WIN32
