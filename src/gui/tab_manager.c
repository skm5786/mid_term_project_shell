// in src/gui/tab_manager.c - UPDATED WITH HISTORY SUPPORT (AND ERRORS FIXED)

#include "tab_manager.h"
#include "x11_render.h"
#include "../shell/command_exec.h"
#include "../shell/redirect_handler.h"
#include "../shell/pipe_handler.h"
#include "../shell/multiwatch.h"
#include "../shell/process_manager.h"
#include "../shell/signal_handler.h"
#include "../shell/history_manager.h"  // NEW
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

    // NEW: Initialize history manager
    mgr->history = history_manager_init();
    if (!mgr->history) {
        fprintf(stderr, "Warning: Failed to initialize history manager\n");
    }

    // Save the initial working directory at startup.
    if (getcwd(initial_working_directory, sizeof(initial_working_directory)) == NULL) {
        perror("getcwd at init");
        strcpy(initial_working_directory, "/");
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

    tab->line_edit = line_edit_init();
    if (!tab->line_edit) {
        text_buffer_free(tab->buffer);
        return -1;
    }

    tab->process_manager = process_manager_init();
    if (!tab->process_manager) {
        line_edit_free(tab->line_edit);
        text_buffer_free(tab->buffer);
        return -1;
    }

    strncpy(tab->working_directory, initial_working_directory, PATH_MAX - 1);
    tab->working_directory[PATH_MAX - 1] = '\0';

    tab->multiwatch_session = NULL;
    tab->shell_pid = 0;
    tab->active = 1;
    tab->in_search_mode = 0;  // NEW

    mgr->num_tabs++;
    mgr->active_tab = tab_idx;
    return tab_idx;
}

void tab_manager_switch_tab(TabManager *mgr, int tab_index) {
    if (!mgr || tab_index < 0 || tab_index >= MAX_TABS || !mgr->tabs[tab_index].active) return;
    
    mgr->active_tab = tab_index;
    
    if (chdir(mgr->tabs[tab_index].working_directory) != 0) {
        perror("chdir on tab switch");
    }
}

