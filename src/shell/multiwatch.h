// in src/shell/multiwatch.h
#ifndef MULTIWATCH_H
#define MULTIWATCH_H

#include <sys/types.h>
#include <time.h>
#include <poll.h> // Include for poll()

#define MAX_WATCH_COMMANDS 16
#define MAX_WATCH_CMD_LENGTH 512

// Structure for a single watched command
typedef struct {
    char command[MAX_WATCH_CMD_LENGTH];
    pid_t pid;
    char temp_file[256];
    int fd;
} WatchCommand;

// Structure to manage the multiWatch session
typedef struct {
    WatchCommand commands[MAX_WATCH_COMMANDS];
    int num_commands;
    struct pollfd poll_fds[MAX_WATCH_COMMANDS];
} MultiWatch;

// Checks if a command string is a multiWatch command
int is_multiwatch_command(const char *cmd_str);

// Starts the multiWatch session (forks processes, creates files)
MultiWatch* multiwatch_start_session(const char *cmd_str);

// Polls for new output from running commands (non-blocking)
void multiwatch_poll_output(MultiWatch *mw, void (*output_callback)(const char *));

// Cleans up all multiWatch resources (processes, files)
void cleanup_multiwatch(MultiWatch *mw);

#endif // MULTIWATCH_H