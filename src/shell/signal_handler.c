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
    // Get the shell's process group
    shell_pgid = getpgrp();
    
    // Make shell the process group leader if not already
    if (shell_pgid != getpid()) {
        shell_pgid = getpid();
        setpgid(0, shell_pgid);
    }
    
    // Get the controlling terminal
    shell_terminal = STDIN_FILENO;
    
    // Save default terminal modes (may fail in GUI, that's OK)
    tcgetattr(shell_terminal, &shell_tmodes);
    
    // Set up SIGCHLD handler - just interrupt, don't reap
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
    
    // Ignore SIGQUIT
    signal(SIGQUIT, SIG_IGN);
    
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
    
    // Take the terminal back
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