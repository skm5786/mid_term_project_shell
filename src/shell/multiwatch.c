// in src/shell/multiwatch.c
#include "multiwatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <time.h>

// Checks if a command starts with "multiWatch"
int is_multiwatch_command(const char *cmd_str) {
    while (*cmd_str && isspace((unsigned char)*cmd_str)) cmd_str++;
    return (strncmp(cmd_str, "multiWatch", 10) == 0);
}

// Parses the multiWatch command string
static MultiWatch* parse_multiwatch_command(const char *cmd_str) {
    const char *p = strchr(cmd_str, '[');
    if (!p) return NULL;
    p++;

    MultiWatch *mw = calloc(1, sizeof(MultiWatch));
    if (!mw) return NULL;

    while (*p && *p != ']' && mw->num_commands < MAX_WATCH_COMMANDS) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (*p == ']' || *p == '\0') break;

        char quote = *p;
        if (quote != '"' && quote != '\'') { free(mw); return NULL; }
        p++;

        const char *start = p;
        while (*p && (*p != quote)) p++;
        if (*p != quote) { free(mw); return NULL; }

        int len = p - start;
        if (len > 0 && len < MAX_WATCH_CMD_LENGTH) {
            strncpy(mw->commands[mw->num_commands].command, start, len);
            mw->commands[mw->num_commands].command[len] = '\0';
            mw->num_commands++;
        }
        p++;
    }

    if (mw->num_commands == 0) { free(mw); return NULL; }
    return mw;
}

// The loop for the "watcher" child process.
static void watch_process_loop(const char *command, const char *temp_file) {
    signal(SIGINT, SIG_IGN); // Watcher processes ignore Ctrl+C
    while (1) {
        pid_t grandchild_pid = fork();
        if (grandchild_pid == 0) { // Grandchild executes the command
            int fd = open(temp_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd != -1) {
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            execlp("/bin/sh", "sh", "-c", command, NULL);
            exit(127);
        } else if (grandchild_pid > 0) {
            waitpid(grandchild_pid, NULL, 0);
        }
        sleep(1);
    }
}

// Starts the session: forks watchers and sets up files
MultiWatch* multiwatch_start_session(const char *cmd_str) {
    MultiWatch *mw = parse_multiwatch_command(cmd_str);
    if (!mw) return NULL;

    for (int i = 0; i < mw->num_commands; i++) {
        WatchCommand *wc = &mw->commands[i];
        snprintf(wc->temp_file, sizeof(wc->temp_file), ".temp.%d_%d.txt", getpid(), i);

        wc->pid = fork();
        if (wc->pid == 0) { // Child watcher process
            watch_process_loop(wc->command, wc->temp_file);
            exit(0); // Should never be reached
        }
    }

    usleep(1000000); // Give children time to create the temp files

    for (int i = 0; i < mw->num_commands; i++) {
        mw->commands[i].fd = open(mw->commands[i].temp_file, O_RDONLY | O_NONBLOCK);
        if (mw->commands[i].fd != -1) {
            mw->poll_fds[i].fd = mw->commands[i].fd;
            mw->poll_fds[i].events = POLLIN;
        } else {
            mw->poll_fds[i].fd = -1; // Mark as invalid
        }
    }
    return mw;
}

// Polls for new output without blocking
void multiwatch_poll_output(MultiWatch *mw, void (*output_callback)(const char *)) {
    if (!mw) return;

    // Poll with a 100ms timeout to avoid busy-waiting
    int ready = poll(mw->poll_fds, mw->num_commands, 100);
    if (ready <= 0) return;

    for (int i = 0; i < mw->num_commands; i++) {
        if (mw->poll_fds[i].revents & POLLIN) {
            char buffer[8192];
            lseek(mw->commands[i].fd, 0, SEEK_SET); // Read from start
            ssize_t bytes = read(mw->commands[i].fd, buffer, sizeof(buffer) - 1);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                char header[1024];
                char timestamp[64];
                time_t now = time(NULL);
                snprintf(timestamp, sizeof(timestamp), "%ld", (long)now);

                snprintf(header, sizeof(header),
                         "\"%s\", current_time: %s\n----------------------------------------------------\n%s----------------------------------------------------\n",
                         mw->commands[i].command, timestamp, buffer);
                output_callback(header);
            }
        }
    }
}

// Cleans up all resources
void cleanup_multiwatch(MultiWatch *mw) {
    if (!mw) return;
    for (int i = 0; i < mw->num_commands; i++) {
        if (mw->commands[i].pid > 0) kill(mw->commands[i].pid, SIGTERM);
        if (mw->commands[i].fd != -1) close(mw->commands[i].fd);
        unlink(mw->commands[i].temp_file);
    }
    for (int i = 0; i < mw->num_commands; i++) {
        if (mw->commands[i].pid > 0) waitpid(mw->commands[i].pid, NULL, 0);
    }
    free(mw);
}