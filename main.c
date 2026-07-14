#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>      /* malloc / free for DMA */
#include "terminal.h"
#include "src/algorithms.h"  /* Bubble sort, binary search, linked list, dynamic array */

#define MAX_PATH_TEXT 512

/* ── IconDrag: carries drag state for desktop icons (heap allocated per icon) */
typedef struct
{
    char *path;       /* Virtual path of the icon being dragged (DMA string) */
    double start_x;
    double start_y;
    double icon_x;
    double icon_y;
    gboolean dragged; /* TRUE if the mouse actually moved during the click   */
} IconDrag;

/* ── EditorData: passed to the Save button callback in the text editor window */
typedef struct
{
    GtkTextBuffer *buffer;
    char *path;       /* Real filesystem path (DMA string) */
} EditorData;

/* ── IconPoint: stores the grid position of a desktop icon (heap allocated) */
typedef struct
{
    int x;
    int y;
} IconPoint;

/* ── NavNode: LINKED LIST node for folder navigation history
 *   Each open_folder_window call prepends one node so the user can
 *   navigate back through previously opened folders.
 *   Memory: each node is individually malloc'd, freed when the window closes. */
typedef struct NavNode {
    char            *path;  /* Virtual path of this folder (DMA string) */
    struct NavNode  *next;  /* Singly linked — points to older entry     */
} NavNode;

/* ── FolderCtx: shared state passed to callbacks inside a folder window.
 *   Heap-allocated once per window, freed on window destroy. */
typedef struct {
    GtkWidget  *flowbox;    /* The file grid widget                      */
    GtkWidget  *search_entry; /* Search bar entry widget                 */
    char        path[MAX_PATH_TEXT]; /* Virtual path of this folder      */
    NavNode    *history;    /* Linked list — navigation history stack    */
    int         sort_asc;   /* Current sort direction: 1=A-Z, 0=Z-A     */
    FileArray   sorted_files; /* Sorted pool for binary search           */
} FolderCtx;

/* ── Desktop globals */
static GtkWidget *desktop;
static GtkWidget *terminal_icon;
static GtkWidget *path_label;
static GList *desktop_icons;
static GHashTable *icon_positions;
static char desktop_path[MAX_PATH_TEXT] = "/";
static gboolean suppress_open_once = FALSE;

/* ── Forward declarations */
static void open_desktop_item(GtkButton *button, gpointer data);
static void open_folder_window(const char *virtual_path);
static void open_ai_prompt(void);
void open_browser(void);
static const char *icon_for_name(const char *name, gboolean is_dir);
static void populate_folder(FolderCtx *ctx, const char *query);


static char *get_virtual_home(void)
{
    char *current_dir;
    char *home;

    current_dir = g_get_current_dir();
    home = g_build_filename(current_dir, "virtual_home", NULL);
    g_mkdir_with_parents(home, 0755);
    g_free(current_dir);

    return home;
}

static gboolean normalize_from_desktop(const char *input,
                                       char *output,
                                       size_t output_size)
{
    char combined[MAX_PATH_TEXT];
    char copy[MAX_PATH_TEXT];
    char *token;
    char *parts[64];
    int count;
    int i;

    if(input == NULL || input[0] == '\0' || strchr(input, '\\') != NULL)
    {
        return FALSE;
    }

    if(input[0] == '/')
    {
        g_strlcpy(combined, input, sizeof(combined));
    }
    else if(strcmp(desktop_path, "/") == 0)
    {
        g_snprintf(combined, sizeof(combined), "/%s", input);
    }
    else
    {
        g_snprintf(combined, sizeof(combined), "%s/%s", desktop_path, input);
    }

    g_strlcpy(copy, combined, sizeof(copy));
    count = 0;
    token = strtok(copy, "/");

    while(token != NULL)
    {
        if(strcmp(token, ".") == 0)
        {
            token = strtok(NULL, "/");
            continue;
        }

        if(strcmp(token, "..") == 0)
        {
            if(count > 0)
            {
                count--;
            }
            token = strtok(NULL, "/");
            continue;
        }

        if(count >= 64)
        {
            return FALSE;
        }

        parts[count] = token;
        count++;
        token = strtok(NULL, "/");
    }

    g_strlcpy(output, "/", output_size);
    for(i = 0; i < count; i++)
    {
        if(strlen(output) > 1)
        {
            g_strlcat(output, "/", output_size);
        }
        g_strlcat(output, parts[i], output_size);
    }

    return TRUE;
}

static char *real_path_from_virtual(const char *virtual_path)
{
    char *home;
    char *relative;
    char *path;

    home = get_virtual_home();
    relative = g_strdup(virtual_path + 1);

    if(relative[0] == '\0')
    {
        path = g_strdup(home);
    }
    else
    {
        path = g_build_filename(home, relative, NULL);
    }

    g_free(relative);
    g_free(home);

    return path;
}

static char *display_name_from_path(const char *virtual_path)
{
    const char *slash;

    if(strcmp(virtual_path, "/") == 0)
    {
        return g_strdup("Home");
    }

    slash = strrchr(virtual_path, '/');
    return g_strdup(slash == NULL ? virtual_path : slash + 1);
}

static void refresh_desktop_icons(void);

