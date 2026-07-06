#include "terminal.h"
#include <ctype.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

#define MAX_PATH_TEXT 512

typedef struct {
    char default_name[32];
    char current_name[32];
    char description[128];
    char usage[64];
    char category[32];
    char example[256];
} CommandDef;

static CommandDef terminal_commands[] = {
    {"help", "help", "Show all available commands", "help", "BASIC", "Example:\n> help\n=== Virtual OS Terminal - Help ===\n..."},
    {"pwd", "pwd", "Print working directory", "pwd", "NAVIGATION", "Example:\n> pwd\n/home/samar"},
    {"ls", "ls", "List files and folders", "ls [dirname]", "NAVIGATION", "Example:\n> ls\nDocuments\nImages\nnotes.txt"},
    {"cd", "cd", "Change directory", "cd <path>", "NAVIGATION", "Example:\n> cd Documents\n> pwd\n/home/samar/Documents"},
    {"touch", "touch", "Create a new empty file", "touch <name>", "FILE MANAGEMENT", "Example:\n> touch hello.txt\n(Creates hello.txt in the current directory)"},
    {"mkdir", "mkdir", "Create a new folder", "mkdir <name>", "FILE MANAGEMENT", "Example:\n> mkdir Projects\n(Creates a folder named Projects)"},
    {"rm", "rm", "Delete a file", "rm <name>", "FILE MANAGEMENT", "Example:\n> rm hello.txt\n(Permanently deletes hello.txt)"},
    {"rmdir", "rmdir", "Delete an empty folder", "rmdir <name>", "FILE MANAGEMENT", "Example:\n> rmdir Projects\n(Deletes the folder Projects if it is empty)"},
    {"cat", "cat", "Display file contents", "cat <file>", "READ & WRITE", "Example:\n> cat notes.txt\nThis is a sample note."},
    {"write", "write", "Write text to file", "write <file> <text>", "READ & WRITE", "Example:\n> write notes.txt Hello World!\n(Overwrites notes.txt with 'Hello World!')"},
    {"append", "append", "Append text to file", "append <file> <text>", "READ & WRITE", "Example:\n> append notes.txt Bye!\n(Adds 'Bye!' to the end of notes.txt)"},
    {"cp", "cp", "Copy a file", "cp <src> <dest>", "COPY & MOVE", "Example:\n> cp notes.txt backup.txt\n(Copies notes.txt to backup.txt)"},
    {"mv", "mv", "Move or rename a file", "mv <src> <dest>", "COPY & MOVE", "Example:\n> mv old.txt new.txt\n(Renames old.txt to new.txt)"},
    {"ai", "ai", "Ask AI a question", "ai <prompt>", "AI", "Example:\n> ai Create a python script that prints hello\n(AI will respond and may execute commands)"},
    {"whoami", "whoami", "Show current user", "whoami", "SYSTEM", "Example:\n> whoami\nsamarth"},
    {"date", "date", "Show current date", "date", "SYSTEM", "Example:\n> date\nJul 06 2026"},
    {"clear", "clear", "Clear the terminal", "clear", "SYSTEM", "Example:\n> clear\n(Clears the terminal screen)"},
    {"echo", "echo", "Print text", "echo <text>", "SYSTEM", "Example:\n> echo Hello World!\nHello World!"},
    {"browse", "browse", "Open web browser", "browse [url]", "SYSTEM", "Example:\n> browse google.com\n(Opens browser at google.com)"}
};
static const int num_terminal_commands = sizeof(terminal_commands) / sizeof(CommandDef);

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

