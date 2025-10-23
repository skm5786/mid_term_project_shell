// src/shell/signal_handler.c
#include "signal_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

// Global variable to store the shell's process group ID
static pid_t shell_pgid = 0;
static int shell_terminal = -1;
static struct termios shell_tmodes;

/**
 * SIGCHLD handler - reaps zombie processes
 * This runs asynchronously when child processes change state
 */
static void sigchld_handler(int sig) {
    // Save errno to avoid interfering with library functions
    int saved_errno = errno;
    
    // Reap all terminated children (non-blocking)
    // The main loop will handle status reporting via process_manager
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // Child reaped
    }
    
    errno = saved_errno;
}

int signal_handler_init(void) {
    // Get the shell's process group
    shell_pgid = getpgrp();
    
    // Get the controlling terminal
    shell_terminal = STDIN_FILENO;
    
    // Make sure we're in the foreground
    if (tcgetpgrp(shell_terminal) != shell_pgid) {
        // We're not in foreground, try to put ourselves there
        // This might fail if we don't have a controlling terminal
        if (tcsetpgrp(shell_terminal, shell_pgid) == -1) {
            // Not a fatal error for GUI terminal
            // perror("tcsetpgrp shell init");
        }
    }
    
    // Save default terminal modes
    if (tcgetattr(shell_terminal, &shell_tmodes) == -1) {
        // perror("tcgetattr");
    }
    
    // Set up SIGCHLD handler to reap zombies
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        perror("sigaction SIGCHLD");
        return -1;
    }
    
    // Ignore SIGINT (Ctrl+C) in the shell
    signal(SIGINT, SIG_IGN);
    
    // Ignore SIGTSTP (Ctrl+Z) in the shell
    signal(SIGTSTP, SIG_IGN);
    
    // Ignore SIGTTOU (background write to terminal)
    signal(SIGTTOU, SIG_IGN);
    
    // Ignore SIGTTIN (background read from terminal)
    signal(SIGTTIN, SIG_IGN);
    
    return 0;
}

void signal_handler_setup_child(void) {
    // Restore default signal handlers in child processes
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

int signal_handler_give_terminal_to(pid_t pgid) {
    if (shell_terminal < 0) return 0; // No terminal control in GUI mode
    
    // Give the terminal to the process group
    if (tcsetpgrp(shell_terminal, pgid) == -1) {
        if (errno != ENOTTY) { // Ignore if not a tty (GUI mode)
            perror("tcsetpgrp give");
        }
        return -1;
    }
    
    return 0;
}

int signal_handler_take_terminal_back(void) {
    if (shell_terminal < 0) return 0; // No terminal control in GUI mode
    
    // Take the terminal back
    if (tcsetpgrp(shell_terminal, shell_pgid) == -1) {
        if (errno != ENOTTY) { // Ignore if not a tty (GUI mode)
            perror("tcsetpgrp take back");
        }
        return -1;
    }
    
    // Restore shell terminal modes
    tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
    
    return 0;
}

pid_t signal_handler_get_shell_pgid(void) {
    return shell_pgid;
}

int signal_handler_has_terminal_control(void) {
    if (shell_terminal < 0) return 0;
    
    pid_t fg_pgid = tcgetpgrp(shell_terminal);
    return (fg_pgid == shell_pgid);
}