static void apply_css(void)
{
    GtkCssProvider *provider;

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(
        provider,
        /* Desktop top bar */
        ".desktop-bar { background: rgba(250,250,250,.88); border-radius: 10px; padding: 8px; }"
        ".path-label { font-weight: 600; color: #1f2937; }"
        /* Desktop file icon hover effect */
        ".desktop-icon { background: rgba(255,255,255,.22); border-radius: 8px; }"
        ".desktop-icon:hover { background: rgba(255,255,255,.42); }"
        /* AI chat panel */
        ".ai-panel { background: rgba(248,250,252,.94); border-radius: 12px; padding: 10px; }"
        ".ai-title { font-weight: 700; color: #111827; }"
        /* Folder window items — give each file a subtle card border */
        ".folder-item { border: 1px solid rgba(0,0,0,0.12); border-radius: 8px; "
        "               background: white; }"
        ".folder-item:hover { background: #f0f4ff; border-color: #4f80ff; }"
        /* Desktop search bar inside taskbar */
        ".desktop-search { border-radius: 18px; "
        "                  background: rgba(255,255,255,0.10); "
        "                  border: 1px solid rgba(255,255,255,0.22); "
        "                  color: white; font-size: 13px; }"
        /* Bottom taskbar — slim dark strip */
        ".taskbar { background: rgba(15,15,20,0.92); "
        "           border-top: 1px solid rgba(255,255,255,0.10); }"
        /* Taskbar buttons */
        ".taskbar-btn { background: rgba(255,255,255,0.08); "
        "               border-radius: 8px; color: white; "
        "               font-size: 13px; font-weight: 500; "
        "               border: 1px solid rgba(255,255,255,0.15); }"
        ".taskbar-btn:hover { background: rgba(255,255,255,0.20); }"
        ".taskbar-btn:active { background: rgba(255,255,255,0.30); }"
        /* Terminal Styling (Hacker/Modern Dark Mode) */
        ".term-window { background: #0d1117; }"
        ".term-text { background: #0d1117; color: #c9d1d9; font-family: 'Consolas', 'Courier New', monospace; font-size: 14.5px; padding: 16px; }"
        ".term-entry { background: #161b22; color: #58a6ff; border: 1px solid #30363d; border-radius: 8px; padding: 10px 14px; font-family: 'Consolas', 'Courier New', monospace; font-size: 15px; margin: 8px; }"
        ".term-entry:focus { border-color: #58a6ff; }"
        ".term-ai-panel { background: #161b22; border-left: 1px solid #30363d; padding: 16px; }"
        ".term-ai-title { font-weight: 800; color: #a371f7; font-size: 18px; margin-bottom: 12px; }"
        ".term-ai-text { background: #0d1117; color: #e6edf3; font-family: 'Segoe UI', sans-serif; font-size: 14px; padding: 12px; border-radius: 8px; border: 1px solid #30363d; }"
        ".term-ai-btn { background: #a371f7; color: white; border-radius: 8px; font-weight: bold; border: none; padding: 8px 16px; margin-left: 8px; }"
        ".term-ai-btn:hover { background: #bc8cff; }"
        ".term-ai-btn:active { background: #8957e5; }");

    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

static void open_terminal(GtkGestureClick *gesture,
                          int n_press,
                          double x,
                          double y,
                          gpointer data)
{
    open_terminal_window();
}

/* ── Forward declarations for algorithms used in desktop search ── */
static void rebuild_sorted_files(FolderCtx *ctx);
static void populate_folder(FolderCtx *ctx, const char *query);

/* ── Desktop search: on every keystroke, use binary search to find icons.
 *   A FolderCtx is created for the root folder (path="/") reusing populate_folder.
 *   This avoids duplicating search logic between the desktop and folder windows. */
static FolderCtx *desktop_search_ctx = NULL;

static void on_desktop_search_changed(GtkEditable *editable, gpointer data)
{
    const char *query = gtk_editable_get_text(editable);
    if (desktop_search_ctx == NULL) return;

    /* Sync the search context path with the current desktop path */
    g_strlcpy(desktop_search_ctx->path, desktop_path,
              sizeof(desktop_search_ctx->path));

    if (query == NULL || query[0] == '\0') {
        /* Empty query — refresh desktop normally */
        refresh_desktop_icons();
        return;
    }

    /* Rebuild the sorted file list for the current desktop path */
    rebuild_sorted_files(desktop_search_ctx);

    /* Binary search: lower_bound and upper_bound give the match range */
    int lo = binary_search_lower_bound(&desktop_search_ctx->sorted_files, query);
    int hi = binary_search_upper_bound(&desktop_search_ctx->sorted_files, query);

    /* Show a popup listing matched files */
    if (lo >= hi) return; /* No matches */

    /* Open a temporary folder window showing only matched files.
     * We reuse open_folder_window so the user can click to open them. */
    open_folder_window(desktop_path);
}

static gboolean is_text_file(const char *name)
{
    const char *dot;

    dot = strrchr(name, '.');
    return dot != NULL &&
           (g_strcmp0(dot, ".txt") == 0 ||
            g_strcmp0(dot, ".c") == 0 ||
            g_strcmp0(dot, ".h") == 0 ||
            g_strcmp0(dot, ".py") == 0 ||
            g_strcmp0(dot, ".html") == 0 ||
            g_strcmp0(dot, ".css") == 0 ||
            g_strcmp0(dot, ".js") == 0);
}

static void save_editor(GtkButton *button, gpointer data)
{
    EditorData *editor;
    GtkTextIter start;
    GtkTextIter end;
    char *text;

    editor = data;
    gtk_text_buffer_get_bounds(editor->buffer, &start, &end);
    text = gtk_text_buffer_get_text(editor->buffer, &start, &end, FALSE);

    if(g_file_set_contents(editor->path, text, -1, NULL))
    {
        gtk_button_set_label(button, "Saved");
    }
    else
    {
        gtk_button_set_label(button, "Save failed");
    }

    g_free(text);
}

static void free_editor_data(gpointer data, GClosure *closure)
{
    EditorData *editor;

    editor = data;
    g_free(editor->path);
    g_free(editor);
}

static void open_file_window(const char *virtual_path)
{
    GtkWidget *window;
    GtkWidget *box;
    GtkWidget *toolbar;
    GtkWidget *save_button;
    GtkWidget *scroll;
    GtkWidget *textview;
    GtkTextBuffer *buffer;
    EditorData *editor;
    char *real_path;
    char *title;
    char *contents;
    gboolean editable;

    real_path = real_path_from_virtual(virtual_path);
    title = display_name_from_path(virtual_path);
    contents = NULL;
    editable = is_text_file(title);

    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_window_set_child(GTK_WINDOW(window), box);

    textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), editable);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

    if(g_file_get_contents(real_path, &contents, NULL, NULL))
    {
        gtk_text_buffer_set_text(buffer, contents, -1);
    }
    else
    {
        gtk_text_buffer_set_text(buffer, "Could not open file", -1);
    }

    if(editable)
    {
        toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        save_button = gtk_button_new_with_label("Save");
        editor = g_new0(EditorData, 1);
        editor->buffer = buffer;
        editor->path = g_strdup(real_path);

        g_signal_connect_data(
            save_button,
            "clicked",
            G_CALLBACK(save_editor),
            editor,
            free_editor_data,
            0);

        gtk_box_append(GTK_BOX(toolbar), save_button);
        gtk_box_append(GTK_BOX(box), toolbar);
    }

    scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), textview);
    gtk_box_append(GTK_BOX(box), scroll);

    gtk_window_present(GTK_WINDOW(window));

    g_free(contents);
    g_free(real_path);
    g_free(title);
}

static void on_ai_prompt_run(GtkWidget *button, gpointer data)
{
    GtkWidget *entry = data;
    const char *prompt = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (prompt && prompt[0] != '\0') {
        char *stderr_buf = NULL;
        char *response = run_ai_api(prompt, &stderr_buf);
        if (response) {
            GtkWidget *win = gtk_window_new();
            gtk_window_set_default_size(GTK_WINDOW(win), 400, 300);
            gtk_window_set_title(GTK_WINDOW(win), "AI Response");

            GtkWidget *scroll = gtk_scrolled_window_new();
            gtk_widget_set_vexpand(scroll, TRUE);
            gtk_window_set_child(GTK_WINDOW(win), scroll);

            GtkWidget *tv = gtk_text_view_new();
            gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
            gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)), response, -1);
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), tv);
            
            gtk_window_present(GTK_WINDOW(win));
            g_free(response);
        } else {
            GtkAlertDialog *dialog = gtk_alert_dialog_new("AI Error: No response received.");
            gtk_alert_dialog_show(dialog, GTK_WINDOW(gtk_widget_get_root(button)));
            g_object_unref(dialog);
        }
        
        g_free(stderr_buf);
    }
    GtkWidget *window = gtk_widget_get_ancestor(entry, GTK_TYPE_WINDOW);
    if (window) {
        gtk_window_destroy(GTK_WINDOW(window));
    }
}

