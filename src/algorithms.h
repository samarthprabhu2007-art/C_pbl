/*
 * algorithms.h - VirtualOS Data Structures & Algorithms Module
 *
 * This header declares all custom data structures and algorithms used
 * throughout the VirtualOS project:
 *
 *   1. FileNode (Linked List) - Stores folder contents as a singly linked list.
 *      Used to navigate folder history (back/forward) and display file entries.
 *
 *   2. FileArray (Dynamic Array) - Dynamically allocated, resizable array of
 *      filenames. Used as the sorted pool for the search bar and sort feature.
 *
 *   3. bubble_sort_files() - Sorts a FileArray in ascending or descending
 *      order using the Bubble Sort algorithm.
 *
 *   4. binary_search_lower_bound() - Finds the first filename in a sorted
 *      FileArray that starts with a given prefix (lower bound).
 *
 *   5. binary_search_upper_bound() - Finds one past the last filename in a
 *      sorted FileArray that starts with a given prefix (upper bound).
 *
 *   All dynamic allocations use malloc/realloc/free (manual DMA) for
 *   educational demonstration, NOT glib helpers.
 */

#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include <stddef.h>

/* ─── 1. LINKED LIST ────────────────────────────────────────────────────────
 *
 * A singly linked list node representing one file or folder entry.
 * Each node owns its 'name' string (heap allocated).
 * The list is used to:
 *   - Hold the ordered contents of a folder for rendering.
 *   - Build the navigation history stack so the user can go "back".
 */
typedef struct FileNode {
    char            *name;      /* Dynamically allocated filename string    */
    int              is_dir;    /* 1 if directory, 0 if regular file        */
    struct FileNode *next;      /* Pointer to the next node in the list     */
} FileNode;

/* Prepend a new node to the front of a linked list.
 * Returns the new head. Caller owns the list. */
FileNode *file_list_prepend(FileNode *head, const char *name, int is_dir);

/* Append a new node to the end of a linked list.
 * Returns the (possibly new) head. Caller owns the list. */
FileNode *file_list_append(FileNode *head, const char *name, int is_dir);

/* Free every node in a linked list (also frees 'name' strings). */
void file_list_free(FileNode *head);

/* Return the number of nodes in the list. */
int file_list_length(const FileNode *head);


/* ─── 2. DYNAMIC ARRAY ──────────────────────────────────────────────────────
 *
 * A heap-allocated, growable array of C strings.
 * Capacity doubles when the array is full (amortised O(1) append).
 * Used as the sorted pool that the binary search operates on.
 */
typedef struct {
    char  **data;       /* Dynamically allocated array of string pointers   */
    int     count;      /* Number of valid entries currently stored          */
    int     capacity;   /* Total allocated slots (always >= count)           */
} FileArray;

/* Initialise an empty FileArray (must call file_array_free when done). */
void file_array_init(FileArray *arr);

/* Append a copy of 'name' to the array, growing storage if needed.
 * Algorithm: if count == capacity, realloc to 2*capacity. */
void file_array_append(FileArray *arr, const char *name);

/* Free all memory owned by the FileArray. Does NOT free the struct itself. */
void file_array_free(FileArray *arr);


/* ─── 3. BUBBLE SORT ────────────────────────────────────────────────────────
 *
 * Sorts arr->data[0..arr->count-1] in place.
 *   ascending == 1  →  A-Z order
 *   ascending == 0  →  Z-A order
 *
 * Time complexity: O(n²) worst-case, O(n) best-case (already sorted).
 * The algorithm repeatedly compares adjacent pairs and swaps them
 * until no more swaps are needed in a full pass.
 */
void bubble_sort_files(FileArray *arr, int ascending);


/* ─── 4 & 5. BINARY SEARCH (Lower & Upper Bound) ───────────────────────────
 *
 * Both functions require the array to be sorted in ascending order first.
 * They implement the classic lower-bound / upper-bound binary search to
 * efficiently find the range of filenames that start with 'prefix'.
 *
 * lower_bound → index of the first element where name >= prefix
 * upper_bound → index one past the last element where name starts with prefix
 *
 * The caller iterates arr->data[lower .. upper-1] to get all matches.
 *
 * Time complexity: O(log n) per call.
 */
int binary_search_lower_bound(const FileArray *arr, const char *prefix);
int binary_search_upper_bound(const FileArray *arr, const char *prefix);

#endif /* ALGORITHMS_H */
