// src/shell/history_manager.c
#include "history_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Helper: Get home directory
static const char* get_home_directory(void) {
    const char *home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE"); // Windows fallback
    if (!home) home = ".";
    return home;
}

// Helper: Strip newlines and whitespace from command
static void sanitize_command(char *command) {
    if (!command) return;
    
    // Remove leading whitespace
    char *start = command;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n')) {
        start++;
    }
    
    // Remove trailing whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    
    // Move string to beginning if needed
    if (start != command) {
        memmove(command, start, strlen(start) + 1);
    }
}

HistoryManager* history_manager_init(void) {
    HistoryManager *hm = calloc(1, sizeof(HistoryManager));
    if (!hm) {
        perror("calloc HistoryManager");
        return NULL;
    }
    
    hm->start_index = 0;
    hm->count = 0;
    
    // Set history file path
    snprintf(hm->history_file, PATH_MAX, "%s/.myterm_history", 
             get_home_directory());
    
    // Load existing history
    history_manager_load_from_file(hm);
    
    return hm;
}

void history_manager_cleanup(HistoryManager *hm) {
    if (!hm) return;
    
    // Save history before cleanup
    history_manager_save_to_file(hm);
    free(hm);
}

int history_manager_add_command(HistoryManager *hm, const char *command) {
    if (!hm || !command) return -1;
    
    // Create a working copy
    char cmd_copy[MAX_COMMAND_LENGTH];
    strncpy(cmd_copy, command, MAX_COMMAND_LENGTH - 1);
    cmd_copy[MAX_COMMAND_LENGTH - 1] = '\0';
    
    sanitize_command(cmd_copy);
    
    // Don't add empty commands
    if (strlen(cmd_copy) == 0) return 0;
    
    // Don't add duplicate of most recent command
    if (hm->count > 0) {
        int last_idx = (hm->start_index + hm->count - 1) % MAX_HISTORY_SIZE;
        if (strcmp(hm->commands[last_idx], cmd_copy) == 0) {
            return 0;
        }
    }
    
    // Add to ring buffer
    if (hm->count < MAX_HISTORY_SIZE) {
        // Buffer not full yet
        int idx = (hm->start_index + hm->count) % MAX_HISTORY_SIZE;
        strncpy(hm->commands[idx], cmd_copy, MAX_COMMAND_LENGTH - 1);
        hm->commands[idx][MAX_COMMAND_LENGTH - 1] = '\0';
        hm->count++;
    } else {
        // Buffer full, overwrite oldest
        strncpy(hm->commands[hm->start_index], cmd_copy, MAX_COMMAND_LENGTH - 1);
        hm->commands[hm->start_index][MAX_COMMAND_LENGTH - 1] = '\0';
        hm->start_index = (hm->start_index + 1) % MAX_HISTORY_SIZE;
    }
    
    return 0;
}

int history_manager_get_recent(HistoryManager *hm, char *buffer, 
                                size_t buffer_size, int count) {
    if (!hm || !buffer || buffer_size == 0) return 0;
    
    buffer[0] = '\0';
    
    if (hm->count == 0) {
        strncpy(buffer, "No commands in history.\n", buffer_size - 1);
        return 0;
    }
    
    // Limit to available commands and display size
    int num_to_show = (count < hm->count) ? count : hm->count;
    if (num_to_show > HISTORY_DISPLAY_SIZE) {
        num_to_show = HISTORY_DISPLAY_SIZE;
    }
    
    char line[MAX_COMMAND_LENGTH + 32];
    size_t current_len = 0;
    
    // Display in reverse chronological order (newest first)
    for (int i = 0; i < num_to_show; i++) {
        int idx = (hm->start_index + hm->count - 1 - i) % MAX_HISTORY_SIZE;
        
        snprintf(line, sizeof(line), "  [%d] %s\n", 
                 hm->count - i, hm->commands[idx]);
        
        size_t line_len = strlen(line);
        if (current_len + line_len >= buffer_size - 1) {
            break; // Buffer full
        }
        
        strcat(buffer, line);
        current_len += line_len;
    }
    
    return num_to_show;
}

int history_manager_search_exact(HistoryManager *hm, const char *search_term,
                                  char *result, size_t result_size) {
    if (!hm || !search_term || !result || result_size == 0) return 0;
    
    result[0] = '\0';
    
    if (strlen(search_term) == 0 || hm->count == 0) return 0;
    
    // Search from newest to oldest
    for (int i = 0; i < hm->count; i++) {
        int idx = (hm->start_index + hm->count - 1 - i) % MAX_HISTORY_SIZE;
        
        if (strcmp(hm->commands[idx], search_term) == 0) {
            strncpy(result, hm->commands[idx], result_size - 1);
            result[result_size - 1] = '\0';
            return 1;
        }
    }
    
    return 0;
}

