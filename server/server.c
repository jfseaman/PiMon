#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_PORT 5000
#define MAX_CLIENTS 32
#define MAX_SAMPLES 2
#define CLIENT_ID_LEN 32

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
    CRITICAL_SECTION lock;
} ClientData;


ClientData clients[MAX_CLIENTS] = {0};

char timestr[64];

static SOCKET g_sock = INVALID_SOCKET;
static HANDLE g_recv_thread = NULL;
static volatile LONG g_running = 1;

static const char *WINDOW_CLASS = "PiMonServerWindow";
static const char *WINDOW_TITLE = "PiMon Server - monitoring telemetry data";
static const UINT_PTR UI_TIMER_ID = 1;
static const UINT UI_TIMER_MS = 5000;
static const int UI_PADDING_X = 10;
static const int UI_PADDING_Y = 10;
static const int UI_EXTRA_ROWS = 4;

void format_time(uint64_t ts, char *buf, size_t len) {
    time_t t = (time_t)ts;
    struct tm tm_info;
    localtime_s(&tm_info, &t);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
}

ClientData* get_client(const char *id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].count == 0) {
            InitializeCriticalSection(&clients[i].lock);
            strncpy(clients[i].samples[0].client_id, id, CLIENT_ID_LEN);
            return &clients[i];
        }
        if (strcmp(clients[i].samples[0].client_id, id) == 0)
            return &clients[i];
    }
    return NULL;
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

        LeaveCriticalSection(&clients[i].lock);

        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &last_addr.sin_addr, ipstr, sizeof(ipstr));

        format_time(last.timestamp, timestr, sizeof(timestr));
        int age = (int)(now - (time_t)last.timestamp);
        const char *seen = (age < 30) ? timestr + 11 : "offline";

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
            SetTimer(hwnd, UI_TIMER_ID, UI_TIMER_MS, NULL);
            adjust_window_to_content(hwnd);
            return 0;
        case WM_TIMER:
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
            if (g_recv_thread) {
                WaitForSingleObject(g_recv_thread, 2000);
                CloseHandle(g_recv_thread);
                g_recv_thread = NULL;
            }
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].count == 0) continue;
                DeleteCriticalSection(&clients[i].lock);
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

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

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

    g_recv_thread = CreateThread(NULL, 0, recv_thread, &g_sock, 0, NULL);
    if (!g_recv_thread) {
        MessageBoxA(hwnd, "Receiver thread creation failed.", WINDOW_TITLE, MB_ICONERROR | MB_OK);
        DestroyWindow(hwnd);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
