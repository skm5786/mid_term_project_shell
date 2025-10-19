// in src/gui/tab_manager.c

#include "tab_manager.h"
#include "x11_render.h"
#include "../shell/command_exec.h"
#include "../shell/redirect_handler.h"
#include "../shell/pipe_handler.h"
#include "../shell/multiwatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

// Stores the directory where MyTerm was launched.
static char initial_working_directory[PATH_MAX] = {0};

TabManager* tab_manager_init() {
    TabManager *mgr = malloc(sizeof(TabManager));
    if (!mgr) return NULL;

    memset(mgr, 0, sizeof(TabManager));
    mgr->active_tab = -1;
    mgr->num_tabs = 0;

    // Save the initial working directory at startup.
    if (getcwd(initial_working_directory, sizeof(initial_working_directory)) == NULL) {
        perror("getcwd at init");
        strcpy(initial_working_directory, "/"); // Fallback to root
    }

    // Create the first tab.
    tab_manager_create_tab(mgr);
    return mgr;
}

int tab_manager_create_tab(TabManager *mgr) {
    if (!mgr || mgr->num_tabs >= MAX_TABS) return -1;

    int tab_idx = -1;
    for (int i = 0; i < MAX_TABS; i++) {
        if (!mgr->tabs[i].active) {
            tab_idx = i;
            break;
        }
    }
    if (tab_idx == -1) return -1;

    Tab *tab = &mgr->tabs[tab_idx];
    tab->buffer = text_buffer_init();
    if (!tab->buffer) return -1;

    // Initialize the LineEdit object for the new tab.
    tab->line_edit = line_edit_init();
    if (!tab->line_edit) {
        text_buffer_free(tab->buffer);
        return -1;
    }

    // Set the new tab's working directory to the initial directory.
    strncpy(tab->working_directory, initial_working_directory, PATH_MAX - 1);
    tab->working_directory[PATH_MAX - 1] = '\0';

    tab->multiwatch_session = NULL;
    tab->shell_pid = 0;
    tab->active = 1;

    mgr->num_tabs++;
    mgr->active_tab = tab_idx;
    return tab_idx;
}

void tab_manager_switch_tab(TabManager *mgr, int tab_index) {
    if (!mgr || tab_index < 0 || tab_index >= MAX_TABS || !mgr->tabs[tab_index].active) return;
    
    mgr->active_tab = tab_index;
    
    // Change the parent process's CWD to match the new active tab.
    if (chdir(mgr->tabs[tab_index].working_directory) != 0) {
        perror("chdir on tab switch");
    }
}

void tab_manager_close_tab(TabManager *mgr, int tab_index) {
    if (!mgr || tab_index < 0 || tab_index >= MAX_TABS || !mgr->tabs[tab_index].active) return;
    Tab *tab = &mgr->tabs[tab_index];

    // Clean up multiWatch if it's running in the closing tab.
    if (tab->multiwatch_session) {
        cleanup_multiwatch((MultiWatch *)tab->multiwatch_session);
        tab->multiwatch_session = NULL;
    }

    // Free the LineEdit and TextBuffer objects.
    line_edit_free(tab->line_edit);
    text_buffer_free(tab->buffer);
    tab->active = 0;
    mgr->num_tabs--;

    if (mgr->num_tabs == 0) {
        mgr->active_tab = -1;
    } else if (mgr->active_tab == tab_index) {
        // Find the next available tab to make active.
        for (int i = 0; i < MAX_TABS; ++i) {
            if (mgr->tabs[i].active) {
                tab_manager_switch_tab(mgr, i);
                break;
            }
        }
    }
}

Tab* tab_manager_get_active(TabManager *mgr) {
    if (!mgr || mgr->num_tabs == 0 || mgr->active_tab < 0) return NULL;
    return &mgr->tabs[mgr->active_tab];
}

void tab_manager_execute_command(TabManager *mgr, const char *cmd_str) {
    Tab *tab = tab_manager_get_active(mgr);
    // Do not run commands if there's no active tab or if multiWatch is running.
    if (!tab || tab->multiwatch_session) {
        return;
    }

    // Append the executed command to the history buffer.
    text_buffer_append(tab->buffer, "$ ");
    text_buffer_append(tab->buffer, cmd_str);
    text_buffer_append(tab->buffer, "\n");
    
    // Save the main process's CWD to restore it later.
    char saved_cwd[PATH_MAX];
    getcwd(saved_cwd, sizeof(saved_cwd));

    // Change to the tab's specific directory for execution.
    chdir(tab->working_directory);

    char *cmd_to_exec = strdup(cmd_str);

    // --- COMMAND DISPATCHER ---
    if (is_multiwatch_command(cmd_to_exec)) {
        // Start the non-blocking multiWatch session.
        tab->multiwatch_session = multiwatch_start_session(cmd_to_exec);
        if (tab->multiwatch_session) {
            text_buffer_append(tab->buffer, "[multiWatch started. Press Ctrl+C to stop.]\n\n");
        } else {
            text_buffer_append(tab->buffer, "Error: Invalid multiWatch syntax.\n");
        }
    } else {
        // --- NORMAL COMMAND EXECUTION (cd, pipes, etc.) ---
        Command cmd;
        RedirectInfo redir_info;
        char* output = NULL;

        init_redirect_info(&redir_info);
        parse_redirections(cmd_to_exec, &redir_info);
        parse_command(redir_info.clean_command, &cmd);

        if (cmd.argc > 0 && strcmp(cmd.args[0], "cd") == 0) {
            builtin_cd(&cmd);
        } else {
            if (has_pipe(cmd_to_exec)) {
                Pipeline *p = parse_pipeline(cmd_to_exec);
                output = execute_pipeline(p);
                free_pipeline(p);
            } else {
                output = execute_command(&cmd, &redir_info);
            }
        }
        
        free_command(&cmd);
        cleanup_redirect_info(&redir_info);
        
        if (output) {
            text_buffer_append(tab->buffer, output);
            free(output);
        }
    }
    
    free(cmd_to_exec);

    // Clear the line edit buffer for the next command.
    line_edit_clear(tab->line_edit);

    // Update the tab's directory state, in case 'cd' was used.
    getcwd(tab->working_directory, sizeof(tab->working_directory));

    // Restore the main process's original directory.
    chdir(saved_cwd);
}

void tab_manager_cleanup(TabManager *mgr) {
    if (!mgr) return;
    for (int i = 0; i < MAX_TABS; i++) {
        if (mgr->tabs[i].active) {
            tab_manager_close_tab(mgr, i);
        }
    }
    free(mgr);
}