char *run_ai_api(const char *prompt, char **error_out)
{
    char *escaped_prompt;
    char *command;
    char *standard_output = NULL;
    char *standard_error = NULL;
    int exit_status;
    char *dir;

    if(error_out != NULL)
    {
        *error_out = NULL;
    }

    if(prompt == NULL || prompt[0] == '\0')
    {
        return NULL;
    }

    escaped_prompt = g_shell_quote(prompt);
    dir = g_get_current_dir();
    
    /* Using Windows python or python3 command. */
    command = g_strdup_printf("python \"%s/ai_api.py\" %s", dir, escaped_prompt);
    
    g_free(dir);
    g_free(escaped_prompt);

    g_spawn_command_line_sync(command,
                              &standard_output,
                              &standard_error,
                              &exit_status,
                              NULL);

    g_free(command);

    if(error_out != NULL && standard_error != NULL && standard_error[0] != '\0')
    {
        *error_out = g_strdup(standard_error);
    }

    if (standard_error != NULL) {
        g_free(standard_error);
    }

    return standard_output;
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

    char *cmd_id = NULL;
    for (int i = 0; i < num_terminal_commands; i++) {
        if (strcmp(cmd, terminal_commands[i].current_name) == 0) {
            cmd_id = terminal_commands[i].default_name;
            break;
        }
    }

    if (cmd_id == NULL) {
        if (!is_from_ai) {
            append_text("Command not found");
        }
        return;
    }

    if (is_from_ai) {
        gchar *ai_msg = g_strdup_printf("> AI executing: %s", input_line);
        append_text(ai_msg);
        g_free(ai_msg);
    }

    if(strcmp(cmd_id, "help") == 0)
    {
        append_text("");
        append_text("=== Virtual OS Terminal - Help ===");
        append_text("");
        
        char current_cat[32] = "";
        for (int i = 0; i < num_terminal_commands; i++) {
            if (strcmp(current_cat, terminal_commands[i].category) != 0) {
                if (i != 0) append_text("");
                gchar *cat_hdr = g_strdup_printf("  %s", terminal_commands[i].category);
                append_text(cat_hdr);
                g_free(cat_hdr);
                g_strlcpy(current_cat, terminal_commands[i].category, sizeof(current_cat));
            }
            gchar *line = g_strdup_printf("    %-16s %s", terminal_commands[i].current_name, terminal_commands[i].description);
            append_text(line);
            g_free(line);
        }
        append_text("");
        append_text("=================================");
        append_text("");
    }
    else if(strcmp(cmd_id, "clear") == 0)
    {
        gtk_text_buffer_set_text(buffer, "", -1);
    }
    else if(strcmp(cmd_id, "whoami") == 0)
    {
        append_text("samarth");
    }
    else if(strcmp(cmd_id, "date") == 0)
    {
        append_text(__DATE__);
    }
    else if(strcmp(cmd_id, "echo") == 0)
    {
        append_text(args == NULL ? "" : args);
    }
    else if(strcmp(cmd_id, "pwd") == 0)
    {
        command_pwd();
    }
    else if(strcmp(cmd_id, "ls") == 0)
    {
        command_ls(args);
    }
    else if(strcmp(cmd_id, "cd") == 0)
    {
        command_cd(args);
    }
    else if(strcmp(cmd_id, "touch") == 0)
    {
        command_touch(args);
    }
    else if(strcmp(cmd_id, "mkdir") == 0)
    {
        command_mkdir(args);
    }
    else if(strcmp(cmd_id, "cat") == 0)
    {
        command_cat(args);
    }
    else if(strcmp(cmd_id, "write") == 0)
    {
        command_write(args, FALSE);
    }
    else if(strcmp(cmd_id, "append") == 0)
    {
        command_write(args, TRUE);
    }
    else if(strcmp(cmd_id, "cp") == 0)
    {
        command_copy_or_move(args, FALSE);
    }
    else if(strcmp(cmd_id, "mv") == 0)
    {
        command_copy_or_move(args, TRUE);
    }
    else if(strcmp(cmd_id, "rm") == 0)
    {
        command_rm(args);
    }
    else if(strcmp(cmd_id, "rmdir") == 0)
    {
        command_rmdir(args);
    }
    else if(strcmp(cmd_id, "ai") == 0)
    {
        if(args == NULL || trim(args)[0] == '\0') {
            append_text("Usage: ai <prompt>");
        } else {
            char *stderr_buf = NULL;
            append_text("Thinking...");
            char *response = run_ai_api(args, &stderr_buf);
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
                append_text("Failed to execute AI script.");
            }
        }
    }
    else if(strcmp(cmd_id, "browse") == 0)
    {
        char *dir = g_get_current_dir();
        char *command;
        if (args != NULL && trim(args)[0] != '\0') {
            char *url = trim(args);
            /* Let Windows file association handle .py */
            command = g_strdup_printf("cmd.exe /c start \"\" \"%s/browser.py\" \"%s\"", dir, url);
        } else {
            command = g_strdup_printf("cmd.exe /c start \"\" \"%s/browser.py\"", dir);
        }
        g_spawn_command_line_async(command, NULL);
        append_text("Opening browser...");
        g_free(command);
        g_free(dir);
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

static void on_terminal_ai_prompt_run(GtkWidget *button, gpointer data) {
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

        char *response = run_ai_api(prompt, &stderr_buf);
        if (response && response[0] != '\0') {
            gtk_text_buffer_get_end_iter(ai_chat_buffer, &iter);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "AI: ", -1);
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
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "AI Error: ", -1);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, stderr_buf, -1);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "\n\n", -1);
        } else {
            gtk_text_buffer_get_end_iter(ai_chat_buffer, &iter);
            gtk_text_buffer_insert(ai_chat_buffer, &iter, "AI: Error calling API\n\n", -1);
        }
        
        gtk_editable_set_text(GTK_EDITABLE(entry), "");
        gtk_widget_set_sensitive(entry, TRUE);
        gtk_widget_grab_focus(entry);

        g_free(response);
        g_free(stderr_buf);
        g_free(prompt);
    }
}

