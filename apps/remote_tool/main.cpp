// RemoteTool entry point — Phase 4 GUI (Win32).
// Runs the Win32 message loop on the main thread; network io_context
// runs on a background thread. Network events update the UI via a
// thread-safe EventQueue.
#ifdef _WIN32
#include "gui/main_window.h"

int main() {
    rmt::gui::MainWindow app;
    return app.run();
}
#else
#include <cstdio>
int main() {
    std::printf("RemoteTool requires Windows (Win32 GUI).\n");
    return 1;
}
#endif
