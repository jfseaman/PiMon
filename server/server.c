#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dwmapi.h>
#include <winreg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")

#define SERVER_PORT 5000
#define MAX_CLIENTS 32
#define MAX_SAMPLES 2
#define CLIENT_ID_LEN 32
#define OFFLINE_SECS 30
#define IDI_APPICON 101

typedef struct {
    char     client_id[CLIENT_ID_LEN];
    float    cpu_load;
    float    cpu_temp;
    float    fan_speed;
    float    cpu_mhz;
    uint64_t timestamp;
} TelemetryPacket;

typedef struct {
    TelemetryPacket samples[MAX_SAMPLES];
    int count;
    struct sockaddr_in last_addr;   // <-- store client IP/port
    int is_offline;
    CRITICAL_SECTION lock;
    int lock_initialized;
} ClientData;


ClientData clients[MAX_CLIENTS] = {0};

char timestr[64];

static SOCKET g_sock = INVALID_SOCKET;
static HANDLE g_recv_thread = NULL;
static volatile LONG g_running = 1;
static HICON g_app_icon_big = NULL;
static HICON g_app_icon_small = NULL;
static HICON g_app_icon_big_alert = NULL;
static HICON g_app_icon_small_alert = NULL;
static int g_offline_clients = 0;
static HANDLE g_instance_mutex = NULL;
static int g_start_minimized = 0;
static HWND g_prefs_hwnd = NULL;

static const char *WINDOW_CLASS = "PiMonServerWindow";
static const char *WINDOW_TITLE = "PiMon Server - monitoring telemetry data";
static const char *WINDOW_TITLE_ACTION = "PiMon Server - Action needed";
static const char *SETTINGS_REG_PATH = "Software\\PiMonServer";
static const UINT_PTR UI_TIMER_ID = 1;
static const UINT UI_TIMER_MS = 10000;
static const int UI_PADDING_X = 10;
static const int UI_PADDING_Y = 10;
static const int UI_EXTRA_ROWS = 4;
static const UINT NOTIFY_ICON_ID = 1;
static const UINT NOTIFY_BALLOON_MS = 15000;
#define WM_TRAYICON (WM_APP + 1)
#define MENU_TRAY_OPEN 1001
#define MENU_TRAY_EXIT 1002
#define MENU_EDIT_CLEAR_ALL 2001
#define MENU_EDIT_CLEAR_OFFLINE 2002
#define MENU_EDIT_SELECT_TEXT 2003
#define MENU_EDIT_PREFERENCES 2004
#define MENU_EDIT_EXIT 2005

#define PREFS_CHK_START_MIN 3001
#define PREFS_BTN_SAVE 3002
#define PREFS_BTN_CANCEL 3003

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

static NOTIFYICONDATAA g_notify = {0};
static int g_notify_added = 0;
static HMENU g_tray_menu = NULL;
static HMENU g_menu_bar = NULL;
static HMENU g_menu_edit = NULL;

static void update_offline_state(HWND hwnd);
static void load_settings(void);
static void save_settings(void);

void format_time(uint64_t ts, char *buf, size_t len) {
    time_t t = (time_t)ts;
    struct tm tm_info;
    localtime_s(&tm_info, &t);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
}

ClientData* get_client(const char *id) {
    ClientData *empty = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].lock_initialized) {
            InitializeCriticalSection(&clients[i].lock);
            clients[i].lock_initialized = 1;
        }
        if (clients[i].count == 0 && !empty) {
            empty = &clients[i];
        }
        if (strcmp(clients[i].samples[0].client_id, id) == 0)
            return &clients[i];
    }
    if (empty) {
        strncpy(empty->samples[0].client_id, id, CLIENT_ID_LEN);
        empty->samples[0].client_id[CLIENT_ID_LEN - 1] = '\0';
        empty->is_offline = 0;
        return empty;
    }
    return NULL;
}

