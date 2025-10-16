// in src/shell/pipe_handler.h

#ifndef PIPE_HANDLER_H
#define PIPE_HANDLER_H

#include "command_parser.h"
#include "redirect_handler.h"
#include <sys/types.h>

#define MAX_PIPE_COMMANDS 16

// A single command within a pipeline, with its own redirections
typedef struct {
    char* raw_command; // The unparsed command string for this segment
    Command cmd;       // The parsed command (args, argc)
    RedirectInfo redirects; // Redirections for this specific command
} PipeCommand;

// The complete pipeline of commands
typedef struct {
    PipeCommand commands[MAX_PIPE_COMMANDS];
    int num_commands;
} Pipeline;

/**
 * @brief Checks if a command string contains a pipe operator.
 * @param cmd_str The raw command string.
 * @return 1 if a pipe is found, 0 otherwise.
 */
int has_pipe(const char *cmd_str);

/**
 * @brief Parses a raw command string into a Pipeline structure.
 * @param cmd_str The raw command string (e.g., "ls -l | wc -l").
 * @return A pointer to a new Pipeline structure, or NULL on failure.
 */
Pipeline* parse_pipeline(char *cmd_str);

/**
 * @brief Frees all memory associated with a Pipeline structure.
 * @param pipeline The pipeline to free.
 */
void free_pipeline(Pipeline *pipeline);

#endif // PIPE_HANDLER_H