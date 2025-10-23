// src/shell/signal_handler.h
#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <signal.h>
#include <sys/types.h>

/**
 * @brief Initialize signal handlers for the shell
 * 
 * Sets up the shell to:
 * - Ignore SIGINT and SIGTSTP (so Ctrl+C and Ctrl+Z don't kill the shell)
 * - Handle SIGCHLD to reap zombie processes
 * - Install custom handlers for proper signal forwarding
 * 
 * @return 0 on success, -1 on failure
 */
int signal_handler_init(void);

/**
 * @brief Set up signal handlers for a child process
 * 
 * This should be called in the child process after fork() but before exec()
 * to restore default signal handlers.
 */
void signal_handler_setup_child(void);

/**
 * @brief Enable signal delivery for foreground process group
 * 
 * This gives terminal control to the foreground process group,
 * allowing it to receive keyboard signals.
 * 
 * @param pgid Process group ID to give terminal control to
 * @return 0 on success, -1 on failure
 */
int signal_handler_give_terminal_to(pid_t pgid);

/**
 * @brief Return terminal control to the shell
 * 
 * This should be called after a foreground process completes or is stopped.
 * 
 * @return 0 on success, -1 on failure
 */
int signal_handler_take_terminal_back(void);

/**
 * @brief Get the shell's process group ID
 * 
 * @return Shell's PGID
 */
pid_t signal_handler_get_shell_pgid(void);

/**
 * @brief Check if the shell has terminal control
 * 
 * @return 1 if shell has control, 0 otherwise
 */
int signal_handler_has_terminal_control(void);

#endif // SIGNAL_HANDLER_H