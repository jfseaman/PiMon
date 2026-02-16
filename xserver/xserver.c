/*
 * PiMon X11 server.
 *
 * Listens on UDP port 5000, renders telemetry with Xlib, provides an Edit
 * menu, clipboard copy, and simple preferences persistence.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <limits.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#if DEBUG
#define DBG_PRINT(...)                  \
    do {                                \
        printf(__VA_ARGS__);            \
        fflush(stdout);                 \
    } while (0)
#else
#define DBG_PRINT(...) do {} while (0)
#endif

#define PORT            5000
#define MAX_LINE        1024
#define CLIENT_ID_LEN   32
#define MAX_CLIENTS     32
#define MAX_SAMPLES     2
#define OFFLINE_SECS    30
#define UI_TIMER_SECS   10
#define WINDOW_W        900
#define WINDOW_H        600

#define MENU_BAR_H      24
#define MENU_PAD_X      10
#define MENU_EDIT_W     44
#define MENU_DROP_W     180
#define MENU_ITEM_H     22

#define PREF_W          360
#define PREF_H          150

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
    struct sockaddr_in last_addr;
} ClientData;

typedef struct {
    int start_minimized;
} Preferences;

/* Global state ---------------------------------------------------------- */
static ClientData g_clients[MAX_CLIENTS];
static char g_latest_text[MAX_LINE] = "";
static pthread_mutex_t g_line_mtx = PTHREAD_MUTEX_INITIALIZER;
static int g_notify_fd = -1;

static int g_menu_open = 0;
static int g_menu_hover = -1;

static Window g_prefs_win = None;
static int g_prefs_checkbox = 0;

static char *g_clip_text = NULL;

static Preferences g_prefs = {0};

static const char *g_menu_items[] = {
    "Clear all",
    "Clear offline",
    "Select text",
    "Preferences",
    NULL,
    "Exit"
};

enum {
    MENU_CLEAR_ALL = 0,
    MENU_CLEAR_OFFLINE = 1,
    MENU_SELECT_TEXT = 2,
    MENU_PREFS = 3,
    MENU_SEPARATOR = 4,
    MENU_EXIT = 5,
    MENU_COUNT = 6
};

static void notify_main_thread(void)
{
    if (g_notify_fd < 0) return;

    {
        char b = 'u';
        ssize_t n = write(g_notify_fd, &b, 1);
        (void)n;
    }
}

static int get_config_path(char *buf, size_t buf_len)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return -1;
    if (snprintf(buf, buf_len, "%s/.PiMon/config.json", home) >= (int)buf_len) {
        return -1;
    }
    return 0;
}

static int ensure_config_dir(void)
{
    char dir_path[PATH_MAX];
    const char *home = getenv("HOME");

    if (!home || !home[0]) return -1;
    if (snprintf(dir_path, sizeof(dir_path), "%s/.PiMon", home) >= (int)sizeof(dir_path)) {
        return -1;
    }

    if (mkdir(dir_path, 0700) < 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static void load_preferences(void)
{
    char path[PATH_MAX];
    FILE *f;
    char json[1024];
    size_t n;
    char *p;

    g_prefs.start_minimized = 0;

    if (get_config_path(path, sizeof(path)) != 0) {
        return;
    }

    f = fopen(path, "r");
    if (!f) return;

    n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';

    p = strstr(json, "\"start_minimized\"");
    if (!p) return;
    p = strchr(p, ':');
    if (!p) return;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    if (strncmp(p, "true", 4) == 0 || strncmp(p, "1", 1) == 0) {
        g_prefs.start_minimized = 1;
    }
}

static void save_preferences(void)
{
    char path[PATH_MAX];
    FILE *f;

    if (ensure_config_dir() != 0) return;
    if (get_config_path(path, sizeof(path)) != 0) return;

    f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "{\n");
    fprintf(f, "  \"start_minimized\": %s\n", g_prefs.start_minimized ? "true" : "false");
    fprintf(f, "}\n");
    fclose(f);
}

static void format_time(uint64_t ts, char *buf, size_t len)
{
    time_t t = (time_t)ts;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm);
}