static void init_notify_icon(HWND hwnd) {
    if (g_notify_added) return;

    memset(&g_notify, 0, sizeof(g_notify));
    g_notify.cbSize = sizeof(g_notify);
    g_notify.hWnd = hwnd;
    g_notify.uID = NOTIFY_ICON_ID;
    g_notify.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_notify.uCallbackMessage = WM_TRAYICON;
    if (g_app_icon_small) {
        g_notify.hIcon = g_app_icon_small;
    } else if (g_app_icon_big) {
        g_notify.hIcon = g_app_icon_big;
    } else {
        g_notify.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    strncpy(g_notify.szTip, "PiMon Server", sizeof(g_notify.szTip) - 1);

    if (Shell_NotifyIconA(NIM_ADD, &g_notify)) {
        g_notify_added = 1;
    }
}

static void restore_window(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

static void show_tray_menu(HWND hwnd) {
    if (!g_tray_menu) {
        g_tray_menu = CreatePopupMenu();
        if (!g_tray_menu) return;
        AppendMenuA(g_tray_menu, MF_STRING, MENU_TRAY_OPEN, "Open");
        AppendMenuA(g_tray_menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(g_tray_menu, MF_STRING, MENU_TRAY_EXIT, "Exit");
    }

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(g_tray_menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
}

static void init_main_menu(HWND hwnd) {
    g_menu_edit = CreatePopupMenu();
    if (!g_menu_edit) return;
    AppendMenuA(g_menu_edit, MF_STRING, MENU_EDIT_CLEAR_ALL, "Clear all");
    AppendMenuA(g_menu_edit, MF_STRING, MENU_EDIT_CLEAR_OFFLINE, "Clear offline");
    AppendMenuA(g_menu_edit, MF_STRING, MENU_EDIT_SELECT_TEXT, "Select text");
    AppendMenuA(g_menu_edit, MF_STRING, MENU_EDIT_PREFERENCES, "Preferences");
    AppendMenuA(g_menu_edit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_menu_edit, MF_STRING, MENU_EDIT_EXIT, "Exit");

    g_menu_bar = CreateMenu();
    if (!g_menu_bar) return;
    AppendMenuA(g_menu_bar, MF_POPUP, (UINT_PTR)g_menu_edit, "Edit");
    SetMenu(hwnd, g_menu_bar);
}

static void apply_system_titlebar_theme(HWND hwnd) {
    DWORD value = 1;
    HKEY key = NULL;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = sizeof(value);
        if (RegQueryValueExA(key, "AppsUseLightTheme", NULL, &type,
                             (LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD) {
            value = 1;
        }
        RegCloseKey(key);
    }

    BOOL dark = (value == 0);
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &dark, sizeof(dark));

    HMODULE ux = LoadLibraryA("uxtheme.dll");
    if (ux) {
        typedef enum {
            APP_MODE_DEFAULT = 0,
            APP_MODE_ALLOW_DARK = 1,
            APP_MODE_FORCE_DARK = 2,
            APP_MODE_FORCE_LIGHT = 3,
            APP_MODE_MAX = 4
        } PreferredAppMode;
        typedef PreferredAppMode (WINAPI *SetPreferredAppModeProc)(PreferredAppMode mode);
        typedef BOOL (WINAPI *AllowDarkModeForWindowProc)(HWND hwnd, BOOL allow);
        typedef void (WINAPI *FlushMenuThemesProc)(void);
        typedef void (WINAPI *RefreshImmersiveColorPolicyStateProc)(void);

        SetPreferredAppModeProc set_app_mode =
            (SetPreferredAppModeProc)GetProcAddress(ux, "SetPreferredAppMode");
        AllowDarkModeForWindowProc allow_dark =
            (AllowDarkModeForWindowProc)GetProcAddress(ux, "AllowDarkModeForWindow");
        RefreshImmersiveColorPolicyStateProc refresh_policy =
            (RefreshImmersiveColorPolicyStateProc)GetProcAddress(ux, "RefreshImmersiveColorPolicyState");
        FlushMenuThemesProc flush_menu =
            (FlushMenuThemesProc)GetProcAddress(ux, "FlushMenuThemes");

        if (set_app_mode) {
            set_app_mode(dark ? APP_MODE_ALLOW_DARK : APP_MODE_DEFAULT);
        }
        if (allow_dark) {
            allow_dark(hwnd, dark);
        }
        if (refresh_policy) {
            refresh_policy();
        }
        if (flush_menu) {
            flush_menu();
        }

        FreeLibrary(ux);
    }

    DrawMenuBar(hwnd);
}

typedef struct {
    HWND parent;
    HWND checkbox;
} PrefsDialog;

static LRESULT CALLBACK PrefsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PrefsDialog *state = (PrefsDialog *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTA *cs = (CREATESTRUCTA *)lParam;
            state = (PrefsDialog *)calloc(1, sizeof(PrefsDialog));
            if (!state) return -1;
            state->parent = (HWND)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int margin = 12;
            int spacing = 8;
            int btn_w = 80;
            int btn_h = 24;
            int btn_y = rc.bottom - margin - btn_h;
            int cancel_x = rc.right - margin - btn_w;
            int save_x = cancel_x - spacing - btn_w;

            state->checkbox = CreateWindowExA(
                0, "BUTTON", "Start Minimized",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                margin, margin, rc.right - margin * 2, 20,
                hwnd, (HMENU)PREFS_CHK_START_MIN, cs->hInstance, NULL);

            HWND btn_save = CreateWindowExA(
                0, "BUTTON", "Save",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                save_x, btn_y, btn_w, btn_h,
                hwnd, (HMENU)PREFS_BTN_SAVE, cs->hInstance, NULL);

            HWND btn_cancel = CreateWindowExA(
                0, "BUTTON", "Cancel",
                WS_CHILD | WS_VISIBLE,
                cancel_x, btn_y, btn_w, btn_h,
                hwnd, (HMENU)PREFS_BTN_CANCEL, cs->hInstance, NULL);

            HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            if (state->checkbox) SendMessage(state->checkbox, WM_SETFONT, (WPARAM)font, TRUE);
            if (btn_save) SendMessage(btn_save, WM_SETFONT, (WPARAM)font, TRUE);
            if (btn_cancel) SendMessage(btn_cancel, WM_SETFONT, (WPARAM)font, TRUE);

            if (state->checkbox) {
                SendMessage(state->checkbox, BM_SETCHECK,
                            g_start_minimized ? BST_CHECKED : BST_UNCHECKED, 0);
            }

            apply_system_titlebar_theme(hwnd);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case PREFS_BTN_SAVE:
                    if (state && state->checkbox) {
                        g_start_minimized =
                            (SendMessage(state->checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        save_settings();
                    }
                    DestroyWindow(hwnd);
                    return 0;
                case PREFS_BTN_CANCEL:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (state) {
                if (state->parent && IsWindow(state->parent)) {
                    EnableWindow(state->parent, TRUE);
                    SetForegroundWindow(state->parent);
                }
                free(state);
            }
            g_prefs_hwnd = NULL;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void show_preferences_dialog(HWND parent) {
    if (g_prefs_hwnd) {
        ShowWindow(g_prefs_hwnd, SW_SHOW);
        SetForegroundWindow(g_prefs_hwnd);
        return;
    }

    static ATOM prefs_class = 0;
    if (!prefs_class) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = PrefsWndProc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "PiMonPreferencesDialog";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hIcon = g_app_icon_big ? g_app_icon_big : LoadIcon(NULL, IDI_APPLICATION);
        prefs_class = RegisterClassA(&wc);
    }

    int width = 360;
    int height = 160;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;

    if (parent) {
        RECT pr;
        GetWindowRect(parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }

    g_prefs_hwnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME, "PiMonPreferencesDialog", "Preferences",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x, y, width, height,
        parent, NULL, GetModuleHandleA(NULL), parent);

    if (!g_prefs_hwnd) return;

    if (parent) {
        EnableWindow(parent, FALSE);
    }

    ShowWindow(g_prefs_hwnd, SW_SHOW);
    UpdateWindow(g_prefs_hwnd);
    SetForegroundWindow(g_prefs_hwnd);
}

static void show_offline_notification(HWND hwnd, const char *client_id, const char *ip) {
    if (!g_notify_added) {
        init_notify_icon(hwnd);
        if (!g_notify_added) return;
    }

    NOTIFYICONDATAA nid = g_notify;
    nid.uFlags = NIF_INFO;
    _snprintf(nid.szInfoTitle, sizeof(nid.szInfoTitle), "Client offline");
    _snprintf(nid.szInfo, sizeof(nid.szInfo), "%s (%s) has gone offline.", client_id, ip);
    nid.dwInfoFlags = NIIF_WARNING;
    nid.uTimeout = NOTIFY_BALLOON_MS;
    Shell_NotifyIconA(NIM_MODIFY, &nid);
}

static HICON create_alert_icon(HICON base, int size) {
    if (!base || size <= 0) return NULL;

    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) return NULL;

    void *bits = NULL;
    HBITMAP color = CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color) {
        DeleteDC(hdc);
        return NULL;
    }

    HGDIOBJ old = SelectObject(hdc, color);
    if (bits) {
        memset(bits, 0, (size_t)size * (size_t)size * 4);
    }

    DrawIconEx(hdc, 0, 0, base, size, size, 0, NULL, DI_NORMAL);

    int r = (int)(size * 0.28f);
    if (r < 3) r = 3;
    int margin = (int)(size * 0.08f);
    int cx = size - r - margin;
    int cy = size - r - margin;

    int pen_w = size >= 24 ? 2 : 1;
    HBRUSH brush = CreateSolidBrush(RGB(245, 210, 0));
    HPEN pen = CreatePen(PS_SOLID, pen_w, RGB(170, 130, 0));
    HGDIOBJ old_brush = SelectObject(hdc, brush);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);

    HPEN check_pen = CreatePen(PS_SOLID, pen_w, RGB(0, 0, 0));
    SelectObject(hdc, check_pen);
    MoveToEx(hdc, cx - r / 2, cy, NULL);
    LineTo(hdc, cx - r / 8, cy + r / 2);
    LineTo(hdc, cx + r / 2, cy - r / 2);

    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);

    DeleteObject(check_pen);
    DeleteObject(pen);
    DeleteObject(brush);

    HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;

    HICON icon = CreateIconIndirect(&ii);

    SelectObject(hdc, old);
    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(hdc);

    return icon;
}

static void update_app_status(HWND hwnd, int offline_count) {
    int had_offline = (g_offline_clients > 0);
    int has_offline = (offline_count > 0);

    g_offline_clients = offline_count;
    if (had_offline == has_offline) return;

    SetWindowTextA(hwnd, has_offline ? WINDOW_TITLE_ACTION : WINDOW_TITLE);

    HICON big_icon = has_offline ? g_app_icon_big_alert : g_app_icon_big;
    HICON small_icon = has_offline ? g_app_icon_small_alert : g_app_icon_small;
    if (!big_icon) big_icon = g_app_icon_big;
    if (!small_icon) small_icon = g_app_icon_small;

    if (big_icon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big_icon);
    }
    if (small_icon) {
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }

    if (g_notify_added) {
        NOTIFYICONDATAA nid = g_notify;
        nid.uFlags = NIF_ICON;
        nid.hIcon = small_icon ? small_icon : g_notify.hIcon;
        g_notify.hIcon = nid.hIcon;
        Shell_NotifyIconA(NIM_MODIFY, &nid);
    }
}

static void load_settings(void) {
    DWORD value = 0;
    HKEY key = NULL;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, SETTINGS_REG_PATH, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = sizeof(value);
        if (RegQueryValueExA(key, "StartMinimized", NULL, &type,
                             (LPBYTE)&value, &size) != ERROR_SUCCESS || type != REG_DWORD) {
            value = 0;
        }
        RegCloseKey(key);
    }
    g_start_minimized = value ? 1 : 0;
}

