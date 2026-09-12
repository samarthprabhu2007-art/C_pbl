# C EL Virtual OS

A small GTK4 virtual desktop written in C. It includes a wallpaper desktop, terminal icon, virtual filesystem commands, clickable file/folder icons, editable text files, draggable icons, and a simple AI command helper panel.

## Features

- **Wallpaper desktop** with draggable icons — icon positions persist between sessions.
- **Folder windows** — double-click a folder to open it in its own window, complete with a search bar and A–Z sort, backed by a hand-written bubble sort + binary search (lower/upper bound) over a dynamic array of filenames.
- **Editable text files** — double-click a text file to open it in a built-in editor with a Save button.
- **Two ways to browse the web:**
  - A lightweight **in-app browser** (text/link based, with Back/Forward/Home and its own history).
  - A full **external browser** via the `browse` command, which launches `browser.py` (pywebview + WebView2) for real page rendering.
- **Terminal emulator** with a virtual filesystem, persisted to `virtual_home/`. Built-in commands: `help`, `clear`, `whoami`, `date`, `pwd`, `ls`, `cd`, `touch`, `mkdir`, `cat`, `write`, `append`, `cp`, `mv`, `rm`, `rmdir`, `echo`, `browse`, `ai`.
- **Command aliasing** — rename any built-in command to whatever you like; saved automatically.
- **Custom commands (macros)** — define your own multi-step commands with placeholder arguments (e.g. `writefilefolder [string1] [file1] [folder1]` → `cd folder1` / `write file1 string1`). Created and deleted from a UI panel, persisted to `.virtualos_config`, and reloaded automatically on startup. A "reset all" option clears aliases and custom commands together.
- **AI helper** — an `ai <prompt>` terminal command and a dedicated AI panel, both calling out to `ai_api.py`, which hits the Grok API (`grok-3-mini` by default). The AI can run filesystem commands and your custom macros on your behalf.
- **Custom data structures & algorithms**, implemented from scratch (manual malloc/realloc/free) for coursework purposes: a singly linked list for folder contents and back/forward navigation history, a resizable dynamic array, bubble sort, and binary search (lower/upper bound) powering the search bars.

## Requirements

Install MSYS2 from:

```text
https://www.msys2.org/
```

Open the **MSYS2 UCRT64** terminal and install the needed tools:

```bash
pacman -Syu
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gtk4 mingw-w64-ucrt-x86_64-pkgconf git
```

If MSYS2 asks you to close and reopen the terminal after `pacman -Syu`, do that, then run the second command.

To use the `ai` command, you'll also need:

- **Python** installed and available on your `PATH`
- A Grok API key, saved in a `.env` file in the project root as `AI_API_KEY=your_key_here` (uses the `grok-3-mini` model by default). Without this, every other feature works fine, but the `ai` command will return "Error calling API".

## Build

In the MSYS2 UCRT64 terminal, go to the project folder (adjust the path to wherever you cloned it):

```bash
cd /c/Users/samar/OneDrive/Desktop/VirtualOS
```

Compile:

```bash
gcc main.c terminal.c src/algorithms.c -o virtualos $(pkg-config --cflags --libs gtk4)
```

> Note: `src/algorithms.c` must be included — `main.c` depends on the data structures and algorithms defined there (linked list, dynamic array, bubble sort, binary search). Leaving it out will cause a linker error.

Alternatively, just run the included build script from the project root:

```bash
./build.sh
```

## Run

```bash
./virtualos
```

On Windows, you can also run:

```bash
./virtualos.exe
```

## Terminal Commands

Inside the virtual terminal:

```text
help
clear
whoami
date
pwd
ls
ls folder
cd folder
cd ..
cd /
touch file.txt
touch folder/file.txt
mkdir folder
mkdir folder/inside
cat file.txt
write file.txt hello world
append file.txt more text
cp source.txt target.txt
mv old.txt new.txt
rm file.txt
rmdir folder
```

## Virtual Files

Files created by the virtual terminal are stored in:

```text
virtual_home/
```

This folder is ignored by git because it is runtime data.

## AI Button / `ai` Command

The desktop has an `AI` button on the right side, and the terminal also supports an `ai <prompt>` command. Both call out to `ai_api.py`, which sends your prompt to the Grok API (`grok-3-mini` by default) using the key from your `.env` file.

Example prompts:

```text
create file notes.txt
create folder docs
write hello world in notes.txt
delete notes.txt
open notes.txt
```
