// in src/shell/pipe_handler.c

#include "pipe_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
// A simple check for a pipe character outside of quotes
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

        // For each segment, parse its own redirections
        init_redirect_info(&p_cmd->redirects);
        parse_redirections(p_cmd->raw_command, &p_cmd->redirects);
        
        // Then, parse the cleaned command into arguments
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

        // Parent Process
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