static void save_settings(void) {
    HKEY key = NULL;
    DWORD disp = 0;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, SETTINGS_REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &key, &disp) == ERROR_SUCCESS) {
        DWORD value = g_start_minimized ? 1 : 0;
        RegSetValueExA(key, "StartMinimized", 0, REG_DWORD,
                       (const BYTE *)&value, sizeof(value));
        RegCloseKey(key);
    }
}

static void clear_client_entry(ClientData *client) {
    client->count = 0;
    client->is_offline = 0;
    client->samples[0].client_id[0] = '\0';
    ZeroMemory(&client->last_addr, sizeof(client->last_addr));
}

static void clear_all_clients(HWND hwnd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i].lock_initialized) continue;
        EnterCriticalSection(&clients[i].lock);
        clear_client_entry(&clients[i]);
        LeaveCriticalSection(&clients[i].lock);
    }
    update_app_status(hwnd, 0);
}

static void clear_offline_clients(HWND hwnd) {
    time_t now = time(NULL);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].count == 0 || !clients[i].lock_initialized) continue;
        EnterCriticalSection(&clients[i].lock);
        if (clients[i].count > 0) {
            TelemetryPacket last = clients[i].samples[clients[i].count - 1];
            int age = (int)(now - (time_t)last.timestamp);
            if (age < 0) age = 0;
            if (age >= OFFLINE_SECS) {
                clear_client_entry(&clients[i]);
            }
        }
        LeaveCriticalSection(&clients[i].lock);
    }
    update_offline_state(hwnd);
}