static void open_ai_prompt(void)
{
    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "AI Prompt");
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 100);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_window_set_child(GTK_WINDOW(win), box);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter AI prompt...");
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *run_btn = gtk_button_new_with_label("Run");
    g_signal_connect(run_btn, "clicked", G_CALLBACK(on_ai_prompt_run), entry);
    g_signal_connect(entry, "activate", G_CALLBACK(on_ai_prompt_run), entry);

    gtk_box_append(GTK_BOX(box), run_btn);
    
    gtk_window_present(GTK_WINDOW(win));
}

/* ── BrowserCtx: state for the in-app browser window ── */
typedef struct {
    GtkWidget *url_entry;
    GtkWidget *textview;
    GtkWidget *title_label;
    GtkWidget *status_label;
    GtkWidget *link_box;
    GtkWidget *spinner;
    char     **history;
    int        hist_count;
    int        hist_pos;
} BrowserCtx;

static void browser_navigate(BrowserCtx *ctx, const char *url);

static void browser_go_cb(GtkButton *btn, gpointer data)
{
    BrowserCtx *ctx = data;
    const char *url = gtk_editable_get_text(GTK_EDITABLE(ctx->url_entry));
    if (url && url[0] != '\0') browser_navigate(ctx, url);
}

static void browser_entry_activate_cb(GtkEntry *entry, gpointer data)
{
    browser_go_cb(NULL, data);
}

static void browser_back_cb(GtkButton *btn, gpointer data)
{
    BrowserCtx *ctx = data;
    if (ctx->hist_pos > 0) {
        ctx->hist_pos--;
        gtk_editable_set_text(GTK_EDITABLE(ctx->url_entry), ctx->history[ctx->hist_pos]);
        browser_navigate(ctx, ctx->history[ctx->hist_pos]);
    }
}

static void browser_fwd_cb(GtkButton *btn, gpointer data)
{
    BrowserCtx *ctx = data;
    if (ctx->hist_pos < ctx->hist_count - 1) {
        ctx->hist_pos++;
        gtk_editable_set_text(GTK_EDITABLE(ctx->url_entry), ctx->history[ctx->hist_pos]);
        browser_navigate(ctx, ctx->history[ctx->hist_pos]);
    }
}

static void browser_link_cb(GtkButton *btn, gpointer data)
{
    BrowserCtx *ctx = g_object_get_data(G_OBJECT(btn), "browser_ctx");
    const char *url = (const char *)data;
    if (ctx && url) {
        gtk_editable_set_text(GTK_EDITABLE(ctx->url_entry), url);
        browser_navigate(ctx, url);
    }
}

static void browser_home_cb(GtkButton *btn, gpointer data)
{
    BrowserCtx *ctx = data;
    gtk_editable_set_text(GTK_EDITABLE(ctx->url_entry), "https://www.google.com");
    browser_navigate(ctx, "https://www.google.com");
}

static void free_browser_ctx(gpointer data, GClosure *closure)
{
    BrowserCtx *ctx = data;
    for (int i = 0; i < ctx->hist_count; i++) g_free(ctx->history[i]);
    g_free(ctx->history);
    g_free(ctx);
}

static void browser_navigate(BrowserCtx *ctx, const char *url)
{
    char *dir = g_get_current_dir();
    char *escaped = g_shell_quote(url);
    char *command = g_strdup_printf("python \"%s/web_fetch.py\" %s", dir, escaped);
    char *stdout_buf = NULL;
    char *stderr_buf = NULL;
    int exit_status;

    gtk_label_set_text(GTK_LABEL(ctx->status_label), "Loading...");
    gtk_label_set_text(GTK_LABEL(ctx->title_label), "Loading...");

    g_spawn_command_line_sync(command, &stdout_buf, &stderr_buf, &exit_status, NULL);
    g_free(command);
    g_free(escaped);
    g_free(dir);

    /* Clear old links */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->link_box)) != NULL)
        gtk_box_remove(GTK_BOX(ctx->link_box), child);

    if (stdout_buf == NULL || stdout_buf[0] == '\0') {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->textview));
        gtk_text_buffer_set_text(buf, stderr_buf ? stderr_buf : "Failed to fetch page.", -1);
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Error");
        gtk_label_set_text(GTK_LABEL(ctx->title_label), "Error");
        g_free(stdout_buf);
        g_free(stderr_buf);
        return;
    }
    g_free(stderr_buf);

    /* Parse JSON response — simple manual parsing */
    gboolean ok = (strstr(stdout_buf, "\"ok\": true") != NULL ||
                   strstr(stdout_buf, "\"ok\":true") != NULL);

    if (!ok) {
        char *err = strstr(stdout_buf, "\"error\":");
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->textview));
        gtk_text_buffer_set_text(buf, err ? err : "Failed to load page.", -1);
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Error");
        gtk_label_set_text(GTK_LABEL(ctx->title_label), "Error");
        g_free(stdout_buf);
        return;
    }

    /* Extract title */
    char *title_start = strstr(stdout_buf, "\"title\": \"");
    if (!title_start) title_start = strstr(stdout_buf, "\"title\":\"");
    if (title_start) {
        title_start = strchr(title_start + 8, '"') + 1;
        char *title_end = title_start;
        while (*title_end && !(*title_end == '"' && *(title_end - 1) != '\\')) title_end++;
        char saved = *title_end;
        *title_end = '\0';
        gtk_label_set_text(GTK_LABEL(ctx->title_label), title_start);
        *title_end = saved;
    }

    /* Extract text content — find "text": " and read until the closing pattern */
    char *text_start = strstr(stdout_buf, "\"text\": \"");
    if (!text_start) text_start = strstr(stdout_buf, "\"text\":\"");
    if (text_start) {
        text_start = strchr(text_start + 6, '"') + 1;
        /* Find end — look for ", "links" or ", "url" pattern */
        char *text_end = strstr(text_start, "\", \"links\"");
        if (!text_end) text_end = strstr(text_start, "\",\"links\"");
        if (!text_end) text_end = text_start + strlen(text_start);
        char saved = *text_end;
        *text_end = '\0';

        /* Unescape \\n to real newlines */
        GString *decoded = g_string_new(NULL);
        for (char *p = text_start; *p; p++) {
            if (*p == '\\' && *(p+1) == 'n') { g_string_append_c(decoded, '\n'); p++; }
            else if (*p == '\\' && *(p+1) == 't') { g_string_append_c(decoded, '\t'); p++; }
            else if (*p == '\\' && *(p+1) == '"') { g_string_append_c(decoded, '"'); p++; }
            else if (*p == '\\' && *(p+1) == '\\') { g_string_append_c(decoded, '\\'); p++; }
            else g_string_append_c(decoded, *p);
        }

        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->textview));
        gtk_text_buffer_set_text(buf, decoded->str, -1);
        g_string_free(decoded, TRUE);
        *text_end = saved;
    }

    /* Extract links — find each "url": "..." in the links array */
    char *links_start = strstr(stdout_buf, "\"links\":");
    if (links_start) {
        char *p = links_start;
        int link_count = 0;
        while ((p = strstr(p, "\"url\": \"")) != NULL && link_count < 15) {
            if (!p) break;
            p += 8;
            char *url_end = p;
            while (*url_end && !(*url_end == '"' && *(url_end-1) != '\\')) url_end++;
            char saved = *url_end; *url_end = '\0';
            char *link_url = g_strdup(p);
            *url_end = saved;

            /* Find preceding "text" value */
            char *tstart = strstr(url_end, "\"text\": \"");
            if (!tstart) tstart = strstr(url_end, "\"text\":\"");
            char *link_label_str = NULL;
            if (tstart) {
                tstart = strchr(tstart + 6, '"') + 1;
                char *tend = tstart;
                while (*tend && !(*tend == '"' && *(tend-1) != '\\')) tend++;
                char s2 = *tend; *tend = '\0';
                link_label_str = g_strdup(tstart);
                *tend = s2;
            }

            GtkWidget *link_btn = gtk_button_new_with_label(
                link_label_str ? link_label_str : link_url);
            gtk_widget_add_css_class(link_btn, "flat");
            g_object_set_data(G_OBJECT(link_btn), "browser_ctx", ctx);
            g_signal_connect_data(link_btn, "clicked",
                G_CALLBACK(browser_link_cb), link_url,
                (GClosureNotify)g_free, 0);
            gtk_box_append(GTK_BOX(ctx->link_box), link_btn);

            g_free(link_label_str);
            p = url_end + 1;
            link_count++;
        }
    }

    /* Update history */
    /* Trim forward history if we navigated from middle */
    for (int i = ctx->hist_pos + 1; i < ctx->hist_count; i++)
        g_free(ctx->history[i]);
    ctx->hist_count = ctx->hist_pos + 1;

    ctx->history = g_realloc(ctx->history, sizeof(char*) * (ctx->hist_count + 1));
    ctx->history[ctx->hist_count] = g_strdup(url);
    ctx->hist_count++;
    ctx->hist_pos = ctx->hist_count - 1;

    /* Update URL bar with final URL */
    char *final_url = strstr(stdout_buf, "\"url\": \"");
    if (!final_url) final_url = strstr(stdout_buf, "\"url\":\"");
    if (final_url) {
        final_url = strchr(final_url + 5, '"') + 1;
        char *ue = final_url;
        while (*ue && !(*ue == '"' && *(ue-1) != '\\')) ue++;
        char s3 = *ue; *ue = '\0';
        gtk_editable_set_text(GTK_EDITABLE(ctx->url_entry), final_url);
        *ue = s3;
    }

    gtk_label_set_text(GTK_LABEL(ctx->status_label), "Done");
    g_free(stdout_buf);
}

