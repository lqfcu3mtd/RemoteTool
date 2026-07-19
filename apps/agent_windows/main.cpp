// Windows Agent entry point — Phase 4 GUI (Win32).
// Config (device_id, server host/port) is loaded from agent.json. The user
// can edit it from the Settings dialog inside the GUI. If agent.json is
// missing, the agent window creates a stub with sensible defaults and
// saves it so the user can edit it later.
#ifdef _WIN32
#include "gui/agent_window.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    rmt::gui::AgentWindow app;
    return app.run();
}
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return 1; }
#endif