static int append_text(char **buf, size_t *len, size_t *cap, const char *text) {
    size_t add = strlen(text);
    if (*len + add + 1 > *cap) {
        size_t new_cap = *cap ? *cap : 1024;
        while (*len + add + 1 > new_cap) new_cap *= 2;
        char *tmp = (char *)realloc(*buf, new_cap);
        if (!tmp) return 0;
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, text, add);
    *len += add;
    (*buf)[*len] = '\0';
    return 1;
}

static char *build_clients_snapshot(void) {
    size_t cap = 0;
    size_t len = 0;
    char *buf = NULL;

    char line[256];
    format_time(time(NULL), timestr, sizeof(timestr));
    _snprintf(line, sizeof(line), "          %s\r\n", timestr);
    if (!append_text(&buf, &len, &cap, line)) return NULL;

    _snprintf(line, sizeof(line), "%-32s %-15s %8s %8s %8s %8s %s\r\n",
              "Client", "IP", "Avg Load", "Avg Temp", "Avg Fan", "Avg MHz", "Seen");
    if (!append_text(&buf, &len, &cap, line)) {
        free(buf);
        return NULL;
    }

    int visible_clients = 0;
    time_t now = time(NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].count == 0 || !clients[i].lock_initialized) continue;

        TelemetryPacket last;
        struct sockaddr_in last_addr;
        float load = 0.0f, temp = 0.0f, fan = 0.0f, mhz = 0.0f;
        int n = 0;

        EnterCriticalSection(&clients[i].lock);
        n = clients[i].count;
        if (n > 0) {
            for (int j = 0; j < n; j++) {
                load += clients[i].samples[j].cpu_load;
                temp += clients[i].samples[j].cpu_temp;
                fan  += clients[i].samples[j].fan_speed;
                mhz  += clients[i].samples[j].cpu_mhz;
            }
            last = clients[i].samples[n - 1];
            last_addr = clients[i].last_addr;
        }
        LeaveCriticalSection(&clients[i].lock);

        if (n <= 0) continue;

        int age = (int)(now - (time_t)last.timestamp);
        if (age < 0) age = 0;
        char seen_time[64];
        format_time(last.timestamp, seen_time, sizeof(seen_time));
        const char *seen = (age < OFFLINE_SECS) ? seen_time + 11 : "offline";

        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &last_addr.sin_addr, ipstr, sizeof(ipstr));

        _snprintf(line, sizeof(line), "%-32s %-15s %7.2f%% %8.2f %8d %8.2f %s\r\n",
                  last.client_id, ipstr, load / n, temp / n,
                  (int)(fan / n), mhz / n, seen);
        if (!append_text(&buf, &len, &cap, line)) {
            free(buf);
            return NULL;
        }
        visible_clients++;
    }

    if (visible_clients == 0) {
        if (!append_text(&buf, &len, &cap, "No clients connected.\r\n")) {
            free(buf);
            return NULL;
        }
    }

    return buf;
}

