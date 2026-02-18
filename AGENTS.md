# AGENTS.md – PiMon Development Guide

This document outlines the essential commands, build and testing workflows, and code style conventions for contributors working with the PiMon repository. It is intended for use by AI agents and developers to ensure consistent implementation, review, and maintenance of the codebase.

## Project Overview

PiMon is a Raspberry Pi monitoring system with three components:
- **client**: Raspberry Pi client that sends telemetry data via UDP
- **server**: Windows desktop application that receives and displays telemetry
- **xserver**: Linux X11-based server for telemetry visualization

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

## Build Commands

### Client (Linux)

```bash
# Build and install
make compile
make strip
make install

# Service management
sudo make start
sudo make stop
sudo make restart
make status
sudo make uninstall
```

### Server (Windows)

```bat
# Build
cd server
compile.bat

# Alternative
make compile
```

### XServer (Linux)

```bash
# Build
make release    # Release version
make debug      # Debug version
make clean      # Remove generated files
```

### Unified Build Script

When invoking from the repository root, change into the desired subdirectory first:

```bash
# Example: Build and install client
cd client && make compile && make strip && make install

# Example: Build server
cd server && make compile

# Example: Build XServer
cd xserver && make release
```

## Test Execution

The repository currently does not contain a formal test suite. The following placeholders are provided for future integration:

- `make test` – (Planned) Run unit and integration tests located in `tests/`
- `./client/PiMon_Client --test` – (Planned) Execute client self-test mode
- `./server/PiMon_Server --test` – (Planned) Execute server health-check mode

Until tests are added, manual verification can be performed by:

1. Starting the server in a test configuration (`make debug`)
2. Sending a UDP telemetry packet from the client (`./client/PiMon_Client --test` when implemented)
3. Observing output in the server console for expected acknowledgments

### Linux Smoke Test

Receiver terminal:
```bash
cd xserver
make debug
./xserver
```

Sender terminal (about 5 seconds):
```bash
cd client
make compile
timeout 5s ./PiMon_Client 127.0.0.1
```

Expected: one client row appears and refreshes roughly once per second.

### Windows Smoke Test

```bat
cd server
compile.bat
PiMon_Server.exe
```

Then run a client targeting the Windows host IP and confirm telemetry rows appear.

## Code Style Guidelines

### General Principles

- **Language**: C (platform-native APIs), not C++
- **Changes**: Keep changes minimal, local, and behavior-preserving
- **Style**: Match surrounding style in each edited file
- **Whitespace**: Avoid broad reformatting and whitespace-only churn

### Includes

- Keep include directives at top of file
- Group headers logically (runtime, networking, OS APIs)
- Add includes only when required by new code
- In Windows files, preserve required preprocessor defines before includes

### Formatting

- Use 4-space indentation in C source; tabs are fine in Makefiles
- Prefer one statement per line and explicit braces for control flow
- Keep line length readable for review
- Preserve existing brace style in the touched file

### Naming Conventions

- **Macros / Constants**: `UPPER_SNAKE_CASE`
- **Functions**: `snake_case`
- **Structs / Enums**: `PascalCase` for type names, `lower_case` for variable names
- **Variables**: Descriptive, lower_case, avoid abbreviations unless universally understood
- **Mutable globals in server/xserver**: Commonly use `g_` prefix
- **File-local helpers**: Prefer `static` declarations

### Types and Protocol Data

- Use fixed-width integer types for networked data
- `TelemetryPacket` must remain binary-compatible across all components
- If packet fields change, update `client/client.c`, `server/server.c`, and `xserver/xserver.c` together
- Null-terminate bounded string copies explicitly

### Error Handling

- Check return values from system/library calls
- Use early returns for unrecoverable initialization failures
- Follow existing logging style per platform:
  - Client: `syslog`/`stderr`
  - Server: `MessageBoxA`
  - XServer: `perror`/`stderr`
- Prefer explicit fallback behavior over silent failure

### Resource and Concurrency Safety

- Pair acquire/release on every path (sockets, handles, heap, UI resources)
- Keep cleanup deterministic in error/shutdown paths
- Protect shared telemetry state with existing locks (`CRITICAL_SECTION` or `pthread_mutex_t`)
- Keep lock hold times short and avoid blocking while locked

### Networking and Platform Boundaries

- UDP telemetry is fire-and-forget on port `5000`
- Client target comes from CLI arg or `PIMON_SERVER_IP`
- Keep resolve/retry behavior non-blocking for main/UI loops
- `client/` and `xserver/` are Linux/POSIX-focused; `server/` is Windows/Win32-focused

## Version Control & Commit Practices

### Branch Naming

- Feature branches: `feat/<short-description>`
- Bug-fix branches: `fix/<short-description>`
- Hotfix branches: `hotfix/<description>`

### Commit Message Format

```
<type>(<scope>): <subject>

<explanation>

<footer>
```

- **Types**: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`
- **Scope**: Short identifier (e.g., `client`, `server`, `xserver`)

### Sign-Off

All commits must be signed off with `Signed-off-by: Name <email>`

### Rebase vs Merge

- Prefer `git rebase` to keep a linear history on feature branches
- Use `git merge --no-ff` only when a merge commit conveys meaningful context

## Linting & Formatting

- **`clang-format`** – Apply the project’s `.clang-format` configuration (if added in future)
  ```bash
  clang-format -i -style=file **/*.c **/*.h
  ```
- **`shellcheck`** – Lint shell scripts (`install.sh`, `Makefile` fragments)
  ```bash
  shellcheck **/*.sh
  ```
- **`cpcop`** – (Planned) Code-pattern checker for C. Configuration to be added.

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

---

> **Final Note** – This file is intentionally comprehensive to aid future agents and contributors. Feel free to trim or extend sections as the project evolves.