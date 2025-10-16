#ifndef TAB_MANAGER_H
#define TAB_MANAGER_H

#include "x11_render.h" // For TextBuffer
#include <unistd.h>     // For pid_t

#define MAX_TABS 10

// The USE_BASH_MODE flag will be defined in the Makefile
// 1 = Step 1 behavior (persistent bash shell)
// 0 = Step 2 behavior (custom fork-per-command shell)

typedef struct Tab {
    TextBuffer *buffer;
    pid_t shell_pid;
    int pipe_stdin[2];
    int pipe_stdout[2];
    int active;
    // As per your new code, we add a buffer for the current input line
    char current_input[MAX_LINE_LENGTH];
    int input_length;
} Tab;

typedef struct TabManager {
    Tab tabs[MAX_TABS];
    int active_tab;
    int num_tabs;
} TabManager;

// Function Prototypes
TabManager* tab_manager_init();
void tab_manager_cleanup(TabManager *mgr);
int tab_manager_create_tab(TabManager *mgr);
void tab_manager_close_tab(TabManager *mgr, int tab_index);
void tab_manager_switch_tab(TabManager *mgr, int tab_index);
Tab* tab_manager_get_active(TabManager *mgr);

// Functions with mode-dependent behavior
void tab_manager_read_output(TabManager *mgr);
void tab_manager_send_input(TabManager *mgr, const char *input);
void tab_manager_execute_command(TabManager *mgr, const char *cmd_str);

#endif // TAB_MANAGER_H