// Calculate longest common substring length using dynamic programming
int calculate_lcs_length(const char *str1, const char *str2) {
    if (!str1 || !str2) return 0;
    
    int len1 = strlen(str1);
    int len2 = strlen(str2);
    
    if (len1 == 0 || len2 == 0) return 0;
    
    // Allocate DP table
    int **dp = malloc((len1 + 1) * sizeof(int*));
    if (!dp) return 0;
    
    for (int i = 0; i <= len1; i++) {
        dp[i] = calloc(len2 + 1, sizeof(int));
        if (!dp[i]) {
            // Cleanup on allocation failure
            for (int j = 0; j < i; j++) free(dp[j]);
            free(dp);
            return 0;
        }
    }
    
    int max_length = 0;
    
    // Fill DP table
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            if (str1[i-1] == str2[j-1]) {
                dp[i][j] = dp[i-1][j-1] + 1;
                if (dp[i][j] > max_length) {
                    max_length = dp[i][j];
                }
            } else {
                dp[i][j] = 0;
            }
        }
    }
    
    // Cleanup
    for (int i = 0; i <= len1; i++) {
        free(dp[i]);
    }
    free(dp);
    
    return max_length;
}

int history_manager_search_fuzzy(HistoryManager *hm, const char *search_term,
                                  HistorySearchResult *results, int max_results) {
    if (!hm || !search_term || !results || max_results == 0) return 0;
    
    if (strlen(search_term) == 0 || hm->count == 0) return 0;
    
    int num_results = 0;
    
    // Calculate LCS length for each command
    for (int i = 0; i < hm->count && num_results < max_results; i++) {
        int idx = (hm->start_index + hm->count - 1 - i) % MAX_HISTORY_SIZE;
        
        int lcs_len = calculate_lcs_length(search_term, hm->commands[idx]);
        
        // Only include if LCS length > 2
        if (lcs_len > 2) {
            strncpy(results[num_results].command, hm->commands[idx], 
                    MAX_COMMAND_LENGTH - 1);
            results[num_results].command[MAX_COMMAND_LENGTH - 1] = '\0';
            results[num_results].lcs_length = lcs_len;
            results[num_results].index = hm->count - i;
            num_results++;
        }
    }
    
    // Sort results by LCS length (descending) using bubble sort
    for (int i = 0; i < num_results - 1; i++) {
        for (int j = 0; j < num_results - i - 1; j++) {
            if (results[j].lcs_length < results[j+1].lcs_length) {
                HistorySearchResult temp = results[j];
                results[j] = results[j+1];
                results[j+1] = temp;
            }
        }
    }
    
    return num_results;
}

void format_search_results(HistorySearchResult *results, int num_results,
                           char *buffer, size_t buffer_size) {
    if (!results || !buffer || buffer_size == 0) return;
    
    buffer[0] = '\0';
    
    if (num_results == 0) {
        strncpy(buffer, "No match for search term in history\n", buffer_size - 1);
        return;
    }
    
    char line[MAX_COMMAND_LENGTH + 64];
    size_t current_len = 0;
    
    for (int i = 0; i < num_results; i++) {
        snprintf(line, sizeof(line), "%d. %s (match length: %d)\n",
                 i + 1, results[i].command, results[i].lcs_length);
        
        size_t line_len = strlen(line);
        if (current_len + line_len >= buffer_size - 1) {
            break;
        }
        
        strcat(buffer, line);
        current_len += line_len;
    }
}

int history_manager_load_from_file(HistoryManager *hm) {
    if (!hm) return -1;
    
    FILE *fp = fopen(hm->history_file, "r");
    if (!fp) {
        // File doesn't exist yet, not an error
        return 0;
    }
    
    char line[MAX_COMMAND_LENGTH];
    int loaded = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL && loaded < MAX_HISTORY_SIZE) {
        // Remove newline
        line[strcspn(line, "\n")] = '\0';
        
        if (strlen(line) > 0) {
            strncpy(hm->commands[loaded], line, MAX_COMMAND_LENGTH - 1);
            hm->commands[loaded][MAX_COMMAND_LENGTH - 1] = '\0';
            loaded++;
        }
    }
    
    fclose(fp);
    
    hm->count = loaded;
    hm->start_index = 0;
    
    return 0;
}

int history_manager_save_to_file(HistoryManager *hm) {
    if (!hm) return -1;
    
    // Create temporary file
    char temp_file[PATH_MAX];
    snprintf(temp_file, PATH_MAX, "%s.tmp", hm->history_file);
    
    FILE *fp = fopen(temp_file, "w");
    if (!fp) {
        perror("fopen history file");
        return -1;
    }
    
    // Write all commands in chronological order
    for (int i = 0; i < hm->count; i++) {
        int idx = (hm->start_index + i) % MAX_HISTORY_SIZE;
        fprintf(fp, "%s\n", hm->commands[idx]);
    }
    
    fclose(fp);
    
    // Atomic replace: rename temp file to actual file
    if (rename(temp_file, hm->history_file) != 0) {
        perror("rename history file");
        unlink(temp_file);
        return -1;
    }
    
    // Set permissions to 600 (user read/write only)
    chmod(hm->history_file, S_IRUSR | S_IWUSR);
    
    return 0;
}
