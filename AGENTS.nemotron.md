# AGENTS.md – Development Guide for PiMon

This document outlines the essential commands, build and testing workflows, and code style conventions for contributors working with the PiMon repository. It is intended for use by AI agents and developers to ensure consistent implementation, review, and maintenance of the codebase.

--- Build & Compilation ---------------------------------------------------------

## 1. Build Commands

### 1.1 Client (`client/`)

- `make compile` – Compile the client binary using the default release settings.
- `make strip` – Strip symbols from the compiled binary to reduce size.
- `make install` – Install the binary as a system service (requires elevated privileges).
- `make clean` – Remove all generated object files and binaries.

### 1.2 Server (`server/`)

- `make compile` – Compile the server executable (`PiMon_Server.exe`) from `server.c`.
- `make release` – Build with optimization (`-O2`) and without debug symbols.
- `make debug` – Build with debug information (`-O0 -g -DDEBUG=1`).
- `make clean` – Clean generated files.

### 1.3 X11 Health‑Monitor Server (`xserver/`)

- `make release` – Compile the X11 server with `-O2` flags.
- `make debug` – Compile with `-g -DDEBUG=1` for debugging.
- `make clean` – Remove the executable and object files.

### 1.4 Unified Build Script

A top‑level `Makefile` is not provided; use the appropriate `Makefile` in each subdirectory.
When invoking from the repository root, change into the desired subdirectory first:

```bash
cd client && make compile && make strip && make install
cd server && make compile
cd xserver && make release
```

--- Testing ---------------------------------------------------------------------

## 2. Test Execution

The repository currently does not contain a formal test suite. The following placeholders are provided for future integration:

- `make test` – (Planned) Run unit and integration tests located in `tests/`.
- `./client/PiMon_Client --test` – (Planned) Execute client self‑test mode.
- `./server/PiMon_Server --test` – (Planned) Execute server health‑check mode.

Until tests are added, manual verification can be performed by:

1. Starting the server in a test configuration (`make debug`).
2. Sending a UDP telemetry packet from the client (`./client/PiMon_Client --test` when implemented).
3. Observing output in the server console for expected acknowledgments.

--- Code Style & Conventions ---------------------------------------------------

## 3. General Principles

1. **File Organization**
   - Keep related functions and types together in a single source file.
   - Place `#include` directives at the top, grouped by:
     - Windows SDK and system headers.
     - C standard library headers.
     - Project‑specific headers.

2. **Import Order**
   - System includes first, ordered alphabetically.
   - Third‑party includes next, alphabetically.
   - Project headers last, preserving relative path order.

3. **Indentation & Spacing**
   - Use 4‑space logical indentation (tabs are acceptable if consistent).
   - Align assignment operators for readability in struct initializers.

4. **Naming Conventions**
   - **Macros / Constants**: `UPPER_SNAKE_CASE`.
   - **Functions**: `lower_case_with_underscores`.
   - **Structs / Enums**: `PascalCase` for type names, `lower_case` for variable names.
   - **Variables**: Descriptive, lower_case, avoid abbreviations unless universally understood.

5. **Typing**
   - Prefer explicit types over `int`/`float` when size or signedness matters.
   - Use `uintXX_t`, `intXX_t` from `<stdint.h>` for fixed‑width integers.
   - Cast only when necessary; avoid C‑style casts, prefer `static_cast`‑like macros if they exist.

6. **Formatting**
   - Braces: Opening brace on the same line as the control statement, closing brace on a new line indented to the same level.
   - Control structures (`if`, `while`, `for`) must use curly braces even for single statements.
   - Line length limit: 120 characters; wrap beyond this for readability.

## 4. Error Handling

1. **Check Return Values**
   - Every system call, library function, or API that returns a status must be checked.
   - Use `if (ret != 0 || ret == INVALID_HANDLE_VALUE || ret == SOCKET_ERROR)` patterns.

2. **Cleanup Path**
   - Adopt a single exit point per function; use labeled cleanup blocks (`goto cleanup;`) to ensure resources are released.
   - Document cleanup actions in comments.

3. **Error Reporting**
   - Use `fprintf(stderr, "Message: %s (error %d)\n", msg, GetLastError());` for system errors.
   - Return distinct error codes (`#define ERR_...`) to aid callers.

4. **Assertions**
   - Use `assert(condition && "Message")` only in debug builds (`#ifdef DEBUG`).
   - For production, replace with graceful failure handling.

## 5. Version Control & Commit Practices

1. **Branch Naming**
   - Feature branches: `feat/<short-description>`.
   - Bug‑fix branches: `fix/<short-description>`.
   - Hotfix branches: `hotfix/<description>`.

2. **Commit Message Format**
   ```
   <type>(<scope>): <subject>

   <explanation>

   <footer>
   ```
   - Types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`.
   - Scope: Short identifier (e.g., `client`, `server`, `xserver`).

3. **Sign‑Off**
   - All commits must be signed off with `Signed-off-by: Name <email>`.

4. **Rebase vs Merge**
   - Prefer `git rebase` to keep a linear history on feature branches.
   - Use `git merge --no-ff` only when a merge commit conveys meaningful context.

--- Tooling & Automation -------------------------------------------------------

## 6. Linting & Formatting (Optional)

- **`clang-format`** – Apply the project’s `.clang-format` configuration (if added in future). Example command:
  ```bash
  clang-format -i -style=file **/*.c **/*.h
  ```
- **`shellcheck`** – Lint shell scripts (`install.sh`, `Makefile` fragments). Run via:
  ```bash
  shellcheck **/*.sh
  ```
- **`cpcop`** – (Planned) Code‑pattern checker for C. Configuration to be added.

--- Documentation & Communication ---------------------------------------------

## 7. Communicating Changes

- Update `README.md` with any new command‑line flags or configuration options.
- Add or update comments in code to explain non‑obvious logic.
- When modifying build scripts, ensure they remain idempotent and produce consistent output.

--- Appendices ------------------------------------------------------------------------

## A. Glossary

- **Telemetry Packet** – A structured binary payload sent from client to server describing CPU, memory, temperature, and fan metrics.
- **System Service** – A Windows service (`PiMon_Client.service`) that runs in the background to launch the client automatically at boot.

## B. Example Build Flow (Client)

```bash
cd client
make compile        # Build binary
make strip          # Strip symbols
make install        # Register as service
systemctl daemon-reload
service PiMon_Client start
```

--- End of Document --------------------------------------------------------------