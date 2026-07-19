#pragma once
// Control and resource IDs for RemoteTool Win32 dialogs and menus.

#define IDI_APPICON  200

#define IDD_DEVICE_DIALOG  201
#define IDD_MAPPING_DIALOG 202
#define IDD_ABOUT_DIALOG   203
#define IDD_SETTINGS_DIALOG 204

#define IDR_MAINMENU       210

// --- Menu commands ---
#define IDM_FILE_EXIT           1001
#define IDM_EDIT_ADD_DEVICE     1002
#define IDM_EDIT_ADD_MAPPING    1003
#define IDM_FILE_SETTINGS       1004
#define IDM_VIEW_REFRESH        1005
#define IDM_VIEW_SHOW_ALL       1006
#define IDM_HELP_ABOUT          1007

// --- Device dialog controls ---
#define IDC_DD_ID          1101
#define IDC_DD_NAME        1102

// --- Mapping dialog controls ---
#define IDC_MD_NAME        1110
#define IDC_MD_DEVICE      1111
#define IDC_MD_LPORT       1112
#define IDC_MD_HOST        1113
#define IDC_MD_TPORT       1114

// --- Settings dialog controls ---
#define IDC_ST_BIND_HOST   1120
#define IDC_ST_AGENT_PORT  1121
#define IDC_ST_HB_TIMEOUT  1122
#define IDC_ST_MAX_SESS    1123
