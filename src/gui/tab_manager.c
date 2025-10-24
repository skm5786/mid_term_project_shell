// in src/gui/tab_manager.c - FINAL FIX

#include "tab_manager.h"
#include "x11_render.h"
#include "../shell/command_exec.h"
#include "../shell/redirect_handler.h"
#include "../shell/pipe_handler.h"
#include "../shell/multiwatch.h"
#include "../shell/process_manager.h"
#include "../shell/signal_handler.h"
#include "../shell/history_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

static char initial_working_directory[PATH_MAX] = {0};

TabManager* tab_manager_init() {
    TabManager *mgr = malloc(sizeof(TabManager));
    if (!mgr) return NULL;

    memset(mgr, 0, sizeof(TabManager));
    mgr->active_tab = -1;
    mgr->num_tabs = 0;

    mgr->history = history_manager_init();
    if (!mgr->history) {
        fprintf(stderr, "Warning: Failed to initialize history manager\n");
    } else {
        printf("[HISTORY] History manager initialized successfully\n");
        fflush(stdout);
    }

    if (getcwd(initial_working_directory, sizeof(initial_working_directory)) == NULL) {
        perror("getcwd at init");
        strcpy(initial_working_directory, "/");
    }

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
    
    tab->in_autocomplete_mode = 0;
    memset(&tab->autocomplete_result, 0, sizeof(AutocompleteResult));
    tab->autocomplete_prefix[0] = '\0';

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
    tab->in_search_mode = 0;

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

void tab_manager_send_sigint(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) return;
    
    ProcessInfo *fg_proc = process_manager_get_foreground(tab->process_manager);
    if (!fg_proc) return;
    
    if (kill(fg_proc->pid, 0) == -1) {
        process_manager_clear_foreground(tab->process_manager);
        signal_handler_take_terminal_back();
        text_buffer_append(tab->buffer, "^C\n");
        return;
    }
    
    int kill_result = kill(-fg_proc->pgid, SIGINT);
    
    if (kill_result == -1) {
        process_manager_clear_foreground(tab->process_manager);
        signal_handler_take_terminal_back();
        text_buffer_append(tab->buffer, "^C\n");
        return;
    }
    
    int status;
    int wait_attempts = 0;
    int max_attempts = 50;
    
    while (wait_attempts < max_attempts) {
        pid_t result = waitpid(fg_proc->pid, &status, WNOHANG);
        
        if (result == fg_proc->pid) {
            break;
        } else if (result == -1) {
            break;
        } else if (result == 0) {
            usleep(10000);
            wait_attempts++;
        }
    }
    
    process_manager_clear_foreground(tab->process_manager);
    signal_handler_take_terminal_back();
    text_buffer_append(tab->buffer, "^C\n");
}

void tab_manager_send_sigtstp(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) return;
    
    ProcessInfo *fg_proc = process_manager_get_foreground(tab->process_manager);
    if (!fg_proc) return;
    
    printf("[SIGTSTP] Sending SIGTSTP to PGID %d (PID %d)\n", fg_proc->pgid, fg_proc->pid);
    fflush(stdout);
    
    // Send SIGTSTP to the process group
    if (kill(-fg_proc->pgid, SIGTSTP) == -1) {
        perror("kill SIGTSTP");
        text_buffer_append(tab->buffer, "^Z\n");
        process_manager_clear_foreground(tab->process_manager);
        signal_handler_take_terminal_back();
        return;
    }
    
    printf("[SIGTSTP] Signal sent successfully\n");
    fflush(stdout);
    
    // The stopped process will be handled by execute_command_with_signals
    // Just send the signal here - the rest will be handled when waitpid detects WIFSTOPPED
}

void tab_manager_check_background_jobs(TabManager *mgr, void (*output_callback)(const char *)) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->process_manager) return;
    
    process_manager_check_background_jobs(tab->process_manager, output_callback);
}

