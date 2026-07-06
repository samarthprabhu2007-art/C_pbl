/*
 * algorithms.c - VirtualOS Data Structures & Algorithms Implementation
 *
 * Implements the following for the VirtualOS project:
 *   - Singly Linked List  (FileNode) for file/folder entries & history
 *   - Dynamic Array       (FileArray) with realloc-based growth
 *   - Bubble Sort         on FileArray (ascending / descending)
 *   - Binary Search       lower_bound and upper_bound on sorted FileArray
 *
 * All dynamic memory uses standard C: malloc / realloc / free.
 */

#include "algorithms.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 1 — LINKED LIST
 * ══════════════════════════════════════════════════════════════════════════
 *
 *  A singly linked list where each node stores one filename and a flag
 *  indicating whether it is a directory.
 *
 *  Memory layout of a single node:
 *
 *    ┌──────────────┬─────────┬──────┐
 *    │  name (ptr)  │ is_dir  │ next │
 *    └──────────────┴─────────┴──────┘
 *            │                   │
 *            ▼                   ▼
 *        heap string         next node
 */

/* Prepend: O(1) — new node inserted at head */
FileNode *file_list_prepend(FileNode *head, const char *name, int is_dir)
{
    /* Dynamic Memory Allocation: each node is individually heap-allocated */
    FileNode *node = (FileNode *)malloc(sizeof(FileNode));
    if (!node) {
        fprintf(stderr, "[algorithms] malloc failed in file_list_prepend\n");
        return head;
    }

    /* Also DMA for the name string — strdup = malloc + strcpy */
    node->name   = strdup(name);
    node->is_dir = is_dir;
    node->next   = head;   /* New node points to the old head */

    return node;           /* New node becomes the new head */
}

/* Append: O(n) — walk to the tail and link new node */
FileNode *file_list_append(FileNode *head, const char *name, int is_dir)
{
    FileNode *node = (FileNode *)malloc(sizeof(FileNode));
    if (!node) {
        fprintf(stderr, "[algorithms] malloc failed in file_list_append\n");
        return head;
    }
    node->name   = strdup(name);
    node->is_dir = is_dir;
    node->next   = NULL;    /* New node is the new tail */

    if (head == NULL) {
        return node;        /* List was empty — new node is the head */
    }

    /* Walk to the current tail */
    FileNode *cur = head;
    while (cur->next != NULL) {
        cur = cur->next;
    }
    cur->next = node;
    return head;
}

/* Free: walk the list and free every node and its 'name' string */
void file_list_free(FileNode *head)
{
    while (head != NULL) {
        FileNode *next = head->next;
        free(head->name);   /* Free the heap-allocated name string first */
        free(head);         /* Then free the node itself */
        head = next;
    }
}

/* Length: O(n) linear walk */
int file_list_length(const FileNode *head)
{
    int count = 0;
    while (head != NULL) {
        count++;
        head = head->next;
    }
    return count;
}


/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 2 — DYNAMIC ARRAY
 * ══════════════════════════════════════════════════════════════════════════
 *
 *  A heap-allocated array of char* pointers that doubles its capacity
 *  whenever it runs out of space.  This is the standard "amortised O(1)
 *  append" approach used by std::vector in C++.
 *
 *  Growth strategy:
 *    initial capacity = 8
 *    when full: new_capacity = old_capacity * 2   (via realloc)
 *
 *  Memory layout:
 *
 *    FileArray.data  →  [ ptr0 | ptr1 | ptr2 | ... | NULL slots ]
 *                          │       │
 *                          ▼       ▼
 *                       "abc"   "def"    ← individual heap strings
 */

#define FILEARRAY_INITIAL_CAPACITY 8

void file_array_init(FileArray *arr)
{
    arr->count    = 0;
    arr->capacity = FILEARRAY_INITIAL_CAPACITY;

    /* Initial DMA: allocate space for FILEARRAY_INITIAL_CAPACITY pointers */
    arr->data = (char **)malloc(sizeof(char *) * arr->capacity);
    if (!arr->data) {
        fprintf(stderr, "[algorithms] malloc failed in file_array_init\n");
        arr->capacity = 0;
    }
}

/* Append a copy of 'name' to the array.
 * If capacity is exhausted, doubles the allocation with realloc. */
void file_array_append(FileArray *arr, const char *name)
{
    if (!arr->data) return;

    /* ── GROW if needed ───────────────────────────────────────────────── */
    if (arr->count == arr->capacity) {
        int new_capacity = arr->capacity * 2;

        /* realloc: resize the pointer array — existing pointers preserved */
        char **new_data = (char **)realloc(arr->data,
                                            sizeof(char *) * new_capacity);
        if (!new_data) {
            fprintf(stderr, "[algorithms] realloc failed in file_array_append\n");
            return; /* Out of memory — skip this entry */
        }
        arr->data     = new_data;
        arr->capacity = new_capacity;
    }

    /* DMA: store a copy of the string (strdup = malloc + strcpy) */
    arr->data[arr->count] = strdup(name);
    arr->count++;
}

