#include "terminal.h"
#include <ctype.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#define MAX_PATH_TEXT 512

static GtkTextBuffer *buffer;
static GtkTextBuffer *ai_chat_buffer;
static char cwd[MAX_PATH_TEXT] = "/";
static void (*desktop_refresh_callback)(void) = NULL;

void set_desktop_refresh_callback(void (*callback)(void))
{
    desktop_refresh_callback = callback;
}

static void refresh_desktop(void)
{
    if(desktop_refresh_callback != NULL)
    {
        desktop_refresh_callback();
    }
}

static void append_text(const char *text)
{
    GtkTextIter end;

    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
    gtk_text_buffer_insert(buffer, &end, "\n", -1);
}

static void append_line(const char *label, const char *value)
{
    char *message;

    message = g_strdup_printf("%s%s", label, value);
    append_text(message);
    g_free(message);
}

static char *trim(char *text)
{
    char *end;

    while(isspace((unsigned char)*text))
    {
        text++;
    }

    if(text[0] == '\0')
    {
        return text;
    }

    end = text + strlen(text) - 1;
    while(end > text && isspace((unsigned char)*end))
    {
        *end = '\0';
        end--;
    }

    return text;
}

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

static gboolean normalize_virtual_path(const char *input,
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
    else if(strcmp(cwd, "/") == 0)
    {
        g_snprintf(combined, sizeof(combined), "/%s", input);
    }
    else
    {
        g_snprintf(combined, sizeof(combined), "%s/%s", cwd, input);
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

        if(strchr(token, ' ') != NULL || strchr(token, '\t') != NULL)
        {
            return FALSE;
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

static gboolean get_virtual_arg(const char *arg,
                                char *virtual_path,
                                size_t size,
                                const char *usage)
{
    char copy[MAX_PATH_TEXT];
    char *clean;

    if(arg == NULL)
    {
        append_text(usage);
        return FALSE;
    }

    g_strlcpy(copy, arg, sizeof(copy));
    clean = trim(copy);

    if(!normalize_virtual_path(clean, virtual_path, size))
    {
        append_text(usage);
        return FALSE;
    }

    return TRUE;
}

static void command_pwd(void)
{
    if(strcmp(cwd, "/") == 0)
    {
        append_text("/home/samar");
    }
    else
    {
        append_line("/home/samar", cwd);
    }
}

static void command_cd(const char *arg)
{
    char virtual_path[MAX_PATH_TEXT];
    char *real_path;

    if(arg == NULL || trim((char *)arg)[0] == '\0')
    {
        g_strlcpy(cwd, "/", sizeof(cwd));
        return;
    }

    if(!get_virtual_arg(arg, virtual_path, sizeof(virtual_path), "Usage: cd dirname"))
    {
        return;
    }

    real_path = real_path_from_virtual(virtual_path);
    if(g_file_test(real_path, G_FILE_TEST_IS_DIR))
    {
        g_strlcpy(cwd, virtual_path, sizeof(cwd));
    }
    else
    {
        append_text("cd: directory not found");
    }

    g_free(real_path);
}

static void command_ls(const char *arg)
{
    char virtual_path[MAX_PATH_TEXT];
    char *real_path;
    GDir *dir;
    const char *name;
    gboolean empty;

    if(arg == NULL || trim((char *)arg)[0] == '\0')
    {
        g_strlcpy(virtual_path, cwd, sizeof(virtual_path));
    }
    else if(!get_virtual_arg(arg, virtual_path, sizeof(virtual_path), "Usage: ls [dirname]"))
    {
        return;
    }

    real_path = real_path_from_virtual(virtual_path);
    dir = g_dir_open(real_path, 0, NULL);

    if(dir == NULL)
    {
        append_text("ls: cannot open directory");
        g_free(real_path);
        return;
    }

    empty = TRUE;
    while((name = g_dir_read_name(dir)) != NULL)
    {
        append_text(name);
        empty = FALSE;
    }

    if(empty)
    {
        append_text("(empty)");
    }

    g_dir_close(dir);
    g_free(real_path);
}

static void command_touch(const char *arg)
{
    char virtual_path[MAX_PATH_TEXT];
    char *real_path;
    char *parent;
    FILE *file;

    if(!get_virtual_arg(arg, virtual_path, sizeof(virtual_path), "Usage: touch filename"))
    {
        return;
    }

    real_path = real_path_from_virtual(virtual_path);
    parent = g_path_get_dirname(real_path);

    if(!g_file_test(parent, G_FILE_TEST_IS_DIR))
    {
        append_text("touch: parent directory not found");
    }
    else
    {
        file = fopen(real_path, "ab");
        if(file == NULL)
        {
            append_text("touch: could not create file");
        }
        else
        {
            fclose(file);
            append_text("File created");
            refresh_desktop();
        }
    }

    g_free(parent);
    g_free(real_path);
}

static void command_mkdir(const char *arg)
{
    char virtual_path[MAX_PATH_TEXT];
    char *real_path;

    if(!get_virtual_arg(arg, virtual_path, sizeof(virtual_path), "Usage: mkdir dirname"))
    {
        return;
    }

    real_path = real_path_from_virtual(virtual_path);
    if(g_mkdir_with_parents(real_path, 0755) == 0)
    {
        append_text("Directory created");
        refresh_desktop();
    }
    else
    {
        append_text("mkdir: could not create directory");
    }

    g_free(real_path);
}

static void command_cat(const char *arg)
{
    char virtual_path[MAX_PATH_TEXT];
    char *real_path;
    char *contents;
    gsize length;

    if(!get_virtual_arg(arg, virtual_path, sizeof(virtual_path), "Usage: cat filename"))
    {
        return;
    }

    real_path = real_path_from_virtual(virtual_path);
    contents = NULL;

    if(g_file_get_contents(real_path, &contents, &length, NULL))
    {
        append_text(length == 0 ? "" : contents);
        g_free(contents);
    }
    else
    {
        append_text("cat: could not read file");
    }

    g_free(real_path);
}

static void command_rm(const char *arg)
{
    char virtual_path[MAX_PATH_TEXT];
    char *real_path;

    if(!get_virtual_arg(arg, virtual_path, sizeof(virtual_path), "Usage: rm filename"))
    {
        return;
    }

    real_path = real_path_from_virtual(virtual_path);
    if(g_file_test(real_path, G_FILE_TEST_IS_DIR))
    {
        append_text("rm: use rmdir for folders");
    }
    else if(g_remove(real_path) == 0)
    {
        append_text("File removed");
        refresh_desktop();
    }
    else
    {
        append_text("rm: could not remove file");
    }

    g_free(real_path);
}

static void command_rmdir(const char *arg)
{
    char virtual_path[MAX_PATH_TEXT];
    char *real_path;

    if(!get_virtual_arg(arg, virtual_path, sizeof(virtual_path), "Usage: rmdir dirname"))
    {
        return;
    }

    real_path = real_path_from_virtual(virtual_path);
    if(g_rmdir(real_path) == 0)
    {
        append_text("Directory removed");
        refresh_desktop();
    }
    else
    {
        append_text("rmdir: folder must be empty");
    }

    g_free(real_path);
}

static gboolean split_two_args(const char *args,
                               char *first,
                               size_t first_size,
                               const char **rest)
{
    char copy[MAX_PATH_TEXT];
    char *clean;
    char *space;

    if(args == NULL)
    {
        return FALSE;
    }

    g_strlcpy(copy, args, sizeof(copy));
    clean = trim(copy);
    space = clean;

    while(space[0] != '\0' && !isspace((unsigned char)space[0]))
    {
        space++;
    }

    if(space[0] == '\0')
    {
        return FALSE;
    }

    *space = '\0';
    g_strlcpy(first, clean, first_size);
    *rest = args + (space - copy) + 1;
    while(isspace((unsigned char)**rest))
    {
        (*rest)++;
    }

    return (*rest)[0] != '\0';
}

static void command_write(const char *args, gboolean append)
{
    char name[MAX_PATH_TEXT];
    char virtual_path[MAX_PATH_TEXT];
    const char *text;
    char *real_path;
    FILE *file;

    if(!split_two_args(args, name, sizeof(name), &text) ||
       !normalize_virtual_path(name, virtual_path, sizeof(virtual_path)))
    {
        append_text(append ? "Usage: append filename text" : "Usage: write filename text");
        return;
    }

    real_path = real_path_from_virtual(virtual_path);

    if(append)
    {
        file = fopen(real_path, "ab");
        if(file != NULL)
        {
            fprintf(file, "%s\n", text);
            fclose(file);
        }
    }
    else
    {
        file = fopen(real_path, "wb");
        if(file != NULL)
        {
            fprintf(file, "%s", text);
            fclose(file);
        }
    }

    if(file == NULL)
    {
        append_text(append ? "append: could not write file" : "write: could not write file");
    }
    else
    {
        append_text(append ? "Text appended" : "File written");
        refresh_desktop();
    }

    g_free(real_path);
}

static void command_copy_or_move(const char *args, gboolean move)
{
    char source[MAX_PATH_TEXT];
    char target[MAX_PATH_TEXT];
    char source_virtual[MAX_PATH_TEXT];
    char target_virtual[MAX_PATH_TEXT];
    char *source_path;
    char *target_path;
    char *contents;
    gsize length;

    if(args == NULL ||
       sscanf(args, "%511s %511s", source, target) != 2 ||
       !normalize_virtual_path(source, source_virtual, sizeof(source_virtual)) ||
       !normalize_virtual_path(target, target_virtual, sizeof(target_virtual)))
    {
        append_text(move ? "Usage: mv source target" : "Usage: cp source target");
        return;
    }

    source_path = real_path_from_virtual(source_virtual);
    target_path = real_path_from_virtual(target_virtual);

    if(move)
    {
        if(g_rename(source_path, target_path) == 0)
        {
            append_text("File moved");
            refresh_desktop();
        }
        else
        {
            append_text("mv: could not move file");
        }
    }
    else
    {
        contents = NULL;
        if(g_file_get_contents(source_path, &contents, &length, NULL) &&
           g_file_set_contents(target_path, contents, length, NULL))
        {
            append_text("File copied");
            refresh_desktop();
        }
        else
        {
            append_text("cp: could not copy file");
        }
        g_free(contents);
    }

    g_free(source_path);
    g_free(target_path);
}

char *run_gemini_api(const char *prompt, char **error_out)
{
    char *home = get_virtual_home();
    char *proj_dir = g_path_get_dirname(home);
    char *api_script = g_build_filename(proj_dir, "gemini_api.py", NULL);
    g_free(proj_dir);
    g_free(home);

    char *stdout_buf = NULL;
    char *stderr_buf = NULL;
    int exit_status;
    GError *error = NULL;

    // Try python first
    char *argv_python[] = {"python", api_script, (char *)prompt, NULL};
    gboolean success = g_spawn_sync(NULL, argv_python, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &stdout_buf, &stderr_buf, &exit_status, &error);

    if (!success) {
        if (error) {
            g_clear_error(&error);
        }
        // Try python3 if python failed
        char *argv_python3[] = {"python3", api_script, (char *)prompt, NULL};
        success = g_spawn_sync(NULL, argv_python3, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &stdout_buf, &stderr_buf, &exit_status, &error);
    }

    g_free(api_script);

    if (success) {
        if (error_out && stderr_buf && stderr_buf[0] != '\0') {
            *error_out = stderr_buf;
        } else {
            g_free(stderr_buf);
        }
        return stdout_buf;
    } else {
        if (error_out) {
            *error_out = g_strdup(error ? error->message : "Unknown spawn error");
        }
        if (error) {
            g_clear_error(&error);
        }
        g_free(stdout_buf);
        g_free(stderr_buf);
        return NULL;
    }
}

static void execute_command_string(const char *input_line, gboolean is_from_ai)
{
    char input[1024];
    char *cmd;
    char *args;

    g_strlcpy(input, input_line, sizeof(input));
    cmd = trim(input);
    args = cmd;

    while(args[0] != '\0' && !isspace((unsigned char)args[0]))
    {
        args++;
    }

    if(args[0] != '\0')
    {
        *args = '\0';
        args++;
        args = trim(args);
    }
    else
    {
        args = NULL;
    }

    if (cmd[0] == '\0') {
        return;
    }

    if (is_from_ai) {
        if (strcmp(cmd, "help") != 0 && strcmp(cmd, "clear") != 0 && strcmp(cmd, "whoami") != 0 && 
            strcmp(cmd, "date") != 0 && strcmp(cmd, "echo") != 0 && strcmp(cmd, "pwd") != 0 && 
            strcmp(cmd, "ls") != 0 && strcmp(cmd, "cd") != 0 && strcmp(cmd, "touch") != 0 && 
            strcmp(cmd, "tocuh") != 0 && strcmp(cmd, "mkdir") != 0 && strcmp(cmd, "cat") != 0 && 
            strcmp(cmd, "write") != 0 && strcmp(cmd, "append") != 0 && strcmp(cmd, "cp") != 0 && 
            strcmp(cmd, "mv") != 0 && strcmp(cmd, "rm") != 0 && strcmp(cmd, "rmdir") != 0) {
            return;
        }
        gchar *ai_msg = g_strdup_printf("> AI executing: %s", input_line);
        append_text(ai_msg);
        g_free(ai_msg);
    }

    if(strcmp(cmd, "help") == 0)
    {
        append_text("");
        append_text("=== Virtual OS Terminal - Help ===");
        append_text("");
        append_text("  NAVIGATION");
        append_text("    pwd              Show current directory");
        append_text("    ls [path]        List files in directory");
        append_text("    cd <path>        Change directory");
        append_text("");
        append_text("  FILE MANAGEMENT");
        append_text("    touch <name>     Create a new empty file");
        append_text("    mkdir <name>     Create a new folder");
        append_text("    rm <name>        Delete a file");
        append_text("    rmdir <name>     Delete an empty folder");
        append_text("");
        append_text("  READ & WRITE");
        append_text("    cat <file>       Display file contents");
        append_text("    write <file> <text>   Write text to file");
        append_text("    append <file> <text>  Append text to file");
        append_text("");
        append_text("  COPY & MOVE");
        append_text("    cp <src> <dest>  Copy a file");
        append_text("    mv <src> <dest>  Move or rename a file");
        append_text("");
        append_text("  AI");
        append_text("    gemini <prompt>  Ask Gemini AI a question");
        append_text("");
        append_text("  SYSTEM");
        append_text("    whoami           Show current user");
        append_text("    date             Show current date");
        append_text("    clear            Clear the terminal");
        append_text("    echo <text>      Print text");
        append_text("");
        append_text("=================================");
        append_text("");
    }
    else if(strcmp(cmd, "clear") == 0)
    {
        gtk_text_buffer_set_text(buffer, "", -1);
    }
    else if(strcmp(cmd, "whoami") == 0)
    {
        append_text("samarth");
    }
    else if(strcmp(cmd, "date") == 0)
    {
        append_text(__DATE__);
    }
    else if(strcmp(cmd, "echo") == 0)
    {
        append_text(args == NULL ? "" : args);
    }
    else if(strcmp(cmd, "pwd") == 0)
    {
        command_pwd();
    }
    else if(strcmp(cmd, "ls") == 0)
    {
        command_ls(args);
    }
    else if(strcmp(cmd, "cd") == 0)
    {
        command_cd(args);
    }
    else if(strcmp(cmd, "touch") == 0 || strcmp(cmd, "tocuh") == 0)
    {
        command_touch(args);
    }
    else if(strcmp(cmd, "mkdir") == 0)
    {
        command_mkdir(args);
    }
    else if(strcmp(cmd, "cat") == 0)
    {
        command_cat(args);
    }
    else if(strcmp(cmd, "write") == 0)
    {
        command_write(args, FALSE);
    }
    else if(strcmp(cmd, "append") == 0)
    {
        command_write(args, TRUE);
    }
    else if(strcmp(cmd, "cp") == 0)
    {
        command_copy_or_move(args, FALSE);
    }
    else if(strcmp(cmd, "mv") == 0)
    {
        command_copy_or_move(args, TRUE);
    }
    else if(strcmp(cmd, "rm") == 0)
    {
        command_rm(args);
    }
    else if(strcmp(cmd, "rmdir") == 0)
    {
        command_rmdir(args);
    }
    else if(strcmp(cmd, "gemini") == 0)
    {
        if(args == NULL || trim(args)[0] == '\0') {
            append_text("Usage: gemini <prompt>");
        } else {
            char *stderr_buf = NULL;
            append_text("Thinking...");
            char *response = run_gemini_api(args, &stderr_buf);
            if (response) {
                if (response[0] != '\0') {
                    append_text(response);
                    char **lines = g_strsplit(response, "\n", -1);
                    for (int i = 0; lines[i] != NULL; i++) {
                        char *line = trim(lines[i]);
                        if (line[0] != '\0') {
                            execute_command_string(line, TRUE);
                        }
                    }
                    g_strfreev(lines);
                }
                g_free(response);
            }
            if (stderr_buf) {
                if (stderr_buf[0] != '\0') {
                    append_text(stderr_buf);
                }
                g_free(stderr_buf);
            }
            if (!response && !stderr_buf) {
                append_text("Failed to execute Gemini script.");
            }
        }
    }
    else
    {
        if (!is_from_ai) {
            append_text("Command not found");
        }
    }
}

static void process_command(GtkEntry *entry, gpointer data)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (text && text[0] != '\0') {
        append_text("> ");
        append_text(text);
        execute_command_string(text, FALSE);
    }
    gtk_editable_set_text(GTK_EDITABLE(entry), "");
}

static void on_terminal_gemini_prompt_run(GtkWidget *button, gpointer data) {
    GtkWidget *entry = data;
    const char *prompt_ptr = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (prompt_ptr && *prompt_ptr) {
        char *prompt = g_strdup(prompt_ptr);
        char *stderr_buf = NULL;

        GtkTextIter iter;
        gtk_text_buffer_get_end_iter(ai_chat_buffer, &iter);
        gtk_text_buffer_insert(ai_chat_buffer, &iter, "You: ", -1);
        gtk_text_buffer_insert(ai_chat_buffer, &iter, prompt, -1);
        gtk_text_buffer_insert(ai_chat_buffer, &iter, "\n", -1);
        
        gtk_editable_set_text(GTK_EDITABLE(entry), "Thinking...");
        gtk_widget_set_sensitive(entry, FALSE);

        char *response = run_gemini_api(prompt, &stderr_buf);
        if (response && response[0] != '\0') {
            gtk_text_buffer_get_end_iter(ai_chat_buffer, &iter);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "Gemini: ", -1);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, response, -1);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "\n\n", -1);
            
            char **lines = g_strsplit(response, "\n", -1);
            for (int i = 0; lines[i] != NULL; i++) {
                char *line = trim(lines[i]);
                if (line[0] != '\0') {
                    execute_command_string(line, TRUE);
                }
            }
            g_strfreev(lines);
        } else if (stderr_buf && stderr_buf[0] != '\0') {
            gtk_text_buffer_get_end_iter(ai_chat_buffer, &iter);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "Gemini Error: ", -1);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, stderr_buf, -1);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "\n\n", -1);
        } else {
            gtk_text_buffer_get_end_iter(ai_chat_buffer, &iter);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "Gemini: Error calling API\n\n", -1);
        }
        
        gtk_editable_set_text(GTK_EDITABLE(entry), "");
        gtk_widget_set_sensitive(entry, TRUE);
        gtk_widget_grab_focus(entry);

        g_free(response);
        g_free(stderr_buf);
        g_free(prompt);
    }
}