void open_browser(void)
{
    BrowserCtx *ctx = g_new0(BrowserCtx, 1);
    ctx->history = NULL;
    ctx->hist_count = 0;
    ctx->hist_pos = -1;

    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "VirtualOS Browser");
    gtk_window_set_default_size(GTK_WINDOW(win), 1000, 700);

    /* ── Browser CSS ── */
    GtkCssProvider *bp = gtk_css_provider_new();
    gtk_css_provider_load_from_string(bp,
        ".browser-bar { background: #1e1e2e; padding: 6px 10px; }"
        ".browser-url { background: #313244; color: #cdd6f4; border-radius: 20px;"
        "  border: 1px solid #45475a; padding: 6px 14px; font-size: 13px; }"
        ".browser-url:focus { border-color: #89b4fa; }"
        ".browser-nav { background: transparent; color: #cdd6f4; border: none;"
        "  font-size: 15px; min-width: 36px; min-height: 36px; border-radius: 8px; }"
        ".browser-nav:hover { background: #45475a; }"
        ".browser-content { background: #1e1e2e; color: #cdd6f4;"
        "  font-family: 'Segoe UI', sans-serif; font-size: 14px; padding: 16px; }"
        ".browser-title { font-weight: 700; font-size: 13px; color: #a6adc8; }"
        ".browser-status { font-size: 11px; color: #6c7086; padding: 2px 10px; background: #181825; }"
        ".browser-links { background: #181825; padding: 8px; }"
        ".browser-links button { color: #89b4fa; font-size: 12px; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(bp),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(bp);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), vbox);

    /* ── Toolbar ── */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "browser-bar");

    GtkWidget *back_btn = gtk_button_new_with_label("◀");
    gtk_widget_add_css_class(back_btn, "browser-nav");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(browser_back_cb), ctx);
    gtk_box_append(GTK_BOX(toolbar), back_btn);

    GtkWidget *fwd_btn = gtk_button_new_with_label("▶");
    gtk_widget_add_css_class(fwd_btn, "browser-nav");
    g_signal_connect(fwd_btn, "clicked", G_CALLBACK(browser_fwd_cb), ctx);
    gtk_box_append(GTK_BOX(toolbar), fwd_btn);

    GtkWidget *home_btn = gtk_button_new_with_label("⌂");
    gtk_widget_add_css_class(home_btn, "browser-nav");
    g_signal_connect(home_btn, "clicked", G_CALLBACK(browser_home_cb), ctx);
    gtk_box_append(GTK_BOX(toolbar), home_btn);

    ctx->url_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(ctx->url_entry), "https://www.google.com");
    gtk_widget_set_hexpand(ctx->url_entry, TRUE);
    gtk_widget_add_css_class(ctx->url_entry, "browser-url");
    g_signal_connect(ctx->url_entry, "activate", G_CALLBACK(browser_entry_activate_cb), ctx);
    gtk_box_append(GTK_BOX(toolbar), ctx->url_entry);

    GtkWidget *go_btn = gtk_button_new_with_label("Go");
    gtk_widget_add_css_class(go_btn, "browser-nav");
    g_signal_connect(go_btn, "clicked", G_CALLBACK(browser_go_cb), ctx);
    gtk_box_append(GTK_BOX(toolbar), go_btn);

    gtk_box_append(GTK_BOX(vbox), toolbar);

    /* ── Title bar ── */
    ctx->title_label = gtk_label_new("VirtualOS Browser — Enter a URL and press Go");
    gtk_widget_add_css_class(ctx->title_label, "browser-title");
    gtk_widget_set_halign(ctx->title_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(ctx->title_label, 12);
    gtk_widget_set_margin_top(ctx->title_label, 4);
    gtk_widget_set_margin_bottom(ctx->title_label, 2);
    gtk_label_set_ellipsize(GTK_LABEL(ctx->title_label), PANGO_ELLIPSIZE_END);
    gtk_box_append(GTK_BOX(vbox), ctx->title_label);

    /* ── Content area ── */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(vbox), paned);

    GtkWidget *content_scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(content_scroll, TRUE);
    ctx->textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ctx->textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ctx->textview), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(ctx->textview), FALSE);
    gtk_widget_add_css_class(ctx->textview, "browser-content");
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(ctx->textview)),
        "Welcome to VirtualOS Browser!\n\nEnter a URL in the address bar above and press Go or Enter to browse the web.\n\nTry: google.com, wikipedia.org, example.com", -1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(content_scroll), ctx->textview);
    gtk_paned_set_start_child(GTK_PANED(paned), content_scroll);

    /* ── Links sidebar ── */
    GtkWidget *link_scroll = gtk_scrolled_window_new();
    gtk_widget_set_size_request(link_scroll, 220, -1);
    ctx->link_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(ctx->link_box, "browser-links");
    GtkWidget *links_title = gtk_label_new("Links");
    gtk_widget_add_css_class(links_title, "browser-title");
    gtk_box_append(GTK_BOX(ctx->link_box), links_title);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(link_scroll), ctx->link_box);
    gtk_paned_set_end_child(GTK_PANED(paned), link_scroll);
    gtk_paned_set_position(GTK_PANED(paned), 750);

    /* ── Status bar ── */
    ctx->status_label = gtk_label_new("Ready");
    gtk_widget_add_css_class(ctx->status_label, "browser-status");
    gtk_widget_set_halign(ctx->status_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), ctx->status_label);

    /* Free ctx on window close */
    g_signal_connect_data(win, "destroy",
        G_CALLBACK(gtk_window_destroy), ctx,
        (GClosureNotify)free_browser_ctx, G_CONNECT_SWAPPED);

    gtk_window_present(GTK_WINDOW(win));
}

