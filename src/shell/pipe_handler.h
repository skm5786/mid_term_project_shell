// in src/shell/pipe_handler.h

#ifndef PIPE_HANDLER_H
#define PIPE_HANDLER_H

#include "command_parser.h"
#include "redirect_handler.h"
#include <sys/types.h>

#define MAX_PIPE_COMMANDS 16

// Forward declaration
struct ProcessManager;

// A single command within a pipeline, with its own redirections
typedef struct {
    char* raw_command;
    Command cmd;
    RedirectInfo redirects;
} PipeCommand;

// The complete pipeline of commands
typedef struct {
    PipeCommand commands[MAX_PIPE_COMMANDS];
    int num_commands;
} Pipeline;

/**
 * @brief Checks if a command string contains a pipe operator.
 */
int has_pipe(const char *cmd_str);

/**
 * @brief Parses a raw command string into a Pipeline structure.
 */
Pipeline* parse_pipeline(char *cmd_str);

/**
 * @brief Frees all memory associated with a Pipeline structure.
 */
void free_pipeline(Pipeline *pipeline);

/**
 * @brief Execute pipeline (legacy version without signal handling).
 */
char* execute_pipeline(Pipeline *pipeline);

/**
 * @brief Execute pipeline with full signal handling support.
 * @param pipeline The pipeline to execute.
 * @param pm Process manager for job control.
 * @param cmd_str Original command string for display.
 */
char* execute_pipeline_with_signals(Pipeline *pipeline, struct ProcessManager *pm, 
                                    const char *cmd_str);

#endif // PIPE_HANDLER_H