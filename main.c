#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <ctype.h>
#include <string.h>
#include "terminal.h"

#define MAX_PATH_TEXT 512

typedef struct
{
    char *path;
    double start_x;
    double start_y;
    double icon_x;
    double icon_y;
    gboolean dragged;
} IconDrag;

typedef struct
{
    GtkTextBuffer *buffer;
    char *path;
} EditorData;

typedef struct
{
    int x;
    int y;
} IconPoint;

static GtkWidget *desktop;
static GtkWidget *terminal_icon;
static GtkWidget *path_label;
static GList *desktop_icons;
static GHashTable *icon_positions;
static char desktop_path[MAX_PATH_TEXT] = "/";
static gboolean suppress_open_once = FALSE;

static void open_desktop_item(GtkButton *button, gpointer data);
static void open_folder_window(const char *virtual_path);
static void open_gemini_prompt(void);
static const char *icon_for_name(const char *name, gboolean is_dir);


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
        ".desktop-bar { background: rgba(250,250,250,.88); border-radius: 10px; padding: 8px; }"
        ".path-label { font-weight: 600; color: #1f2937; }"
        ".desktop-icon { background: rgba(255,255,255,.22); border-radius: 8px; }"
        ".desktop-icon:hover { background: rgba(255,255,255,.42); }"
        ".ai-panel { background: rgba(248,250,252,.94); border-radius: 12px; padding: 10px; }"
        ".ai-title { font-weight: 700; color: #111827; }");

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



static void on_gemini_prompt_run(GtkWidget *button, gpointer data) {
    GtkWidget *entry = data;
    const char *prompt = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (prompt && *prompt) {
        char *py_cmd = g_strdup_printf("python gemini_api.py \"%s\"", prompt);
        char *stdout_buf = NULL;
        int exit_status;
        if (g_spawn_command_line_sync(py_cmd, &stdout_buf, NULL, &exit_status, NULL) && stdout_buf && *stdout_buf) {
            GtkWidget *win = gtk_window_new();
            gtk_window_set_title(GTK_WINDOW(win), "Gemini Response");
            gtk_window_set_default_size(GTK_WINDOW(win), 500, 400);
            GtkWidget *scroll = gtk_scrolled_window_new();
            gtk_widget_set_vexpand(scroll, TRUE);
            gtk_window_set_child(GTK_WINDOW(win), scroll);
            GtkWidget *label = gtk_label_new(stdout_buf);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_widget_set_margin_start(label, 12);
            gtk_widget_set_margin_end(label, 12);
            gtk_widget_set_margin_top(label, 12);
            gtk_widget_set_margin_bottom(label, 12);
            gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), label);
            gtk_window_present(GTK_WINDOW(win));
        }
        g_free(stdout_buf);
        g_free(py_cmd);
    }
    GtkWidget *window = gtk_widget_get_ancestor(entry, GTK_TYPE_WINDOW);
    if (window) {
        gtk_window_destroy(GTK_WINDOW(window));
    }
}

