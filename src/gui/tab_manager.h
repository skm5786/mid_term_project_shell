// in src/gui/tab_manager.h - UPDATED FOR AUTOCOMPLETE
#ifndef TAB_MANAGER_H
#define TAB_MANAGER_H

#include <unistd.h>
#include <limits.h>
#include "../input/line_edit.h"
#include "../input/autocomplete.h"

#define MAX_TABS 10

struct TextBuffer;
struct MultiWatch;
struct ProcessManager;
struct HistoryManager;

typedef struct Tab{
    struct TextBuffer *buffer;
    pid_t shell_pid;
    int pipe_stdin[2];
    int pipe_stdout[2];
    int active;
    LineEdit *line_edit;
    char working_directory[PATH_MAX];
    void *multiwatch_session;
    struct ProcessManager *process_manager;
    int in_search_mode;
    
    // NEW: Autocomplete state
    int in_autocomplete_mode;           // Are we showing autocomplete menu?
    AutocompleteResult autocomplete_result;  // Last autocomplete results
    char autocomplete_prefix[MAX_FILENAME_LENGTH];  // Original prefix typed
} Tab;

// Tab manager to handle multiple tabs
typedef struct TabManager{
    Tab tabs[MAX_TABS];
    int active_tab;
    int num_tabs;
    struct HistoryManager *history;
} TabManager;

// Function Prototypes
TabManager* tab_manager_init();
void tab_manager_cleanup(TabManager *mgr);
int tab_manager_create_tab(TabManager *mgr);
void tab_manager_close_tab(TabManager *mgr, int tab_index);
void tab_manager_switch_tab(TabManager *mgr, int tab_index);
Tab* tab_manager_get_active(TabManager *mgr);
void tab_manager_execute_command(TabManager *mgr, const char *cmd_str);

// Signal handling functions
void tab_manager_send_sigint(TabManager *mgr);
void tab_manager_send_sigtstp(TabManager *mgr);
void tab_manager_check_background_jobs(TabManager *mgr, void (*output_callback)(const char *));

// History-related functions
void tab_manager_show_history(TabManager *mgr);
void tab_manager_enter_search_mode(TabManager *mgr);
void tab_manager_execute_search(TabManager *mgr, const char *search_term);

// NEW: Autocomplete functions
/**
 * @brief Handle Tab key press for autocomplete
 * @param mgr Tab manager
 * @return 0 if autocomplete was handled, -1 otherwise
 */
int tab_manager_handle_autocomplete(TabManager *mgr);

/**
 * @brief Handle number selection in autocomplete mode
 * @param mgr Tab manager
 * @param selection The number selected (1-based)
 * @return 0 on success, -1 on failure
 */
int tab_manager_select_autocomplete(TabManager *mgr, int selection);

/**
 * @brief Cancel autocomplete mode
 * @param mgr Tab manager
 */
void tab_manager_cancel_autocomplete(TabManager *mgr);

#endif // TAB_MANAGER_H