#pragma once
// Windows Agent GUI (Phase 4 MVP).
// Displays device_id, connection state, and RemoteTool address.
// Exit button triggers graceful shutdown.
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

namespace rmt::gui {

class AgentWindow {
public:
    AgentWindow(const std::string& device_id, const std::string& server_addr);
    ~AgentWindow();

    int run();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp);
    void on_create();
    void on_close();
    void update_state(tunnel::AgentState state);

    HWND hwnd_ = nullptr;
    HWND lbl_device_ = nullptr;
    HWND lbl_state_ = nullptr;
    HWND lbl_server_ = nullptr;
    HWND btn_exit_ = nullptr;

    std::string device_id_;
    std::string server_addr_;
    std::unique_ptr<asio::io_context> io_;
    std::shared_ptr<tunnel::AgentConnection> agent_;
    std::thread io_thread_;

    static AgentWindow* instance_;
};

}  // namespace rmt::gui
#endif
