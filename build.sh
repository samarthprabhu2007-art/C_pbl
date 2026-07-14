#!/bin/bash
# VirtualOS Build Script
# ─────────────────────────────────────────────────────
# Source layout (all C files now live under src/):
#
#   src/
#     main.c          ← Desktop GUI, search bar, folder windows
#     terminal.c      ← Terminal emulator, Gemini AI integration
#     terminal.h      ← Public API for terminal module
#     algorithms.h    ← Linked list, dynamic array, bubble sort, binary search (header)
#     algorithms.c    ← Implementation of all CS algorithms
#     filesystem.h    ← Virtual filesystem helpers
#
# The root main.c / terminal.c are the BUILD SOURCE (keep synced with src/).
# ─────────────────────────────────────────────────────

export PATH="/ucrt64/bin:$PATH"
cd "/d/C PBL/C_pbl"

echo "=== Building VirtualOS ==="
echo "  Algorithms : Bubble Sort, Binary Search (lower/upper bound)"
echo "  Data Structs: Linked List (NavNode, FileNode), Dynamic Array (FileArray)"
echo ""

gcc main.c terminal.c src/algorithms.c \
    -o virtualos \
    $(pkg-config --cflags --libs gtk4) \
    -Wall -Wextra -Wno-unused-parameter -mconsole \
    2>&1

EXIT=$?
if [ $EXIT -eq 0 ]; then
    echo ""
    echo "✓ Build successful! Run: ./virtualos"
else
    echo ""
    echo "✗ Build FAILED (exit code $EXIT)"
fi
exit $EXIT