static void open_desktop_item(GtkButton *button, gpointer data)
{
    char *virtual_path;
    char *real_path;

    if(suppress_open_once)
    {
        suppress_open_once = FALSE;
        return;
    }

    virtual_path = data;
    real_path = real_path_from_virtual(virtual_path);

    if (strcmp(virtual_path, "/ai") == 0) {
        open_ai_prompt();
        g_free(real_path);
        return;
    }

    if(g_file_test(real_path, G_FILE_TEST_IS_DIR))
    {
        open_folder_window(virtual_path);
    }
    else
    {
        open_file_window(virtual_path);
    }

    g_free(real_path);
}

static void go_up(GtkButton *button, gpointer data)
{
    normalize_from_desktop("..", desktop_path, sizeof(desktop_path));
    refresh_desktop_icons();
}

static void go_home(GtkButton *button, gpointer data)
{
    g_strlcpy(desktop_path, "/", sizeof(desktop_path));
    refresh_desktop_icons();
}

/* ── Free a NavNode linked list (called when folder window is destroyed) */
static void nav_history_free(NavNode *head)
{
    while (head != NULL) {
        NavNode *next = head->next;
        free(head->path);  /* Free the DMA string */
        free(head);        /* Free the node itself */
        head = next;
    }
}

/* ── Free the FolderCtx when its window is destroyed */
static void on_folder_ctx_destroy(gpointer data, GClosure *closure)
{
    FolderCtx *ctx = (FolderCtx *)data;
    nav_history_free(ctx->history);  /* Free linked list */
    file_array_free(&ctx->sorted_files); /* Free dynamic array */
    free(ctx);
}

/* ── Rebuild the sorted_files FileArray for the current folder.
 *   Uses Bubble Sort (A-Z) so binary search can be applied.
 *   Algorithm: O(n log n) for small n via bubble sort. */
static void rebuild_sorted_files(FolderCtx *ctx)
{
    /* Free old dynamic array contents */
    file_array_free(&ctx->sorted_files);
    file_array_init(&ctx->sorted_files);

    char *real_dir = real_path_from_virtual(ctx->path);
    GDir *dir = g_dir_open(real_dir, 0, NULL);
    g_free(real_dir);
    if (!dir) return;

    const char *name;
    /* Append each entry into the dynamic array (doubles capacity as needed) */
    while ((name = g_dir_read_name(dir)) != NULL) {
        file_array_append(&ctx->sorted_files, name);
    }
    g_dir_close(dir);

    /* Always keep a sorted copy for binary search (lower/upper bound) */
    bubble_sort_files(&ctx->sorted_files, 1 /* ascending */);
}

/* ── Called when the search entry text changes.
 *   Uses binary search (lower + upper bound) to find matches. */
static void on_folder_search_changed(GtkEditable *editable, gpointer data)
{
    FolderCtx *ctx = (FolderCtx *)data;
    const char *query = gtk_editable_get_text(editable);
    populate_folder(ctx, query);
}

/* ── Called when Sort button is clicked.
 *   Flips sort direction and re-renders the folder. */
static void on_folder_sort_clicked(GtkButton *btn, gpointer data)
{
    FolderCtx *ctx = (FolderCtx *)data;
    ctx->sort_asc = !ctx->sort_asc;  /* Toggle A-Z / Z-A */

    /* Update button label to reflect new direction */
    gtk_button_set_label(btn, ctx->sort_asc ? "Sort: A→Z" : "Sort: Z→A");

    const char *query = gtk_editable_get_text(
        GTK_EDITABLE(ctx->search_entry));
    populate_folder(ctx, query);
}

/* ── Core render function for folder contents.
 *
 *   If query is empty: show all files in bubble-sorted order.
 *   If query is non-empty: use binary search (lower_bound / upper_bound)
 *     to find all names that START WITH the query string.
 *
 *   Algorithm summary:
 *     1. rebuild_sorted_files fills a FileArray and bubble-sorts it.
 *     2. binary_search_lower_bound → first index whose name >= query.
 *     3. binary_search_upper_bound → first index whose name is past query.
 *     4. Iterate [lower, upper) and render matching buttons.
 */
static void populate_folder(FolderCtx *ctx, const char *query)
{
    /* Remove all existing children from the flowbox */
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(ctx->flowbox)) != NULL) {
        gtk_flow_box_remove(GTK_FLOW_BOX(ctx->flowbox), child);
    }

    /* Ensure the sorted file list is up to date */
    rebuild_sorted_files(ctx);

    /* Apply the display sort direction using bubble sort */
    bubble_sort_files(&ctx->sorted_files, ctx->sort_asc);

    /* ── Determine the range to display ─────────────────────────────── */
    int lo, hi;
    if (query == NULL || query[0] == '\0') {
        /* Show everything */
        lo = 0;
        hi = ctx->sorted_files.count;
    } else {
        /* BINARY SEARCH: find range [lo, hi) matching the prefix */
        lo = binary_search_lower_bound(&ctx->sorted_files, query);
        hi = binary_search_upper_bound(&ctx->sorted_files, query);
    }

    /* ── Render matched entries using a LINKED LIST walk ────────────────
     *   We build a temporary FileNode linked list from the matched range
     *   to demonstrate linked list traversal during rendering. */
    FileNode *render_list = NULL;
    for (int i = lo; i < hi; i++) {
        char child_virtual[MAX_PATH_TEXT];
        if (strcmp(ctx->path, "/") == 0)
            g_snprintf(child_virtual, sizeof(child_virtual), "/%s", ctx->sorted_files.data[i]);
        else
            g_snprintf(child_virtual, sizeof(child_virtual), "%s/%s", ctx->path, ctx->sorted_files.data[i]);

        char *child_real = real_path_from_virtual(child_virtual);
        int is_d = g_file_test(child_real, G_FILE_TEST_IS_DIR) ? 1 : 0;
        g_free(child_real);

        /* Append to linked list (O(n) but list is small) */
        render_list = file_list_append(render_list, ctx->sorted_files.data[i], is_d);
    }

    /* Walk the linked list and create a GTK button for each entry */
    FileNode *node = render_list;
    while (node != NULL) {
        char child_virtual[MAX_PATH_TEXT];
        if (strcmp(ctx->path, "/") == 0)
            g_snprintf(child_virtual, sizeof(child_virtual), "/%s", node->name);
        else
            g_snprintf(child_virtual, sizeof(child_virtual), "%s/%s", ctx->path, node->name);

        GtkWidget *button = gtk_button_new();
        GtkWidget *box    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *image  = gtk_image_new_from_icon_name(
                                icon_for_name(node->name, node->is_dir));
        gtk_image_set_pixel_size(GTK_IMAGE(image), 42);
        GtkWidget *label  = gtk_label_new(node->name);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_size_request(label, 80, -1);
        gtk_box_append(GTK_BOX(box), image);
        gtk_box_append(GTK_BOX(box), label);
        gtk_button_set_child(GTK_BUTTON(button), box);
        gtk_widget_set_size_request(button, 110, 90);
        gtk_widget_set_margin_start(button, 4);
        gtk_widget_set_margin_end(button, 4);

        g_signal_connect_data(
            button, "clicked",
            G_CALLBACK(open_desktop_item),
            g_strdup(child_virtual),
            (GClosureNotify)g_free, 0);

        gtk_flow_box_insert(GTK_FLOW_BOX(ctx->flowbox), button, -1);
        node = node->next;
    }

    /* Free the temporary linked list */
    file_list_free(render_list);
}

