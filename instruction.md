# 🖥️ VirtualOS — Setup & Installation Guide

A custom C-based desktop environment built with **GTK4**, featuring a virtual filesystem, a fully-featured terminal emulator, and an integrated AI assistant.

---

## 📋 Prerequisites

Before you begin, make sure the following are installed on your **Windows** system:

### 1. MSYS2 (provides GCC & GTK4)

1. Download MSYS2 from: https://www.msys2.org/
2. Run the installer and complete the setup.
3. Open **MSYS2 UCRT64** terminal and install the required packages:

```bash
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkg-config
```

4. Add MSYS2 to your system PATH:
   - Add `C:\msys64\ucrt64\bin` to your Windows Environment Variables → `Path`

### 2. Python (for AI integration)

1. Download Python 3.10+ from: https://www.python.org/downloads/
2. During installation, **check "Add Python to PATH"**
3. Verify installation:

```
python --version
```

### 3. Git

1. Download from: https://git-scm.com/downloads
2. Install with default settings.

---

## 🚀 Installation

### Step 1: Clone the Repository

```bash
git clone https://github.com/samarthprabhu2007-art/C_pbl.git
cd C_pbl
```

### Step 2: Set Up Your AI API Key

The AI assistant requires an **xAI (Grok) API key** to function. Your keys are stored locally and **never uploaded to GitHub**.

1. Go to https://console.x.ai/ and create an account
2. Generate an API key from the dashboard
3. Create a file called `.env` in the project root directory:

**Windows (PowerShell):**
```powershell
New-Item -Path .env -ItemType File
```

**Or manually** create a file named `.env` (no filename, just the extension) in the project folder.

4. Open `.env` in any text editor and add your API key(s):

```env
AI_API_KEY_1=xai-your-first-api-key-here
AI_API_KEY_2=xai-your-second-api-key-here
AI_API_KEY_3=xai-your-third-api-key-here
AI_MODEL=grok-3-mini
```

> **Note:** You can add up to 9 keys (`AI_API_KEY_1` through `AI_API_KEY_9`). If one key hits a rate limit, the system automatically falls back to the next key.

> **⚠️ Important:** The `.env` file is listed in `.gitignore` and will NOT be pushed to GitHub. Your keys stay private on your machine only.

### Step 3: Update the Build Script Path

Open `build.sh` and update line 18 to match your system:

```bash
cd "/c/Users/YOUR_USERNAME/path/to/C_pbl"
```

Replace `YOUR_USERNAME` and the path with where you cloned the project.

---

## 🔨 Building the Project

### Option A: Using MSYS2 Terminal

Open **MSYS2 UCRT64** and run:

```bash
cd /c/Users/YOUR_USERNAME/path/to/C_pbl
bash build.sh
```

### Option B: Using PowerShell

```powershell
& "C:\msys64\usr\bin\bash.exe" -l "path\to\C_pbl\build.sh"
```

If the build succeeds, you'll see:
```
✓ Build successful! Run: ./virtualos
```

---

## ▶️ Running VirtualOS

After a successful build, run the application:

**From MSYS2:**
```bash
./virtualos
```

**From PowerShell:**
```powershell
.\virtualos.exe
```

---

## 🧭 Features Overview

### Desktop Environment
- Full-screen desktop with draggable icons
- Taskbar with **Terminal**, **Search**, and **AI** buttons
- Folder navigation with file manager windows
- Wallpaper background with CSS styling

### Terminal Commands

| Command | Usage | Description |
|---------|-------|-------------|
| `help` | `help` | Show all available commands |
| `pwd` | `pwd` | Print current working directory |
| `ls` | `ls [dir]` | List files and folders |
| `cd` | `cd <path>` | Change directory |
| `touch` | `touch <name>` | Create a new empty file |
| `mkdir` | `mkdir <name>` | Create a new folder |
| `rm` | `rm <name>` | Delete a file |
| `rmdir` | `rmdir <name>` | Delete an empty folder |
| `cat` | `cat <file>` | Display file contents |
| `write` | `write <file> <text>` | Write text to a file |
| `append` | `append <file> <text>` | Append text to a file |
| `cp` | `cp <src> <dest>` | Copy a file |
| `mv` | `mv <src> <dest>` | Move or rename a file |
| `echo` | `echo <text>` | Print text to terminal |
| `clear` | `clear` | Clear the terminal screen |
| `ai` | `ai <prompt>` | Ask AI a question |
| `whoami` | `whoami` | Show current user |
| `date` | `date` | Show current date |

### AI Integration
- Type `ai <your question>` in the terminal
- Use the dedicated **AI Chat** panel on the right side of the terminal
- AI can execute file commands automatically (e.g., `ai create a file called notes.txt`)

### Help & Documentation System
- Click **"Help & Docs"** button in the terminal header
- Searchable command documentation with examples
- **Rename commands** to your preference (e.g., rename `ls` to `list`)
- **Reset Commands** button to restore all default names

---

## 📁 Project Structure

```
C_pbl/
├── main.c              # Desktop GUI, taskbar, folder windows
├── terminal.c          # Terminal emulator, AI chat, command engine
├── terminal.h          # Terminal public API
├── filesystem.h        # Virtual filesystem helpers
├── ai_api.py           # AI backend (xAI/Grok API client)
├── build.sh            # Build script for MSYS2
├── .env                # YOUR API keys (local only, never pushed)
├── .gitignore          # Git ignore rules
├── assets/
│   ├── wallpaper.png   # Desktop wallpaper
│   └── terminal.png    # Terminal icon
├── src/
│   ├── algorithms.c    # Bubble sort, binary search implementations
│   ├── algorithms.h    # Algorithm headers
│   └── ...             # Source mirrors
├── virtual_home/       # Virtual filesystem root directory
│   └── ...             # User files created via terminal
└── README.md
```

---

## 🛠️ Troubleshooting

### Build fails with "gtk4 not found"
Make sure you installed GTK4 via MSYS2:
```bash
pacman -S mingw-w64-ucrt-x86_64-gtk4
```
And that `C:\msys64\ucrt64\bin` is in your system PATH.

### AI returns "No API keys found"
Make sure you created the `.env` file in the project root with at least one key:
```env
AI_API_KEY_1=xai-your-key-here
AI_MODEL=grok-3-mini
```

### AI returns "permission-denied" error
Your xAI account needs credits. Visit https://console.x.ai/ and activate the free tier or purchase credits.

### Window appears too small or off-screen
The app launches in fullscreen mode. Press `Alt+F4` to close, or use the window controls if visible.

### Python not found
Ensure Python is installed and added to PATH. Test with:
```
python --version
```

---

## 📝 License

This project is developed as a PBL (Project-Based Learning) submission.

---

*Built with C, GTK4, and xAI — VirtualOS © 2026*