void open_terminal_window(void)
{
    GtkWidget *window;
    GtkWidget *hbox;
    GtkWidget *box;
    GtkWidget *scroll;
    GtkWidget *textview;
    GtkWidget *entry;
    
    GtkWidget *ai_vbox;
    GtkWidget *ai_label;
    GtkWidget *ai_scroll;
    GtkWidget *ai_textview;
    GtkWidget *ai_entry;
    GtkWidget *ai_btn;
    GtkWidget *ai_entry_box;

    window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Virtual Terminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 600);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_window_set_child(GTK_WINDOW(window), hbox);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_size_request(box, 960, -1);
    gtk_box_append(GTK_BOX(hbox), box);

    textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

    append_text("Welcome to Virtual OS");
    append_text("Type help");

    scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), textview);
    gtk_box_append(GTK_BOX(box), scroll);

    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter command...");
    g_signal_connect(entry, "activate", G_CALLBACK(process_command), NULL);
    gtk_box_append(GTK_BOX(box), entry);

    // AI Chat panel
    ai_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(ai_vbox, 240, -1);
    gtk_box_append(GTK_BOX(hbox), ai_vbox);
    
    ai_label = gtk_label_new("Gemini Chat");
    gtk_widget_add_css_class(ai_label, "ai-title");
    gtk_box_append(GTK_BOX(ai_vbox), ai_label);
    
    ai_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ai_textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ai_textview), GTK_WRAP_WORD_CHAR);
    ai_chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ai_textview));
    
    ai_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(ai_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ai_scroll), ai_textview);
    gtk_box_append(GTK_BOX(ai_vbox), ai_scroll);
    
    ai_entry_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    ai_entry = gtk_entry_new();
    gtk_widget_set_hexpand(ai_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ai_entry), "Ask Gemini...");
    ai_btn = gtk_button_new_with_label("Send");
    
    g_signal_connect(ai_btn, "clicked", G_CALLBACK(on_terminal_gemini_prompt_run), ai_entry);
    g_signal_connect(ai_entry, "activate", G_CALLBACK(on_terminal_gemini_prompt_run), ai_entry);
    
    gtk_box_append(GTK_BOX(ai_entry_box), ai_entry);
    gtk_box_append(GTK_BOX(ai_entry_box), ai_btn);
    gtk_box_append(GTK_BOX(ai_vbox), ai_entry_box);

    gtk_window_present(GTK_WINDOW(window));
}
