// in src/shell/command_exec.c

#include "command_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h> // For open()

// Built-in 'cd' command
int builtin_cd(Command *cmd) {
    if (cmd->argc < 2) {
        if (chdir(getenv("HOME")) != 0) perror("cd");
    } else {
        if (chdir(cmd->args[1]) != 0) perror("cd");
    }
    return 0;
}

// The internal function to run external commands
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
        
        // --- UPDATED REDIRECTION LOGIC ---
        for (int i = 0; i < redir_info->count; ++i) {
            Redirect *r = &redir_info->redirects[i];
            if (r->type == REDIRECT_INPUT) {
                int in_fd = open(r->filename, O_RDONLY);
                if (in_fd == -1) { perror("open"); exit(1); }
                if (dup2(in_fd, STDIN_FILENO) == -1) { perror("dup2"); exit(1); }
                close(in_fd);
            } else if (r->type == REDIRECT_OUTPUT) {
                // Open the file for writing, create if it doesn't exist,
                // and truncate it to zero length if it does.
                int out_fd = open(r->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd == -1) { perror("open"); exit(1); }
                // Redirect standard output to the file
                if (dup2(out_fd, STDOUT_FILENO) == -1) { perror("dup2"); exit(1); }
                close(out_fd);
            }
        }

        execvp(cmd->args[0], cmd->args);
        perror("execvp");
        exit(127);
    } else { // --- Parent Process ---
        close(output_pipe[1]);
        char *output = malloc(8192); // 8KB buffer
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

// This is the main function that gets called
char* execute_command(Command *cmd, RedirectInfo *redir_info) {
    if (cmd->argc == 0) return NULL;
    
    // Check for built-ins first
    if (strcmp(cmd->args[0], "cd") == 0) {
        builtin_cd(cmd);
        return strdup(""); // Return empty string to signify success with no output
    }
    
    // If not a built-in, run it as an external command
    return execute_external_command(cmd, redir_info);
}