#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <stdbool.h>
#include <syslog.h>
#include <glob.h> // Required for wildcard matching

#define SERVER_PORT 5000
#define CLIENT_ID_LEN 32
static const char *SERVER_ENV = "PIMON_SERVER_IP";

// #define CLIENT_DIAGNOSTICS

#ifdef CLIENT_DIAGNOSTICS
#define DIAG_PRINT(fmt, ...) \
    do { printf(fmt, ##__VA_ARGS__); } while (0)
#else
#define DIAG_PRINT(fmt, ...) do {} while (0)
#endif

typedef struct {
    char     client_id[CLIENT_ID_LEN];
    float    cpu_load;
    float    cpu_temp;
    float    fan_speed;
    float    cpu_mhz;
    uint64_t timestamp;
} TelemetryPacket;

#define FAN_GLOB "/sys/devices/platform/cooling_fan/hwmon/*/fan1_input"
char fan_file[PATH_MAX];
bool argon40_fan=false;
uint64_t argon40_period=0;

/* ---------- Helpers ---------- */

void print_packet(const TelemetryPacket *p) {
    DIAG_PRINT("---- Telemetry Packet ----\n");
    DIAG_PRINT(" Client ID : %s\n", p->client_id);
    DIAG_PRINT(" Timestamp : %llu\n",
               (unsigned long long)p->timestamp);
    DIAG_PRINT(" CPU Load  : %.1f %%\n", p->cpu_load);
    DIAG_PRINT(" CPU Temp  : %.1f C\n", p->cpu_temp);
    DIAG_PRINT(" CPU Speed : %.1f MHz\n", p->cpu_mhz);
    DIAG_PRINT(" Fan Speed : %.1f\n", p->fan_speed);
    DIAG_PRINT("--------------------------\n\n");
}

float read_cpu_temp() {
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return -1;
    int temp;
    fscanf(f, "%d", &temp);
    fclose(f);
    return temp / 1000.0f;
}

float read_cpu_mhz() {
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
    if (!f) return -1;
    int khz;
    fscanf(f, "%d", &khz);
    fclose(f);
    return khz / 1000.0f;
}

float read_cpu_load() {
    static long prev_idle = 0, prev_total = 0;
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;

    long user, nice, system, idle, iowait, irq, softirq;
    fscanf(f, "cpu %ld %ld %ld %ld %ld %ld %ld",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    fclose(f);

    long total = user + nice + system + idle + iowait + irq + softirq;
    long totald = total - prev_total;
    long idled  = idle  - prev_idle;

    prev_total = total;
    prev_idle  = idle;

    if (totald == 0) return 0;
    return (1.0f - (float)idled / totald) * 100.0f;
}

// Get the fan file name
// If this is a Pi 5 with pwm_fan module running we will be reading the fan speed from the sysfs
// If this is not a Pi 5, look for the pwm files updated by the pwm_fan_control2 service
// If found, we can assume it is an Argon40 mini fan. MAx speed is 8400 rpm but that is not stored
// We can extimate the fan speed by:
//     ( /sys/class/pwm/pwmchip0/pwm0/duty_cycle divided by /sys/class/pwm/pwmchip0/pwm0/period ) * 8400
//
void get_fan_file(void)
{
    glob_t globbuf;
    int ret = glob(FAN_GLOB, 0, NULL, &globbuf);

    if (ret != 0 || globbuf.gl_pathc == 0) {
        // see if ther is a pwm0 setup
        FILE *f;
        f = fopen("/sys/class/pwm/pwmchip0/pwm0/period", "r");
        if(f) {
            argon40_fan = true;
            fscanf(f, "%lu", &argon40_period);
            fclose(f);
            strcpy(fan_file, "/sys/class/pwm/pwmchip0/pwm0/duty_cycle");
            fan_file[sizeof("/sys/class/pwm/pwmchip0/pwm0/duty_cycle")] = '\0';
        }
    } else {
        // Use the first fan found
        strncpy(fan_file, globbuf.gl_pathv[0], sizeof(fan_file) - 1);
        fan_file[sizeof(fan_file) - 1] = '\0';
    }
    globfree(&globbuf);
    return;
}


// Function to read fan speed from sysfs
float read_fan_speed() {
    if(!fan_file[0]) return 0.0f; // The fan file was not found in get_fan_file()
    FILE *f;
    uint64_t fan_speed_rpm = -1;
    f = fopen(fan_file, "r");
    if (!f) return 0.0f;
    fscanf(f, "%lu", &fan_speed_rpm);
    fclose(f);
    if(argon40_fan)
        fan_speed_rpm = ((float) fan_speed_rpm / (float) argon40_period) * 8400.0;
    return (float) fan_speed_rpm;
}

/* ---------- Main ---------- */

int main(int argc, char **argv) {
    DIAG_PRINT("Starting client UDP broadcaster\n");

    const char *server_ip = NULL;
    if (argc > 1 && argv[1][0] != '\0') {
        server_ip = argv[1];
    } else {
        server_ip = getenv(SERVER_ENV);
    }
    if (!server_ip || server_ip[0] == '\0') {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        fprintf(stderr, "Or set %s in the environment.\n", SERVER_ENV);
        return 1;
    }

    openlog("PiMon_Client", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_ERR,"Server: %s", server_ip);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct addrinfo hints = {0};
    struct addrinfo *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", SERVER_PORT);
    if (getaddrinfo(server_ip, port_str, &hints, &res) != 0 || !res) {
        syslog(LOG_ERR, "Unable to resolve server: %s", server_ip);
        closelog();
        return 1;
    }

    struct sockaddr_in server = {0};
    memcpy(&server, res->ai_addr, sizeof(server));
    char resolved_ip[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &server.sin_addr, resolved_ip, sizeof(resolved_ip))) {
        syslog(LOG_INFO, "Server resolved to %s", resolved_ip);
    } else {
        syslog(LOG_INFO, "Server resolved (address conversion failed)");
    }
    freeaddrinfo(res);

    TelemetryPacket pkt = {0};
    gethostname(pkt.client_id, CLIENT_ID_LEN);
    get_fan_file();

    syslog(LOG_ERR,"Entering main loop");

    while (1) {
        pkt.cpu_load  = read_cpu_load();
        pkt.cpu_temp  = read_cpu_temp();
        pkt.cpu_mhz   = read_cpu_mhz();
        pkt.fan_speed = read_fan_speed();
        pkt.timestamp = time(NULL);

        print_packet(&pkt);

        sendto(sock, &pkt, sizeof(pkt), 0,
               (struct sockaddr*)&server, sizeof(server));

        sleep(1);
    }
    closelog();
    return 0;
}
