// src/shell/command_exec.h
#ifndef COMMAND_EXEC_H
#define COMMAND_EXEC_H

#include "command_parser.h"
#include "redirect_handler.h"

// Forward declaration
struct ProcessManager;

/**
 * @brief Executes a parsed command (legacy version without signal handling).
 * @param cmd The command to execute.
 * @param redir_info Redirection information.
 * @return The output of the command as a dynamically allocated string.
 * The caller is responsible for freeing this string.
 * Returns NULL on failure or for commands with no output (like cd).
 */
char* execute_command(Command *cmd, RedirectInfo *redir_info);

/**
 * @brief Executes a parsed command with full signal handling support.
 * @param cmd The command to execute.
 * @param redir_info Redirection information.
 * @param pm Process manager for job control.
 * @param cmd_str Original command string for display.
 * @return The output of the command as a dynamically allocated string.
 */
char* execute_command_with_signals(Command *cmd, RedirectInfo *redir_info,
                                   struct ProcessManager *pm, const char *cmd_str);
void set_event_processor_callback(int (*callback)(void));

/**
 * @brief Built-in cd command handler.
 * @param cmd The parsed cd command.
 * @return 0 on success, -1 on failure.
 */
int builtin_cd(Command *cmd);

#endif // COMMAND_EXEC_H