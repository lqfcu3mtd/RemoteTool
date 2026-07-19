#pragma once
// Win32 dialog procedures for RemoteTool. The dialogs are modal: the caller
// provides a result struct, the dialog mutates it on OK. See main_window.h
// for the result struct definitions.
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

#include "main_window.h"
#include "resources/resource.h"

namespace rmt::gui {

// Show the device dialog (Add or Edit). Pre-fills the result struct on Edit.
// `existing_ids` is a list of device_ids already in use (to validate against
// duplicates on Add).
INT_PTR ShowDeviceDialog(HWND parent,
                         MainWindow::DeviceDialogResult* result,
                         const std::vector<std::string>& existing_ids,
                         bool is_edit);

// Show the mapping dialog (Add or Edit). `known_device_ids` populates the
// device dropdown.
INT_PTR ShowMappingDialog(HWND parent,
                          MainWindow::MappingDialogResult* result,
                          const std::vector<std::string>& known_device_ids,
                          const std::string& default_device_id,
                          bool is_edit);

}  // namespace rmt::gui
#endif  // _WIN32
