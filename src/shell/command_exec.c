// src/shell/command_exec.c - COMPLETE VERSION WITH BUILT-IN ECHO

#include "command_exec.h"
#include "process_manager.h"
#include "signal_handler.h"
#include "../utils/unicode_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

// Global callback for processing events during command execution
static int (*g_event_processor_callback)(void) = NULL;

void set_event_processor_callback(int (*callback)(void)) {
    g_event_processor_callback = callback;
    printf("[EXEC] Event processor callback registered\n");
    fflush(stdout);
}

// Built-in 'cd' command
int builtin_cd(Command *cmd) {
    if (cmd->argc < 2) {
        if (chdir(getenv("HOME")) != 0) perror("cd");
    } else {
        if (chdir(cmd->args[1]) != 0) perror("cd");
    }
    return 0;
}

// Built-in 'echo' command with -e flag support
static char* builtin_echo(Command *cmd) {
    if (cmd->argc < 2) {
        return strdup("\n");
    }
    
    int enable_escapes = 0;
    int start_index = 1;
    int suppress_newline = 0;
    
    // Parse flags
    for (int i = 1; i < cmd->argc; i++) {
        if (strcmp(cmd->args[i], "-e") == 0) {
            enable_escapes = 1;
            start_index = i + 1;
        } else if (strcmp(cmd->args[i], "-n") == 0) {
            suppress_newline = 1;
            start_index = i + 1;
        } else if (strcmp(cmd->args[i], "-ne") == 0 || strcmp(cmd->args[i], "-en") == 0) {
            enable_escapes = 1;
            suppress_newline = 1;
            start_index = i + 1;
        } else {
            // First non-flag argument, stop parsing flags
            break;
        }
    }
    
    // Allocate buffer for output
    char *output = malloc(8192);
    if (!output) return NULL;
    output[0] = '\0';
    
    // Process each argument
    for (int i = start_index; i < cmd->argc; i++) {
        if (i > start_index) {
            strcat(output, " ");  // Space between arguments
        }
        
        if (enable_escapes) {
            // Process escape sequences
            char processed[4096];
            process_escape_sequences(cmd->args[i], processed, sizeof(processed));
            strcat(output, processed);
        } else {
            // No escape processing
            strcat(output, cmd->args[i]);
        }
    }
    
    if (!suppress_newline) {
        strcat(output, "\n");
    }
    
    return output;
}

// Handle output redirection for built-in commands
static int handle_builtin_output_redirection(char *output, RedirectInfo *redir_info) {
    if (!redir_info || redir_info->count == 0) {
        return 0;  // No redirection
    }
    
    for (int i = 0; i < redir_info->count; i++) {
        if (redir_info->redirects[i].type == REDIRECT_OUTPUT) {
            FILE *fp = fopen(redir_info->redirects[i].filename, "w");
            if (!fp) {
                perror("fopen");
                return -1;
            }
            fprintf(fp, "%s", output);
            fclose(fp);
            return 1;  // Output redirected to file
        }
    }
    
    return 0;  // No output redirection
}

