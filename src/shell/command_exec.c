// src/shell/command_exec.c - FIXED VERSION
#include "command_exec.h"
#include "process_manager.h"
#include "signal_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
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

// NEW: Execute command with full signal handling support - FIXED VERSION
char* execute_command_with_signals(Command *cmd, RedirectInfo *redir_info,
                                   struct ProcessManager *pm, const char *cmd_str) {
    if (!cmd || cmd->argc == 0) return NULL;
    
    int output_pipe[2];
    if (pipe(output_pipe) == -1) { 
        perror("pipe"); 
        return NULL; 
    }

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
        
        fprintf(stderr, "[DEBUG] Child PID=%d, PGID=%d starting\n", getpid(), child_pgid);
        
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
        
        // Put child in its own process group (parent side)
        pid_t child_pgid = pid;
        setpgid(pid, child_pgid);
        
        fprintf(stderr, "[DEBUG] Parent registered child PID=%d, PGID=%d\n", pid, child_pgid);
        
        // Register as foreground process if we have a process manager
        if (pm) {
            process_manager_set_foreground(pm, pid, child_pgid, cmd_str);
            // Give terminal control to child (may fail in GUI, that's OK)
            signal_handler_give_terminal_to(child_pgid);
        }
        
        // Read output using select() for proper interruption
        char *output = malloc(8192);
        if (!output) {
            close(output_pipe[0]);
            waitpid(pid, NULL, 0);
            return NULL;
        }
        output[0] = '\0';
        int output_len = 0;
        
        // Set pipe to non-blocking mode
        int flags = fcntl(output_pipe[0], F_GETFL, 0);
        fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
        
        int process_exited = 0;
        int exit_status = 0;
        
        while (!process_exited) {
            // Use select with timeout for interruptible I/O
            fd_set read_fds;
            struct timeval timeout;
            
            FD_ZERO(&read_fds);
            FD_SET(output_pipe[0], &read_fds);
            
            timeout.tv_sec = 0;
            timeout.tv_usec = 50000; // 50ms timeout
            
            int select_result = select(output_pipe[0] + 1, &read_fds, NULL, NULL, &timeout);
            
            if (select_result > 0 && FD_ISSET(output_pipe[0], &read_fds)) {
                // Data available to read
                char buffer[256];
                ssize_t bytes_read = read(output_pipe[0], buffer, sizeof(buffer) - 1);
                
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    if (output_len + bytes_read < 8191) {
                        strcat(output, buffer);
                        output_len += bytes_read;
                    }
                } else if (bytes_read == 0) {
                    // EOF - pipe closed
                    break;
                }
            }
            
            // Check if process has exited
            int status;
            pid_t wait_result = waitpid(pid, &status, WNOHANG | WUNTRACED);
            
            if (wait_result == pid) {
                if (WIFSTOPPED(status)) {
                    // Process was stopped (Ctrl+Z)
                    fprintf(stderr, "[DEBUG] Process %d stopped\n", pid);
                    process_exited = 1;
                } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    // Process exited or was killed
                    exit_status = status;
                    process_exited = 1;
                    
                    if (WIFSIGNALED(status)) {
                        fprintf(stderr, "[DEBUG] Process %d terminated by signal %d\n", 
                                pid, WTERMSIG(status));
                    } else {
                        fprintf(stderr, "[DEBUG] Process %d exited with status %d\n", 
                                pid, WEXITSTATUS(status));
                    }
                }
            } else if (wait_result == -1 && errno != EINTR) {
                // Error (but not interrupted by signal)
                fprintf(stderr, "[DEBUG] waitpid error: %s\n", strerror(errno));
                break;
            }
        }
        
        // Read any remaining output after process exits
        char buffer[256];
        ssize_t bytes_read;
        while ((bytes_read = read(output_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            if (output_len + bytes_read < 8191) {
                strcat(output, buffer);
                output_len += bytes_read;
            }
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