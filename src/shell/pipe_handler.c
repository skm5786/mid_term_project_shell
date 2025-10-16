// in src/shell/pipe_handler.c

#include "pipe_handler.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// A simple check for a pipe character outside of quotes
int has_pipe(const char *cmd_str) {
    int in_quote = 0;
    for (int i = 0; cmd_str[i] != '\0'; i++) {
        if (cmd_str[i] == '"' || cmd_str[i] == '\'') {
            in_quote = !in_quote;
        } else if (cmd_str[i] == '|' && !in_quote) {
            return 1;
        }
    }
    return 0;
}

Pipeline* parse_pipeline(char *cmd_str) {
    Pipeline *pipeline = malloc(sizeof(Pipeline));
    pipeline->num_commands = 0;

    char *saveptr;
    char *segment = strtok_r(cmd_str, "|", &saveptr);

    while (segment != NULL && pipeline->num_commands < MAX_PIPE_COMMANDS) {
        PipeCommand *p_cmd = &pipeline->commands[pipeline->num_commands];
        p_cmd->raw_command = strdup(segment);

        // For each segment, parse its own redirections
        init_redirect_info(&p_cmd->redirects);
        parse_redirections(p_cmd->raw_command, &p_cmd->redirects);
        
        // Then, parse the cleaned command into arguments
        parse_command(p_cmd->redirects.clean_command, &p_cmd->cmd);

        pipeline->num_commands++;
        segment = strtok_r(NULL, "|", &saveptr);
    }
    return pipeline;
}

void free_pipeline(Pipeline *pipeline) {
    if (!pipeline) return;
    for (int i = 0; i < pipeline->num_commands; i++) {
        free(pipeline->commands[i].raw_command);
        free_command(&pipeline->commands[i].cmd);
        cleanup_redirect_info(&pipeline->commands[i].redirects);
    }
    free(pipeline);
}