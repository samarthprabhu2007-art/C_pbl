# C EL Virtual OS

A small GTK4 virtual desktop written in C. It includes a wallpaper desktop, terminal icon, virtual filesystem commands, clickable file/folder icons, editable text files, draggable icons, and a simple AI command helper panel.

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

## Build

In the MSYS2 UCRT64 terminal, go to the project folder:

```bash
cd /c/Users/samar/OneDrive/Desktop/VirtualOS
```

Compile:

```bash
gcc main.c terminal.c -o virtualos $(pkg-config --cflags --libs gtk4)
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

## AI Button

The desktop has an `AI` button on the right side. It currently has a local simple-English parser. Example prompts:

```text
create file notes.txt
create folder docs
write hello world in notes.txt
delete notes.txt
open notes.txt
```

Later, a Grok API call can be added inside `run_ai_request()` in `main.c`.
