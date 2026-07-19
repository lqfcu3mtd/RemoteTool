#pragma once
// Shared dark theme for the RemoteTool / Agent Win32 GUIs.
//
// One modern dark skin used by both executables: palette + GDI brushes,
// system-DPI scaling, dark window title bar (DWM), flat owner-drawn buttons
// with hover feedback, rounded "card" painting, and dark scrollbars for
// ListView / Edit controls (the undocumented but widely used
// "DarkMode_Explorer" theme, no-op on older Windows).
//
// Usage per top-level window:
//   1. theme::init() once before RegisterClass (creates brushes, caches DPI).
//   2. WC.hbrBackground = theme::bg_brush(); add WS_CLIPCHILDREN to the style.
//   3. theme::enable_dark_frame(hwnd) after CreateWindowEx.
//   4. Handle WM_ERASEBKGND (return 1) and paint the background in WM_PAINT.
//   5. theme::make_flat_button() on each push button; forward WM_DRAWITEM to
//      theme::draw_button().
//   6. theme::shutdown() at exit (after the last window is destroyed).
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

#include <windows.h>

namespace rmt::gui::theme {

// ---- Palette (matches the design: bg #1E1F24, card #26282E, accent #3B82F6)
inline constexpr COLORREF kBg        = RGB(0x1E, 0x1F, 0x24);  // window bg
inline constexpr COLORREF kBand      = RGB(0x17, 0x18, 0x1C);  // header / status band
inline constexpr COLORREF kCard      = RGB(0x26, 0x28, 0x2E);  // card surface
inline constexpr COLORREF kDeep      = RGB(0x14, 0x15, 0x18);  // inputs / log area
inline constexpr COLORREF kBorder    = RGB(0x3A, 0x3C, 0x44);  // card border
inline constexpr COLORREF kText      = RGB(0xE5, 0xE7, 0xEB);  // primary text
inline constexpr COLORREF kTextDim   = RGB(0x9C, 0xA3, 0xAF);  // captions / hints
inline constexpr COLORREF kAccent    = RGB(0x3B, 0x82, 0xF6);  // primary action
inline constexpr COLORREF kAccentHot = RGB(0x60, 0xA5, 0xFA);  // accent hover
inline constexpr COLORREF kAccentDn  = RGB(0x2C, 0x62, 0xBC);  // accent pressed
inline constexpr COLORREF kOk        = RGB(0x4A, 0xDE, 0x80);  // online / running
inline constexpr COLORREF kWarn      = RGB(0xFB, 0xBF, 0x24);  // connecting
inline constexpr COLORREF kErr       = RGB(0xF8, 0x71, 0x71);  // offline / error
inline constexpr COLORREF kBtnFace   = RGB(0x2E, 0x30, 0x38);  // flat button
inline constexpr COLORREF kBtnHot    = RGB(0x3D, 0x3F, 0x49);  // flat button hover
inline constexpr COLORREF kBtnDown   = RGB(0x24, 0x26, 0x2C);  // flat button pressed

// Creates the shared brushes/pen and caches the system DPI. Idempotent.
void init();
// Deletes GDI objects created by init(). Call after the last window is gone.
void shutdown();

HBRUSH bg_brush();
HBRUSH band_brush();
HBRUSH card_brush();
HBRUSH deep_brush();

// System DPI (manifest declares "system" awareness) and a pixel scaler for
// layout constants written against 96 DPI.
int dpi();
int px(int v);

// Dark title bar / window frame via DwmSetWindowAttribute. No-op on systems
// older than Windows 10 1809.
void enable_dark_frame(HWND top_level);

// Dark scrollbars (+ dark header for ListViews) using the DarkMode_Explorer
// theme. No-op on older Windows.
void enable_dark_scrollbars(HWND control);

// Removes visual styles from a control so WM_CTLCOLOR* fully controls its
// colors (used for Edit controls sitting on dark surfaces).
void untheme(HWND control);

// Font helper: point size scaled by the system DPI.
HFONT make_font(int pt, int weight, const wchar_t* face = L"Segoe UI");

// Converts a push button to a flat owner-drawn dark button (subclasses it to
// track hover). `primary` paints it with the accent color.
void make_flat_button(HWND button, bool primary = false);
// Paints a button converted by make_flat_button. Returns true when the
// DRAWITEMSTRUCT belonged to a flat button and was painted.
bool draw_button(const DRAWITEMSTRUCT* dis);

// Fills a rounded rectangle with a 1px border (card / input surface).
void fill_round_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF border, int radius);

// Double-buffer helper: paints `paint(hdc)` into a memory DC and blits it.
// Use inside WM_PAINT handlers to avoid flicker.
void buffered_paint(HWND hwnd, void (*paint)(HDC, RECT, void*), void* ctx);

// Dialogs: restyles the dialog (dark frame, flat buttons, unthemed edits).
// Call from WM_INITDIALOG.
void style_dialog(HWND dlg);
// Handles the dialog's color/draw messages (WM_CTLCOLORDLG, WM_CTLCOLOR*,
// WM_DRAWITEM). Returns true and stores the result in *out when handled.
bool handle_dialog_message(HWND dlg, UINT msg, WPARAM wp, LPARAM lp, INT_PTR* out);

}  // namespace rmt::gui::theme
#endif  // _WIN32
