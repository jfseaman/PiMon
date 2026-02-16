# AGENTS.md

## 1. Build / Lint / Test Commands

| Target | Description | Command |
|--------|-------------|---------|
| **build** | Compile the client or server binaries using the provided Makefiles. | `make -C client compile`<br>`make -C server compile` |
| **clean** | Remove generated binaries. | `make -C client clean`<br>`make -C server clean` |
| **strip** | Strip symbols from the binaries. | `make -C client strip`<br>`make -C server strip` |
| **install** | Install binaries and systemd service files to `/usr/sbin` and `/etc/systemd/system`. | `sudo make -C client install`<br>`sudo make -C server install` |
| **uninstall** | Remove binaries and service files. | `sudo make -C client uninstall`<br>`sudo make -C server uninstall` |
| **start / stop / restart / status** | Control the service via systemd. | `sudo systemctl <action> PiMon_Client.service`<br>`sudo systemctl <action> PiMon_Server.service` |
| **lint** | Run clang‑format and clang‑tidy on all `.c` and `.h` files. | `clang-format -i $(git ls-files '*.{c,h}')`<br>`clang-tidy -p build $(git ls-files '*.c')` |
| **test** | (Optional) Run unit tests if a test harness is added. | `make -C tests run` (placeholder) |

> **Running a single test**
> If you add a test harness (e.g., using CUnit, Unity, or a custom framework), you can run a single2011format and clang‑tidy on all `.c` and `.h` files. | `clang-format -i $(git ls-files '*.{c,h}')`<br>`clang-tidy -p build $(git ls-files '*.c')` |
| **test** | (Optional) Run unit tests if a test harness is added. | `make -C tests run` (placeholder) |

> **Running a single test**
> If you add a test harness (e.g., using CUnit, Unity, or a custom framework), you can run a single test by specifying the test name:
> ```bash
> make -C tests run TEST=TestName
> ```
> 
> **NOTE** – The current codebase contains no automated tests. Feel free to create a `tests/` directory with a `Makefile` that compiles and runs your test suite.

## 2. Code Style Guidelines

| Topic | Recommendation |
|-------|----------------|
| **File Layout** | • Each source file should have a single, descriptive name (e.g., `client.c`, `server.c`).<br>• Header files (`*.h`) must use include guards or `#pragma once`. |
| **Formatting** | • 4‑space indentation.<br>• Use `clang-format` (`-style=file`) with a `.clang-format` file in the repo root.<br>• Run `make lint` before committing. |
| **Naming Conventions** | • Functions: `snake_case` (e.g., `send_heartbeat`).<br>• Variables: `snake_case`.<br>• Constants / #defines: `UPPER_SNAKE_CASE`.<br>• Structs / Enums: `CamelCase` with `struct_` prefix if needed (e.g., `struct SensorData`). |
| **Imports / Includes** | • Use angle brackets for system headers (`#include <stdio.h>`).<br>• Use quotes for project headers (`#include "sensor.h"`).<br>• Order: system headers → project headers. |
| **Error Handling** | • Return `int` status codes (`0` = success, non‑zero = error).<br>• Use `errno` and `perror` for system errors.<br>• Document each function’s contract in a comment block. |
| **Memory Management** | • Allocate with `malloc`/`calloc`; free with `free`.<br>• Always check return values of allocations.<br>• Avoid memory leaks: run `valgrind` during CI. |
| **Thread Safety** | • Use `pthread_mutex_t` for shared data.<br>• Document critical sections. |
| **Logging** | • Use `fprintf(stderr, …)` or a lightweight logging macro (`LOG_ERROR`, `LOG_INFO`).<br>• Do **not** leave stray `printf` in production code. |
| **Portability** | • Stick to POSIX APIs for the client.<br>• For the server, use Windows APIs only when required; keep the code path isolated. |
| **Comments & Documentation** | • Every function must have a brief description, parameter list, and return value.<br>• Use `/* ... */` for block comments; `//` for single‑line comments. |
| **Version Control** | • Do **not** commit compiled binaries (`PiMon_Client`, `PiMon_Server`).<br>• Add `*.o`, `*.exe`, `*.dll`, `*.lib` to `.gitignore`. |
| **License Header** | • Add the project license (MIT) to the top of each source file. |

## 3. Tooling & Environment