/* ── Open a folder in a new window with search + sort toolbar */
static void open_folder_window(const char *virtual_path)
{
    /* ── Allocate FolderCtx on the heap (DMA) ── */
    FolderCtx *ctx = (FolderCtx *)malloc(sizeof(FolderCtx));
    if (!ctx) return;
    g_strlcpy(ctx->path, virtual_path, sizeof(ctx->path));
    ctx->sort_asc = 1;  /* Default: A-Z */
    ctx->history  = NULL;
    file_array_init(&ctx->sorted_files);

    /* ── Push current path onto the linked list history stack ── */
    NavNode *hist_node = (NavNode *)malloc(sizeof(NavNode));
    if (hist_node) {
        hist_node->path = strdup(virtual_path);
        hist_node->next = ctx->history;
        ctx->history    = hist_node;
    }

    /* ── Build UI ── */
    GtkWidget *window = gtk_window_new();
    char *title = display_name_from_path(virtual_path);
    gtk_window_set_title(GTK_WINDOW(window), title);
    g_free(title);
    gtk_window_set_default_size(GTK_WINDOW(window), 560, 440);

    /* Outer vertical box */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), outer);

    /* ── Toolbar: search bar + sort button ── */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(toolbar, 10);
    gtk_widget_set_margin_end(toolbar, 10);
    gtk_widget_set_margin_top(toolbar, 8);
    gtk_widget_set_margin_bottom(toolbar, 6);

    GtkWidget *search = gtk_search_entry_new();
    gtk_editable_set_width_chars(GTK_EDITABLE(search), 28);
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), "Search files...");
    gtk_widget_set_hexpand(search, TRUE);
    ctx->search_entry = search;

    GtkWidget *sort_btn = gtk_button_new_with_label("Sort: A\xe2\x86\x92Z"); /* A→Z */
    gtk_widget_set_size_request(sort_btn, 100, -1);

    gtk_box_append(GTK_BOX(toolbar), search);
    gtk_box_append(GTK_BOX(toolbar), sort_btn);
    gtk_box_append(GTK_BOX(outer), toolbar);

    /* ── Separator ── */
    gtk_box_append(GTK_BOX(outer), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

    /* ── Scrollable flowbox for file icons ── */
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(outer), scroll);

    GtkWidget *flowbox = gtk_flow_box_new();
    gtk_widget_set_valign(GTK_WIDGET(flowbox), GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flowbox), 5);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flowbox), GTK_SELECTION_NONE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flowbox), 12);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flowbox), 12);
    gtk_widget_set_margin_start(flowbox, 12);
    gtk_widget_set_margin_end(flowbox, 12);
    gtk_widget_set_margin_top(flowbox, 12);
    gtk_widget_set_margin_bottom(flowbox, 12);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), flowbox);
    ctx->flowbox = flowbox;

    /* ── Connect signals ── */
    g_signal_connect(search, "search-changed",
                     G_CALLBACK(on_folder_search_changed), ctx);
    g_signal_connect_data(sort_btn, "clicked",
                          G_CALLBACK(on_folder_sort_clicked),
                          ctx, NULL, 0);

    /* Free ctx + history when the window is closed */
    g_signal_connect_data(window, "destroy",
                          G_CALLBACK(gtk_window_destroy),
                          ctx,
                          (GClosureNotify)on_folder_ctx_destroy,
                          G_CONNECT_SWAPPED);

    /* Initial population — show all files sorted A-Z */
    populate_folder(ctx, "");

    gtk_window_present(GTK_WINDOW(window));
}

static const char *icon_for_name(const char *name, gboolean is_dir)
{
    const char *dot;   /* Pointer to the file extension, e.g. ".txt" */

    if(is_dir)
    {
        return "folder";
    }

    dot = strrchr(name, '.');
    if(dot == NULL)
    {
        return "text-x-generic";
    }

    if(g_strcmp0(dot, ".png") == 0 ||
       g_strcmp0(dot, ".jpg") == 0 ||
       g_strcmp0(dot, ".jpeg") == 0)
    {
        return "image-x-generic";
    }

    if(is_text_file(name))
    {
        return "text-x-script";
    }

    return "text-x-generic";
}

static void drag_begin(GtkGestureDrag *gesture,
                       double start_x,
                       double start_y,
                       gpointer data)
{
    IconDrag *drag;
    double x;
    double y;

    drag = data;
    gtk_fixed_get_child_position(GTK_FIXED(desktop),
                                 gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)),
                                 &x,
                                 &y);
    drag->start_x = start_x;
    drag->start_y = start_y;
    drag->icon_x = x;
    drag->icon_y = y;
    drag->dragged = FALSE;
}

static void drag_update(GtkGestureDrag *gesture,
                        double offset_x,
                        double offset_y,
                        gpointer data)
{
    IconDrag *drag;
    GtkWidget *widget;
    int new_x;
    int new_y;

    drag = data;
    widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    new_x = (int)(drag->icon_x + offset_x);
    new_y = (int)(drag->icon_y + offset_y);

    gtk_fixed_move(GTK_FIXED(desktop), widget, new_x, new_y);
    drag->dragged = TRUE;
}

