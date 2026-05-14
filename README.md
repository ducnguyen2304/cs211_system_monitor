# System Monitor

A lightweight terminal-based system monitor written in C.  
Displays real-time CPU usage per core, RAM/swap, and a live process table — all read directly from the Linux `/proc` filesystem.

```
 System Monitor  (press 'q' to quit)
────────────────────────────────────────────────────────────────
 CPU  (8 cores)
  Total  [||||||||||||||                ] 47.2%
  Core0  [||||||||||||||||              ] 53.1%
  Core1  [||||||||||                    ] 34.8%
  ...
────────────────────────────────────────────────────────────────
 Memory
  RAM    [||||||||||||||||              ] 49.8%
    7500 MB used / 15360 MB total  (avail: 7900 MB)
────────────────────────────────────────────────────────────────
 Processes (312)
 PID     Name                  CPU%   Mem(MB)  S
 1234    firefox               12.4    512.00  S
 5678    code                   8.1    340.20  S
```

---

## Requirements

| Platform | Supported |
|----------|-----------|
| Linux (Ubuntu, Debian, Arch, Fedora, etc.) | Yes |
| Windows (WSL2) | Yes — run inside WSL2 |
| macOS | No — depends on Linux `/proc` filesystem |
| Windows (native) | No |

---

## Linux — Setup & Run

### 1. Install dependencies

**Ubuntu / Debian:**
```bash
sudo apt-get update
sudo apt-get install -y gcc make libncurses-dev
```

**Arch Linux:**
```bash
sudo pacman -S gcc make ncurses
```

**Fedora / RHEL:**
```bash
sudo dnf install gcc make ncurses-devel
```

### 2. Clone or download the project

```bash
git clone https://github.com/ducnguyen2304/cs211_system_monitor.git
cd cs211_system_monitor
```

Or if you downloaded a zip:
```bash
unzip System-monitor.zip
cd System-monitor
```

### 3. Build

```bash
make
```

You should see:
```
gcc -Wall -Wextra -I include -c -o src/main.o src/main.c
gcc -Wall -Wextra -I include -c -o src/cpu.o src/cpu.c
...
```

### 4. Run

```bash
./sysmon
```

Press **`q`** to quit.

### 5. Clean build files (optional)

```bash
make clean
```

---

## Windows — Setup & Run (via WSL2)

> This program uses the Linux `/proc` filesystem, so it cannot run natively on Windows.  
> WSL2 (Windows Subsystem for Linux) gives you a full Linux environment inside Windows.

### 1. Enable WSL2

Open **PowerShell as Administrator** and run:
```powershell
wsl --install
```
Restart your computer when prompted. This installs Ubuntu by default.

### 2. Open WSL2

After restart, search **"Ubuntu"** in the Start menu and open it.  
First launch will ask you to create a username and password.

### 3. Follow the Linux instructions above

Inside the WSL2 terminal, run exactly the same commands as Linux:
```bash
sudo apt-get update
sudo apt-get install -y gcc make libncurses-dev
# then clone/download the project and:
make
./sysmon
```

---

## Project Structure

```
System-monitor/
├── Makefile              # build instructions
├── include/              # header files — data structures & function declarations
│   ├── cpu.h
│   ├── memory.h
│   ├── process.h
│   └── display.h
└── src/                  # source files — actual implementation
    ├── main.c            # entry point and main loop
    ├── cpu.c             # reads /proc/stat → CPU usage per core
    ├── memory.c          # reads /proc/meminfo → RAM & swap
    ├── process.c         # scans /proc/[pid]/ → process list sorted by CPU%
    └── display.c         # ncurses terminal UI
```

---

## How It Works

The program reads directly from the Linux kernel's `/proc` virtual filesystem:

| Data | Source |
|------|--------|
| CPU usage per core | `/proc/stat` — sampled twice, 1s apart, delta computed |
| RAM & swap | `/proc/meminfo` |
| Process list | `/proc/[pid]/stat` and `/proc/[pid]/status` for each running process |

No external monitoring agents or network calls — everything comes from the kernel.

---

## Controls

| Key | Action |
|-----|--------|
| `q` or `Q` | Quit |

---

## Troubleshooting

**`ncurses.h: No such file or directory`**
```bash
sudo apt-get install -y libncurses-dev
```

**`make: command not found`**
```bash
sudo apt-get install -y build-essential
```

**Terminal too small — display looks broken**  
Resize your terminal window to at least 80×24 characters.

**`Permission denied` on `/proc/[pid]/`**  
Normal — some kernel process folders are root-only. The monitor skips those automatically.
