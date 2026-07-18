// Windows Agent entry point — Phase 4 GUI (Win32).
#ifdef _WIN32
#include "gui/agent_window.h"

int main() {
    rmt::gui::AgentWindow app("AGENT001", "127.0.0.1:4433");
    return app.run();
}
#else
int main() { return 1; }
#endif
