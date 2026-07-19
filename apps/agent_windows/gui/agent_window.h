#pragma once
// Windows Agent GUI (Phase 4 MVP, redesigned).
//
// Reads / writes agent.json (Device ID, Server host, Server port). Provides
// a minimal status window with:
//   - Device ID, Server, current connection state
//   - Settings button: edit Device ID / Server host/port (writes agent.json)
//   - Reconnect button: forces an immediate re-connect
//   - About button: version info
//   - Exit button: graceful shutdown
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

#include <memory>
#include <string>
#include <thread>

#include <asio.hpp>
#include <windows.h>

#include "rmt/tunnel/agent_connection.h"
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
    void on_settings();
    void on_reconnect();
    void on_about();
    void update_state(tunnel::AgentState state);
    void update_labels();
    void save_config();

    HWND hwnd_ = nullptr;
    HWND lbl_device_  = nullptr;
    HWND lbl_server_  = nullptr;
    HWND lbl_state_   = nullptr;
    HWND btn_settings_   = nullptr;
    HWND btn_reconnect_  = nullptr;
    HWND btn_about_      = nullptr;
    HWND btn_exit_       = nullptr;

    config::AgentConfigFile cfg_;
    std::unique_ptr<asio::io_context> io_;
    std::shared_ptr<tunnel::AgentConnection> agent_;
    std::thread io_thread_;

    static AgentWindow* instance_;
};

}  // namespace rmt::gui
#endif