static void on_help_reset_clicked(GtkWidget *button, gpointer data) {
    for (int i = 0; i < num_terminal_commands; i++) {
        g_strlcpy(terminal_commands[i].current_name, terminal_commands[i].default_name, sizeof(terminal_commands[i].current_name));
    }
    append_text("> All commands have been reset to their default names.");
}

static void on_command_name_changed(GtkEditable *editable, gpointer user_data) {
    int idx = GPOINTER_TO_INT(user_data);
    const char *text = gtk_editable_get_text(editable);
    if (text && text[0] != '\0') {
        g_strlcpy(terminal_commands[idx].current_name, text, sizeof(terminal_commands[idx].current_name));
    }
}

static gboolean help_filter_func(GtkListBoxRow *row, gpointer user_data) {
    GtkSearchEntry *search_entry = GTK_SEARCH_ENTRY(user_data);
    const char *search_text = gtk_editable_get_text(GTK_EDITABLE(search_entry));
    if (!search_text || search_text[0] == '\0') {
        return TRUE;
    }
    
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "cmd_idx"));
    const char *cmd_name = terminal_commands[idx].current_name;
    const char *cmd_desc = terminal_commands[idx].description;
    
    char *lower_search = g_utf8_strdown(search_text, -1);
    char *lower_name = g_utf8_strdown(cmd_name, -1);
    char *lower_desc = g_utf8_strdown(cmd_desc, -1);
    
    gboolean match = (strstr(lower_name, lower_search) != NULL) || (strstr(lower_desc, lower_search) != NULL);
    
    g_free(lower_search);
    g_free(lower_name);
    g_free(lower_desc);
    
    return match;
}

static void on_help_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    GtkListBox *listbox = GTK_LIST_BOX(user_data);
    gtk_list_box_invalidate_filter(listbox);
}

