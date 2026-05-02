# RT-THM — Real-Time Task & Health Monitor

RT-THM is a **real-time process supervision project**. A **Supervisor (parent)** process spawns and monitors multiple **Worker (child)** processes that execute simulated tasks. Workers continuously publish their status using **shared memory**, and the supervisor reacts in near real time when a worker becomes unstable, hangs, or crashes.

This project also includes:
- **Ncurses UI dashboard** for live monitoring in the terminal
- **Logging** with timestamps
- A basic **MISRA-style audit checker** (Python)
- **Doxygen documentation** generation

---

## Key Features

- **Multi-process management:** uses `fork()` to create worker processes.
- **IPC (Inter-Process Communication):**
  - **Shared Memory (System V)** for publishing worker statistics.
  - **Semaphores (System V)** to protect shared data and avoid race conditions.
- **Signals:** supervisor can signal workers (pause/resume strategy) using `SIGUSR1` / `SIGUSR2`.
- **Healing / Auto-restart:** supervisor detects dead workers (via `waitpid()`) and restarts them.
- **Real-time monitoring UI:** `ncurses` dashboard with live stats and log messages.
- **Static analysis support:** includes a Python checker for basic MISRA-like rules.
- **Doxygen documentation:** codebase is documented using Doxygen comments.

---

## Repository Structure

```text
.
├── include/                 # Public headers
│   ├── config.h
│   ├── ipc.h
│   ├── logger.h
│   ├── project.h
│   ├── signals.h
│   ├── UI.h
│   └── worker.h
├── src/                     # Implementation files
│   ├── config.c
│   ├── ipc_utils.c
│   ├── logger.c
│   ├── main.c
│   ├── signals.c
│   ├── UI.c
│   └── worker.c
├── Documentation/           # Reports / docs output (and Doxygen output)
├── config.txt               # Runtime configuration
├── checker.py               # MISRA-style audit checker (basic rules)
├── makefile
└── baseProj.c               # Legacy / base prototype (if still used)
```

---

## Requirements

### Build dependencies
- GCC (C99)
- ncurses development library

### Optional (documentation)
- Doxygen
- Graphviz (`dot`) for call graphs

---

## Build & Run

### 1) Compile

```bash
make
```

Output executable:
```text
bin/rt-thm
```

### 2) Run

```bash
make run
```

Or:
```bash
./bin/rt-thm
```

---

## UI Controls (Ncurses)

Inside the dashboard:
- `q` : quit cleanly
- `r` : restart all workers (sends SIGTERM to all workers; supervisor restarts them)
- Terminal resize is handled (KEY_RESIZE)

> Note: If ncurses fails to initialize, the program may fall back to console logging output depending on your implementation.

---

## Configuration

The project reads runtime parameters from:

- `config.txt`

Typical configurable fields (depending on your `config.c` implementation) may include:
- number of workers
- refresh rate
- timeout thresholds
- log verbosity

---

## Logging

Logs are written to a log file (commonly `system.log` if configured in code).  
Logged events typically include:
- startup/shutdown
- worker crash detection
- worker restart events
- timeouts and health alerts
![system log output](Documentation/images/LogOUT.png)

---

## IPC Model (High Level)

Workers write their status into a shared array in System V shared memory, commonly using a structure like:

- PID
- health score
- tasks completed
- last update timestamp
- slow/hang detection flags
- command field (pause/resume)

The supervisor reads this data periodically to:
- display live metrics in the UI
- detect timeouts / instability
- restart crashed processes

To avoid race conditions, a System V semaphore is used as a **mutex** around shared memory access.

---

## Signals (Control Channel)

RT-THM uses signals for quick control commands:

- `SIGUSR1`: ask a worker to slow down / pause (depending on implementation)
- `SIGUSR2`: resume normal operation

---

## MISRA-Style Audit Checker

Run the basic checker:

```bash
make audit
```

This uses `checker.py` to scan files (for example `src/main.c`) and emits a report.

> The checker is intentionally simple and should be extended for deeper MISRA compliance.

---

## Generate Doxygen Documentation

### 1) Install tools

Ubuntu/Debian:
```bash
sudo apt update
sudo apt install doxygen graphviz
```

### 2) Generate docs

If you have a `Doxyfile` at repo root:

```bash
doxygen Doxyfile
```

Typical output:
```text
Documentation/doxygen/html/index.html
```

Open in browser:
```bash
xdg-open Documentation/doxygen/html/index.html
```

### Recommended Doxyfile settings
To use this README as the Doxygen main page:

```ini
USE_MDFILE_AS_MAINPAGE = README.md
MARKDOWN_SUPPORT       = YES
```

---

## Troubleshooting

### Doxygen "Main Page" is empty
Enable README as main page:
- Ensure `README.md` is included in `INPUT`
- Add: `USE_MDFILE_AS_MAINPAGE = README.md`

### Ncurses build errors
Install ncurses dev package:
- Ubuntu/Debian: `sudo apt install libncurses-dev`
- Fedora: `sudo dnf install ncurses-devel`

---

## Roadmap / Next Improvements

- Improve watchdog logic for hung workers (kill + restart policy)
- Add richer ncurses panels (global system stats, scrolling logs)
- Expand configuration format (INI/TOML/JSON)
- Expand MISRA checker + integrate with CI
- Add unit tests for config parser and IPC helpers

---

## License

No license specified yet. If you want this to be open-source, consider adding a LICENSE file (MIT/BSD-2-Clause/GPLv3).