static ClientData *get_client(const char *id)
{
    int i;
    ClientData *empty = NULL;

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].count == 0 && empty == NULL) {
            empty = &g_clients[i];
        }
        if (g_clients[i].count > 0 &&
            strncmp(g_clients[i].samples[0].client_id, id, CLIENT_ID_LEN) == 0) {
            return &g_clients[i];
        }
    }

    if (empty) {
        memset(empty, 0, sizeof(*empty));
        strncpy(empty->samples[0].client_id, id, CLIENT_ID_LEN - 1);
        empty->samples[0].client_id[CLIENT_ID_LEN - 1] = '\0';
        return empty;
    }
    return NULL;
}

static void clear_client_entry(ClientData *c)
{
    memset(c, 0, sizeof(*c));
}

static void clear_all_clients(void)
{
    int i;
    pthread_mutex_lock(&g_line_mtx);
    for (i = 0; i < MAX_CLIENTS; i++) {
        clear_client_entry(&g_clients[i]);
    }
    g_latest_text[0] = '\0';
    pthread_mutex_unlock(&g_line_mtx);
}

static void clear_offline_clients(void)
{
    int i;
    time_t now = time(NULL);

    pthread_mutex_lock(&g_line_mtx);
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].count <= 0) continue;
        {
            TelemetryPacket last = g_clients[i].samples[g_clients[i].count - 1];
            int age = (int)(now - (time_t)last.timestamp);
            if (age < 0) age = 0;
            if (age >= OFFLINE_SECS) {
                clear_client_entry(&g_clients[i]);
            }
        }
    }
    pthread_mutex_unlock(&g_line_mtx);
}

static int append_text(char **buf, size_t *len, size_t *cap, const char *text)
{
    size_t add = strlen(text);
    char *tmp;
    if (*len + add + 1 > *cap) {
        size_t new_cap = *cap ? *cap : 1024;
        while (*len + add + 1 > new_cap) new_cap *= 2;
        tmp = (char *)realloc(*buf, new_cap);
        if (!tmp) return 0;
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, text, add);
    *len += add;
    (*buf)[*len] = '\0';
    return 1;
}

static char *build_clients_snapshot(void)
{
    ClientData clients[MAX_CLIENTS];
    char latest_text[MAX_LINE];
    char *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    char line[256];
    char ts[64];
    int i;
    int visible = 0;
    time_t now = time(NULL);

    pthread_mutex_lock(&g_line_mtx);
    memcpy(clients, g_clients, sizeof(clients));
    strncpy(latest_text, g_latest_text, sizeof(latest_text) - 1);
    latest_text[sizeof(latest_text) - 1] = '\0';
    pthread_mutex_unlock(&g_line_mtx);

    format_time((uint64_t)now, ts, sizeof(ts));
    snprintf(line, sizeof(line), "          %s\n", ts);
    if (!append_text(&buf, &len, &cap, line)) return NULL;

    snprintf(line, sizeof(line), "%-32s %-15s %8s %8s %8s %8s %s\n",
             "Client", "IP", "Avg Load", "Avg Temp", "Avg Fan", "Avg MHz", "Seen");
    if (!append_text(&buf, &len, &cap, line)) {
        free(buf);
        return NULL;
    }

    for (i = 0; i < MAX_CLIENTS; i++) {
        int j;
        int n = clients[i].count;
        float load = 0.0f;
        float temp = 0.0f;
        float fan = 0.0f;
        float mhz = 0.0f;
        TelemetryPacket last;
        int age;
        char ip[INET_ADDRSTRLEN];
        char seen_time[64];
        const char *seen;

        if (n <= 0) continue;
        for (j = 0; j < n; j++) {
            load += clients[i].samples[j].cpu_load;
            temp += clients[i].samples[j].cpu_temp;
            fan += clients[i].samples[j].fan_speed;
            mhz += clients[i].samples[j].cpu_mhz;
        }

        last = clients[i].samples[n - 1];
        age = (int)(now - (time_t)last.timestamp);
        if (age < 0) age = 0;
        format_time(last.timestamp, seen_time, sizeof(seen_time));
        seen = (age < OFFLINE_SECS) ? seen_time + 11 : "offline";
        inet_ntop(AF_INET, &clients[i].last_addr.sin_addr, ip, sizeof(ip));

        snprintf(line, sizeof(line), "%-32s %-15s %7.2f%% %8.2f %8d %8.2f %s\n",
                 last.client_id,
                 ip[0] ? ip : "0.0.0.0",
                 load / n,
                 temp / n,
                 (int)(fan / n),
                 mhz / n,
                 seen);
        if (!append_text(&buf, &len, &cap, line)) {
            free(buf);
            return NULL;
        }
        visible++;
    }

    if (visible == 0) {
        if (latest_text[0]) {
            if (!append_text(&buf, &len, &cap, latest_text) ||
                !append_text(&buf, &len, &cap, "\n")) {
                free(buf);
                return NULL;
            }
        } else {
            if (!append_text(&buf, &len, &cap, "No clients connected.\n")) {
                free(buf);
                return NULL;
            }
        }
    }
    return buf;
}

