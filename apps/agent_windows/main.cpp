// Windows Agent entry point — Phase 4 GUI (Win32).
#ifdef _WIN32
#include "gui/agent_window.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    rmt::gui::AgentWindow app("AGENT001", "127.0.0.1:4433");
    return app.run();
}
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return 1; }
#endif
