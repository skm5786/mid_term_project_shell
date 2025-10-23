// in src/shell/pipe_handler.c

#include "pipe_handler.h"
#include "process_manager.h"
#include "signal_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

// Check for pipe character outside of quotes
int has_pipe(const char *cmd_str) {
    int in_quote = 0;
    for (int i = 0; cmd_str[i] != '\0'; i++) {
        if (cmd_str[i] == '"' || cmd_str[i] == '\'') {
            in_quote = !in_quote;
        } else if (cmd_str[i] == '|' && !in_quote) {
            return 1;
        }
    }
    return 0;
}

Pipeline* parse_pipeline(char *cmd_str) {
    Pipeline *pipeline = malloc(sizeof(Pipeline));
    pipeline->num_commands = 0;

    char *saveptr;
    char *segment = strtok_r(cmd_str, "|", &saveptr);

    while (segment != NULL && pipeline->num_commands < MAX_PIPE_COMMANDS) {
        PipeCommand *p_cmd = &pipeline->commands[pipeline->num_commands];
        p_cmd->raw_command = strdup(segment);

        init_redirect_info(&p_cmd->redirects);
        parse_redirections(p_cmd->raw_command, &p_cmd->redirects);
        parse_command(p_cmd->redirects.clean_command, &p_cmd->cmd);

        pipeline->num_commands++;
        segment = strtok_r(NULL, "|", &saveptr);
    }
    return pipeline;
}

void free_pipeline(Pipeline *pipeline) {
    if (!pipeline) return;
    for (int i = 0; i < pipeline->num_commands; i++) {
        free(pipeline->commands[i].raw_command);
        free_command(&pipeline->commands[i].cmd);
        cleanup_redirect_info(&pipeline->commands[i].redirects);
    }
    free(pipeline);
}