void tab_manager_close_tab(TabManager *mgr, int tab_index) {
    if (!mgr || tab_index < 0 || tab_index >= MAX_TABS || !mgr->tabs[tab_index].active) return;
    Tab *tab = &mgr->tabs[tab_index];

    if (tab->multiwatch_session) {
        cleanup_multiwatch((MultiWatch *)tab->multiwatch_session);
        tab->multiwatch_session = NULL;
    }

    if (tab->process_manager) {
        process_manager_cleanup(tab->process_manager);
        tab->process_manager = NULL;
    }

    line_edit_free(tab->line_edit);
    text_buffer_free(tab->buffer);
    tab->active = 0;
    mgr->num_tabs--;

    if (mgr->num_tabs == 0) {
        mgr->active_tab = -1;
    } else if (mgr->active_tab == tab_index) {
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

// Replace the tab_manager_send_sigint() function in src/gui/tab_manager.c

void tab_manager_send_sigint(TabManager *mgr) {
    printf("[SIGINT] tab_manager_send_sigint() called\n");
    fflush(stdout);
    
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab) {
        printf("[SIGINT] ERROR: No active tab\n");
        fflush(stdout);
        return;
    }
    
    if (!tab->process_manager) {
        printf("[SIGINT] ERROR: No process manager\n");
        fflush(stdout);
        return;
    }
    
    ProcessInfo *fg_proc = process_manager_get_foreground(tab->process_manager);
    if (!fg_proc) {
        printf("[SIGINT] ERROR: No foreground process\n");
        fflush(stdout);
        return;
    }
    
    printf("[SIGINT] Foreground process: PID=%d, PGID=%d, Command='%s'\n", 
            fg_proc->pid, fg_proc->pgid, fg_proc->command);
    fflush(stdout);
    
    // Check if process still exists
    if (kill(fg_proc->pid, 0) == -1) {
        printf("[SIGINT] Process %d doesn't exist (errno=%d: %s)\n", 
               fg_proc->pid, errno, strerror(errno));
        fflush(stdout);
        process_manager_clear_foreground(tab->process_manager);
        signal_handler_take_terminal_back();
        text_buffer_append(tab->buffer, "^C\n");
        return;
    }
    
    printf("[SIGINT] Process exists, sending SIGINT to group -%d\n", fg_proc->pgid);
    fflush(stdout);
    
    // Send SIGINT to the entire process group
    int kill_result = kill(-fg_proc->pgid, SIGINT);
    
    printf("[SIGINT] kill(-%d, SIGINT) = %d, errno=%d (%s)\n", 
           fg_proc->pgid, kill_result, errno, 
           kill_result == -1 ? strerror(errno) : "success");
    fflush(stdout);
    
    if (kill_result == -1) {
        printf("[SIGINT] kill() failed, cleaning up anyway\n");
        fflush(stdout);
        process_manager_clear_foreground(tab->process_manager);
        signal_handler_take_terminal_back();
        text_buffer_append(tab->buffer, "^C\n");
        return;
    }
    
    // Wait for process to terminate (with timeout)
    int status;
    int wait_attempts = 0;
    int max_attempts = 50; // 500ms total
    
    printf("[SIGINT] Waiting for process to terminate...\n");
    fflush(stdout);
    
    while (wait_attempts < max_attempts) {
        pid_t result = waitpid(fg_proc->pid, &status, WNOHANG);
        
        if (result == fg_proc->pid) {
            // Process exited
            printf("[SIGINT] Process terminated: ");
            if (WIFSIGNALED(status)) {
                printf("killed by signal %d\n", WTERMSIG(status));
            } else if (WIFEXITED(status)) {
                printf("exited with status %d\n", WEXITSTATUS(status));
            } else {
                printf("unknown status\n");
            }
            fflush(stdout);
            break;
        } else if (result == -1) {
            printf("[SIGINT] waitpid returned -1, errno=%d (%s)\n", 
                   errno, strerror(errno));
            fflush(stdout);
            break;
        } else if (result == 0) {
            // Still running
            usleep(10000); // 10ms
            wait_attempts++;
        }
    }
    
    if (wait_attempts >= max_attempts) {
        printf("[SIGINT] WARNING: Process didn't terminate after 500ms\n");
        fflush(stdout);
    }
    
    // Clean up
    process_manager_clear_foreground(tab->process_manager);
    signal_handler_take_terminal_back();
    text_buffer_append(tab->buffer, "^C\n");
    
    printf("[SIGINT] Cleanup complete, ^C added to buffer\n");
    fflush(stdout);
}

void tab_manager_send_sigtstp(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) return;
    
    ProcessInfo *fg_proc = process_manager_get_foreground(tab->process_manager);
    if (fg_proc) {
        kill(-fg_proc->pgid, SIGTSTP);
        
        int status;
        pid_t result = waitpid(fg_proc->pid, &status, WUNTRACED);
        
        if (result == fg_proc->pid && WIFSTOPPED(status)) {
            int job_id = process_manager_move_to_background(tab->process_manager);
            signal_handler_take_terminal_back();
            
            char notification[1024];
            snprintf(notification, sizeof(notification),
                     "\n[%d]+ Stopped                 %s\n",
                     job_id, fg_proc->command);
            text_buffer_append(tab->buffer, notification);
        }
    }
}

void tab_manager_check_background_jobs(TabManager *mgr, void (*output_callback)(const char *)) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) return;
    
    process_manager_check_background_jobs(tab->process_manager, output_callback);
}

// NEW: Show history command
void tab_manager_show_history(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !mgr->history) return;
    
    char *output = malloc(102400); // ~100KB for history output
    if (!output) {
        text_buffer_append(tab->buffer, "Error: Out of memory\n");
        return;
    }
    
    history_manager_get_recent(mgr->history, output, 102400, HISTORY_DISPLAY_SIZE);
    text_buffer_append(tab->buffer, output);
    free(output);
}