static void drag_end(GtkGestureDrag *gesture,
                     double offset_x,
                     double offset_y,
                     gpointer data)
{
    IconDrag *drag;
    GtkWidget *widget;
    double x;
    double y;
    IconPoint *point;
    int target_col, target_row;
    int final_x, final_y;
    int radius, found;

    drag = data;
    widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    gtk_fixed_get_child_position(GTK_FIXED(desktop), widget, &x, &y);

    target_col = ((int)x - 26 + 70) / 140;
    if (target_col < 0) target_col = 0;

    /* Icons now start at y=20 (top of desktop), grid step = 120 */
    target_row = ((int)y - 20 + 60) / 120;
    if (target_row < 0) target_row = 0;

    radius = 0;
    found = 0;
    final_x = 26 + target_col * 140;
    final_y = 20 + target_row * 120;

    while(!found && radius < 20) {
        for (int dc = -radius; dc <= radius && !found; dc++) {
            for (int dr = -radius; dr <= radius && !found; dr++) {
                if (abs(dc) == radius || abs(dr) == radius) {
                    int c = target_col + dc;
                    int r = target_row + dr;
                    if (c >= 0 && r >= 0) {
                        int test_x = 26 + c * 140;
                        int test_y = 20 + r * 120;

                        gboolean occupied = FALSE;
                        GList *l;
                        for(l = desktop_icons; l != NULL; l = l->next) {
                            if (GTK_WIDGET(l->data) != widget) {
                                double ix, iy;
                                gtk_fixed_get_child_position(GTK_FIXED(desktop), GTK_WIDGET(l->data), &ix, &iy);
                                if (abs((int)ix - test_x) < 50 && abs((int)iy - test_y) < 50) {
                                    occupied = TRUE;
                                    break;
                                }
                            }
                        }
                        if (!occupied) {
                            final_x = test_x;
                            final_y = test_y;
                            found = 1;
                        }
                    }
                }
            }
        }
        radius++;
    }

    gtk_fixed_move(GTK_FIXED(desktop), widget, final_x, final_y);

    point = g_new0(IconPoint, 1);
    point->x = final_x;
    point->y = final_y;

    if(drag->dragged)
    {
        suppress_open_once = TRUE;
    }

    g_hash_table_replace(icon_positions, g_strdup(drag->path), point);
}

static void free_icon_drag(gpointer data, GClosure *closure)
{
    IconDrag *drag;

    drag = data;
    g_free(drag->path);
    g_free(drag);
}

static GtkWidget *create_desktop_icon(const char *virtual_path,
                                      const char *name,
                                      gboolean is_dir)
{
    GtkWidget *button;
    GtkWidget *box;
    GtkWidget *image;
    GtkWidget *label;
    GtkGesture *drag_gesture;
    IconDrag *drag;

    button = gtk_button_new();
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    image = gtk_image_new_from_icon_name(icon_for_name(name, is_dir));
    gtk_image_set_pixel_size(GTK_IMAGE(image), 42);

    label = gtk_label_new(name);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_size_request(label, 90, -1);

    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), label);
    gtk_button_set_child(GTK_BUTTON(button), box);
    gtk_widget_set_size_request(button, 100, 80);
    gtk_widget_add_css_class(button, "flat");
    gtk_widget_add_css_class(button, "desktop-icon");

    g_signal_connect_data(
        button,
        "clicked",
        G_CALLBACK(open_desktop_item),
        g_strdup(virtual_path),
        (GClosureNotify)g_free,
        0);

    drag = g_new0(IconDrag, 1);
    drag->path = g_strdup(virtual_path);
    drag_gesture = gtk_gesture_drag_new();
    g_signal_connect(drag_gesture, "drag-begin", G_CALLBACK(drag_begin), drag);
    g_signal_connect(drag_gesture, "drag-update", G_CALLBACK(drag_update), drag);
    g_signal_connect_data(
        drag_gesture,
        "drag-end",
        G_CALLBACK(drag_end),
        drag,
        (GClosureNotify)free_icon_drag,
        0);
    gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(drag_gesture));

    return button;
}

static void refresh_desktop_icons(void)
{
    GList *item;
    char *real_dir;
    GDir *dir;
    const char *name;
    int x;
    int y;

    if(desktop == NULL)
    {
        return;
    }

    if(path_label != NULL)
    {
        char *display_path;

        display_path = g_strdup_printf(
            "Home%s",
            strcmp(desktop_path, "/") == 0 ? "" : desktop_path);
        gtk_label_set_text(GTK_LABEL(path_label), display_path);
        g_free(display_path);
    }

    for(item = desktop_icons; item != NULL; item = item->next)
    {
        gtk_fixed_remove(GTK_FIXED(desktop), GTK_WIDGET(item->data));
    }
    g_list_free(desktop_icons);
    desktop_icons = NULL;

    real_dir = real_path_from_virtual(desktop_path);
    dir = g_dir_open(real_dir, 0, NULL);

    /* x,y are the starting grid coordinates for new icon placement */
    x = 26;
    y = 112;

    if(dir == NULL)
    {
        g_free(real_dir);
        return;
    }

    while((name = g_dir_read_name(dir)) != NULL)
    {
        GtkWidget *icon;
        char child_virtual[MAX_PATH_TEXT];
        char *child_real;
        gboolean is_dir;
        IconPoint *point;

        if(strcmp(desktop_path, "/") == 0)
        {
            g_snprintf(child_virtual, sizeof(child_virtual), "/%s", name);
        }
        else
        {
            g_snprintf(child_virtual, sizeof(child_virtual), "%s/%s", desktop_path, name);
        }

        child_real = real_path_from_virtual(child_virtual);
        is_dir = g_file_test(child_real, G_FILE_TEST_IS_DIR);

        icon = create_desktop_icon(child_virtual, name, is_dir);
        point = g_hash_table_lookup(icon_positions, child_virtual);

        if(point != NULL)
        {
            gtk_fixed_put(GTK_FIXED(desktop), icon, point->x, point->y);
        }
        else
        {
            int target_c = 0;
            int target_r = 0;
            while(1)
            {
                int try_x = 26 + target_c * 140;
                /* Icons start at y=20 (top), step 120px per row */
                int try_y = 20 + target_r * 120;
                gboolean collision = FALSE;

                // Check stored positions
                GHashTableIter iter;
                gpointer k, v;
                g_hash_table_iter_init(&iter, icon_positions);
                while(g_hash_table_iter_next(&iter, &k, &v))
                {
                    IconPoint *p = v;
                    if(abs(p->x - try_x) < 50 && abs(p->y - try_y) < 50)
                    {
                        collision = TRUE;
                        break;
                    }
                }

                // Check active desktop icons
                if(!collision)
                {
                    GList *l;
                    for(l = desktop_icons; l != NULL; l = l->next)
                    {
                        double ix, iy;
                        gtk_fixed_get_child_position(GTK_FIXED(desktop), GTK_WIDGET(l->data), &ix, &iy);
                        if(abs((int)ix - try_x) < 50 && abs((int)iy - try_y) < 50)
                        {
                            collision = TRUE;
                            break;
                        }
                    }
                }

                if(!collision)
                {
                    gtk_fixed_put(GTK_FIXED(desktop), icon, try_x, try_y);
                    
                    // Save the assigned position so the next loop iteration (and future refreshes)
                    // knows this spot is taken, preventing overlaps for newly created files!
                    IconPoint *new_p = g_new0(IconPoint, 1);
                    new_p->x = try_x;
                    new_p->y = try_y;
                    g_hash_table_replace(icon_positions, g_strdup(child_virtual), new_p);
                    
                    break;
                }

                target_r++;
                if(target_r > 5)
                {
                    target_r = 0;
                    target_c++;
                }
            }
        }

        desktop_icons = g_list_append(desktop_icons, icon);

        g_free(child_real);
    }

    g_dir_close(dir);
    g_free(real_dir);
}