| Tool | Purpose | How to Use |
|------|----------|------------|
| `clang-format` | Enforces code style. | `clang-format -i $(git ls-files '*.{c,h}')` |
| `clang-tidy` | Static analysis. | `clang-tidy -p build $(git ls-files '*.c')` |
| `valgrind` | Detect memory leaks. | `valgrind --leak-check=full ./PiMon_Client` |
| `gcc` / `cl` | Compile. | Handled by the Makefiles. |
| `systemd` | Service management. | `sudo systemctl <action> PiMon_Client.service` |

> **CI Recommendations**
> • Run `make lint` and `make test` (if tests exist).<br>• Add a `check.sh` script that executes both linting and unit tests.<br>• Configure GitHub Actions to run on every push.

## 4. Cursor / Copilot Rules

- **Cursor** – No `.cursor` directory or rules found.<br>- **Copilot** – No `.github/copilot-instructions.md` present.

If you decide to add these in the future, place the rules in their respective directories and reference them here.

## 5. Quick Reference – Makefile Targets

```make
# Common targets
compile   : Build the binary
clean     : Remove the binary
strip     : Strip symbols
install   : Copy binary & service file
uninstall : Remove binary & service file
start     : systemctl2192 project headers. |
| **Error Handling** | • Return `int` status codes (`0` = success, non‑zero = error).<br>• Use `errno` and `perror` for system errors.<br>• Document each function’s contract in a comment block. |
| **Memory Management** | • Allocate with `malloc`/`calloc`; free with `free`.<br>• Always check return values of allocations.<br>• Avoid memory leaks: run `valgrind` during CI. |
| **Thread Safety** | • Use `pthread_mutex_t` for shared data.<br>• Document critical sections. |
| **Logging** | • Use `fprintf(stderr, …)` or a lightweight logging macro (`LOG_ERROR`, `LOG_INFO`).<br>• Do **not** leave stray `printf` in production code. |
| **Portability** | • Stick to POSIX APIs for the client.<br>• For the server, use Windows APIs only when required; keep the code path isolated. |
| **Comments & Documentation** | • Every function must have a brief description, parameter list, and return value.<br>• Use `/* ... */` for block comments; `//` for single‑line comments. |
| **Version Control** | • Do **not** commit compiled binaries (`PiMon_Client`, `PiMon_Server`).<br>• Add `*.o`, `*.exe`, `*.dll`, `*.lib` to `.gitignore`. |
| **License Header** | • Add the project license (MIT) to the top of each source file. |

## 3. Tooling & Environment

| Tool | Purpose | How to Use |
|------|----------|------------|
| `clang-format` | Enforces code style. | `clang-format -i $(git ls-files '*.{c,h}')` |
| `clang-tidy` | Static analysis. | `clang-tidy -p build $(git ls-files '*.c')` |
| `valgrind` | Detect memory leaks. | `valgrind --leak-check=full ./PiMon_Client` |
| `gcc` / `cl` | Compile. | Handled by the Makefiles. |
| `systemd` | Service management. | `sudo systemctl <action> PiMon_Client.service` |

> **CI Recommendations**
> • Run `make lint` and `make test` (if tests exist).<br>• Add a `check.sh` script that executes both linting and unit tests.<br>• Configure GitHub Actions to run on every push.

## 4. Cursor / Copilot Rules

- **Cursor** – No `.cursor` directory or rules found.<br>- **Copilot** – No `.github/copilot-instructions.md` present.

If you decide to add these in the future, place the rules in their respective directories and reference them here.

## 5. Quick Reference – Makefile Targets

```make
# Common targets
compile   : Build the binary
clean     : Remove the binary
strip     : Strip symbols
install   : Copy binary & service file
uninstall : Remove binary & service file
start     : systemctl start
stop      : systemctl stop
restart   : systemctl restart
status    : systemctl status
```

> **Tip:** Use `sudo` for install/uninstall and systemd commands.

## 6. Adding Tests (Optional)

If you want to add automated tests:

1. Create a `tests/` directory.<br>2. Add a `Makefile` that compiles test binaries.<br>3. Use a lightweight testing framework (e.g., Unity or CUnit).<br>4. Add a `run` target that executes all tests.<br>5. Add a `TEST` variable to run a single test: `make run TEST=TestFoo`.

---

> **Final Note** – This file is intentionally verbose to aid future agents and contributors.<br>Feel free to trim or extend sections as the project evolves.
