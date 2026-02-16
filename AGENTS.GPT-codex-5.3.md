# AGENTS.md

Guide for coding agents working in the PiMon repository.
Last reviewed against repo contents: 2026-02-15.

## Project structure
- `client/`: Linux UDP telemetry sender (`client.c`, service, Makefile).
- `server/`: Windows Win32 telemetry UI (`server.c`, `.rc`, `.bat`, Makefile).
- `xserver/`: Linux X11 telemetry UI (`xserver.c`, Makefile).
- `README.md`: project background.

## Cursor/Copilot rule files
Checked locations:
- `.cursor/rules/`
- `.cursorrules`
- `.github/copilot-instructions.md`
No files were found in those locations.
If they are added later, treat them as higher-priority repo rules and update this file.

## Build commands
### Client (Linux)
Run from `client/`:
```bash
make compile
make strip
make clean
```
Service/install helpers:
```bash
sudo make install
sudo make uninstall
sudo make start
sudo make restart
sudo make stop
make status
sudo ./install.sh
```

### Server (Windows)
Preferred from `server/`:
```bat
compile.bat
```
`compile.bat` calls `setenv.bat`, compiles `pimon.rc`, and builds `PiMon_Server.exe`.
Alternative:
```bat
make compile
```

### XServer (Linux)
Run from `xserver/`:
```bash
make          # default: release
make release
make debug
make clean
```

## Lint / static analysis
There is no dedicated lint script or CI lint pipeline.
Use compiler warnings as the lint signal.

Client lint pass:
```bash
cd client
gcc -std=c11 -Wall -Wextra -Wpedantic -fsyntax-only client.c
```
XServer lint pass:
```bash
cd xserver
gcc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -fsyntax-only xserver.c
```
Server lint pass (MSVC):
```bat
cd server
cl /nologo /W4 /analyze /c server.c
```

## Test commands
There is no automated unit/integration test framework yet.
There is no built-in "run one unit test" command.
In this repo, a "single test" means a focused manual smoke scenario.
Recommended single test: local UDP telemetry path on Linux.

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

Windows smoke test:
```bat
cd server
compile.bat
PiMon_Server.exe
```
Then run a client targeting the Windows host IP and confirm telemetry rows appear.

## Code style guidelines
### General
- Language is C (platform-native APIs), not C++.
- Keep changes minimal, local, and behavior-preserving.
- Match surrounding style in each edited file.
- Avoid broad reformatting and whitespace-only churn.

### Includes
- Keep include directives at top of file.
- Group headers logically (runtime, networking, OS APIs).
- Add includes only when required by new code.
- In Windows files, preserve required preprocessor defines before includes.

### Formatting
- Use 4-space indentation in C source; tabs are fine in Makefiles.
- Prefer one statement per line and explicit braces for control flow.
- Keep line length readable for review.
- Preserve existing brace style in the touched file.

### Types and protocol data
- Use fixed-width integer types for networked data.
- `TelemetryPacket` must remain binary-compatible across all components.
- If packet fields change, update `client/client.c`, `server/server.c`, and `xserver/xserver.c` together.
- Null-terminate bounded string copies explicitly.

### Naming
- Macros/constants: `UPPER_SNAKE_CASE`.
- Struct/type names: `PascalCase`.
- Functions: `snake_case`.
- Mutable globals in server/xserver commonly use `g_` prefix.
- Prefer `static` for file-local helpers.

### Error handling
- Check return values from system/library calls.
- Use early returns for unrecoverable initialization failures.
- Follow existing logging style per platform: client=`syslog`/stderr, server=`MessageBoxA`, xserver=`perror`/stderr.
- Prefer explicit fallback behavior over silent failure.

### Resource and concurrency safety
- Pair acquire/release on every path (sockets, handles, heap, UI resources).
- Keep cleanup deterministic in error/shutdown paths.
- Protect shared telemetry state with existing locks (`CRITICAL_SECTION` or `pthread_mutex_t`).
- Keep lock hold times short and avoid blocking while locked.

### Networking and platform boundaries
- UDP telemetry is fire-and-forget on port `5000`.
- Client target comes from CLI arg or `PIMON_SERVER_IP`.
- Keep resolve/retry behavior non-blocking for main/UI loops.
- `client/` and `xserver/` are Linux/POSIX-focused; `server/` is Windows/Win32-focused.

## File hygiene for agents
- Prefer editing source/docs (`.c`, `.h`, `.rc`, Makefiles, scripts, markdown).
- Do not hand-edit generated binaries (`.exe`, `.obj`, `.res`).
- Build the touched component before finishing.
- If tests were not run, state exactly what was verified.