void tab_manager_show_history(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !mgr->history) return;
    
    char *output = malloc(102400);
    if (!output) {
        text_buffer_append(tab->buffer, "Error: Out of memory\n");
        return;
    }
    
    history_manager_get_recent(mgr->history, output, 102400, HISTORY_DISPLAY_SIZE);
    text_buffer_append(tab->buffer, output);
    free(output);
}

void tab_manager_enter_search_mode(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab) return;
    
    printf("[SEARCH] Entering search mode\n");
    fflush(stdout);
    
    tab->in_search_mode = 1;
    line_edit_clear(tab->line_edit);
    text_buffer_append(tab->buffer, "\nEnter search term: ");
}

void tab_manager_execute_search(TabManager *mgr, const char *search_term) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !mgr->history) return;
    
    printf("[SEARCH] Executing search for: '%s'\n", search_term);
    fflush(stdout);
    
    tab->in_search_mode = 0;
    
    text_buffer_append(tab->buffer, search_term);
    text_buffer_append(tab->buffer, "\n");
    
    if (strlen(search_term) == 0) {
        text_buffer_append(tab->buffer, "No search term entered.\n");
        return;
    }
    
    char exact_result[MAX_COMMAND_LENGTH];
    if (history_manager_search_exact(mgr->history, search_term, 
                                     exact_result, sizeof(exact_result))) {
        text_buffer_append(tab->buffer, "[Exact match found]\n");
        text_buffer_append(tab->buffer, exact_result);
        text_buffer_append(tab->buffer, "\n");
        return;
    }
    
    HistorySearchResult results[MAX_SEARCH_RESULTS];
    int num_results = history_manager_search_fuzzy(mgr->history, search_term,
                                                    results, MAX_SEARCH_RESULTS);
    
    if (num_results > 0) {
        char *output = malloc(51200);
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
    if (!tab || tab->multiwatch_session) {
        return;
    }

    // Handle search mode
    if (tab->in_search_mode) {
        tab_manager_execute_search(mgr, cmd_str);
        line_edit_clear(tab->line_edit);
        return;
    }

    // Don't process empty commands
    if (strlen(cmd_str) == 0) {
        return;
    }

    // CRITICAL FIX: Save the original command BEFORE any modification
    char original_cmd[MAX_COMMAND_LENGTH];
    strncpy(original_cmd, cmd_str, MAX_COMMAND_LENGTH - 1);
    original_cmd[MAX_COMMAND_LENGTH - 1] = '\0';

    printf("[EXECUTE] Command: '%s'\n", original_cmd);
    fflush(stdout);

    text_buffer_append(tab->buffer, "$ ");
    text_buffer_append(tab->buffer, original_cmd);
    text_buffer_append(tab->buffer, "\n");
    
    char saved_cwd[PATH_MAX];
    getcwd(saved_cwd, sizeof(saved_cwd));
    chdir(tab->working_directory);

    char *cmd_to_exec = strdup(original_cmd);

    // Check for history command
    if (strcmp(cmd_to_exec, "history") == 0) {
        tab_manager_show_history(mgr);
        free(cmd_to_exec);
        line_edit_clear(tab->line_edit);
        getcwd(tab->working_directory, sizeof(tab->working_directory));
        chdir(saved_cwd);
        
        // Add history command to history
        if (mgr->history) {
            printf("[HISTORY] Adding 'history' command to history\n");
            fflush(stdout);
            history_manager_add_command(mgr->history, original_cmd);
            history_manager_save_to_file(mgr->history);
        }
        return;
    }

    // Execute the command
    if (is_multiwatch_command(cmd_to_exec)) {
        tab->multiwatch_session = multiwatch_start_session(cmd_to_exec);
        if (tab->multiwatch_session) {
            text_buffer_append(tab->buffer, "[multiWatch started. Press Ctrl+C to stop.]\n\n");
        } else {
            text_buffer_append(tab->buffer, "Error: Invalid multiWatch syntax.\n");
        }
    } else {
        Command cmd;
        RedirectInfo redir_info;
        char* output = NULL;

        init_redirect_info(&redir_info);
        parse_redirections(cmd_to_exec, &redir_info);
        parse_command(redir_info.clean_command, &cmd);

        if (cmd.argc > 0 && strcmp(cmd.args[0], "cd") == 0) {
            builtin_cd(&cmd);
        } else if (cmd.argc > 0) {
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
    
    printf("[EXECUTE] Command execution completed, cleaning up\n");
    fflush(stdout);
    
    free(cmd_to_exec);
    line_edit_clear(tab->line_edit);
    getcwd(tab->working_directory, sizeof(tab->working_directory));
    chdir(saved_cwd);
    
    printf("[EXECUTE] Tab manager cleanup completed, ready for next input\n");
    fflush(stdout);
    
    // CRITICAL: Use original_cmd (not cmd_str which may be modified)
    if (mgr->history) {
        printf("[HISTORY] Adding command to history: '%s'\n", original_cmd);
        fflush(stdout);
        int result = history_manager_add_command(mgr->history, original_cmd);
        if (result == 0) {
            printf("[HISTORY] Command added successfully, saving to file...\n");
            fflush(stdout);
            history_manager_save_to_file(mgr->history);
            printf("[HISTORY] File saved\n");
            fflush(stdout);
        } else {
            printf("[HISTORY] ERROR: Failed to add command\n");
            fflush(stdout);
        }
    } else {
        printf("[HISTORY] ERROR: History manager is NULL!\n");
        fflush(stdout);
    }
}
int tab_manager_handle_autocomplete(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || tab->multiwatch_session || tab->in_search_mode) {
        return -1;
    }
    
    printf("[AUTOCOMPLETE] Tab key pressed\n");
    fflush(stdout);
    
    const char *command_line = line_edit_get_line(tab->line_edit);
    
    // Extract the last token (the filename prefix to complete)
    const char *token_start, *token_end;
    if (autocomplete_extract_last_token(command_line, &token_start, &token_end) != 0) {
        printf("[AUTOCOMPLETE] No token to complete\n");
        fflush(stdout);
        return -1;
    }
    
    // Copy the prefix
    size_t prefix_len = token_end - token_start;
    if (prefix_len >= MAX_FILENAME_LENGTH) {
        printf("[AUTOCOMPLETE] Prefix too long\n");
        fflush(stdout);
        return -1;
    }
    
    strncpy(tab->autocomplete_prefix, token_start, prefix_len);
    tab->autocomplete_prefix[prefix_len] = '\0';
    
    printf("[AUTOCOMPLETE] Prefix: '%s'\n", tab->autocomplete_prefix);
    fflush(stdout);
    
    // Find matches
    if (autocomplete_find_matches(tab->autocomplete_prefix, 
                                   &tab->autocomplete_result) != 0) {
        printf("[AUTOCOMPLETE] Error finding matches\n");
        fflush(stdout);
        return -1;
    }
    
    printf("[AUTOCOMPLETE] Found %d matches\n", tab->autocomplete_result.num_matches);
    fflush(stdout);
    
    // Handle different cases based on number of matches
    if (tab->autocomplete_result.num_matches == 0) {
        // No matches - do nothing
        printf("[AUTOCOMPLETE] No matches found\n");
        fflush(stdout);
        return 0;
        
    } else if (tab->autocomplete_result.num_matches == 1) {
        // Single match - auto-complete immediately
        printf("[AUTOCOMPLETE] Single match: %s\n", 
               tab->autocomplete_result.matches[0]);
        fflush(stdout);
        
        char new_command[MAX_INPUT_LENGTH];
        if (autocomplete_replace_last_token(command_line,
                                           tab->autocomplete_result.matches[0],
                                           new_command,
                                           sizeof(new_command)) == 0) {
            // Update the line edit buffer
            line_edit_clear(tab->line_edit);
            line_edit_insert_string(tab->line_edit, new_command);
            
            printf("[AUTOCOMPLETE] Completed to: %s\n", new_command);
            fflush(stdout);
        }
        
        return 0;
        
    } else {
        // Multiple matches
        printf("[AUTOCOMPLETE] Multiple matches, common prefix: '%s' (len=%d)\n",
               tab->autocomplete_result.longest_common_prefix,
               tab->autocomplete_result.prefix_length);
        fflush(stdout);
        
        // If there's a longer common prefix, complete to that first
        if (tab->autocomplete_result.prefix_length > (int)prefix_len) {
            char new_command[MAX_INPUT_LENGTH];
            if (autocomplete_replace_last_token(command_line,
                                               tab->autocomplete_result.longest_common_prefix,
                                               new_command,
                                               sizeof(new_command)) == 0) {
                line_edit_clear(tab->line_edit);
                line_edit_insert_string(tab->line_edit, new_command);
                
                printf("[AUTOCOMPLETE] Completed to common prefix: %s\n", new_command);
                fflush(stdout);
                return 0;
            }
        }
        
        // Common prefix is same as what's typed - show selection menu
        char matches_display[4096];
        autocomplete_format_matches(&tab->autocomplete_result,
                                    matches_display,
                                    sizeof(matches_display));
        
        text_buffer_append(tab->buffer, "\n");
        text_buffer_append(tab->buffer, matches_display);
        text_buffer_append(tab->buffer, "\nSelect file (1-");
        
        char num_str[16];
        snprintf(num_str, sizeof(num_str), "%d", tab->autocomplete_result.num_matches);
        text_buffer_append(tab->buffer, num_str);
        text_buffer_append(tab->buffer, "): ");
        
        // Enter autocomplete selection mode
        tab->in_autocomplete_mode = 1;
        
        printf("[AUTOCOMPLETE] Entered selection mode\n");
        fflush(stdout);
        
        return 0;
    }
}

// NEW: Handle number selection in autocomplete mode
int tab_manager_select_autocomplete(TabManager *mgr, int selection) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !tab->in_autocomplete_mode) {
        return -1;
    }
    
    printf("[AUTOCOMPLETE] Selection: %d\n", selection);
    fflush(stdout);
    
    // Validate selection
    if (selection < 1 || selection > tab->autocomplete_result.num_matches) {
        text_buffer_append(tab->buffer, "Invalid selection\n");
        tab->in_autocomplete_mode = 0;
        return -1;
    }
    
    // Get the selected filename (convert from 1-based to 0-based)
    const char *selected_file = tab->autocomplete_result.matches[selection - 1];
    
    printf("[AUTOCOMPLETE] Selected file: %s\n", selected_file);
    fflush(stdout);
    
    // Get current command and replace last token
    const char *current_command = line_edit_get_line(tab->line_edit);
    char new_command[MAX_INPUT_LENGTH];
    
    if (autocomplete_replace_last_token(current_command,
                                       selected_file,
                                       new_command,
                                       sizeof(new_command)) == 0) {
        // Update the line edit buffer
        line_edit_clear(tab->line_edit);
        line_edit_insert_string(tab->line_edit, new_command);
        
        printf("[AUTOCOMPLETE] Completed to: %s\n", new_command);
        fflush(stdout);
    }
    
    // Exit autocomplete mode
    tab->in_autocomplete_mode = 0;
    
    return 0;
}

// NEW: Cancel autocomplete mode
void tab_manager_cancel_autocomplete(TabManager *mgr) {
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab) return;
    
    if (tab->in_autocomplete_mode) {
        printf("[AUTOCOMPLETE] Cancelled\n");
        fflush(stdout);
        text_buffer_append(tab->buffer, "Cancelled\n");
        tab->in_autocomplete_mode = 0;
    }
}

void tab_manager_cleanup(TabManager *mgr) {
    if (!mgr) return;
    
    if (mgr->history) {
        history_manager_cleanup(mgr->history);
    }
    
    for (int i = 0; i < MAX_TABS; i++) {
        if (mgr->tabs[i].active) {
            tab_manager_close_tab(mgr, i);
        }
    }
    free(mgr);
}