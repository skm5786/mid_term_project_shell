# 🖥️ MyTerm - X11-Based Custom Terminal Emulator

A feature-rich terminal emulator built with X11, supporting multiple tabs, Unicode input, command history, pipes, and advanced shell features.

## 📋 Table of Contents
- [Features](#-features)
- [Requirements](#-requirements)
- [Installation](#-installation)
  - [macOS](#macos)
  - [Linux](#linux)
- [Building the Project](#-building-the-project)
- [Running MyTerm](#-running-myterm)
- [Usage](#-usage)
- [Troubleshooting](#-troubleshooting)
- [Project Structure](#-project-structure)

## ✨ Features
- X11-based GUI with multiple tab support  
- Unicode and multiline input support  
- Command execution with full shell functionality  
- I/O Redirection (`<`, `>`)  
- Pipe support (`|`)  
- Signal handling (`Ctrl+C`, `Ctrl+Z`)  
- Searchable command history (10,000 commands, `Ctrl+R`)  
- Filename auto-completion (`Tab` key)  
- MultiWatch command for parallel command execution  
- Line navigation (`Ctrl+A`, `Ctrl+E`)  
- Scrolling support (Page Up/Down, Shift+Arrow keys)  

## 📦 Requirements

### macOS
- XQuartz (X11 server for macOS)
- Xcode Command Line Tools
- Make

### Linux
- X11 development libraries
- GCC or Clang
- Make

## 🚀 Installation

### macOS

1. **Install Xcode Command Line Tools**
   ```bash
   xcode-select --install
   ```

2. **Install XQuartz**
   ```bash
   # Using Homebrew
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   brew install --cask xquartz
   ```

   Or download manually from [https://www.xquartz.org/](https://www.xquartz.org/).

3. **Configure XQuartz**
   - Enable “Emulate three button mouse”
   - Enable “Follow system keyboard layout”
   - Optionally enable “Allow connections from network clients”

### Linux

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y build-essential libx11-dev
```

#### Fedora/RHEL
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install libX11-devel
```

#### Arch Linux
```bash
sudo pacman -S base-devel libx11
```

## 🔨 Building the Project

```bash
cd 25CS60R63_project
make clean
make
./myterm
```

**Alternative:**  
```bash
make clean && make
./myterm
```

## ▶️ Running MyTerm

### macOS
Ensure XQuartz is running:
```bash
open -a XQuartz
./myterm
```

If “Cannot open display” error occurs:
```bash
export DISPLAY=:0
./myterm
```

### Linux
```bash
./myterm
```

If needed:
```bash
export DISPLAY=:0
```

## 📖 Usage

### Basic Commands for testing various features
```bash
$ ls -la
$ echo "Hello, World!"
$ cd /path/to/directory
$ cat < input.txt
$ ls > output.txt
$ ./myprog < input.txt > output.txt
$ ls *.txt | wc -l
$ multiWatch ["date", "uptime","ls -l"]
```

### Keyboard Shortcuts
| Shortcut | Action |
|-----------|---------|
| Ctrl+C | Interrupt command |
| Ctrl+Z | Stop command |
| Ctrl+A | Move cursor to start |
| Ctrl+E | Move cursor to end |
| Ctrl+R | Search command history |
| Ctrl+N | New tab |
| Ctrl+W | Close tab |
| Tab | Auto-complete filename |
| Tab | Tab |  Select Option for closest match  |Auto-complete filename |
| Page Up/Down | Scroll |

### Tab Management
- **New tab:** Ctrl+N  
- **Switch tabs:** Click tab  
- **Close tab:** Ctrl+W  

### History Search
Press `Ctrl+R`, type, and hit Enter for fuzzy or exact match.

### Auto-completion
Press `Tab` to auto-complete file names or show options.

## 🐛 Troubleshooting

### macOS Issues
**Cannot open display**
```bash
open -a XQuartz
export DISPLAY=:0
```
**dyld: Library not loaded**
```bash
brew reinstall --cask xquartz
```

### Linux Issues
**Cannot open display**
```bash
echo $DISPLAY
startx
```
**Missing X11 Libraries**
```bash
sudo apt-get install libx11-dev
```

## 📁 Project Structure
```
myterm/
├── Makefile
├── src/
│   ├── main.c
│   ├── gui/
│   ├── shell/
│   ├── input/
│   └── utils/
└── obj/
```

📝 **Notes**
- History stored in `~/.myterm_history` (10,000 commands)  
- MultiWatch temp files auto-cleaned  
- MultiWatch follows specific formatting multiWatch["command1","command2",....]
- Debug logs: `/tmp/myterm_debug.log`  
- Supports up to 10 tabs and 100 background jobs