void file_array_free(FileArray *arr)
{
    for (int i = 0; i < arr->count; i++) {
        free(arr->data[i]);   /* Free each individual string */
    }
    free(arr->data);          /* Free the pointer array itself */
    arr->data     = NULL;
    arr->count    = 0;
    arr->capacity = 0;
}


/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 3 — BUBBLE SORT
 * ══════════════════════════════════════════════════════════════════════════
 *
 *  Classic Bubble Sort on arr->data[0..n-1].
 *
 *  Algorithm:
 *    Repeat n-1 passes over the array.
 *    In each pass, compare adjacent elements.  If they are in the wrong
 *    order, swap them.  After each pass the largest unsorted element
 *    "bubbles up" to its correct position.
 *    Optimisation: if a full pass produces zero swaps, the array is already
 *    sorted — exit early (best case O(n)).
 *
 *  ascending == 1 → A before Z  (strcmp < 0 means correct order)
 *  ascending == 0 → Z before A  (strcmp > 0 means correct order)
 */
void bubble_sort_files(FileArray *arr, int ascending)
{
    int n = arr->count;
    if (n < 2) return;   /* 0 or 1 element — always sorted */

    for (int pass = 0; pass < n - 1; pass++) {
        int swapped = 0;  /* Optimisation flag */

        /* Inner pass: compare arr[j] with arr[j+1] */
        for (int j = 0; j < n - 1 - pass; j++) {
            int cmp = strcmp(arr->data[j], arr->data[j + 1]);

            /* Decide whether this pair is out of order */
            int out_of_order = ascending ? (cmp > 0) : (cmp < 0);

            if (out_of_order) {
                /* Swap the two string pointers (NOT the strings themselves) */
                char *tmp          = arr->data[j];
                arr->data[j]       = arr->data[j + 1];
                arr->data[j + 1]   = tmp;
                swapped = 1;
            }
        }

        /* Early exit: no swaps in this pass means the array is sorted */
        if (!swapped) break;
    }
}


/* ══════════════════════════════════════════════════════════════════════════
 *  SECTION 4 & 5 — BINARY SEARCH (Lower Bound & Upper Bound)
 * ══════════════════════════════════════════════════════════════════════════
 *
 *  Both functions assume arr is sorted in ASCENDING order.
 *
 *  They find the range [lower, upper) of entries whose names START WITH
 *  'prefix' (case-insensitive prefix match).
 *
 *  Lower Bound:
 *    Returns the index of the FIRST element whose name is >= prefix.
 *    Uses strncasecmp(arr->data[mid], prefix, prefix_len).
 *    If result < 0  →  mid is before the range, search right half.
 *    If result >= 0 →  mid could be the answer or earlier, search left half.
 *
 *  Upper Bound:
 *    Returns the index ONE PAST the LAST element that starts with prefix.
 *    Same idea but looks for the first element that is CLEARLY AFTER prefix.
 *
 *  Caller iterates arr->data[lower_bound .. upper_bound - 1] for matches.
 */

/* Helper: portable case-insensitive comparison */
static int prefix_cmp(const char *name, const char *prefix, size_t prefix_len)
{
#ifdef _WIN32
    return _strnicmp(name, prefix, prefix_len);
#else
    return strncasecmp(name, prefix, prefix_len);
#endif
}

int binary_search_lower_bound(const FileArray *arr, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    int lo = 0, hi = arr->count;   /* Search range: [lo, hi) */

    /*
     * Loop invariant:
     *   arr->data[lo-1]  starts before prefix  (or lo == 0)
     *   arr->data[hi]    starts at or after prefix (or hi == count)
     */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;   /* Avoid integer overflow */
        int cmp = prefix_cmp(arr->data[mid], prefix, prefix_len);

        if (cmp < 0) {
            lo = mid + 1;  /* mid is before the range — search right */
        } else {
            hi = mid;      /* mid could be the answer — narrow left */
        }
    }
    return lo;
}

int binary_search_upper_bound(const FileArray *arr, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    int lo = 0, hi = arr->count;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = prefix_cmp(arr->data[mid], prefix, prefix_len);

        if (cmp <= 0) {
            /*
             * If cmp == 0 the name starts with prefix — could still have
             * more matches to the right.  If cmp < 0 name is before prefix
             * (shouldn't happen if lower_bound was used first, but safe).
             */
            lo = mid + 1;
        } else {
            hi = mid;      /* mid is clearly past the prefix — narrow left */
        }
    }
    return lo;
}
