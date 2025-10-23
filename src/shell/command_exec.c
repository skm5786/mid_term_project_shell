// in src/shell/command_exec.c

#include "command_exec.h"
#include "process_manager.h"
#include "signal_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

// Built-in 'cd' command
int builtin_cd(Command *cmd) {
    if (cmd->argc < 2) {
        if (chdir(getenv("HOME")) != 0) perror("cd");
    } else {
        if (chdir(cmd->args[1]) != 0) perror("cd");
    }
    return 0;
}

// Legacy function - kept for backward compatibility
char* execute_external_command(Command *cmd, RedirectInfo *redir_info) {
    int output_pipe[2];
    if (pipe(output_pipe) == -1) { perror("pipe"); return NULL; }

    pid_t pid = fork();
    if (pid == -1) { perror("fork"); return NULL; }

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
                if (in_fd == -1) { perror("open"); exit(1); }
                if (dup2(in_fd, STDIN_FILENO) == -1) { perror("dup2"); exit(1); }
                close(in_fd);
            } else if (r->type == REDIRECT_OUTPUT) {
                int out_fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd == -1) { perror("open"); exit(1); }
                if (dup2(out_fd, STDOUT_FILENO) == -1) { perror("dup2"); exit(1); }
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

// NEW: Execute command with full signal handling support
// CRITICAL FIX: Make output pipe non-blocking and use select() for interruptible I/O
char* execute_command_with_signals(Command *cmd, RedirectInfo *redir_info,
                                   struct ProcessManager *pm, const char *cmd_str) {
    if (!cmd || cmd->argc == 0) return NULL;
    
    int output_pipe[2];
    if (pipe(output_pipe) == -1) { 
        perror("pipe"); 
        return NULL; 
    }
    
    // Make read end non-blocking for interruptible reads
    int flags = fcntl(output_pipe[0], F_GETFL, 0);
    fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);

    pid_t pid = fork();
    if (pid == -1) { 
        perror("fork"); 
        close(output_pipe[0]);
        close(output_pipe[1]);
        return NULL; 
    }

    if (pid == 0) { // --- Child Process ---
        // Create a new process group for this command
        pid_t child_pgid = getpid();
        if (setpgid(0, child_pgid) == -1) {
            perror("setpgid in child");
        }
        
        // Restore default signal handlers
        signal_handler_setup_child();
        
        // Set up output capture
        close(output_pipe[0]);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[1]);
        
        // Handle redirections
        for (int i = 0; i < redir_info->count; ++i) {
            Redirect *r = &redir_info->redirects[i];
            if (r->type == REDIRECT_INPUT) {
                int in_fd = open(r->filename, O_RDONLY);
                if (in_fd == -1) { 
                    perror("open input"); 
                    exit(1); 
                }
                if (dup2(in_fd, STDIN_FILENO) == -1) { 
                    perror("dup2 input"); 
                    exit(1); 
                }
                close(in_fd);
            } else if (r->type == REDIRECT_OUTPUT) {
                int out_fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd == -1) { 
                    perror("open output"); 
                    exit(1); 
                }
                if (dup2(out_fd, STDOUT_FILENO) == -1) { 
                    perror("dup2 output"); 
                    exit(1); 
                }
                close(out_fd);
            }
        }

        // Execute the command
        execvp(cmd->args[0], cmd->args);
        perror("execvp");
        exit(127);
        
    } else { // --- Parent Process ---
        close(output_pipe[1]);
        
        // Put child in its own process group
        pid_t child_pgid = pid;
        setpgid(pid, child_pgid);
        
        // Register as foreground process if we have a process manager
        if (pm) {
            process_manager_set_foreground(pm, pid, child_pgid, cmd_str);
            signal_handler_give_terminal_to(child_pgid);
        }
        
        // Read output from child using non-blocking I/O
        char *output = malloc(8192);
        if (output) output[0] = '\0';
        int output_len = 0;
        char buffer[256];
        
        // Keep reading until process exits
        while (1) {
            // Check if process has exited
            int status;
            pid_t wait_result = waitpid(pid, &status, WNOHANG | WUNTRACED);
            
            if (wait_result == pid) {
                // Process exited or stopped
                if (WIFSTOPPED(status)) {
                    // Process was stopped by signal (shouldn't happen here since 
                    // Ctrl+Z is handled in main loop, but check anyway)
                    break;
                } else {
                    // Process exited normally or was terminated
                    // Read any remaining output
                    int bytes_read;
                    while ((bytes_read = read(output_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
                        buffer[bytes_read] = '\0';
                        if (output && output_len + bytes_read < 8192) {
                            strcat(output, buffer);
                            output_len += bytes_read;
                        }
                    }
                    break;
                }
            } else if (wait_result == -1) {
                // Error - process might have been reaped elsewhere
                break;
            }
            
            // Try to read some output (non-blocking)
            int bytes_read = read(output_pipe[0], buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                if (output && output_len + bytes_read < 8192) {
                    strcat(output, buffer);
                    output_len += bytes_read;
                }
            } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                // Real error, not just "would block"
                break;
            }
            
            // Small delay to avoid busy-waiting (1ms)
            usleep(1000);
        }
        
        close(output_pipe[0]);
        
        // Clean up
        if (pm) {
            process_manager_clear_foreground(pm);
            signal_handler_take_terminal_back();
        }
        
        return output;
    }
}

// Legacy wrapper function
char* execute_command(Command *cmd, RedirectInfo *redir_info) {
    if (cmd->argc == 0) return NULL;
    
    // Check for built-ins first
    if (strcmp(cmd->args[0], "cd") == 0) {
        builtin_cd(cmd);
        return strdup("");
    }
    
    // Execute as external command without signal handling
    return execute_external_command(cmd, redir_info);
}