static void draw_text(Display *dpy, Window win, GC gc, int x, int y, const char *text)
{
    XDrawString(dpy, win, gc, x, y, text, (int)strlen(text));
}

static void draw_menu(Display *dpy, Window win, GC gc, int line_height)
{
    int i;
    unsigned long black = BlackPixel(dpy, DefaultScreen(dpy));
    unsigned long white = WhitePixel(dpy, DefaultScreen(dpy));

    XSetForeground(dpy, gc, white);
    XFillRectangle(dpy, win, gc, 0, 0, WINDOW_W, MENU_BAR_H);
    XSetForeground(dpy, gc, black);
    XDrawRectangle(dpy, win, gc, 0, 0, WINDOW_W - 1, MENU_BAR_H - 1);
    draw_text(dpy, win, gc, MENU_PAD_X, 16, "Edit");

    if (!g_menu_open) return;

    XSetForeground(dpy, gc, white);
    XFillRectangle(dpy, win, gc, MENU_PAD_X + 1, MENU_BAR_H + 1,
                   MENU_DROP_W - 1, MENU_ITEM_H * MENU_COUNT - 1);
    XSetForeground(dpy, gc, black);
    XDrawRectangle(dpy, win, gc, MENU_PAD_X, MENU_BAR_H,
                   MENU_DROP_W, MENU_ITEM_H * MENU_COUNT);
    for (i = 0; i < MENU_COUNT; i++) {
        int y = MENU_BAR_H + i * MENU_ITEM_H;
        if (i == MENU_SEPARATOR) {
            int line_y = y + (MENU_ITEM_H / 2);
            XDrawLine(dpy, win, gc,
                      MENU_PAD_X + 8, line_y,
                      MENU_PAD_X + MENU_DROP_W - 8, line_y);
            continue;
        }
        if (i == g_menu_hover) {
            XFillRectangle(dpy, win, gc, MENU_PAD_X + 1, y + 1,
                           MENU_DROP_W - 1, MENU_ITEM_H - 1);
            XSetForeground(dpy, gc, WhitePixel(dpy, DefaultScreen(dpy)));
            draw_text(dpy, win, gc, MENU_PAD_X + 8, y + (line_height - 2), g_menu_items[i]);
            XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
        } else {
            draw_text(dpy, win, gc, MENU_PAD_X + 8, y + (line_height - 2), g_menu_items[i]);
        }
    }
}

