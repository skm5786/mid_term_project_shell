// in src/gui/tab_manager.c

#include "tab_manager.h"
#include "x11_render.h"
#include "../shell/command_exec.h"
#include "../shell/redirect_handler.h"
#include "../shell/pipe_handler.h"
#include "../shell/multiwatch.h"
#include "../shell/process_manager.h"
#include "../shell/signal_handler.h"
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

    // NEW: Initialize process manager for this tab
    tab->process_manager = process_manager_init();
    if (!tab->process_manager) {
        line_edit_free(tab->line_edit);
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

    // NEW: Clean up process manager
    if (tab->process_manager) {
        process_manager_cleanup(tab->process_manager);
        tab->process_manager = NULL;
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

// NEW: Handle Ctrl+C signal
// Add this debug version to tab_manager_send_sigint() function

void tab_manager_send_sigint(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) {
        fprintf(stderr, "[DEBUG] No active tab or process manager\n");
        return;
    }
    
    ProcessInfo *fg_proc = process_manager_get_foreground(tab->process_manager);
    if (!fg_proc) {
        fprintf(stderr, "[DEBUG] No foreground process found\n");
        return;
    }
    
    fprintf(stderr, "[DEBUG] Foreground process: PID=%d, PGID=%d, Command=%s\n", 
            fg_proc->pid, fg_proc->pgid, fg_proc->command);
    
    // Check if process still exists
    if (kill(fg_proc->pid, 0) == -1) {
        fprintf(stderr, "[DEBUG] Process %d no longer exists (errno=%d)\n", 
                fg_proc->pid, errno);
        process_manager_clear_foreground(tab->process_manager);
        signal_handler_take_terminal_back();
        return;
    }
    
    fprintf(stderr, "[DEBUG] Sending SIGINT to process group -%d\n", fg_proc->pgid);
    
    // Send SIGINT to the entire process group
    if (kill(-fg_proc->pgid, SIGINT) == -1) {
        fprintf(stderr, "[DEBUG] kill() failed: errno=%d (%s)\n", 
                errno, strerror(errno));
    } else {
        fprintf(stderr, "[DEBUG] SIGINT sent successfully\n");
    }
    
    // Wait briefly for the process to terminate
    int status;
    pid_t result = waitpid(fg_proc->pid, &status, WNOHANG);
    
    fprintf(stderr, "[DEBUG] waitpid result: %d\n", result);
    
    if (result == fg_proc->pid || result == -1) {
        fprintf(stderr, "[DEBUG] Process terminated or error, clearing foreground\n");
        process_manager_clear_foreground(tab->process_manager);
        signal_handler_take_terminal_back();
        text_buffer_append(tab->buffer, "^C\n");
    } else {
        fprintf(stderr, "[DEBUG] Process still running after SIGINT\n");
    }
}
// NEW: Handle Ctrl+Z signal
void tab_manager_send_sigtstp(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) return;
    
    ProcessInfo *fg_proc = process_manager_get_foreground(tab->process_manager);
    if (fg_proc) {
        // Send SIGTSTP to the entire process group
        kill(-fg_proc->pgid, SIGTSTP);
        
        // Wait for the process to stop
        int status;
        pid_t result = waitpid(fg_proc->pid, &status, WUNTRACED);
        
        if (result == fg_proc->pid && WIFSTOPPED(status)) {
            // Process was stopped, move it to background
            int job_id = process_manager_move_to_background(tab->process_manager);
            
            // Take terminal control back
            signal_handler_take_terminal_back();
            
            // Print notification
            char notification[1024];
            snprintf(notification, sizeof(notification),
                     "\n[%d]+ Stopped                 %s\n",
                     job_id, fg_proc->command);
            text_buffer_append(tab->buffer, notification);
        }
    }
}

// NEW: Check for completed background jobs
void tab_manager_check_background_jobs(TabManager *mgr, void (*output_callback)(const char *)) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) return;
    
    process_manager_check_background_jobs(tab->process_manager, output_callback);
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
        } else if (cmd.argc > 0) {
            // NEW: Execute with process management
            if (has_pipe(cmd_to_exec)) {
                Pipeline *p = parse_pipeline(cmd_to_exec);
                output = execute_pipeline_with_signals(p, tab->process_manager, cmd_to_exec);
                free_pipeline(p);
            } else {
                output = execute_command_with_signals(&cmd, &redir_info, 
                                                     tab->process_manager, cmd_to_exec);
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