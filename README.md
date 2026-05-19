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

> This program uses the Linux `/proc` filesystem, so it **cannot run natively on Windows**.  
> The solution is **WSL2** (Windows Subsystem for Linux) — a free tool from Microsoft that gives you a real Linux terminal inside Windows. No dual-boot, no virtual machine setup needed.

---

### Step 1 — Check your Windows version

WSL2 requires **Windows 10 version 2004 or later**, or **Windows 11**.

To check: press `Win + R`, type `winver`, press Enter. You will see your version number.

---

### Step 2 — Enable WSL2

1. Click the **Start menu**
2. Search for **"PowerShell"**
3. Right-click it → select **"Run as administrator"**
4. A blue terminal window opens. Paste this command and press Enter:

```powershell
wsl --install
```

This automatically installs WSL2 and Ubuntu. It will take a few minutes to download.

5. When it finishes, **restart your computer**.

> If you see an error like `wsl: command not found`, your Windows is too old. Update Windows first via Settings → Windows Update.

---

### Step 3 — Set up Ubuntu for the first time

1. After restarting, the **Ubuntu** app will open automatically.  
   (If it doesn't, open the Start menu and search for **"Ubuntu"**.)
2. Wait for it to finish installing — this takes about 1–2 minutes.
3. It will ask you to create a **username** and **password**.
   - This is your Linux account — it does not have to match your Windows account.
   - When typing the password, nothing will appear on screen — that is normal.

You now have a Linux terminal running inside Windows.

---

### Step 4 — Install required tools

Inside the Ubuntu terminal, type these commands one by one and press Enter after each:

```bash
sudo apt-get update
```
> It will ask for the password you just created. Type it and press Enter.

```bash
sudo apt-get install -y gcc make libncurses-dev git
```

Wait for it to finish. This installs the compiler, build tool, display library, and git.

---

### Step 5 — Clone the project

Still inside the Ubuntu terminal, run:

```bash
git clone https://github.com/ducnguyen2304/cs211_system_monitor.git
```

Then move into the project folder:

```bash
cd cs211_system_monitor
```

---

### Step 6 — Build the program

```bash
make
```

You should see several lines starting with `gcc ...`. That means it compiled successfully.

---

### Step 7 — Run the program

```bash
./sysmon
```

The system monitor will launch in your terminal. Press **`q`** to quit.

---

### Step 8 — Clean build files (optional)

If you want to remove compiled files and start fresh:

```bash
make clean
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

Sampling runs in a **background pthread** so the UI stays responsive. Per-thread stats are read from `/proc/[pid]/task/*`, and scheduling info (priority, nice, policy) from `/proc/[pid]/stat`.

---

## Controls

| Key | Action |
|-----|--------|
| `q` or `Q` | Quit |
| `↑` / `↓` | Navigate process list |
| `k` | Kill selected process |
| `r` | Renice selected process |
| `s` | Send signal to selected process |

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