static void redraw_window(Display *dpy, Window win, GC gc, int line_height)
{
    ClientData clients[MAX_CLIENTS];
    char latest_text[MAX_LINE];
    char line[256];
    char timebuf[64];
    int i;
    int visible_clients = 0;
    time_t now;
    int x = 10;
    int y = MENU_BAR_H + 20;

    pthread_mutex_lock(&g_line_mtx);
    memcpy(clients, g_clients, sizeof(clients));
    strncpy(latest_text, g_latest_text, sizeof(latest_text) - 1);
    latest_text[sizeof(latest_text) - 1] = '\0';
    pthread_mutex_unlock(&g_line_mtx);

    now = time(NULL);
    format_time((uint64_t)now, timebuf, sizeof(timebuf));

    XClearWindow(dpy, win);

    snprintf(line, sizeof(line), "          %s", timebuf);
    draw_text(dpy, win, gc, x, y, line);
    y += line_height;

    snprintf(line, sizeof(line), "%-32s %-15s %8s %8s %8s %8s %s",
             "Client", "IP", "Avg Load", "Avg Temp", "Avg Fan", "Avg MHz", "Seen");
    draw_text(dpy, win, gc, x, y, line);
    y += line_height;

    for (i = 0; i < MAX_CLIENTS; i++) {
        int j;
        float load = 0.0f;
        float temp = 0.0f;
        float fan = 0.0f;
        float mhz = 0.0f;
        int n = clients[i].count;
        TelemetryPacket last;
        int age;
        char seen_time[64];
        const char *seen;
        char ip[INET_ADDRSTRLEN];

        if (n <= 0) continue;

        for (j = 0; j < n; j++) {
            load += clients[i].samples[j].cpu_load;
            temp += clients[i].samples[j].cpu_temp;
            fan += clients[i].samples[j].fan_speed;
            mhz += clients[i].samples[j].cpu_mhz;
        }

        last = clients[i].samples[n - 1];
        age = (int)(now - (time_t)last.timestamp);
        if (age < 0) age = 0;
        format_time(last.timestamp, seen_time, sizeof(seen_time));
        seen = (age < OFFLINE_SECS) ? seen_time + 11 : "offline";
        inet_ntop(AF_INET, &clients[i].last_addr.sin_addr, ip, sizeof(ip));

        snprintf(line, sizeof(line), "%-32s %-15s %7.2f%% %8.2f %8d %8.2f %s",
                 last.client_id,
                 ip[0] ? ip : "0.0.0.0",
                 load / n,
                 temp / n,
                 (int)(fan / n),
                 mhz / n,
                 seen);
        draw_text(dpy, win, gc, x, y, line);
        y += line_height;
        visible_clients++;
    }

    if (visible_clients == 0) {
        draw_text(dpy, win, gc, x, y,
                  latest_text[0] ? latest_text : "No clients connected.");
    }

    draw_menu(dpy, win, gc, line_height);

    XFlush(dpy);
}

static void redraw_prefs(Display *dpy, Window prefs, GC gc)
{
    char line[128];

    XClearWindow(dpy, prefs);
    draw_text(dpy, prefs, gc, 12, 24, "Preferences");

    XDrawRectangle(dpy, prefs, gc, 14, 38, 14, 14);
    if (g_prefs_checkbox) {
        XDrawLine(dpy, prefs, gc, 16, 45, 20, 50);
        XDrawLine(dpy, prefs, gc, 20, 50, 27, 40);
    }
    draw_text(dpy, prefs, gc, 36, 50, "Start minimized");

    XDrawRectangle(dpy, prefs, gc, 170, 98, 74, 28);
    draw_text(dpy, prefs, gc, 194, 116, "Save");

    XDrawRectangle(dpy, prefs, gc, 258, 98, 86, 28);
    draw_text(dpy, prefs, gc, 282, 116, "Cancel");

    snprintf(line, sizeof(line), "Config: ~/.PiMon/config.json");
    draw_text(dpy, prefs, gc, 12, 82, line);
    XFlush(dpy);
}

