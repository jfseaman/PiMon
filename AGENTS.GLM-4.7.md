# PiMon Development Guide

## Project Overview

PiMon is a Raspberry Pi monitoring system with three components:
- **client**: Raspberry Pi client that sends telemetry data via UDP
- **server**: Windows desktop application that receives and displays telemetry
- **xserver**: Linux X11-based server for telemetry visualization

## Build Commands

### Client (Linux)

```bash
cd client
make          # Build with debug symbols (-g -O2 -Wall)
make clean    # Remove build artifacts
make strip    # Strip binary for production
make install  # Install with systemd service
make uninstall
make start
make stop
make status
make restart
make update
```

### Server (Windows)

```batch
cd server
compile.bat   # Build with MSVC cl (creates PiMon_Server.exe)
setenv.bat    # Set environment variables
```

### XServer (Linux)

```bash
cd xserver
make          # Build debug version (-O0 -g -DDEBUG=1)
make release  # Build release version (-O2)
make debug    # Build debug version
make clean    # Remove generated files
```

### Running a Single Test

No test framework is currently configured. To add tests, create a `tests/` directory with `Makefile`:

```bash
# Example test setup
cd tests
make test  # Run all tests
make test-server  # Run server-specific tests
make test-client  # Run client-specific tests
```

## Code Style Guidelines

### Naming Conventions

- **Functions**: `snake_case` (e.g., `get_client`, `clear_client_entry`)
- **Macros**: `UPPER_CASE` (e.g., `SERVER_PORT`, `MAX_CLIENTS`)
- **Local Variables**: `camelCase` or `snake_case`
- **Global/Static Variables**: `g_` prefix (e.g., `g_clients`, `g_running`)
- **Structures**: `PascalCase` (e.g., `TelemetryPacket`, `ClientData`)
- **Constants**: `UPPER_CASE` (e.g., `WINDOW_W`, `MAX_LINE`)

### File Organization

```
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
#include <glob.h>
```

Headers should be grouped by category:
1. Standard library headers
2. Platform-specific headers
3. Project-specific headers

### Indentation and Spacing

- Use 8-space indentation
- No tabs in source files
- Function arguments separated by spaces

### Comments and Documentation

- Add file-level header comments describing purpose
- Use section dividers for code organization (e.g., `/* ---------- Helpers ---------- */`)
- Comment complex logic with `//` or `/* */`
- Document function behavior with descriptive names and inline comments

### Error Handling

- Return `0` on success, `-1` on error
- Check return values after system calls
- Use `perror()` or `strerror()` for error messages
- Log errors using `syslog()` where appropriate
- Validate input parameters

### Memory Management

- Use `malloc()`/`calloc()` for allocation, `free()` for deallocation
- Check for NULL after allocation
- Use `strncpy()` with explicit length, null-terminate manually
- For string buffers, pre-allocate sufficient space

### Platform-Specific Code

- Use conditional compilation for platform differences
- Define macros for cross-platform compatibility
- Example:
  ```c
  #define _CRT_SECURE_NO_WARNINGS  // Windows MSVC
  #pragma comment(lib, "ws2_32.lib")  // Linking
  ```

### Struct Definitions

```c
typedef struct {
    char     client_id[CLIENT_ID_LEN];
    float    cpu_load;
    float    cpu_temp;
    float    fan_speed;
    float    cpu_mhz;
    uint64_t timestamp;
} TelemetryPacket;
```

### Macro Usage

```c
#define SERVER_PORT 5000
#define CLIENT_ID_LEN 32
#define RESOLVE_INTERVAL_SEC 60
static const char *SERVER_ENV = "PIMON_SERVER_IP";

// Conditional compilation
#ifdef CLIENT_DIAGNOSTICS
#define DIAG_PRINT(fmt, ...) \
    do { printf(fmt, ##__VA_ARGS__); } while (0)
#else
#define DIAG_PRINT(fmt, ...) do {} while (0)
#endif
```

### Code Organization

Organize code sections with clear separators:

```c
/* ---------- Helpers ---------- */

/* ---------- Main ---------- */
```

### Constants and Magic Numbers

Define constants instead of using magic numbers:

```c
#define PORT            5000
#define MAX_LINE        1024
#define CLIENT_ID_LEN   32
#define MAX_CLIENTS     32
#define MAX_SAMPLES     2
#define OFFLINE_SECS    30
#define UI_TIMER_SECS   10
```

### Logging

Use `syslog()` for daemon processes, `printf()`/`fprintf(stderr,)` for standalone programs:

```c
openlog("PiMon_Client", LOG_PID | LOG_CONS, LOG_DAEMON);
syslog(LOG_INFO, "Server resolved to %s", resolved_ip);
syslog(LOG_ERR, "Unable to resolve server: %s", server_ip);
```

## Project Structure

```
PiMon/
├── client/
│   ├── client.c          # Raspberry Pi telemetry sender
│   ├── Makefile          # Linux build instructions
│   ├── install.sh        # Installation script
│   └── PiMon_Client.service  # systemd service
├── server/
│   ├── server.c          # Windows desktop application
│   ├── Makefile          # Windows build instructions
│   ├── compile.bat       # MSVC build script
│   └── pimon.ico         # Application icon
├── xserver/
│   ├── xserver.c         # Linux X11-based server
│   └── Makefile          # Linux build instructions
└── README.md             # Project documentation
```

## Platform Considerations

### Linux (client/xserver)
- Use `gcc` compiler
- Link against: `-lm -lrt -lpthread -lX11`
- Use `systemd` for service management
- Use `syslog()` for logging

### Windows (server)
- Use `cl` compiler (MSVC)
- Link against: `ws2_32.lib user32.lib gdi32.lib shell32.lib dwmapi.lib advapi32.lib`
- Use Windows API for UI and networking
- Use `WSAStartup()`/`WSACleanup()` for Winsock

### Cross-Platform
- Use conditional compilation for platform differences
- Use `#pragma` for MSVC-specific directives
- Define macros for consistent behavior

## Development Workflow

1. **Clone the repository**
2. **Navigate to target component** (client, server, or xserver)
3. **Build the project** using appropriate Makefile
4. **Run tests** if available
5. **Make changes** following code style guidelines
6. **Commit changes** with descriptive messages

## Common Patterns

### Reading from Sysfs

```c
FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
if (!f) return -1;
int temp;
fscanf(f, "%d", &temp);
fclose(f);
return temp / 1000.0f;
```

### UDP Socket Communication

```c
int sock = socket(AF_INET, SOCK_DGRAM, 0);
struct sockaddr_in addr = {0};
addr.sin_family = AF_INET;
addr.sin_port = htons(PORT);
addr.sin_addr.s_addr = INADDR_ANY;
```

### Mutex Locking

```c
pthread_mutex_lock(&g_line_mtx);
// Critical section
pthread_mutex_unlock(&g_line_mtx);
```

### Critical Sections (Windows)

```c
EnterCriticalSection(&clients[i].lock);
// Critical section
LeaveCriticalSection(&clients[i].lock);
```

## Additional Resources

- Project README: `README.md`
- Client source: `client/client.c`
- Server source: `server/server.c`
- XServer source: `xserver/xserver.c`

## Notes

- This project uses low-level C programming
- No external dependencies beyond standard system libraries
- Platform-specific implementations are separate
- Follow existing code style and patterns when adding new features
