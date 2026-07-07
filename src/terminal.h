#ifndef TERMINAL_H
#define TERMINAL_H

#include <gtk/gtk.h>

void open_terminal_window(void);
void set_desktop_refresh_callback(void (*callback)(void));
char *run_ai_api(const char *prompt, char **error_out);
void open_browser(void);

#endif
