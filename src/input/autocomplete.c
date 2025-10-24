// src/input/autocomplete.c
#include "autocomplete.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * Check if a file matches the given prefix
 */
static int matches_prefix(const char *filename, const char *prefix) {
    if (!filename || !prefix) return 0;
    
    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) return 0; // Empty prefix doesn't match
    
    return strncmp(filename, prefix, prefix_len) == 0;
}

/**
 * Find all files in current directory matching the prefix
 */
int autocomplete_find_matches(const char *prefix, AutocompleteResult *result) {
    if (!prefix || !result) return -1;
    
    // Initialize result
    result->num_matches = 0;
    result->prefix_length = 0;
    result->longest_common_prefix[0] = '\0';
    
    // If prefix is empty, don't match anything
    if (strlen(prefix) == 0) {
        return 0;
    }
    
    // Open current directory
    DIR *dir = opendir(".");
    if (!dir) {
        perror("opendir");
        return -1;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && result->num_matches < MAX_MATCHES) {
        // Skip hidden files (starting with .)
        if (entry->d_name[0] == '.') continue;
        
        // Check if this file matches the prefix
        if (matches_prefix(entry->d_name, prefix)) {
            strncpy(result->matches[result->num_matches], entry->d_name, 
                    MAX_FILENAME_LENGTH - 1);
            result->matches[result->num_matches][MAX_FILENAME_LENGTH - 1] = '\0';
            result->num_matches++;
        }
    }
    
    closedir(dir);
    
    // Calculate longest common prefix if we have matches
    if (result->num_matches > 0) {
        char *match_ptrs[MAX_MATCHES];
        for (int i = 0; i < result->num_matches; i++) {
            match_ptrs[i] = result->matches[i];
        }
        
        result->prefix_length = autocomplete_longest_common_prefix(
            match_ptrs, result->num_matches,
            result->longest_common_prefix, MAX_FILENAME_LENGTH);
    }
    
    return 0;
}

/**
 * Calculate longest common prefix among strings
 */
int autocomplete_longest_common_prefix(char **strings, int count,
                                       char *output, size_t max_len) {
    if (!strings || count <= 0 || !output || max_len == 0) return 0;
    
    output[0] = '\0';
    
    if (count == 1) {
        // Only one match - the entire string is the common prefix
        strncpy(output, strings[0], max_len - 1);
        output[max_len - 1] = '\0';
        return strlen(output);
    }
    
    // Find the shortest string length
    size_t min_len = strlen(strings[0]);
    for (int i = 1; i < count; i++) {
        size_t len = strlen(strings[i]);
        if (len < min_len) min_len = len;
    }
    
    // Find common prefix
    size_t prefix_len = 0;
    for (size_t i = 0; i < min_len && prefix_len < max_len - 1; i++) {
        char c = strings[0][i];
        
        // Check if all strings have this character at position i
        int all_match = 1;
        for (int j = 1; j < count; j++) {
            if (strings[j][i] != c) {
                all_match = 0;
                break;
            }
        }
        
        if (all_match) {
            output[prefix_len++] = c;
        } else {
            break;
        }
    }
    
    output[prefix_len] = '\0';
    return prefix_len;
}

/**
 * Extract the last token from command line
 */
int autocomplete_extract_last_token(const char *command_line,
                                     const char **token_start,
                                     const char **token_end) {
    if (!command_line || !token_start || !token_end) return -1;
    
    *token_start = NULL;
    *token_end = NULL;
    
    int len = strlen(command_line);
    if (len == 0) return -1;
    
    // Start from the end and move backwards
    const char *p = command_line + len - 1;
    
    // Skip trailing whitespace
    while (p >= command_line && isspace(*p)) {
        p--;
    }
    
    if (p < command_line) return -1; // Only whitespace
    
    *token_end = p + 1;
    
    // Move backwards to find start of token
    // A token ends at whitespace (unless quoted)
    int in_quote = 0;
    char quote_char = 0;
    
    while (p >= command_line) {
        if ((*p == '\'' || *p == '"') && !in_quote) {
            // Found opening quote
            in_quote = 1;
            quote_char = *p;
            p--;
            continue;
        }
        
        if (in_quote && *p == quote_char) {
            // Found closing quote (going backwards, so this is opening)
            in_quote = 0;
            p--;
            continue;
        }
        
        if (!in_quote && isspace(*p)) {
            // Found start of token
            break;
        }
        
        p--;
    }
    
    *token_start = p + 1;
    
    // If token is quoted, skip the quotes
    if (*token_start < *token_end && 
        (**token_start == '\'' || **token_start == '"')) {
        (*token_start)++;
        if (*token_end > *token_start && 
            (*((*token_end) - 1) == '\'' || *((*token_end) - 1) == '"')) {
            (*token_end)--;
        }
    }
    
    return 0;
}

/**
 * Format matches for display
 */
int autocomplete_format_matches(const AutocompleteResult *result,
                                char *output, size_t max_len) {
    if (!result || !output || max_len == 0) return 0;
    
    output[0] = '\0';
    
    if (result->num_matches == 0) {
        return 0;
    }
    
    if (result->num_matches == 1) {
        // Single match - just return it
        strncpy(output, result->matches[0], max_len - 1);
        output[max_len - 1] = '\0';
        return strlen(output);
    }
    
    // Multiple matches - format as numbered list
    size_t offset = 0;
    for (int i = 0; i < result->num_matches && offset < max_len - 50; i++) {
        int written = snprintf(output + offset, max_len - offset,
                              "%d. %s  ", i + 1, result->matches[i]);
        if (written < 0) break;
        offset += written;
    }
    
    return offset;
}

/**
 * Replace last token in command line
 */
int autocomplete_replace_last_token(const char *command_line,
                                    const char *new_token,
                                    char *output, size_t max_len) {
    if (!command_line || !new_token || !output || max_len == 0) return -1;
    
    const char *token_start, *token_end;
    if (autocomplete_extract_last_token(command_line, &token_start, &token_end) != 0) {
        // No token found - just append the new token
        strncpy(output, command_line, max_len - 1);
        output[max_len - 1] = '\0';
        return 0;
    }
    
    // Calculate prefix length (everything before the token)
    size_t prefix_len = token_start - command_line;
    
    // Copy prefix
    if (prefix_len >= max_len) {
        return -1; // Not enough space
    }
    
    strncpy(output, command_line, prefix_len);
    output[prefix_len] = '\0';
    
    // Append new token
    strncat(output, new_token, max_len - strlen(output) - 1);
    
    return 0;
}