// NEW: Enter search mode (Ctrl+R)
void tab_manager_enter_search_mode(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab) return;
    
    tab->in_search_mode = 1;
    line_edit_clear(tab->line_edit);
    text_buffer_append(tab->buffer, "\nEnter search term: ");
}

// NEW: Execute history search
void tab_manager_execute_search(TabManager *mgr, const char *search_term) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !mgr->history) return;
    
    tab->in_search_mode = 0;
    
    // Show what user searched for
    text_buffer_append(tab->buffer, search_term);
    text_buffer_append(tab->buffer, "\n");
    
    if (strlen(search_term) == 0) {
        text_buffer_append(tab->buffer, "No search term entered.\n");
        return;
    }
    
    // Try exact match first
    char exact_result[MAX_COMMAND_LENGTH];
    if (history_manager_search_exact(mgr->history, search_term, 
                                     exact_result, sizeof(exact_result))) {
        text_buffer_append(tab->buffer, "[Exact match found]\n");
        text_buffer_append(tab->buffer, exact_result);
        text_buffer_append(tab->buffer, "\n");
        return;
    }
    
    // No exact match, try fuzzy search
    HistorySearchResult results[MAX_SEARCH_RESULTS];
    int num_results = history_manager_search_fuzzy(mgr->history, search_term,
                                                    results, MAX_SEARCH_RESULTS);
    
    if (num_results > 0) {
        char *output = malloc(51200); // 50KB for search results
        if (output) {
            format_search_results(results, num_results, output, 51200);
            text_buffer_append(tab->buffer, "[Fuzzy matches found]\n");
            text_buffer_append(tab->buffer, output);
            free(output);
        }
    } else {
        text_buffer_append(tab->buffer, "No match for search term in history\n");
    }
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
            // NEW: Execute with process management - NON-BLOCKING VERSION
            if (has_pipe(cmd_to_exec)) {
                Pipeline *p = parse_pipeline(cmd_to_exec);
                // ** FIX: Call the correct function **
                output = execute_pipeline_with_signals(p, tab->process_manager, cmd_to_exec);
                free_pipeline(p);
            } else {
                // ** FIX: Call the correct function **
                output = execute_command_with_signals(&cmd, &redir_info, 
                                              tab->process_manager, cmd_to_exec);
            }
        }
        
        free_command(&cmd);
        cleanup_redirect_info(&redir_info);
        
        // Output will be NULL for async commands (collected later)
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

// NEW: Check if foreground command has output ready and collect it
void tab_manager_collect_command_output(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) return;
    
    ProcessInfo *fg_proc = process_manager_get_foreground(tab->process_manager);
    if (!fg_proc) return;
    
    // Check if process has completed
    int status;
    pid_t result = waitpid(fg_proc->pid, &status, WNOHANG | WUNTRACED);
    
    if (result == fg_proc->pid) {
        if (WIFSTOPPED(status)) {
            // Process was stopped (Ctrl+Z)
            printf("[COLLECT] Process %d stopped\n", fg_proc->pid);
            fflush(stdout);
            
            int job_id = process_manager_move_to_background(tab->process_manager);
            signal_handler_take_terminal_back();
            
            char notification[1024];
            snprintf(notification, sizeof(notification),
                     "\n[%d]+ Stopped                 %s\n",
                     job_id, fg_proc->command);
            text_buffer_append(tab->buffer, notification);
        } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
            // Process exited or was killed
            printf("[COLLECT] Process %d terminated\n", fg_proc->pid);
            fflush(stdout);
            
            // TODO: Collect any remaining output from the pipe
            // For now, just clean up
            process_manager_clear_foreground(tab->process_manager);
            signal_handler_take_terminal_back();
        }
    }
}
void tab_manager_cleanup(TabManager *mgr) {
    if (!mgr) return;

    // NEW: Clean up history manager
    if (mgr->history) {
        history_manager_cleanup(mgr->history);
        mgr->history = NULL;
    }

    for (int i = 0; i < MAX_TABS; i++) {
        if (mgr->tabs[i].active) {
            tab_manager_close_tab(mgr, i);
        }
    }
    free(mgr);
}