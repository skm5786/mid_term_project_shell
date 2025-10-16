// in src/shell/command_parser.c

#include "command_parser.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// This is a more advanced parser that correctly handles quoted arguments.
//
int parse_command(char *cmd_str, Command *cmd) {
    cmd->argc = 0;
    char *p = cmd_str;
    
    while (*p && cmd->argc < MAX_ARGS - 1) {
        // Skip leading whitespace
        while (*p && isspace(*p)) {
            p++;
        }
        if (*p == '\0') break; // End of string

        char *start = p;
        char quote = 0;

        if (*p == '\'' || *p == '"') {
            quote = *p;
            start++; // Move past the opening quote
            p++;
            // Find the matching closing quote
            while (*p && *p != quote) {
                p++;
            }
        } else {
            // Unquoted argument
            while (*p && !isspace(*p)) {
                p++;
            }
        }

        // The token is from 'start' to 'p'
        int len = p - start;
        if (len >= 0) {
            cmd->args[cmd->argc] = malloc(len + 1);
            strncpy(cmd->args[cmd->argc], start, len);
            cmd->args[cmd->argc][len] = '\0';
            cmd->argc++;
        }

        // If we were in a quote, move past the closing quote
        if (quote && *p == quote) {
            p++;
        }
    }

    cmd->args[cmd->argc] = NULL;
    return 0;
}

void free_command(Command *cmd) {
    for (int i = 0; i < cmd->argc; ++i) {
        free(cmd->args[i]);
    }
}