static void open_gemini_prompt(void)
{
    GtkWidget *win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win), "Gemini Prompt");
    gtk_window_set_default_size(GTK_WINDOW(win), 400, 100);
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_window_set_child(GTK_WINDOW(win), box);
    
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter Gemini prompt...");
    gtk_box_append(GTK_BOX(box), entry);
    
    GtkWidget *run_btn = gtk_button_new_with_label("Run");
    g_signal_connect(run_btn, "clicked", G_CALLBACK(on_gemini_prompt_run), entry);
    g_signal_connect(entry, "activate", G_CALLBACK(on_gemini_prompt_run), entry);
    gtk_box_append(GTK_BOX(box), run_btn);
    
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

    if (strcmp(virtual_path, "/gemini") == 0) {
        open_gemini_prompt();
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

static void open_folder_window(const char *virtual_path)
{
    GtkWidget *window;
    GtkWidget *scroll;
    GtkWidget *flowbox;
    char *real_dir;
    GDir *dir;
    const char *name;

    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), display_name_from_path(virtual_path));
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 400);

    scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_window_set_child(GTK_WINDOW(window), scroll);

    flowbox = gtk_flow_box_new();
    gtk_widget_set_valign(GTK_WIDGET(flowbox), GTK_ALIGN_START);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flowbox), 5);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flowbox), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), flowbox);

    real_dir = real_path_from_virtual(virtual_path);
    dir = g_dir_open(real_dir, 0, NULL);

    if(dir != NULL)
    {
        while((name = g_dir_read_name(dir)) != NULL)
        {
            GtkWidget *button;
            GtkWidget *box;
            GtkWidget *image;
            GtkWidget *label;
            char child_virtual[MAX_PATH_TEXT];
            char *child_real;
            gboolean is_dir;

            if(strcmp(virtual_path, "/") == 0)
            {
                g_snprintf(child_virtual, sizeof(child_virtual), "/%s", name);
            }
            else
            {
                g_snprintf(child_virtual, sizeof(child_virtual), "%s/%s", virtual_path, name);
            }

            child_real = real_path_from_virtual(child_virtual);
            is_dir = g_file_test(child_real, G_FILE_TEST_IS_DIR);

            button = gtk_button_new();
            box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
            image = gtk_image_new_from_icon_name(icon_for_name(name, is_dir));
            gtk_image_set_pixel_size(GTK_IMAGE(image), 42);
            label = gtk_label_new(name);
            gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
            gtk_widget_set_size_request(label, 80, -1);
            gtk_box_append(GTK_BOX(box), image);
            gtk_box_append(GTK_BOX(box), label);
            gtk_button_set_child(GTK_BUTTON(button), box);
            gtk_widget_add_css_class(button, "flat");

            g_signal_connect_data(
                button,
                "clicked",
                G_CALLBACK(open_desktop_item),
                g_strdup(child_virtual),
                (GClosureNotify)g_free,
                0);

            gtk_flow_box_insert(GTK_FLOW_BOX(flowbox), button, -1);
            g_free(child_real);
        }
        g_dir_close(dir);
    }
    g_free(real_dir);

    gtk_window_present(GTK_WINDOW(window));
}

static const char *icon_for_name(const char *name, gboolean is_dir)
{
    const char *dot;

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

    target_col = ((int)x - 26 + 55) / 110;
    if (target_col < 0) target_col = 0;
    target_row = ((int)y - 112 + 45) / 90;
    if (target_row < 0) target_row = 0;

    radius = 0;
    found = 0;
    final_x = 26 + target_col * 110;
    final_y = 112 + target_row * 90;

    while(!found && radius < 20) {
        for (int dc = -radius; dc <= radius && !found; dc++) {
            for (int dr = -radius; dr <= radius && !found; dr++) {
                if (abs(dc) == radius || abs(dr) == radius) {
                    int c = target_col + dc;
                    int r = target_row + dr;
                    if (c >= 0 && r >= 0) {
                        int test_x = 26 + c * 110;
                        int test_y = 112 + r * 90;

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
                int try_x = 26 + target_c * 110;
                int try_y = 112 + target_r * 90;
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

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *window;
    GtkWidget *background;
    GtkWidget *bar;
    GtkWidget *gemini_button;
    GtkWidget *image;
    GtkGesture *gesture;

    icon_positions = g_hash_table_new_full(
        g_str_hash,
        g_str_equal,
        g_free,
        g_free);
    apply_css();

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Virtual Operating System");
    gtk_window_maximize(GTK_WINDOW(window));

    desktop = gtk_fixed_new();
    gtk_window_set_child(GTK_WINDOW(window), desktop);

    background = gtk_picture_new_for_filename("assets/wallpaper.png");
    gtk_picture_set_content_fit(GTK_PICTURE(background), GTK_CONTENT_FIT_COVER);
    gtk_widget_set_size_request(background, 1920, 1080);
    gtk_fixed_put(GTK_FIXED(desktop), background, 0, 0);

    gemini_button = gtk_button_new_with_label("Gemini");
    g_signal_connect(gemini_button, "clicked", G_CALLBACK(open_gemini_prompt), NULL);
    gtk_widget_set_size_request(gemini_button, 90, 42);
    gtk_fixed_put(GTK_FIXED(desktop), gemini_button, 1810, 20);

    image = gtk_picture_new_for_filename("assets/terminal.png");
    terminal_icon = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(terminal_icon), image);
    gtk_widget_set_size_request(terminal_icon, 80, 80);
    gtk_widget_add_css_class(terminal_icon, "flat");
    gtk_fixed_put(GTK_FIXED(desktop), terminal_icon, 20, 20);

    gesture = gtk_gesture_click_new();
    g_signal_connect(gesture, "pressed", G_CALLBACK(open_terminal), NULL);
    gtk_widget_add_controller(terminal_icon, GTK_EVENT_CONTROLLER(gesture));

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