static void show_help_gui_window(GtkWidget *button, gpointer data) {
    GtkWidget *help_win = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(help_win), "Command Documentation & Settings");
    gtk_window_set_default_size(GTK_WINDOW(help_win), 600, 700);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    gtk_window_set_child(GTK_WINDOW(help_win), vbox);
    
    GtkWidget *search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), "Search commands...");
    gtk_box_append(GTK_BOX(vbox), search);
    
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(vbox), scroll);
    
    GtkWidget *listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), listbox);
    
    gtk_list_box_set_filter_func(GTK_LIST_BOX(listbox), help_filter_func, search, NULL);
    g_signal_connect(search, "search-changed", G_CALLBACK(on_help_search_changed), listbox);
    
    for (int i = 0; i < num_terminal_commands; i++) {
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_set_margin_start(row_box, 15);
        gtk_widget_set_margin_end(row_box, 15);
        gtk_widget_set_margin_top(row_box, 10);
        gtk_widget_set_margin_bottom(row_box, 10);
        gtk_widget_add_css_class(row_box, "folder-item");
        
        GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
        gtk_widget_set_margin_start(title_box, 15);
        gtk_widget_set_margin_end(title_box, 15);
        gtk_widget_set_margin_top(title_box, 10);
        gtk_box_append(GTK_BOX(row_box), title_box);
        
        char *title_str = g_strdup_printf("<b>%s</b> <span foreground='gray'>(%s)</span>", 
                                          terminal_commands[i].default_name, 
                                          terminal_commands[i].category);
        GtkWidget *name_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(name_label), title_str);
        gtk_widget_set_halign(name_label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(title_box), name_label);
        g_free(title_str);
        
        GtkWidget *spacer = gtk_label_new("");
        gtk_widget_set_hexpand(spacer, TRUE);
        gtk_box_append(GTK_BOX(title_box), spacer);
        
        GtkWidget *edit_entry = gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(edit_entry), terminal_commands[i].current_name);
        gtk_widget_set_size_request(edit_entry, 150, -1);
        g_signal_connect(edit_entry, "changed", G_CALLBACK(on_command_name_changed), GINT_TO_POINTER(i));
        gtk_box_append(GTK_BOX(title_box), edit_entry);
        
        GtkWidget *desc = gtk_label_new(terminal_commands[i].description);
        gtk_widget_set_halign(desc, GTK_ALIGN_START);
        gtk_widget_set_margin_start(desc, 15);
        gtk_widget_set_margin_bottom(desc, 10);
        gtk_box_append(GTK_BOX(row_box), desc);
        
        /* Expander for detailed usage and example */
        GtkWidget *expander = gtk_expander_new("View Details & Examples");
        gtk_widget_set_margin_start(expander, 15);
        gtk_widget_set_margin_bottom(expander, 10);
        gtk_box_append(GTK_BOX(row_box), expander);
        
        GtkWidget *details_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_margin_start(details_box, 10);
        gtk_widget_set_margin_end(details_box, 15);
        gtk_widget_set_margin_top(details_box, 10);
        
        char *usage_str = g_strdup_printf("<b>Usage:</b> %s", terminal_commands[i].usage);
        GtkWidget *usage_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(usage_label), usage_str);
        gtk_widget_set_halign(usage_label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(details_box), usage_label);
        g_free(usage_str);
        
        GtkWidget *example_label = gtk_label_new(terminal_commands[i].example);
        gtk_widget_set_halign(example_label, GTK_ALIGN_START);
        gtk_widget_add_css_class(example_label, "term-text");
        gtk_box_append(GTK_BOX(details_box), example_label);
        
        gtk_expander_set_child(GTK_EXPANDER(expander), details_box);
        
        GtkWidget *row = gtk_list_box_row_new();
        g_object_set_data(G_OBJECT(row), "cmd_idx", GINT_TO_POINTER(i));
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_append(GTK_LIST_BOX(listbox), row);
    }
    
    gtk_window_present(GTK_WINDOW(help_win));
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
    gtk_widget_add_css_class(window, "term-window");
    
    GtkWidget *header = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window), header);
    
    GtkWidget *help_btn = gtk_button_new_with_label("Help & Docs");
    gtk_widget_add_css_class(help_btn, "suggested-action");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), help_btn);
    g_signal_connect(help_btn, "clicked", G_CALLBACK(show_help_gui_window), NULL);
    
    GtkWidget *reset_btn = gtk_button_new_with_label("Reset Commands");
    gtk_widget_add_css_class(reset_btn, "destructive-action");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), reset_btn);
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_help_reset_clicked), NULL);

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_window_set_child(GTK_WINDOW(window), hbox);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_size_request(box, 960, -1);
    gtk_box_append(GTK_BOX(hbox), box);

    textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);
    gtk_widget_add_css_class(textview, "term-text");
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

    append_text("Welcome to Virtual OS");
    append_text("Type help");

    scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), textview);
    gtk_box_append(GTK_BOX(box), scroll);

    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter command...");
    gtk_widget_add_css_class(entry, "term-entry");
    g_signal_connect(entry, "activate", G_CALLBACK(process_command), NULL);
    gtk_box_append(GTK_BOX(box), entry);

    // AI Chat panel
    ai_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(ai_vbox, 300, -1);
    gtk_widget_add_css_class(ai_vbox, "term-ai-panel");
    gtk_box_append(GTK_BOX(hbox), ai_vbox);
    
    ai_label = gtk_label_new("\xe2\x9c\xa6 AI Chat");
    gtk_widget_add_css_class(ai_label, "term-ai-title");
    gtk_box_append(GTK_BOX(ai_vbox), ai_label);
    
    ai_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ai_textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ai_textview), GTK_WRAP_WORD_CHAR);
    gtk_widget_add_css_class(ai_textview, "term-ai-text");
    ai_chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ai_textview));
    
    ai_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(ai_scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(ai_scroll), ai_textview);
    gtk_box_append(GTK_BOX(ai_vbox), ai_scroll);
    
    ai_entry_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    ai_entry = gtk_entry_new();
    gtk_widget_set_hexpand(ai_entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(ai_entry), "Ask AI...");
    gtk_widget_add_css_class(ai_entry, "term-entry");
    
    ai_btn = gtk_button_new_with_label("Send");
    gtk_widget_add_css_class(ai_btn, "term-ai-btn");
    
    g_signal_connect(ai_btn, "clicked", G_CALLBACK(on_terminal_ai_prompt_run), ai_entry);
    g_signal_connect(ai_entry, "activate", G_CALLBACK(on_terminal_ai_prompt_run), ai_entry);
    
    gtk_box_append(GTK_BOX(ai_entry_box), ai_entry);
    gtk_box_append(GTK_BOX(ai_entry_box), ai_btn);
    gtk_box_append(GTK_BOX(ai_vbox), ai_entry_box);

    gtk_window_present(GTK_WINDOW(window));
}
