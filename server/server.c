#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
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

// Clear the console window using Windows API
HANDLE hConsole;
COORD coordScreen = {0, 0};
DWORD cCharsWritten;
CONSOLE_SCREEN_BUFFER_INFO csbi;
// DWORD dwConSize;

/* ---------- Helpers ---------- */

void clearScreen() {
    // Fill the entire screen with spaces
    // FillConsoleOutputCharacter(hConsole, (TCHAR) ' ', dwConSize, coordScreen, &cCharsWritten);
    // Move the cursor back to the top left for the next display
    SetConsoleCursorPosition(hConsole, coordScreen);
}

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
    SOCKET sock = *(SOCKET*)arg;
    TelemetryPacket pkt;
    struct sockaddr_in from;
    int fromlen = sizeof(from);

    // printf("Beginning receive thread\n");

    while (1) {
        // printf("Listening\n");
        int n = recvfrom(sock, (char*)&pkt, sizeof(pkt), 0,
                         (struct sockaddr*)&from, &fromlen);
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

/* ---------- Averaging Thread ---------- */

DWORD WINAPI avg_thread(LPVOID arg) {
    while (1) {
        // system("cls");
        clearScreen();
        format_time(time(NULL), timestr, sizeof(timestr));
        // printf("---- Telemetry ---- %s\n", timestr+11);  // Yeah, it's bad form to add an absolute offset to skip part of the date but...
        printf("          %s\n", timestr);

        printf("%-32s ", "Client");
        printf("%-15s ", "IP");
        printf("%8s ", "Avg Load");
        printf("%8s ","Avg Temp");
        printf("%8s ","Avg Fan");
        printf("%8s ","Avg MHz");
        printf("%s\n", "Seen");

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].count == 0) continue;

            EnterCriticalSection(&clients[i].lock);

            float load=0, temp=0, fan=0, mhz=0;
            for (int j = 0; j < clients[i].count; j++) {
                load += clients[i].samples[j].cpu_load;
                temp += clients[i].samples[j].cpu_temp;
                fan  += clients[i].samples[j].fan_speed;
                mhz  += clients[i].samples[j].cpu_mhz;
            }

            int n = clients[i].count;
            TelemetryPacket *last =
                &clients[i].samples[n - 1];

            format_time(last->timestamp, timestr, sizeof(timestr));

            char ipstr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clients[i].last_addr.sin_addr, ipstr, sizeof(ipstr));

            time_t now = time(NULL);
            int age = (int)(now - last->timestamp);

            printf("%-32s ", last->client_id);
            printf("%-15s ", ipstr);
            printf("%7.2f%% ", load/n);
            printf("%8.2f ", temp/n);
            printf("%8d ", (int) fan/n);
            printf("%8.2f ", mhz/n);
            if (30 > age) 
                printf("%s\n", timestr+11);
            else
                printf("offline\n");

            LeaveCriticalSection(&clients[i].lock);
        }
        Sleep(5000);
    }
}

BOOL WINAPI ConsoleHandler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_CLOSE_EVENT) {
        // Perform cleanup here
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].count == 0) continue;
            DeleteCriticalSection(&clients[i].lock);
        }        
        return TRUE; // Indicates the event was handled
    }
    return FALSE;
}

/* ---------- Main ---------- */

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
    // Get the number of character cells in the current buffer
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    // dwConSize = csbi.dwSize.X * csbi.dwSize.Y;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(sock, (struct sockaddr*)&addr, sizeof(addr));

    CreateThread(NULL, 0, recv_thread, &sock, 0, NULL);
    CreateThread(NULL, 0, avg_thread, NULL, 0, NULL);

    SetConsoleTitle(TEXT("PiMon Server - monitoring telemetry data"));
    Sleep(INFINITE);
}
