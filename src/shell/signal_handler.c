// src/shell/signal_handler.c - FIXED VERSION
#include "signal_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

// Global variable to store the shell's process group ID
static pid_t shell_pgid = 0;
static int shell_terminal = -1;
static struct termios shell_tmodes;

/**
 * SIGCHLD handler - DO NOT reap processes here
 * Let the main code handle waitpid() to avoid race conditions
 */
static void sigchld_handler(int sig) {
    // DO NOTHING - just interrupt system calls
    // The main event loop will handle process cleanup
}

int signal_handler_init(void) {
    // CRITICAL FIX: Create a new process group for the GUI shell
    // This prevents terminal Ctrl+C from affecting the GUI
    shell_pgid = getpid();
    if (setpgid(0, shell_pgid) == -1) {
        perror("setpgid");
        return -1;
    }
    
    // Get the controlling terminal
    shell_terminal = STDIN_FILENO;
    
    // Save default terminal modes (may fail in GUI, that's OK)
    tcgetattr(shell_terminal, &shell_tmodes);
    
    // CRITICAL FIX: Don't take terminal control immediately
    // This allows the shell to maintain its prompt when GUI runs in background
    // Terminal control will be taken only when needed for child processes
    fprintf(stderr, "[DEBUG] GUI shell created with PGID=%d, not taking terminal control yet\n", shell_pgid);
    
    // Set up SIGCHLD handler - just interrupt, don't reap
    struct sigaction sa_chld;
    sa_chld.sa_handler = sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART;
    
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
    
    // Ignore SIGQUIT
    signal(SIGQUIT, SIG_IGN);
    
    // Handle SIGTERM gracefully (for proper shutdown)
    signal(SIGTERM, SIG_DFL);
    
    fprintf(stderr, "[DEBUG] Signal handlers initialized for shell PGID=%d\n", shell_pgid);
    
    return 0;
}

void signal_handler_setup_child(void) {
    // Restore default signal handlers in child processes
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
}

int signal_handler_give_terminal_to(pid_t pgid) {
    if (shell_terminal < 0) return 0;
    
    // Check if we're running in the background
    pid_t current_fg_pgid = tcgetpgrp(shell_terminal);
    if (current_fg_pgid != shell_pgid) {
        // We're in the background, can't give terminal control
        fprintf(stderr, "[DEBUG] GUI running in background, cannot give terminal to PGID %d\n", pgid);
        return -1;
    }
    
    // Give the terminal to the process group
    if (tcsetpgrp(shell_terminal, pgid) == -1) {
        if (errno != ENOTTY && errno != EBADF) {
            fprintf(stderr, "[DEBUG] tcsetpgrp give to %d failed: %s\n", 
                    pgid, strerror(errno));
        }
        return -1;
    }
    
    fprintf(stderr, "[DEBUG] Gave terminal to PGID %d\n", pgid);
    return 0;
}

int signal_handler_take_terminal_back(void) {
    if (shell_terminal < 0) return 0;
    
    // Check if we're running in the background
    pid_t current_fg_pgid = tcgetpgrp(shell_terminal);
    if (current_fg_pgid != shell_pgid) {
        // We're in the background, don't try to take terminal control
        // This prevents interfering with the parent shell's prompt
        fprintf(stderr, "[DEBUG] GUI running in background (FG PGID=%d), not taking terminal control\n", current_fg_pgid);
        return 0;
    }
    
    // Take the terminal back only if we're in the foreground
    if (tcsetpgrp(shell_terminal, shell_pgid) == -1) {
        if (errno != ENOTTY && errno != EBADF) {
            fprintf(stderr, "[DEBUG] tcsetpgrp take back to %d failed: %s\n",
                    shell_pgid, strerror(errno));
        }
        return -1;
    }
    
    // Restore shell terminal modes
    tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
    
    fprintf(stderr, "[DEBUG] Took terminal back to shell PGID %d\n", shell_pgid);
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