static void copy_text_to_clipboard(HWND hwnd, const char *text) {
    if (!text) return;
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t len = strlen(text) + 1;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (mem) {
        void *ptr = GlobalLock(mem);
        if (ptr) {
            memcpy(ptr, text, len);
            GlobalUnlock(mem);
            SetClipboardData(CF_TEXT, mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

static void update_offline_state(HWND hwnd) {
    int offline_count = 0;
    time_t now = time(NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].count == 0) continue;

        int just_went_offline = 0;
        TelemetryPacket last;
        struct sockaddr_in last_addr;
        int age = 0;

        EnterCriticalSection(&clients[i].lock);
        int n = clients[i].count;
        last = clients[i].samples[n - 1];
        last_addr = clients[i].last_addr;

        age = (int)(now - (time_t)last.timestamp);
        if (age < 0) age = 0;

        int is_offline = (age >= OFFLINE_SECS);
        if (is_offline && !clients[i].is_offline) {
            clients[i].is_offline = 1;
            just_went_offline = 1;
        } else if (!is_offline && clients[i].is_offline) {
            clients[i].is_offline = 0;
        }
        LeaveCriticalSection(&clients[i].lock);

        if (is_offline) {
            offline_count++;
        }

        if (just_went_offline) {
            char ipstr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &last_addr.sin_addr, ipstr, sizeof(ipstr));
            show_offline_notification(hwnd, last.client_id, ipstr);
        }
    }

    update_app_status(hwnd, offline_count);
}

/* ---------- Receiver Thread ---------- */

DWORD WINAPI recv_thread(LPVOID arg) {
    TelemetryPacket pkt;
    struct sockaddr_in from;
    int fromlen = sizeof(from);
    SOCKET sock = *(SOCKET*)arg;

    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        int n = recvfrom(sock, (char*)&pkt, sizeof(pkt), 0,
                         (struct sockaddr*)&from, &fromlen);
        if (!InterlockedCompareExchange(&g_running, 1, 1))
            break;
        if (n == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAENOTSOCK || err == WSAESHUTDOWN)
                break;
            continue;
        }
        if (n <= 0) continue;

        ClientData *c = get_client(pkt.client_id);
        if (!c) continue;

        EnterCriticalSection(&c->lock);
        c->last_addr = from;
        if (c->count < MAX_SAMPLES)
            c->samples[c->count++] = pkt;
        else {
            memmove(&c->samples[0], &c->samples[1],
                    sizeof(TelemetryPacket) * (MAX_SAMPLES - 1));
            c->samples[MAX_SAMPLES - 1] = pkt;
        }
        LeaveCriticalSection(&c->lock);
    }
}