static void open_preferences_dialog(Display *dpy, int screen, Window parent, Atom wm_delete_window)
{
    int display_w = DisplayWidth(dpy, screen);
    int display_h = DisplayHeight(dpy, screen);
    int x = (display_w - PREF_W) / 2;
    int y = (display_h - PREF_H) / 2;

    if (x < 0) x = 0;
    if (y < 0) y = 0;

    if (g_prefs_win != None) {
        XRaiseWindow(dpy, g_prefs_win);
        return;
    }

    g_prefs_checkbox = g_prefs.start_minimized;
    g_prefs_win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                                      x, y, PREF_W, PREF_H, 1,
                                      BlackPixel(dpy, screen),
                                      WhitePixel(dpy, screen));
    XStoreName(dpy, g_prefs_win, "Preferences");
    XSetTransientForHint(dpy, g_prefs_win, parent);
    XSelectInput(dpy, g_prefs_win,
                 ExposureMask | ButtonPressMask | StructureNotifyMask | KeyPressMask);
    XSetWMProtocols(dpy, g_prefs_win, &wm_delete_window, 1);
    XMapWindow(dpy, g_prefs_win);
}

static void close_preferences_dialog(Display *dpy)
{
    if (g_prefs_win != None) {
        XDestroyWindow(dpy, g_prefs_win);
        g_prefs_win = None;
    }
}

static int menu_hit_item(int x, int y)
{
    int rel;
    if (x < MENU_PAD_X || x > (MENU_PAD_X + MENU_DROP_W)) return -1;
    if (y < MENU_BAR_H || y > (MENU_BAR_H + MENU_ITEM_H * MENU_COUNT)) return -1;
    rel = (y - MENU_BAR_H) / MENU_ITEM_H;
    if (rel < 0 || rel >= MENU_COUNT) return -1;
    if (rel == MENU_SEPARATOR) return -1;
    return rel;
}

static int menu_hit_edit(int x, int y)
{
    return (y >= 0 && y <= MENU_BAR_H && x >= MENU_PAD_X && x <= (MENU_PAD_X + MENU_EDIT_W));
}

static void handle_selection_request(Display *dpy,
                                     XSelectionRequestEvent *req,
                                     Atom atom_clipboard,
                                     Atom atom_targets,
                                     Atom atom_utf8)
{
    XEvent resp;
    Atom property = req->property ? req->property : req->target;

    memset(&resp, 0, sizeof(resp));
    resp.xselection.type = SelectionNotify;
    resp.xselection.display = req->display;
    resp.xselection.requestor = req->requestor;
    resp.xselection.selection = req->selection;
    resp.xselection.target = req->target;
    resp.xselection.time = req->time;
    resp.xselection.property = None;

    if (!g_clip_text) {
        XSendEvent(dpy, req->requestor, False, 0, &resp);
        return;
    }

    if (req->target == atom_targets) {
        Atom targets[3];
        int count = 0;
        targets[count++] = XA_STRING;
        targets[count++] = atom_utf8;
        targets[count++] = atom_targets;
        XChangeProperty(dpy, req->requestor, property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)targets, count);
        resp.xselection.property = property;
    } else if (req->target == XA_STRING || req->target == atom_utf8) {
        Atom type = (req->target == atom_utf8) ? atom_utf8 : XA_STRING;
        XChangeProperty(dpy, req->requestor, property, type, 8,
                        PropModeReplace,
                        (const unsigned char *)g_clip_text,
                        (int)strlen(g_clip_text));
        resp.xselection.property = property;
    }

    XSendEvent(dpy, req->requestor, False, 0, &resp);
    XFlush(dpy);
    (void)atom_clipboard;
}

static void set_clipboard_text(Display *dpy, Window owner, const char *text, Atom atom_clipboard)
{
    free(g_clip_text);
    g_clip_text = NULL;

    if (!text) return;
    g_clip_text = strdup(text);
    if (!g_clip_text) return;

    XSetSelectionOwner(dpy, XA_PRIMARY, owner, CurrentTime);
    XSetSelectionOwner(dpy, atom_clipboard, owner, CurrentTime);
    XFlush(dpy);
}

