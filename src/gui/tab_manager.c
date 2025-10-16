// in src/gui/tab_manager.c

#include "tab_manager.h"
#include "../shell/command_exec.h"
#include "../shell/command_parser.h"
#include "redirect_handler.h"
#include "../shell/pipe_handler.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

TabManager* tab_manager_init() {
    TabManager *mgr = malloc(sizeof(TabManager));
    if (!mgr) return NULL;
    memset(mgr, 0, sizeof(TabManager));
    mgr->active_tab = -1;
    mgr->num_tabs = 0;
    // Create the first tab, and set it as active
    tab_manager_create_tab(mgr);
    return mgr;
}

int tab_manager_create_tab(TabManager *mgr) {
    if (!mgr || mgr->num_tabs >= MAX_TABS) return -1;
    
    // Find the first inactive slot for the new tab
    int tab_idx = -1;
    for (int i = 0; i < MAX_TABS; ++i) {
        if (!mgr->tabs[i].active) {
            tab_idx = i;
            break;
        }
    }
    if (tab_idx == -1) return -1; // No free slots

    Tab *tab = &mgr->tabs[tab_idx];
    tab->buffer = text_buffer_init();
    if (!tab->buffer) return -1;

#if USE_BASH_MODE
    // Logic for persistent bash shell
    if (pipe(tab->pipe_stdin) == -1 || pipe(tab->pipe_stdout) == -1) {
        perror("pipe"); text_buffer_free(tab->buffer); return -1;
    }
    fcntl(tab->pipe_stdout[0], F_SETFL, O_NONBLOCK);
    tab->shell_pid = fork();
    if (tab->shell_pid == -1) { perror("fork"); return -1; }
    if (tab->shell_pid == 0) { // Child
        dup2(tab->pipe_stdin[0], STDIN_FILENO);
        dup2(tab->pipe_stdout[1], STDOUT_FILENO);
        dup2(tab->pipe_stdout[1], STDERR_FILENO);
        close(tab->pipe_stdin[0]); close(tab->pipe_stdin[1]);
        close(tab->pipe_stdout[0]); close(tab->pipe_stdout[1]);
        execlp("/bin/bash", "bash", "--norc", "--noprofile", NULL);
        perror("execlp"); exit(1);
    } // Parent
    close(tab->pipe_stdin[0]); close(tab->pipe_stdout[1]);
#else
    // Logic for custom command execution
    tab->shell_pid = 0;
#endif

    tab->active = 1;
    tab->input_length = 0;
    memset(tab->current_input, 0, MAX_LINE_LENGTH);
    mgr->num_tabs++;
    text_buffer_append(tab->buffer, "$ ");
    
    // --- THIS IS THE FIX ---
    // After creating a new tab, immediately make it the active one.
    mgr->active_tab = tab_idx;
    
    return tab_idx;
}

void tab_manager_switch_tab(TabManager *mgr, int tab_index) {
    if (mgr && tab_index >= 0 && tab_index < MAX_TABS && mgr->tabs[tab_index].active) {
        mgr->active_tab = tab_index;
    }
}

void tab_manager_close_tab(TabManager *mgr, int tab_index) {
    if (!mgr || tab_index < 0 || tab_index >= MAX_TABS || !mgr->tabs[tab_index].active) return;
    
    Tab *tab = &mgr->tabs[tab_index];
#if USE_BASH_MODE
    if (tab->shell_pid > 0) { kill(tab->shell_pid, SIGTERM); waitpid(tab->shell_pid, NULL, 0); }
    if (tab->pipe_stdin[1] != -1) close(tab->pipe_stdin[1]);
    if (tab->pipe_stdout[0] != -1) close(tab->pipe_stdout[0]);
#endif
    text_buffer_free(tab->buffer);
    tab->active = 0; // Mark the slot as free
    mgr->num_tabs--;

    if (mgr->num_tabs == 0) {
        mgr->active_tab = -1; // No tabs left
    } else if (mgr->active_tab == tab_index) {
        // If we closed the active tab, find the next available one to make active
        for (int i = 0; i < MAX_TABS; ++i) {
            if (mgr->tabs[i].active) {
                mgr->active_tab = i;
                break;
            }
        }
    }
}

Tab* tab_manager_get_active(TabManager *mgr) {
    if (!mgr || mgr->num_tabs == 0 || mgr->active_tab < 0) {
        return NULL;
    }
    return &mgr->tabs[mgr->active_tab];
}

void tab_manager_cleanup(TabManager *mgr) {
    if (!mgr) return;
    for (int i = 0; i < MAX_TABS; i++) {
        if(mgr->tabs[i].active) {
            tab_manager_close_tab(mgr, i);
        }
    }
    free(mgr);
}

// All other functions (execute_command, send_input, read_output) are correct
// and do not need to be changed from the previous version.
void tab_manager_execute_command(TabManager *mgr, const char *cmd_str) {
    #if !USE_BASH_MODE
    Tab *tab = tab_manager_get_active(mgr);
    if (!tab || !cmd_str) return;

    char *cmd_str_copy = strdup(cmd_str);

    // Check if the command contains a pipe
    if (has_pipe(cmd_str_copy)) {
        // --- PIPELINE EXECUTION LOGIC ---
        Pipeline *pipeline = parse_pipeline(cmd_str_copy);
        
        int num_pipes = pipeline->num_commands - 1;
        int pipes[num_pipes][2];
        pid_t pids[pipeline->num_commands];

        // Create all necessary pipes
        for (int i = 0; i < num_pipes; i++) {
            if (pipe(pipes[i]) < 0) { perror("pipe"); free_pipeline(pipeline); free(cmd_str_copy); return; }
        }

        // Fork a process for each command in the pipeline
        for (int i = 0; i < pipeline->num_commands; i++) {
            pids[i] = fork();
            if (pids[i] < 0) { perror("fork"); free_pipeline(pipeline); free(cmd_str_copy); return; }

            if (pids[i] == 0) { // --- Child Process ---
                // If not the first command, redirect stdin from the previous pipe
                if (i > 0) {
                    dup2(pipes[i - 1][0], STDIN_FILENO);
                }
                // If not the last command, redirect stdout to the next pipe
                if (i < pipeline->num_commands - 1) {
                    dup2(pipes[i][1], STDOUT_FILENO);
                }

                // Close all pipe ends in the child
                for (int j = 0; j < num_pipes; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                
                // (This is where you would apply per-command redirections like < and >)

                // Execute the command
                execvp(pipeline->commands[i].cmd.args[0], pipeline->commands[i].cmd.args);
                perror("execvp");
                exit(127);
            }
        }
        
        // --- Parent Process ---
        // Close all pipe ends in the parent
        for (int i = 0; i < num_pipes; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }
        
        // Wait for all children to complete
        for (int i = 0; i < pipeline->num_commands; i++) {
            waitpid(pids[i], NULL, 0);
        }

        free_pipeline(pipeline);

    } else {
        // --- SINGLE COMMAND EXECUTION (No changes from before) ---
        RedirectInfo redir_info;
        init_redirect_info(&redir_info);
        parse_redirections(cmd_str_copy, &redir_info);

        Command cmd;
        parse_command(redir_info.clean_command, &cmd);
        
        char *output = execute_command(&cmd, &redir_info);
        if (output) {
            text_buffer_append(tab->buffer, output);
            free(output);
        }
        free_command(&cmd);
        cleanup_redirect_info(&redir_info);
    }

    text_buffer_append(tab->buffer, "$ ");
    free(cmd_str_copy);
    #endif
}