/* ---------- UI ---------- */

static void draw_text_line(HDC hdc, int x, int y, const char *text) {
    TextOutA(hdc, x, y, text, (int)strlen(text));
}

static void adjust_window_to_content(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    HFONT font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    HFONT old_font = (HFONT)SelectObject(hdc, font);

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    int line_height = tm.tmHeight + tm.tmExternalLeading;

    char header[256];
    _snprintf(header, sizeof(header), "%-32s %-15s %8s %8s %8s %8s %s",
              "Client", "IP", "Avg Load", "Avg Temp", "Avg Fan", "Avg MHz", "Seen");

    char sample[256];
    _snprintf(sample, sizeof(sample), "%-32s %-15s %7.2f%% %8.2f %8d %8.2f %s",
              "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",
              "255.255.255.255", 99.99f, 999.99f, 99999, 9999.99f,
              "00:00:00");

    SIZE sz_header = {0};
    SIZE sz_sample = {0};
    GetTextExtentPoint32A(hdc, header, (int)strlen(header), &sz_header);
    GetTextExtentPoint32A(hdc, sample, (int)strlen(sample), &sz_sample);

    int content_width = (sz_header.cx > sz_sample.cx) ? sz_header.cx : sz_sample.cx;
    int content_height = line_height * (UI_EXTRA_ROWS + MAX_CLIENTS);

    RECT rc = {0, 0, content_width + UI_PADDING_X * 2, content_height + UI_PADDING_Y * 2};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(hwnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOMOVE);

    SelectObject(hdc, old_font);
    ReleaseDC(hwnd, hdc);
}

