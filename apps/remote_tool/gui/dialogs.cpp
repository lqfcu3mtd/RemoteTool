// Win32 dialog procedures for RemoteTool — Add/Edit Device and Add/Edit
// Port Mapping. Both return the result via the context struct passed by
// the caller; `accepted = true` means the user clicked OK.
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

#include "dialogs.h"
#include "resources/resource.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace rmt::gui {

std::wstring to_wide(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}
std::string to_narrow(const std::wstring& w) {
    return std::string(w.begin(), w.end());
}

void set_edit_text(HWND hwnd, int id, const std::string& s) {
    SetDlgItemText(hwnd, id, to_wide(s).c_str());
}
std::string get_edit_text(HWND hwnd, int id) {
    wchar_t buf[256] = {};
    GetDlgItemText(hwnd, id, buf, 255);
    return to_narrow(buf);
}

// ============ Device dialog ============

struct DeviceDlgContext {
    MainWindow::DeviceDialogResult* result;
    std::vector<std::string> existing_ids;  // for duplicate check
    bool is_edit;
};

static INT_PTR CALLBACK DeviceDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<DeviceDlgContext*>(
        GetWindowLongPtr(hwnd, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<DeviceDlgContext*>(lp);
            SetWindowLongPtr(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
            if (ctx->is_edit) {
                set_edit_text(hwnd, IDC_DD_ID, ctx->result->device_id);
                set_edit_text(hwnd, IDC_DD_NAME, ctx->result->display_name);
                // Device ID is immutable in edit mode.
                EnableWindow(GetDlgItem(hwnd, IDC_DD_ID), FALSE);
                SetWindowText(hwnd, L"Edit device");
            } else {
                SetWindowText(hwnd, L"Add device");
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                std::string id = get_edit_text(hwnd, IDC_DD_ID);
                std::string name = get_edit_text(hwnd, IDC_DD_NAME);
                if (id.empty()) {
                    MessageBox(hwnd, L"Device ID cannot be empty.",
                               L"Validation", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                if (!ctx->is_edit) {
                    bool dup = std::any_of(ctx->existing_ids.begin(),
                                           ctx->existing_ids.end(),
                                           [&](const std::string& x) { return x == id; });
                    if (dup) {
                        MessageBox(hwnd, L"Device ID already exists.",
                                   L"Validation", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                }
                ctx->result->device_id = id;
                ctx->result->display_name = name;
                ctx->result->accepted = true;
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

INT_PTR ShowDeviceDialog(HWND parent,
                         MainWindow::DeviceDialogResult* result,
                         const std::vector<std::string>& existing_ids,
                         bool is_edit) {
    DeviceDlgContext ctx;
    ctx.result = result;
    ctx.existing_ids = existing_ids;
    ctx.is_edit = is_edit;
    return DialogBoxParam(GetModuleHandle(nullptr),
                          MAKEINTRESOURCE(IDD_DEVICE_DIALOG),
                          parent, DeviceDlgProc,
                          reinterpret_cast<LPARAM>(&ctx));
}

// ============ Mapping dialog ============

struct MappingDlgContext {
    MainWindow::MappingDialogResult* result;
    std::vector<std::string> known_device_ids;
    std::string default_device_id;
    bool is_edit;
};

void populate_device_dropdown(HWND hwnd, int combo_id,
                              const std::vector<std::string>& ids,
                              const std::string& selected) {
    HWND cb = GetDlgItem(hwnd, combo_id);
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    int sel_idx = -1;
    for (size_t i = 0; i < ids.size(); ++i) {
        SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)to_wide(ids[i]).c_str());
        if (ids[i] == selected) sel_idx = static_cast<int>(i);
    }
    if (sel_idx >= 0) {
        SendMessage(cb, CB_SETCURSEL, sel_idx, 0);
    } else if (!ids.empty()) {
        SendMessage(cb, CB_SETCURSEL, 0, 0);
    }
}

static INT_PTR CALLBACK MappingDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<MappingDlgContext*>(
        GetWindowLongPtr(hwnd, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            ctx = reinterpret_cast<MappingDlgContext*>(lp);
            SetWindowLongPtr(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
            set_edit_text(hwnd, IDC_MD_NAME, ctx->result->name);
            if (ctx->result->local_port > 0) {
                set_edit_text(hwnd, IDC_MD_LPORT,
                              std::to_string(ctx->result->local_port));
            }
            set_edit_text(hwnd, IDC_MD_HOST, ctx->result->target_host);
            if (ctx->result->target_port > 0) {
                set_edit_text(hwnd, IDC_MD_TPORT,
                              std::to_string(ctx->result->target_port));
            }
            populate_device_dropdown(hwnd, IDC_MD_DEVICE,
                                     ctx->known_device_ids,
                                     ctx->result->device_id.empty()
                                         ? ctx->default_device_id
                                         : ctx->result->device_id);
            if (ctx->is_edit) {
                SetWindowText(hwnd, L"Edit port mapping");
                // Name immutable in edit mode (config id derives from name).
                EnableWindow(GetDlgItem(hwnd, IDC_MD_NAME), FALSE);
            } else {
                SetWindowText(hwnd, L"Add port mapping");
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                std::string name = get_edit_text(hwnd, IDC_MD_NAME);
                std::string lport_s = get_edit_text(hwnd, IDC_MD_LPORT);
                std::string host = get_edit_text(hwnd, IDC_MD_HOST);
                std::string tport_s = get_edit_text(hwnd, IDC_MD_TPORT);
                if (name.empty() || lport_s.empty() || host.empty() || tport_s.empty()) {
                    MessageBox(hwnd,
                               L"Please fill all fields.",
                               L"Validation", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                int lport = 0, tport = 0;
                try { lport = std::stoi(lport_s); } catch (...) {}
                try { tport = std::stoi(tport_s); } catch (...) {}
                if (lport <= 0 || lport > 65535 || tport <= 0 || tport > 65535) {
                    MessageBox(hwnd,
                               L"Port must be in 1..65535.",
                               L"Validation", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                HWND cb = GetDlgItem(hwnd, IDC_MD_DEVICE);
                int sel = static_cast<int>(SendMessage(cb, CB_GETCURSEL, 0, 0));
                if (sel == CB_ERR || sel >= static_cast<int>(ctx->known_device_ids.size())) {
                    MessageBox(hwnd, L"Please select a device.",
                               L"Validation", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }
                ctx->result->name = name;
                ctx->result->device_id = ctx->known_device_ids[sel];
                ctx->result->local_port = static_cast<std::uint16_t>(lport);
                ctx->result->target_host = host;
                ctx->result->target_port = static_cast<std::uint16_t>(tport);
                ctx->result->accepted = true;
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            return FALSE;
    }
    return FALSE;
}

INT_PTR ShowMappingDialog(HWND parent,
                          MainWindow::MappingDialogResult* result,
                          const std::vector<std::string>& known_device_ids,
                          const std::string& default_device_id,
                          bool is_edit) {
    MappingDlgContext ctx;
    ctx.result = result;
    ctx.known_device_ids = known_device_ids;
    ctx.default_device_id = default_device_id;
    ctx.is_edit = is_edit;
    return DialogBoxParam(GetModuleHandle(nullptr),
                          MAKEINTRESOURCE(IDD_MAPPING_DIALOG),
                          parent, MappingDlgProc,
                          reinterpret_cast<LPARAM>(&ctx));
}

}  // namespace rmt::gui
#endif  // _WIN32
