#ifndef COMMAND_EXEC_H
#define COMMAND_EXEC_H

#include "command_parser.h"
#include "redirect_handler.h"

/**
 * @brief Executes a parsed command.
 * @param cmd The command to execute.
 * @return The output of the command as a dynamically allocated string.
 * The caller is responsible for freeing this string.
 * Returns NULL on failure or for commands with no output (like cd).
 */
char* execute_command(Command *cmd, RedirectInfo *redir_info);
int builtin_cd(Command *cmd);

#endif // COMMAND_EXEC_H