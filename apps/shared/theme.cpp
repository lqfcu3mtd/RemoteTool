// Shared dark theme for the RemoteTool / Agent Win32 GUIs. See theme.h.
#ifdef _WIN32
#include "theme.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <cstring>
#include <cwchar>

namespace rmt::gui::theme {

namespace {

HBRUSH g_bg = nullptr;
HBRUSH g_band = nullptr;
HBRUSH g_card = nullptr;
HBRUSH g_deep = nullptr;
int g_dpi = 96;

constexpr wchar_t kPropHover[]   = L"RmtBtnHover";
constexpr wchar_t kPropPrimary[] = L"RmtBtnPrimary";
constexpr UINT_PTR kBtnSubclassId = 0x52544D54;  // 'RTMT'

// Hover-tracking subclass for flat buttons. Owner-drawn buttons don't get
// WM_MOUSEHOVER without TrackMouseEvent, so we track enter/leave ourselves
// and stash the state in window properties read by draw_button().
LRESULT CALLBACK FlatButtonProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR /*subclass_id*/, DWORD_PTR /*ref*/) {
    switch (msg) {
        case WM_MOUSEMOVE:
            if (!GetPropW(hwnd, kPropHover)) {
                SetPropW(hwnd, kPropHover, reinterpret_cast<HANDLE>(1));
                TRACKMOUSEEVENT tme = {};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_MOUSELEAVE:
            SetPropW(hwnd, kPropHover, nullptr);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_NCDESTROY:
            RemovePropW(hwnd, kPropHover);
            RemovePropW(hwnd, kPropPrimary);
            RemoveWindowSubclass(hwnd, FlatButtonProc, kBtnSubclassId);
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

COLORREF button_face(const DRAWITEMSTRUCT* dis, bool primary) {
    const bool hot = GetPropW(dis->hwndItem, kPropHover) != nullptr;
    const bool down = (dis->itemState & ODS_SELECTED) != 0;
    if (primary) {
        if (down) return kAccentDn;
        if (hot) return kAccentHot;
        return kAccent;
    }
    if (down) return kBtnDown;
    if (hot) return kBtnHot;
    return kBtnFace;
}

// EnumChildWindows callback for style_dialog: flatten buttons, untheme edits.
BOOL CALLBACK style_dialog_child(HWND child, LPARAM /*lp*/) {
    wchar_t cls[32] = {};
    GetClassNameW(child, cls, 31);
    if (_wcsicmp(cls, L"Button") == 0) {
        const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
        const UINT type = static_cast<UINT>(style & BS_TYPEMASK);
        if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) {
            make_flat_button(child, type == BS_DEFPUSHBUTTON);
        }
    } else if (_wcsicmp(cls, L"Edit") == 0) {
        untheme(child);
    }
    return TRUE;
}

}  // anonymous namespace

void init() {
    if (g_bg) return;  // already initialized
    g_bg   = CreateSolidBrush(kBg);
    g_band = CreateSolidBrush(kBand);
    g_card = CreateSolidBrush(kCard);
    g_deep = CreateSolidBrush(kDeep);
    HDC screen = GetDC(nullptr);
    g_dpi = screen ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen) ReleaseDC(nullptr, screen);
}

void shutdown() {
    if (g_bg)   { DeleteObject(g_bg);   g_bg = nullptr; }
    if (g_band) { DeleteObject(g_band); g_band = nullptr; }
    if (g_card) { DeleteObject(g_card); g_card = nullptr; }
    if (g_deep) { DeleteObject(g_deep); g_deep = nullptr; }
}

HBRUSH bg_brush()   { return g_bg; }
HBRUSH band_brush() { return g_band; }
HBRUSH card_brush() { return g_card; }
HBRUSH deep_brush() { return g_deep; }

int dpi() { return g_dpi; }
int px(int v) { return MulDiv(v, g_dpi, 96); }

void enable_dark_frame(HWND top_level) {
    // DWMWA_USE_IMMERSIVE_DARK_MODE: 19 on Win10 1809..20H1, 20 on newer.
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(top_level, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(top_level, 19, &dark, sizeof(dark));
    }
}

void enable_dark_scrollbars(HWND control) {
    SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
}

void untheme(HWND control) {
    SetWindowTheme(control, L"", L"");
}

HFONT make_font(int pt, int weight, const wchar_t* face) {
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(pt, g_dpi, 96);
    lf.lfWeight = weight;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, face);
    return CreateFontIndirectW(&lf);
}

void make_flat_button(HWND button, bool primary) {
    const LONG_PTR style = GetWindowLongPtrW(button, GWL_STYLE);
    SetWindowLongPtrW(button, GWL_STYLE,
                      (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
    SetPropW(button, kPropPrimary,
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(primary ? 1 : 0)));
    SetWindowSubclass(button, FlatButtonProc, kBtnSubclassId, 0);
    InvalidateRect(button, nullptr, TRUE);
}

bool draw_button(const DRAWITEMSTRUCT* dis) {
    if (!dis || dis->CtlType != ODT_BUTTON) return false;
    const bool primary =
        GetPropW(dis->hwndItem, kPropPrimary) == reinterpret_cast<HANDLE>(1);
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    const COLORREF face = disabled ? kBtnFace : button_face(dis, primary);
    fill_round_rect(hdc, rc, face, primary ? face : kBorder, px(4));

    if (!disabled && (dis->itemState & ODS_FOCUS) && !primary) {
        InflateRect(&rc, -px(2), -px(2));
        HPEN pen = CreatePen(PS_DOT, 1, kTextDim);
        HGDIOBJ old_pen = SelectObject(hdc, pen);
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, px(4), px(4));
        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(pen);
    }

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, 127);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, disabled ? kTextDim
                               : (primary ? RGB(0xFF, 0xFF, 0xFF) : kText));
    HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0));
    HGDIOBJ old_font = font ? SelectObject(hdc, font) : nullptr;
    RECT text_rc = dis->rcItem;
    DrawTextW(hdc, text, -1, &text_rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old_font) SelectObject(hdc, old_font);
    return true;
}

void fill_round_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF border,
                     int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(hdc, brush);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void buffered_paint(HWND hwnd, void (*paint)(HDC, RECT, void*), void* ctx) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    paint(mem, rc, ctx);
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void style_dialog(HWND dlg) {
    enable_dark_frame(dlg);
    EnumChildWindows(dlg, style_dialog_child, 0);
    // Keep Enter working even though DEFPUSHBUTTON became owner-drawn.
    SendMessageW(dlg, DM_SETDEFID, IDOK, 0);
}

bool handle_dialog_message(HWND dlg, UINT msg, WPARAM wp, LPARAM lp,
                           INT_PTR* out) {
    switch (msg) {
        case WM_CTLCOLORDLG: {
            *out = reinterpret_cast<INT_PTR>(g_bg);
            return true;
        }
        case WM_CTLCOLORSTATIC: {
            auto* hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, kText);
            SetBkColor(hdc, kBg);
            *out = reinterpret_cast<INT_PTR>(g_bg);
            return true;
        }
        case WM_CTLCOLOREDIT: {
            auto* hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, kText);
            SetBkColor(hdc, kDeep);
            *out = reinterpret_cast<INT_PTR>(g_deep);
            return true;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (draw_button(dis)) {
                *out = TRUE;
                return true;
            }
            return false;
        }
    }
    (void)dlg;
    return false;
}

}  // namespace rmt::gui::theme
#endif  // _WIN32