// Legacy version without signal handling
char* execute_pipeline(Pipeline *pipeline) {
    if (pipeline->num_commands == 0) return NULL;

    int capture_pipe[2];
    if (pipe(capture_pipe) == -1) {
        perror("capture pipe");
        return NULL;
    }

    pid_t *pids = malloc(pipeline->num_commands * sizeof(pid_t));
    int input_fd = STDIN_FILENO;
    int pipe_fds[2];

    for (int i = 0; i < pipeline->num_commands; i++) {
        PipeCommand *p_cmd = &pipeline->commands[i];

        if (i < pipeline->num_commands - 1) {
            if (pipe(pipe_fds) == -1) {
                perror("inter-process pipe");
                free(pids);
                return NULL;
            }
        }

        pids[i] = fork();
        if (pids[i] == 0) { // Child Process
            if (input_fd != STDIN_FILENO) {
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
            }

            if (i < pipeline->num_commands - 1) {
                close(pipe_fds[0]);
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);
            } else {
                close(capture_pipe[0]);
                dup2(capture_pipe[1], STDOUT_FILENO);
                close(capture_pipe[1]);
            }

            execvp(p_cmd->cmd.args[0], p_cmd->cmd.args);
            perror("execvp in pipe");
            exit(127);
        }

        if (input_fd != STDIN_FILENO) close(input_fd);
        if (i < pipeline->num_commands - 1) {
            close(pipe_fds[1]);
            input_fd = pipe_fds[0];
        }
    }

    close(capture_pipe[1]);

    char *output = malloc(8192);
    if (output) output[0] = '\0';
    char buffer[256];
    ssize_t bytes_read;

    while ((bytes_read = read(capture_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        strcat(output, buffer);
    }
    close(capture_pipe[0]);

    for (int i = 0; i < pipeline->num_commands; i++) {
        waitpid(pids[i], NULL, 0);
    }

    free(pids);
    return output;
}

// NEW: Execute pipeline with signal handling and non-blocking I/O
char* execute_pipeline_with_signals(Pipeline *pipeline, struct ProcessManager *pm, 
                                    const char *cmd_str) {
    if (pipeline->num_commands == 0) return NULL;

    int capture_pipe[2];
    if (pipe(capture_pipe) == -1) {
        perror("capture pipe");
        return NULL;
    }
    
    // Make read end non-blocking
    int flags = fcntl(capture_pipe[0], F_GETFL, 0);
    fcntl(capture_pipe[0], F_SETFL, flags | O_NONBLOCK);

    pid_t *pids = malloc(pipeline->num_commands * sizeof(pid_t));
    pid_t pipeline_pgid = 0;
    int input_fd = STDIN_FILENO;
    int pipe_fds[2];

    for (int i = 0; i < pipeline->num_commands; i++) {
        PipeCommand *p_cmd = &pipeline->commands[i];

        if (i < pipeline->num_commands - 1) {
            if (pipe(pipe_fds) == -1) {
                perror("inter-process pipe");
                free(pids);
                return NULL;
            }
        }

        pids[i] = fork();
        if (pids[i] == 0) { // Child Process
            // Put all pipeline processes in the same process group
            if (i == 0) {
                // First process creates the group
                setpgid(0, 0);
            } else {
                // Subsequent processes join the group
                setpgid(0, pipeline_pgid);
            }
            
            // Restore default signal handlers
            signal_handler_setup_child();
            
            if (input_fd != STDIN_FILENO) {
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
            }

            if (i < pipeline->num_commands - 1) {
                close(pipe_fds[0]);
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[1]);
            } else {
                close(capture_pipe[0]);
                dup2(capture_pipe[1], STDOUT_FILENO);
                close(capture_pipe[1]);
            }

            execvp(p_cmd->cmd.args[0], p_cmd->cmd.args);
            perror("execvp in pipe");
            exit(127);
        }

        // Parent: Set up process group
        if (i == 0) {
            pipeline_pgid = pids[0];
            setpgid(pids[0], pipeline_pgid);
        } else {
            setpgid(pids[i], pipeline_pgid);
        }

        if (input_fd != STDIN_FILENO) close(input_fd);
        if (i < pipeline->num_commands - 1) {
            close(pipe_fds[1]);
            input_fd = pipe_fds[0];
        }
    }

    close(capture_pipe[1]);

    // Register as foreground process
    if (pm && pipeline_pgid > 0) {
        process_manager_set_foreground(pm, pids[0], pipeline_pgid, cmd_str);
        signal_handler_give_terminal_to(pipeline_pgid);
    }

    // Read output using non-blocking I/O
    char *output = malloc(8192);
    if (output) output[0] = '\0';
    int output_len = 0;
    char buffer[256];
    int all_exited = 0;

    // Keep reading until all processes exit
    while (!all_exited) {
        all_exited = 1;
        
        // Check status of all pipeline processes
        for (int i = 0; i < pipeline->num_commands; i++) {
            int status;
            pid_t result = waitpid(pids[i], &status, WNOHANG | WUNTRACED);
            
            if (result == 0) {
                // Process still running
                all_exited = 0;
            } else if (result == pids[i]) {
                if (WIFSTOPPED(status)) {
                    // Process stopped
                    all_exited = 0;
                }
                // else: process exited or terminated
            }
        }
        
        // Try to read output (non-blocking)
        ssize_t bytes_read = read(capture_pipe[0], buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            if (output && output_len + bytes_read < 8192) {
                strcat(output, buffer);
                output_len += bytes_read;
            }
        } else if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            // Real error
            break;
        }
        
        if (!all_exited) {
            usleep(1000); // 1ms delay to avoid busy-waiting
        }
    }
    
    // Read any remaining output
    ssize_t bytes_read;
    while ((bytes_read = read(capture_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        if (output && output_len + bytes_read < 8192) {
            strcat(output, buffer);
            output_len += bytes_read;
        }
    }
    
    close(capture_pipe[0]);

    // Clean up
    if (pm) {
        process_manager_clear_foreground(pm);
        signal_handler_take_terminal_back();
    }

    free(pids);
    return output;
}