static void render(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    HFONT font = (HFONT)GetStockObject(ANSI_FIXED_FONT);
    HFONT old_font = (HFONT)SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    int line_height = tm.tmHeight + tm.tmExternalLeading;

    int x = UI_PADDING_X;
    int y = UI_PADDING_Y;
    char line[256];

    format_time(time(NULL), timestr, sizeof(timestr));
    _snprintf(line, sizeof(line), "          %s", timestr);
    draw_text_line(hdc, x, y, line);
    y += line_height;

    _snprintf(line, sizeof(line), "%-32s %-15s %8s %8s %8s %8s %s",
              "Client", "IP", "Avg Load", "Avg Temp", "Avg Fan", "Avg MHz", "Seen");
    draw_text_line(hdc, x, y, line);
    y += line_height;

    int visible_clients = 0;
    time_t now = time(NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].count == 0) continue;

        int age = 0;

        EnterCriticalSection(&clients[i].lock);

        float load = 0.0f, temp = 0.0f, fan = 0.0f, mhz = 0.0f;
        for (int j = 0; j < clients[i].count; j++) {
            load += clients[i].samples[j].cpu_load;
            temp += clients[i].samples[j].cpu_temp;
            fan  += clients[i].samples[j].fan_speed;
            mhz  += clients[i].samples[j].cpu_mhz;
        }

        int n = clients[i].count;
        TelemetryPacket last = clients[i].samples[n - 1];
        struct sockaddr_in last_addr = clients[i].last_addr;

        age = (int)(now - (time_t)last.timestamp);
        if (age < 0) age = 0;

        LeaveCriticalSection(&clients[i].lock);

        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &last_addr.sin_addr, ipstr, sizeof(ipstr));

        format_time(last.timestamp, timestr, sizeof(timestr));
        const char *seen = (age < OFFLINE_SECS) ? timestr + 11 : "offline";

        _snprintf(line, sizeof(line), "%-32s %-15s %7.2f%% %8.2f %8d %8.2f %s",
                  last.client_id, ipstr, load / n, temp / n,
                  (int)(fan / n), mhz / n, seen);
        draw_text_line(hdc, x, y, line);
        y += line_height;
        visible_clients++;
    }

    if (visible_clients == 0) {
        draw_text_line(hdc, x, y, "No clients connected.");
    }

    SelectObject(hdc, old_font);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            init_notify_icon(hwnd);
            init_main_menu(hwnd);
            SetTimer(hwnd, UI_TIMER_ID, UI_TIMER_MS, NULL);
            adjust_window_to_content(hwnd);
            return 0;
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) {
                ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case MENU_TRAY_OPEN:
                    restore_window(hwnd);
                    return 0;
                case MENU_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    return 0;
                case MENU_EDIT_CLEAR_ALL:
                    clear_all_clients(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;
                case MENU_EDIT_CLEAR_OFFLINE:
                    clear_offline_clients(hwnd);
                    InvalidateRect(hwnd, NULL, TRUE);
                    return 0;
                case MENU_EDIT_SELECT_TEXT: {
                    char *snapshot = build_clients_snapshot();
                    copy_text_to_clipboard(hwnd, snapshot);
                    free(snapshot);
                    return 0;
                }
                case MENU_EDIT_PREFERENCES:
                    show_preferences_dialog(hwnd);
                    return 0;
                case MENU_EDIT_EXIT:
                    DestroyWindow(hwnd);
                    return 0;
            }
            break;
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) {
                restore_window(hwnd);
                return 0;
            }
            if (lParam == WM_RBUTTONUP) {
                show_tray_menu(hwnd);
                return 0;
            }
            break;
        case WM_TIMER:
            update_offline_state(hwnd);
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            render(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, UI_TIMER_ID);
            InterlockedExchange(&g_running, 0);
            if (g_sock != INVALID_SOCKET) {
                closesocket(g_sock);
                g_sock = INVALID_SOCKET;
            }
            if (g_instance_mutex) {
                CloseHandle(g_instance_mutex);
                g_instance_mutex = NULL;
            }
            if (g_notify_added) {
                Shell_NotifyIconA(NIM_DELETE, &g_notify);
                g_notify_added = 0;
            }
            if (g_recv_thread) {
                WaitForSingleObject(g_recv_thread, 2000);
                CloseHandle(g_recv_thread);
                g_recv_thread = NULL;
            }
            if (g_tray_menu) {
                DestroyMenu(g_tray_menu);
                g_tray_menu = NULL;
            }
            if (g_menu_bar) {
                DestroyMenu(g_menu_bar);
                g_menu_bar = NULL;
                g_menu_edit = NULL;
            }
            if (g_prefs_hwnd) {
                DestroyWindow(g_prefs_hwnd);
                g_prefs_hwnd = NULL;
            }
            if (g_app_icon_big_alert) {
                DestroyIcon(g_app_icon_big_alert);
                g_app_icon_big_alert = NULL;
            }
            if (g_app_icon_small_alert) {
                DestroyIcon(g_app_icon_small_alert);
                g_app_icon_small_alert = NULL;
            }
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i].lock_initialized) continue;
                DeleteCriticalSection(&clients[i].lock);
                clients[i].lock_initialized = 0;
            }
            WSACleanup();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ---------- Main ---------- */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    g_instance_mutex = CreateMutexA(NULL, FALSE, "PiMonServerSingleton");
    if (g_instance_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowA(WINDOW_CLASS, NULL);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(g_instance_mutex);
        g_instance_mutex = NULL;
        return 0;
    }

    load_settings();

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        MessageBoxA(NULL, "WSAStartup failed.", WINDOW_TITLE, MB_ICONERROR | MB_OK);
        return 1;
    }

    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock == INVALID_SOCKET) {
        MessageBoxA(NULL, "Socket creation failed.", WINDOW_TITLE, MB_ICONERROR | MB_OK);
        WSACleanup();
        return 1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        MessageBoxA(NULL, "Bind failed (is another server already running?).", WINDOW_TITLE, MB_ICONERROR | MB_OK);
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        WSACleanup();
        return 1;
    }

    int cx_big = GetSystemMetrics(SM_CXICON);
    int cy_big = GetSystemMetrics(SM_CYICON);
    int cx_small = GetSystemMetrics(SM_CXSMICON);
    int cy_small = GetSystemMetrics(SM_CYSMICON);

    g_app_icon_big = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APPICON),
                                       IMAGE_ICON, cx_big, cy_big, LR_SHARED);
    g_app_icon_small = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APPICON),
                                         IMAGE_ICON, cx_small, cy_small, LR_SHARED);
    if (!g_app_icon_big || !g_app_icon_small) {
        HICON file_big = (HICON)LoadImageA(NULL, "pimon.ico", IMAGE_ICON,
                                           cx_big, cy_big, LR_LOADFROMFILE);
        HICON file_small = (HICON)LoadImageA(NULL, "pimon.ico", IMAGE_ICON,
                                             cx_small, cy_small, LR_LOADFROMFILE);
        if (!g_app_icon_big) g_app_icon_big = file_big;
        if (!g_app_icon_small) g_app_icon_small = file_small;
    }
    if (!g_app_icon_big) {
        g_app_icon_big = LoadIcon(NULL, IDI_APPLICATION);
    }
    if (!g_app_icon_small) {
        g_app_icon_small = (HICON)LoadImageA(NULL, MAKEINTRESOURCEA(IDI_APPLICATION),
                                             IMAGE_ICON, cx_small, cy_small, LR_SHARED);
    }

    if (g_app_icon_big) {
        g_app_icon_big_alert = create_alert_icon(g_app_icon_big, cx_big);
    }
    if (g_app_icon_small) {
        g_app_icon_small_alert = create_alert_icon(g_app_icon_small, cx_small);
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = g_app_icon_big ? g_app_icon_big : LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Window class registration failed.", WINDOW_TITLE, MB_ICONERROR | MB_OK);
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        WSACleanup();
        return 1;
    }

    HWND hwnd = CreateWindowExA(
        0, WINDOW_CLASS, WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxA(NULL, "Window creation failed.", WINDOW_TITLE, MB_ICONERROR | MB_OK);
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        WSACleanup();
        return 1;
    }

    apply_system_titlebar_theme(hwnd);

    if (g_app_icon_big) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_app_icon_big);
    }
    if (g_app_icon_small) {
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_app_icon_small);
    }

    g_recv_thread = CreateThread(NULL, 0, recv_thread, &g_sock, 0, NULL);
    if (!g_recv_thread) {
        MessageBoxA(hwnd, "Receiver thread creation failed.", WINDOW_TITLE, MB_ICONERROR | MB_OK);
        DestroyWindow(hwnd);
        return 1;
    }

    ShowWindow(hwnd, g_start_minimized ? SW_MINIMIZE : nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
