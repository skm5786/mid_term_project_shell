// in src/shell/redirect_handler.c

#include "redirect_handler.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h> 

void init_redirect_info(RedirectInfo *info) {
    info->count = 0;
    info->clean_command = NULL;
}

void cleanup_redirect_info(RedirectInfo *info) {
    for (int i = 0; i < info->count; ++i) {
        free(info->redirects[i].filename);
    }
    free(info->clean_command);
}

void parse_redirections(char *cmd_str, RedirectInfo *info) {
    info->clean_command = strdup(cmd_str);
    char *p = info->clean_command;
    char *token_start;

    // A simple loop to find redirection symbols
    for (token_start = p; *token_start != '\0'; token_start++) {
        if (*token_start == '<' || *token_start == '>') {
            if (info->count >= MAX_REDIRECTS) break;
            
            RedirectType type = (*token_start == '<') ? REDIRECT_INPUT : REDIRECT_OUTPUT;

            char *filename_start = token_start + 1;
            while (*filename_start && isspace(*filename_start)) filename_start++;
            
            char *filename_end = filename_start;
            while (*filename_end && !isspace(*filename_end)) filename_end++;
            
            if (filename_start != filename_end) {
                int len = filename_end - filename_start;
                info->redirects[info->count].type = type;
                info->redirects[info->count].filename = malloc(len + 1);
                strncpy(info->redirects[info->count].filename, filename_start, len);
                info->redirects[info->count].filename[len] = '\0';
                info->count++;
                
                // Overwrite the redirection part with spaces to clean the command
                memset(token_start, ' ', filename_end - token_start);
            }
        }
    }
}