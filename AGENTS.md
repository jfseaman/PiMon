# AGENTS.md - PiMon Codebase Guidelines

## Project Overview
PiMon is a Raspberry Pi monitoring system with UDP-based telemetry. Three components:
- **client/**: Linux client sending CPU metrics via UDP
- **server/**: Windows GUI server (Win32 API)
- **xserver/**: X11 Linux server (Xlib)

## Build Commands

### Client (Linux)
```bash
cd client
make compile          # Build PiMon_Client
make clean            # Remove binary
make strip            # Strip symbols
make install          # Install to /usr/sbin + systemd
make uninstall        # Remove service and binary
make start/stop/restart/status  # Service control
```

### Server (Windows)
```bash
cd server
nmake compile         # Build with MSVC (cl)
nmake clean           # Remove binary
```

### XServer (Linux X11)
```bash
cd xserver
make                  # or make release
make debug            # Debug build with -O0 -g
make clean            # Remove binary
```

**Note**: No automated test suite exists. Test manually by running client against server.

## Code Style Guidelines

### Formatting
- **Indentation**: 4 spaces (no tabs)
- **Line length**: ~100 characters
- **Braces**: K&R style (opening brace on same line for functions)
- **Comments**: `/* Block comments */` for sections, `//` for inline (minimal)

### Naming Conventions
- **Functions**: `snake_case()` (e.g., `read_cpu_temp`, `get_client`)
- **Variables**: `snake_case` (e.g., `cpu_load`, `fan_speed`)
- **Constants**: `UPPER_CASE` with underscores (e.g., `MAX_CLIENTS`, `SERVER_PORT`)
- **Types**: `CamelCase` structs (e.g., `TelemetryPacket`, `ClientData`)
- **Globals**: `g_` prefix (e.g., `g_clients`, `g_prefs`)

### File Organization
```c
#include <system_headers>     // stdio.h, stdlib.h, etc.
#include "local_headers"      // if any

#define CONSTANTS             // First
#define MACROS

typedef struct { ... } Type;  // Types next

static GlobalType g_global;   // Static globals

/* ---------- Section ---------- */
static void helper_func() {   // Static helpers
    // implementation
}

void public_func() {          // Public API
    // implementation
}

int main() {                  // Main last
    return 0;
}
```

### Error Handling
- **Linux**: Check return values, use `syslog(LOG_ERR/LOG_INFO, ...)` for errors
- **Windows**: Check return values, use `MessageBoxA()` for fatal errors
- Always cleanup resources (sockets, handles, memory) before returning
- Use `goto` only for centralized cleanup (acceptable pattern here)

### Memory Management
- Use `malloc/free` or `calloc/free` (standard C)
- Check allocations: `if (!ptr) return/error`
- Use `memset()` to initialize structs
- Windows: Use `LocalAlloc/GlobalAlloc` for clipboard only

### Thread Safety
- Linux: Use `pthread_mutex_t` (see `g_line_mtx` pattern)
- Windows: Use `CRITICAL_SECTION` for client data
- Always lock before accessing shared state

### Platform-Specific
- **Linux**: Use POSIX APIs (`localtime_r`, `inet_ntop`, `pthread`)
- **Windows**: Use Win32 APIs, `_snprintf` instead of `snprintf`, define `_CRT_SECURE_NO_WARNINGS`
- UDP port: 5000 (hardcoded constant)

### Debugging
- Linux: Use `DIAG_PRINT` macro pattern (compile-time toggle)
- XServer: Use `DBG_PRINT` macro with `DEBUG=1` flag
- Prefer syslog over printf for production

## Key Constants
- Port: `5000`
- Max clients: `32`
- Max samples: `2`
- Client ID length: `32`
- Offline threshold: `30` seconds

## Adding Features
1. Match existing brace and naming style
2. Add syslog/MessageBox error reporting
3. Update Makefile if new files added
4. Test both client and server manually
5. Ensure cross-platform code stays portable
