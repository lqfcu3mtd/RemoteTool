// RemoteTool entry point — Phase 4 GUI (Win32).
// Use WinMain so -mwindows -static linking works (main() stub may be missing
// with static CRT).
#ifdef _WIN32
#include "gui/main_window.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    rmt::gui::MainWindow app;
    return app.run();
}
#else
int main() { return 1; }
#endif
