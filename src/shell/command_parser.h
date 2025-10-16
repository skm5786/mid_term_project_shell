#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#define MAX_ARGS 128

// This structure holds the tokenized command.
//
typedef struct {
    char *args[MAX_ARGS];    // NULL-terminated array of strings
    int argc;                // Number of arguments
} Command;

/**
 * @brief Parses a command string into a Command structure.
 * @param cmd_str The raw command string from the user.
 * @param cmd A pointer to a Command structure to be filled.
 * @return 0 on success, -1 on failure (e.g., too many arguments).
 */
int parse_command(char *cmd_str, Command *cmd);

/**
 * @brief Frees the memory allocated for a command's arguments.
 * @param cmd A pointer to the Command structure to clean up.
 */
void free_command(Command *cmd);

#endif // COMMAND_PARSER_H