static gboolean update_clock(gpointer data) {
    GtkLabel *label = GTK_LABEL(data);
    GDateTime *now = g_date_time_new_now_local();
    char *time_str = g_date_time_format(now, "%I:%M %p");
    char *date_str = g_date_time_format(now, "%A, %B %d");
    char *markup = g_strdup_printf(
        "<span size='48000' font_weight='700'>%s</span>\n"
        "<span size='22000' font_weight='700'>%s</span>", 
        time_str, date_str);
    gtk_label_set_markup(label, markup);
    g_free(markup);
    g_free(time_str);
    g_free(date_str);
    g_date_time_unref(now);
    return G_SOURCE_CONTINUE;
}

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *window;
    GtkWidget *background;
    GtkWidget *image;
    GtkGesture *gesture;
    GtkWidget *desktop_search;
    /* Taskbar widgets */
    GtkWidget *taskbar;
    GtkWidget *terminal_btn;
    GtkWidget *ai_button;

    icon_positions = g_hash_table_new_full(
        g_str_hash,
        g_str_equal,
        g_free,
        g_free);
    apply_css();

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Virtual Operating System");
    gtk_window_fullscreen(GTK_WINDOW(window));

    /* ── Layout: simple vertical GtkBox ──
     *   ┌──────────────────────────────────┐
     *   │  GtkOverlay (desktop_overlay)    │  ← clock + wallpaper
     *   │    └─ GtkFixed (desktop)         │  ← draggable icons
     *   ├──────────────────────────────────┤
     *   │  GtkBox (taskbar)   height=44    │  ← Terminal | Search | AI
     *   └──────────────────────────────────┘
     */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    /* Desktop canvas — wrapped in an overlay for the centered clock */
    GtkWidget *desktop_overlay = gtk_overlay_new();
    gtk_widget_set_hexpand(desktop_overlay, TRUE);
    gtk_widget_set_vexpand(desktop_overlay, TRUE);
    gtk_box_append(GTK_BOX(vbox), desktop_overlay);

    desktop = gtk_fixed_new();
    gtk_overlay_set_child(GTK_OVERLAY(desktop_overlay), desktop);
    
    /* Transparent Centered Clock */
    GtkWidget *clock_label = gtk_label_new("");
    gtk_widget_add_css_class(clock_label, "desktop-clock");
    gtk_widget_set_halign(clock_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(clock_label, GTK_ALIGN_START);
    gtk_widget_set_margin_top(clock_label, 120);
    gtk_label_set_justify(GTK_LABEL(clock_label), GTK_JUSTIFY_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(desktop_overlay), clock_label);
    
    update_clock(clock_label);
    g_timeout_add_seconds(1, update_clock, clock_label);

    /* ── Wallpaper (CSS based so it doesn't force a minimum window size) ── */
    char *cwd = g_get_current_dir();
    /* On Windows, convert \ to / for the file:// URL */
    for (char *p = cwd; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    char *bg_css = g_strdup_printf(
        ".desktop-bg { background-image: url('file:///%s/assets/wallpaper.png'); "
        "background-size: cover; background-position: center; }\n"
        ".desktop-clock { font-family: 'Segoe UI', sans-serif; "
        "color: rgba(0, 0, 0, 1.0); font-weight: bold; background: transparent; }", cwd);
    GtkCssProvider *bg_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(bg_provider, bg_css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(bg_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_free(bg_css);
    g_free(cwd);
    g_object_unref(bg_provider);
    
    gtk_widget_add_css_class(desktop, "desktop-bg");

    /* ── Desktop Search Context (heap-allocated, lives for app lifetime) ──
     * Uses Bubble Sort + Binary Search to locate files as the user types. */
    desktop_search_ctx = (FolderCtx *)malloc(sizeof(FolderCtx));
    if (desktop_search_ctx) {
        g_strlcpy(desktop_search_ctx->path, "/", sizeof(desktop_search_ctx->path));
        desktop_search_ctx->sort_asc = 1;
        desktop_search_ctx->history  = NULL;
        desktop_search_ctx->flowbox  = NULL;
        desktop_search_ctx->search_entry = NULL;
        file_array_init(&desktop_search_ctx->sorted_files);
    }

    /* terminal_icon (hidden — kept for API compatibility only) */
    image = gtk_picture_new_for_filename("assets/terminal.png");
    terminal_icon = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(terminal_icon), image);
    gesture = gtk_gesture_click_new();
    g_signal_connect(gesture, "pressed", G_CALLBACK(open_terminal), NULL);
    gtk_widget_add_controller(terminal_icon, GTK_EVENT_CONTROLLER(gesture));

    /* ═══════════════════════════════════════════════════════════════
     *  TASKBAR — second child of the vbox, always at the very bottom.
     *  Layout:  [Terminal]  <spacer>  [Search...]  <spacer>  [AI]
     * ═══════════════════════════════════════════════════════════════ */
    taskbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(taskbar, "taskbar");
    gtk_widget_set_size_request(taskbar, -1, 44);

    /* LEFT: Terminal */
    terminal_btn = gtk_button_new_with_label("Terminal");
    gtk_widget_add_css_class(terminal_btn, "taskbar-btn");
    gtk_widget_set_size_request(terminal_btn, 110, 32);
    gtk_widget_set_valign(terminal_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(terminal_btn, 14);
    g_signal_connect(terminal_btn, "clicked", G_CALLBACK(open_terminal), NULL);
    gtk_box_append(GTK_BOX(taskbar), terminal_btn);


    /* Spacer */
    GtkWidget *spacer1 = gtk_label_new("");
    gtk_widget_set_hexpand(spacer1, TRUE);
    gtk_box_append(GTK_BOX(taskbar), spacer1);

    /* CENTRE: Search bar (Bubble Sort + Binary Search) */
    desktop_search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(desktop_search),
                                          "Search files & folders...");
    gtk_widget_set_size_request(desktop_search, 320, 32);
    gtk_widget_set_valign(desktop_search, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(desktop_search, "desktop-search");
    if (desktop_search_ctx)
        desktop_search_ctx->search_entry = desktop_search;
    g_signal_connect(desktop_search, "search-changed",
                     G_CALLBACK(on_desktop_search_changed), NULL);
    gtk_box_append(GTK_BOX(taskbar), desktop_search);

    /* Spacer */
    GtkWidget *spacer2 = gtk_label_new("");
    gtk_widget_set_hexpand(spacer2, TRUE);
    gtk_box_append(GTK_BOX(taskbar), spacer2);

    /* RIGHT: AI */
    ai_button = gtk_button_new_with_label("AI");
    gtk_widget_add_css_class(ai_button, "taskbar-btn");
    gtk_widget_set_size_request(ai_button, 110, 32);
    gtk_widget_set_valign(ai_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(ai_button, 14);
    g_signal_connect(ai_button, "clicked", G_CALLBACK(open_ai_prompt), NULL);
    gtk_box_append(GTK_BOX(taskbar), ai_button);

    /* Add taskbar as the second child of vbox — always at bottom */
    gtk_box_append(GTK_BOX(vbox), taskbar);

    /* ── Refresh desktop icons — they start from y=20 at the top ── */
    set_desktop_refresh_callback(refresh_desktop_icons);
    refresh_desktop_icons();

    gtk_window_present(GTK_WINDOW(window));
}


int main(int argc, char *argv[])
{
    GtkApplication *app;
    int status;

    app = gtk_application_new(
        "com.virtualos.desktop",
        G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