// Legacy function - kept for backward compatibility
char* execute_external_command(Command *cmd, RedirectInfo *redir_info) {
    int output_pipe[2];
    if (pipe(output_pipe) == -1) { 
        perror("pipe"); 
        return NULL; 
    }

    pid_t pid = fork();
    if (pid == -1) { 
        perror("fork"); 
        return NULL; 
    }

    if (pid == 0) { // --- Child Process ---
        close(output_pipe[0]);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[1]);
        
        // Redirection logic
        for (int i = 0; i < redir_info->count; ++i) {
            Redirect *r = &redir_info->redirects[i];
            if (r->type == REDIRECT_INPUT) {
                int in_fd = open(r->filename, O_RDONLY);
                if (in_fd == -1) { 
                    perror("open"); 
                    exit(1); 
                }
                if (dup2(in_fd, STDIN_FILENO) == -1) { 
                    perror("dup2"); 
                    exit(1); 
                }
                close(in_fd);
            } else if (r->type == REDIRECT_OUTPUT) {
                int out_fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd == -1) { 
                    perror("open"); 
                    exit(1); 
                }
                if (dup2(out_fd, STDOUT_FILENO) == -1) { 
                    perror("dup2"); 
                    exit(1); 
                }
                close(out_fd);
            }
        }

        execvp(cmd->args[0], cmd->args);
        perror("execvp");
        exit(127);
        
    } else { // --- Parent Process ---
        close(output_pipe[1]);
        char *output = malloc(8192);
        if (output) output[0] = '\0';
        char buffer[256];
        int bytes_read;
        while ((bytes_read = read(output_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            strcat(output, buffer);
        }
        close(output_pipe[0]);
        waitpid(pid, NULL, 0);
        return output;
    }
}

// NEW: Execute command with full signal handling support and event processing
// NEW: Execute command with full signal handling support and event processing
char* execute_command_with_signals(Command *cmd, RedirectInfo *redir_info,
                                    ProcessManager *pm, const char *cmd_str) {
    if (!cmd || cmd->argc == 0) {
        printf("[EXEC] ERROR: NULL command\n");
        fflush(stdout);
        return NULL;
    }
    
    printf("[EXEC] Starting command: %s\n", cmd_str);
    fflush(stdout);
    
    int output_pipe[2];
    if (pipe(output_pipe) == -1) { 
        perror("pipe"); 
        return NULL; 
    }

    printf("[EXEC] Forking child process...\n");
    fflush(stdout);
    if (strcmp(cmd->args[0], "echo") == 0) {
        char *echo_output = builtin_echo(cmd);
        if (!echo_output) return NULL;
        
        // Handle output redirection
        int redirected = handle_builtin_output_redirection(echo_output, redir_info);
        if (redirected == 1) {
            free(echo_output);
            return strdup("");  // Output went to file
        } else if (redirected == -1) {
            free(echo_output);
            return strdup("[Error: could not open output file]\n");
        }
        
        return echo_output;  // Return output for display
    }

    pid_t pid = fork();
    if (pid == -1) { 
        perror("fork"); 
        close(output_pipe[0]);
        close(output_pipe[1]);
        return NULL; 
    }

    if (pid == 0) { 
        // ============================================
        // CHILD PROCESS
        // ============================================
        
        pid_t child_pgid = getpid();
        
        // Create new process group
        if (setpgid(0, child_pgid) == -1) {
            perror("[CHILD] setpgid");
            exit(1);
        }
        
        printf("[CHILD] PID=%d, PGID=%d starting command: %s\n", 
               getpid(), child_pgid, cmd->args[0]);
        fflush(stdout);
        
        // Restore default signal handlers
        signal_handler_setup_child();
        
        // Set up output redirection
        close(output_pipe[0]);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[1]);
        
        // Handle file redirections
        for (int i = 0; i < redir_info->count; ++i) {
            Redirect *r = &redir_info->redirects[i];
            if (r->type == REDIRECT_INPUT) {
                int in_fd = open(r->filename, O_RDONLY);
                if (in_fd == -1) { 
                    perror("[CHILD] open input"); 
                    exit(1); 
                }
                if (dup2(in_fd, STDIN_FILENO) == -1) { 
                    perror("[CHILD] dup2 input"); 
                    exit(1); 
                }
                close(in_fd);
            } else if (r->type == REDIRECT_OUTPUT) {
                int out_fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd == -1) { 
                    perror("[CHILD] open output"); 
                    exit(1); 
                }
                if (dup2(out_fd, STDOUT_FILENO) == -1) { 
                    perror("[CHILD] dup2 output"); 
                    exit(1); 
                }
                close(out_fd);
            }
        }

        // Execute the command
        execvp(cmd->args[0], cmd->args);
        
        // If we get here, exec failed
        fprintf(stderr, "[CHILD] execvp failed: %s\n", strerror(errno));
        exit(127);
    }
    
    // ============================================
    // PARENT PROCESS
    // ============================================
    
    close(output_pipe[1]);
    
    // Set child's process group (parent side)
    pid_t child_pgid = pid;
    if (setpgid(pid, child_pgid) == -1 && errno != ESRCH) {
        printf("[PARENT] WARNING: setpgid failed: %s\n", strerror(errno));
        fflush(stdout);
    }
    
    printf("[PARENT] Child PID=%d, PGID=%d started\n", pid, child_pgid);
    fflush(stdout);
    
    // Register as foreground process
    if (pm) {
        printf("[PARENT] Registering as foreground process\n");
        fflush(stdout);
        process_manager_set_foreground(pm, pid, child_pgid, cmd_str);
        signal_handler_give_terminal_to(child_pgid);
    }
    
    // Allocate output buffer
    char *output = malloc(8192);
    if (!output) {
        perror("[PARENT] malloc");
        close(output_pipe[0]);
        waitpid(pid, NULL, 0);
        if (pm) {
            process_manager_clear_foreground(pm);
            signal_handler_take_terminal_back();
        }
        return NULL;
    }
    output[0] = '\0';
    int output_len = 0;
    
    // Make pipe non-blocking for better responsiveness
    int flags = fcntl(output_pipe[0], F_GETFL, 0);
    fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
    
    int process_exited = 0;
    int eof_reached = 0;
    int final_status = 0;  // Store the final status for later checking
    
    printf("[PARENT] Starting read loop, waiting for child to exit...\n");
    fflush(stdout);
    
    // ============================================
    // MAIN WAIT LOOP WITH EVENT PROCESSING
    // ============================================
    
    while (!process_exited) {
        // *** KEY FIX: Process X11 events while waiting ***
        if (g_event_processor_callback) {
            g_event_processor_callback();
        }
        
        // Try to read output (non-blocking)
        if (!eof_reached) {
            char buffer[256];
            ssize_t bytes_read = read(output_pipe[0], buffer, sizeof(buffer) - 1);
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                if (output_len + bytes_read < 8191) {
                    strcat(output, buffer);
                    output_len += bytes_read;
                }
            } else if (bytes_read == 0) {
                // EOF on pipe - child closed stdout
                if (!eof_reached) {
                    printf("[PARENT] EOF on pipe, but continuing to wait for process...\n");
                    fflush(stdout);
                    eof_reached = 1;
                }
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                // Real error (not just "would block")
                printf("[PARENT] Read error: %s\n", strerror(errno));
                fflush(stdout);
                eof_reached = 1;
            }
        }
        
        // Check if process has exited or been stopped
        int status;
        pid_t wait_result = waitpid(pid, &status, WNOHANG | WUNTRACED);
        
        if (wait_result == pid) {
            // Store the status for later use
            final_status = status;
            
            // Process changed state
            if (WIFSTOPPED(status)) {
                // Process was stopped (Ctrl+Z)
                printf("[PARENT] Process %d stopped by signal\n", pid);
                fflush(stdout);
                
                // CRITICAL: Move to background and clean up immediately
                if (pm) {
                    int job_id = process_manager_move_to_background(pm);
                    signal_handler_take_terminal_back();
                    
                    // Format the notification that will be shown
                    char notification[1024];
                    // Get the command from the process manager background job we just added
                    ProcessInfo *bg_job = process_manager_find_by_pid(pm, pid);
                    if (bg_job) {
                        snprintf(notification, sizeof(notification),
                                 "\n[%d]+ Stopped                 %s\n",
                                 job_id, bg_job->command);
                    } else {
                        snprintf(notification, sizeof(notification),
                                 "\n[%d]+ Stopped\n", job_id);
                    }
                    
                    // Append ^Z and notification to output buffer
                    if (output && output_len + strlen("^Z\n") + strlen(notification) < 8191) {
                        strcat(output, "^Z\n");
                        strcat(output, notification);
                        output_len = strlen(output);
                    }
                    
                    printf("[PARENT] Moved to background as job %d\n", job_id);
                    fflush(stdout);
                }
                
                // Exit the wait loop - process is stopped and moved to background
                process_exited = 1;
            } else if (WIFEXITED(status)) {
                // Process exited normally
                printf("[PARENT] Process %d exited with status %d\n", 
                       pid, WEXITSTATUS(status));
                fflush(stdout);
                process_exited = 1;
            } else if (WIFSIGNALED(status)) {
                // Process was terminated by signal
                printf("[PARENT] Process %d terminated by signal %d\n", 
                       pid, WTERMSIG(status));
                fflush(stdout);
                process_exited = 1;
            }
        } else if (wait_result == -1) {
            // Error from waitpid
            if (errno == ECHILD) {
                // Child doesn't exist (already reaped somehow)
                printf("[PARENT] Child process already reaped\n");
                fflush(stdout);
                process_exited = 1;
            } else if (errno != EINTR) {
                // Real error (not interrupted by signal)
                printf("[PARENT] waitpid error: %s\n", strerror(errno));
                fflush(stdout);
                process_exited = 1;
            }
        }
        // If wait_result == 0, process is still running, continue loop
        
        // Small delay to avoid busy-waiting (10ms = responsive but not CPU-heavy)
        usleep(10000);
    }
    
    printf("[PARENT] Process exited, reading remaining output...\n");
    fflush(stdout);
    
    // Only try to read remaining output if process actually exited (not stopped)
    // For stopped processes, the pipe is still open and will block
    if (!WIFSTOPPED(final_status)) {
        // Read any remaining output after process exits
        // (Switch back to blocking mode for final read)
        fcntl(output_pipe[0], F_SETFL, flags & ~O_NONBLOCK);
        
        char buffer[256];
        ssize_t bytes_read;
        while ((bytes_read = read(output_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            if (output_len + bytes_read < 8191) {
                strcat(output, buffer);
                output_len += bytes_read;
            }
        }
    }
    
    close(output_pipe[0]);
    
    printf("[PARENT] Cleaning up, clearing foreground process\n");
    fflush(stdout);
    
    // Clean up process manager state
    // Only clear foreground if the process wasn't moved to background
    if (pm && !WIFSTOPPED(final_status)) {
        process_manager_clear_foreground(pm);
        signal_handler_take_terminal_back();
    }
    // If WIFSTOPPED, we already moved to background and took terminal back
    
    printf("[PARENT] Command execution complete, returning output (%d bytes)\n", output_len);
    fflush(stdout);
    
    return output;
}

// Legacy wrapper function
char* execute_command(Command *cmd, RedirectInfo *redir_info) {
    if (cmd->argc == 0) return NULL;
    
    // Check for built-in echo
    if (strcmp(cmd->args[0], "echo") == 0) {
        char *echo_output = builtin_echo(cmd);
        if (!echo_output) return NULL;
        
        int redirected = handle_builtin_output_redirection(echo_output, redir_info);
        if (redirected == 1) {
            free(echo_output);
            return strdup("");
        } else if (redirected == -1) {
            free(echo_output);
            return strdup("[Error: could not open output file]\n");
        }
        
        return echo_output;
    }
    
    // Check for built-in cd
    if (strcmp(cmd->args[0], "cd") == 0) {
        builtin_cd(cmd);
        return strdup("");
    }
    
    return execute_external_command(cmd, redir_info);
}