static void handle_menu_action(Display *dpy,
                               Window win,
                               GC gc,
                               int line_height,
                               int action,
                               Atom atom_clipboard,
                               int screen,
                               Atom wm_delete_window)
{
    if (action == MENU_CLEAR_ALL) {
        clear_all_clients();
    } else if (action == MENU_CLEAR_OFFLINE) {
        clear_offline_clients();
    } else if (action == MENU_SELECT_TEXT) {
        char *snapshot = build_clients_snapshot();
        if (snapshot) {
            set_clipboard_text(dpy, win, snapshot, atom_clipboard);
            free(snapshot);
        }
    } else if (action == MENU_PREFS) {
        open_preferences_dialog(dpy, screen, win, wm_delete_window);
    } else if (action == MENU_EXIT) {
        XEvent close_event;
        memset(&close_event, 0, sizeof(close_event));
        close_event.type = ClientMessage;
        close_event.xclient.window = win;
        close_event.xclient.format = 32;
        close_event.xclient.data.l[0] = wm_delete_window;
        XPutBackEvent(dpy, &close_event);
        return;
    }

    notify_main_thread();
    redraw_window(dpy, win, gc, line_height);
}

static void *udp_receiver(void *arg)
{
    int sock;
    struct sockaddr_in bind_addr;
    unsigned char buf[MAX_LINE];
    (void)arg;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return NULL;
    }

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(PORT);

    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        perror("bind");
        close(sock);
        return NULL;
    }

    while (1) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from_addr, &from_len);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }

        if (n == (ssize_t)sizeof(TelemetryPacket)) {
            TelemetryPacket pkt;
            ClientData *c;

            memcpy(&pkt, buf, sizeof(pkt));
            pkt.client_id[CLIENT_ID_LEN - 1] = '\0';

            pthread_mutex_lock(&g_line_mtx);
            c = get_client(pkt.client_id);
            if (c) {
                c->last_addr = from_addr;
                if (c->count < MAX_SAMPLES) {
                    c->samples[c->count++] = pkt;
                } else {
                    memmove(&c->samples[0], &c->samples[1],
                            sizeof(TelemetryPacket) * (MAX_SAMPLES - 1));
                    c->samples[MAX_SAMPLES - 1] = pkt;
                }
            }
            g_latest_text[0] = '\0';
            pthread_mutex_unlock(&g_line_mtx);
        } else {
            size_t copy_len = (size_t)n;
            if (copy_len >= sizeof(g_latest_text)) {
                copy_len = sizeof(g_latest_text) - 1;
            }

            pthread_mutex_lock(&g_line_mtx);
            memcpy(g_latest_text, buf, copy_len);
            g_latest_text[copy_len] = '\0';
            pthread_mutex_unlock(&g_line_mtx);
        }

        notify_main_thread();
    }

    close(sock);
    return NULL;
}

