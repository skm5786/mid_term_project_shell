// in src/gui/tab_manager.h
#ifndef TAB_MANAGER_H
#define TAB_MANAGER_H

#include <unistd.h>
#include <limits.h>
#include "../input/line_edit.h"
#define MAX_TABS 10
struct TextBuffer;
struct MultiWatch;
typedef struct Tab{
    // This now uses the forward-declared type, which is fine for a pointer.
    struct TextBuffer *buffer;
    pid_t shell_pid;
    int pipe_stdin[2];
    int pipe_stdout[2];
    int active;
    LineEdit *line_edit;
    char working_directory[PATH_MAX];
    void *multiwatch_session;
} Tab;

// Tab manager to handle multiple tabs
typedef struct TabManager{
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
void tab_manager_execute_command(TabManager *mgr, const char *cmd_str);

#endif // TAB_MANAGER_H