#pragma once
// Windows Agent GUI — dark-themed status window.
//
// Reads / writes agent.json (Device ID, Server host, Server port). Shows:
//   - Header band with app title
//   - Status card: large colored state (Connecting / Online / Offline / Auth)
//   - Info card: Device ID, Server, reconnect count / active sessions
//   - Read-only event log on a dark card (recent state transitions)
//   - Settings dialog: edit Device ID / Server host/port (writes agent.json)
//   - Reconnect button: tears down and re-creates the connection
//
// Look & feel: shared dark theme (apps/shared/theme). Cards are painted in
// WM_PAINT (double-buffered); buttons are flat owner-drawn.
//
// Threading: AgentConnection callbacks run on the io_context thread and only
// PostMessage to the GUI thread; all control updates happen on the GUI thread.
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

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <asio.hpp>
#include <windows.h>

#include "rmt/tunnel/agent_connection.h"
#include "rmt/tunnel/agent_session_manager.h"
#include "rmt/config/config_loader.h"

namespace rmt::gui {

class AgentWindow {
public:
    AgentWindow();
    ~AgentWindow();

    // Loads agent.json. If not present, falls back to defaults and saves a
    // minimal stub so subsequent Settings edits work normally.
    int run();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);
    void on_create();
    void on_close();
    void on_destroy();
    void on_size();
    void on_settings();
    void on_reconnect();
    void on_about();
    void update_state(tunnel::AgentState state);
    void update_labels();
    void on_timer();

    // Paint the themed background (bands + cards), double-buffered.
    void paint(HDC hdc, RECT client);
    static void paint_tramp(HDC hdc, RECT client, void* ctx);
    LRESULT handle_ctl_color_static(HDC hdc, HWND control);

    // (Re)create io_context + AgentConnection from cfg_ and start it.
    // Tears down any previous instance first.
    void restart_agent();
    void save_config();
    void append_log(const std::wstring& line);
    void drain_event_queue();
    void create_fonts();
    void apply_fonts();
    void layout_controls(int client_w, int client_h);

    HWND hwnd_ = nullptr;
    HWND static_title_ = nullptr;
    HWND static_subtitle_ = nullptr;
    HWND lbl_state_big_ = nullptr;   // large colored state text
    HWND cap_device_ = nullptr;      // dim captions on the info card
    HWND cap_server_ = nullptr;
    HWND cap_activity_ = nullptr;
    HWND lbl_device_ = nullptr;
    HWND lbl_server_ = nullptr;
    HWND lbl_attempts_ = nullptr;
    HWND lbl_log_caption_ = nullptr;
    HWND edit_log_ = nullptr;
    HWND btn_settings_   = nullptr;
    HWND btn_reconnect_  = nullptr;
    HWND btn_about_      = nullptr;
    HWND btn_exit_       = nullptr;

    HFONT font_ui_ = nullptr;
    HFONT font_title_ = nullptr;
    HFONT font_subtitle_ = nullptr;
    HFONT font_state_ = nullptr;

    // Card rects computed by layout_controls, painted by paint().
    RECT rc_status_card_ = {};
    RECT rc_info_card_ = {};
    RECT rc_log_card_ = {};

    config::AgentConfigFile cfg_;
    std::unique_ptr<asio::io_context> io_;
    std::shared_ptr<tunnel::AgentConnection> agent_;
    std::thread io_thread_;

    // Session dispatcher for OPEN_SESSION / SESSION_DATA / ...
    std::unique_ptr<tunnel::AgentSessionManager> session_mgr_;

    tunnel::AgentState state_ = tunnel::AgentState::Disconnected;
    COLORREF state_color_ = RGB(0x88, 0x88, 0x88);
    int reconnect_attempts_ = 0;  // completed reconnect cycles since start
    int sessions_ui_ = 0;         // last known active session count

    // Cross-thread event queue (io thread → GUI thread), drained by WM_TIMER.
    std::mutex events_m_;
    std::deque<std::wstring> gui_events_;
    std::atomic<int> pending_session_count_{-1};

    static constexpr int kTimerId = 1;
    static AgentWindow* instance_;
};

}  // namespace rmt::gui
#endif