int main(void)
{
    Display *dpy;
    Window win;
    XEvent ev;
    const char *win_title = "PiMon XServer - Health Monitor";
    int screen;
    int display_w;
    int display_h;
    int win_x;
    int win_y;
    GC gc;
    XFontStruct *font_info = NULL;
    int line_height = 18;
    pthread_t thr;
    int notify_pipe[2] = { -1, -1 };
    Atom wm_delete_window;
    Atom atom_clipboard;
    Atom atom_targets;
    Atom atom_utf8;
    XSizeHints size_hints;

    load_preferences();
    DBG_PRINT("Starting X health monitor server...\n");

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Cannot open X display.\n");
        return 1;
    }

    DBG_PRINT("X display opened.\n");
    screen = DefaultScreen(dpy);
    display_w = DisplayWidth(dpy, screen);
    display_h = DisplayHeight(dpy, screen);
    win_x = (display_w - WINDOW_W) / 2;
    win_y = (display_h - WINDOW_H) / 2;
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;
    DBG_PRINT("Display size: %dx%d, window pos: x=%d y=%d\n",
              display_w, display_h, win_x, win_y);

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                              win_x, win_y, WINDOW_W, WINDOW_H, 2,
                              BlackPixel(dpy, screen),
                              WhitePixel(dpy, screen));
    DBG_PRINT("Window created.\n");

    XStoreName(dpy, win, win_title);
    XSetIconName(dpy, win, win_title);

    memset(&size_hints, 0, sizeof(size_hints));
    size_hints.flags = PPosition | PSize;
    size_hints.x = win_x;
    size_hints.y = win_y;
    size_hints.width = WINDOW_W;
    size_hints.height = WINDOW_H;
    XSetWMNormalHints(dpy, win, &size_hints);

    gc = XCreateGC(dpy, win, 0, NULL);
    XSetForeground(dpy, gc, BlackPixel(dpy, screen));
    DBG_PRINT("GC created.\n");

    font_info = XLoadQueryFont(dpy, "fixed");
    if (font_info) {
        XSetFont(dpy, gc, font_info->fid);
        line_height = font_info->ascent + font_info->descent + 2;
    }

    if (pipe(notify_pipe) < 0) {
        perror("pipe");
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 1;
    }

    if (fcntl(notify_pipe[0], F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl(read-end)");
    }
    if (fcntl(notify_pipe[1], F_SETFL, O_NONBLOCK) < 0) {
        perror("fcntl(write-end)");
    }

    g_notify_fd = notify_pipe[1];

    atom_clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    atom_targets = XInternAtom(dpy, "TARGETS", False);
    atom_utf8 = XInternAtom(dpy, "UTF8_STRING", False);

    XSelectInput(dpy, win,
                 ExposureMask | KeyPressMask | ButtonPressMask |
                 StructureNotifyMask);

    wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete_window, 1);

    XMapWindow(dpy, win);
    XMoveWindow(dpy, win, win_x, win_y);
    if (g_prefs.start_minimized) {
        XIconifyWindow(dpy, win, screen);
    }
    XFlush(dpy);

    DBG_PRINT("Window mapped and visible.\n");

    if (pthread_create(&thr, NULL, udp_receiver, NULL) != 0) {
        perror("pthread_create");
        close(notify_pipe[0]);
        close(notify_pipe[1]);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 1;
    }

    DBG_PRINT("Receiver thread started.\n");
    redraw_window(dpy, win, gc, line_height);

    while (1) {
        fd_set rfds;
        int xfd = ConnectionNumber(dpy);
        int maxfd;
        int sel;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(xfd, &rfds);
        FD_SET(notify_pipe[0], &rfds);
        maxfd = (xfd > notify_pipe[0]) ? xfd : notify_pipe[0];
        tv.tv_sec = UI_TIMER_SECS;
        tv.tv_usec = 0;

        sel = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        if (sel == 0) {
            redraw_window(dpy, win, gc, line_height);
            if (g_prefs_win != None) redraw_prefs(dpy, g_prefs_win, gc);
            continue;
        }

        if (FD_ISSET(notify_pipe[0], &rfds)) {
            char drain[64];
            while (read(notify_pipe[0], drain, sizeof(drain)) > 0) {
                /* drain bytes */
            }
            redraw_window(dpy, win, gc, line_height);
        }

        if (FD_ISSET(xfd, &rfds)) {
            while (XPending(dpy) > 0) {
                XNextEvent(dpy, &ev);

                if (ev.type == SelectionRequest) {
                    handle_selection_request(dpy, &ev.xselectionrequest,
                                             atom_clipboard, atom_targets, atom_utf8);
                    continue;
                }

                if (ev.xany.window == g_prefs_win) {
                    if (ev.type == Expose) {
                        if (ev.xexpose.count == 0) redraw_prefs(dpy, g_prefs_win, gc);
                    } else if (ev.type == ClientMessage) {
                        if ((Atom)ev.xclient.data.l[0] == wm_delete_window) {
                            close_preferences_dialog(dpy);
                        }
                    } else if (ev.type == ButtonPress) {
                        int px = ev.xbutton.x;
                        int py = ev.xbutton.y;
                        if (px >= 14 && px <= 28 && py >= 38 && py <= 52) {
                            g_prefs_checkbox = !g_prefs_checkbox;
                            redraw_prefs(dpy, g_prefs_win, gc);
                        } else if (px >= 170 && px <= 244 && py >= 98 && py <= 126) {
                            g_prefs.start_minimized = g_prefs_checkbox ? 1 : 0;
                            save_preferences();
                            close_preferences_dialog(dpy);
                        } else if (px >= 258 && px <= 344 && py >= 98 && py <= 126) {
                            close_preferences_dialog(dpy);
                        }
                    } else if (ev.type == KeyPress) {
                        char keybuf[8];
                        KeySym keysym;
                        int n = XLookupString(&ev.xkey, keybuf, (int)sizeof(keybuf), &keysym, NULL);
                        if (n > 0 && (keybuf[0] == 'q' || keybuf[0] == 'Q')) {
                            close_preferences_dialog(dpy);
                        }
                    }
                    continue;
                }

                DBG_PRINT("Event received: %d\n", (int)ev.type);

                if (ev.type == Expose) {
                    if (ev.xexpose.count == 0) {
                        redraw_window(dpy, win, gc, line_height);
                    }
                } else if (ev.type == MapNotify) {
                    XMoveWindow(dpy, win, win_x, win_y);
                    XFlush(dpy);
                    redraw_window(dpy, win, gc, line_height);
                } else if (ev.type == ConfigureNotify) {
                    DBG_PRINT("ConfigureNotify: actual x=%d y=%d w=%d h=%d\n",
                              ev.xconfigure.x,
                              ev.xconfigure.y,
                              ev.xconfigure.width,
                              ev.xconfigure.height);
                } else if (ev.type == ButtonPress) {
                    int x = ev.xbutton.x;
                    int y = ev.xbutton.y;
                    int item = -1;

                    if (menu_hit_edit(x, y)) {
                        g_menu_open = !g_menu_open;
                        g_menu_hover = -1;
                        redraw_window(dpy, win, gc, line_height);
                    } else if (g_menu_open && (item = menu_hit_item(x, y)) >= 0) {
                        g_menu_open = 0;
                        g_menu_hover = -1;
                        handle_menu_action(dpy, win, gc, line_height, item,
                                           atom_clipboard, screen, wm_delete_window);
                    } else {
                        if (g_menu_open) {
                            g_menu_open = 0;
                            g_menu_hover = -1;
                            redraw_window(dpy, win, gc, line_height);
                        }
                    }
                } else if (ev.type == ClientMessage) {
                    if ((Atom)ev.xclient.data.l[0] == wm_delete_window) {
                        goto done;
                    }
                } else if (ev.type == KeyPress) {
                    char keybuf[8];
                    KeySym keysym;
                    int n = XLookupString(&ev.xkey, keybuf, (int)sizeof(keybuf), &keysym, NULL);
                    if (n > 0 && (keybuf[0] == 'q' || keybuf[0] == 'Q')) {
                        goto done;
                    }
                }
            }
        }
    }

done:
    g_notify_fd = -1;
    close(notify_pipe[0]);
    close(notify_pipe[1]);

    free(g_clip_text);
    g_clip_text = NULL;

    if (g_prefs_win != None) {
        XDestroyWindow(dpy, g_prefs_win);
        g_prefs_win = None;
    }

    if (font_info) {
        XFreeFont(dpy, font_info);
    }
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);

    pthread_cancel(thr);
    pthread_join(thr, NULL);
    pthread_mutex_destroy(&g_line_mtx);
    return 0;
}
