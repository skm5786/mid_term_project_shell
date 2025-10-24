// src/shell/history_manager.c - WITH DEBUG OUTPUT
#include "history_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static const char* get_home_directory(void) {
    const char *home = getenv("HOME");
    if (!home) home = getenv("USERPROFILE");
    if (!home) home = ".";
    return home;
}

static void sanitize_command(char *command) {
    if (!command) return;
    
    char *start = command;
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n')) {
        start++;
    }
    
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\n')) {
        *end = '\0';
        end--;
    }
    
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
    
    snprintf(hm->history_file, PATH_MAX, "%s/.myterm_history", 
             get_home_directory());
    
    printf("[HISTORY_INIT] History file: %s\n", hm->history_file);
    fflush(stdout);
    
    history_manager_load_from_file(hm);
    
    printf("[HISTORY_INIT] Loaded %d commands from file\n", hm->count);
    fflush(stdout);
    
    return hm;
}

void history_manager_cleanup(HistoryManager *hm) {
    if (!hm) return;
    
    printf("[HISTORY_CLEANUP] Saving %d commands\n", hm->count);
    fflush(stdout);
    
    history_manager_save_to_file(hm);
    free(hm);
}

int history_manager_add_command(HistoryManager *hm, const char *command) {
    if (!hm || !command) {
        printf("[HISTORY_ADD] ERROR: NULL parameter\n");
        fflush(stdout);
        return -1;
    }
    
    char cmd_copy[MAX_COMMAND_LENGTH];
    strncpy(cmd_copy, command, MAX_COMMAND_LENGTH - 1);
    cmd_copy[MAX_COMMAND_LENGTH - 1] = '\0';
    
    sanitize_command(cmd_copy);
    
    if (strlen(cmd_copy) == 0) {
        printf("[HISTORY_ADD] Skipping empty command\n");
        fflush(stdout);
        return 0;
    }
    
    // Don't add duplicate of most recent command
    if (hm->count > 0) {
        int last_idx = (hm->start_index + hm->count - 1) % MAX_HISTORY_SIZE;
        if (strcmp(hm->commands[last_idx], cmd_copy) == 0) {
            printf("[HISTORY_ADD] Skipping duplicate: '%s'\n", cmd_copy);
            fflush(stdout);
            return 0;
        }
    }
    
    if (hm->count < MAX_HISTORY_SIZE) {
        int idx = (hm->start_index + hm->count) % MAX_HISTORY_SIZE;
        strncpy(hm->commands[idx], cmd_copy, MAX_COMMAND_LENGTH - 1);
        hm->commands[idx][MAX_COMMAND_LENGTH - 1] = '\0';
        hm->count++;
        printf("[HISTORY_ADD] Added command #%d: '%s' at index %d\n", 
               hm->count, cmd_copy, idx);
    } else {
        strncpy(hm->commands[hm->start_index], cmd_copy, MAX_COMMAND_LENGTH - 1);
        hm->commands[hm->start_index][MAX_COMMAND_LENGTH - 1] = '\0';
        hm->start_index = (hm->start_index + 1) % MAX_HISTORY_SIZE;
        printf("[HISTORY_ADD] Added command (buffer full): '%s'\n", cmd_copy);
    }
    
    fflush(stdout);
    return 0;
}

int history_manager_get_recent(HistoryManager *hm, char *buffer, 
                                size_t buffer_size, int count) {
    if (!hm || !buffer || buffer_size == 0) return 0;
    
    printf("[HISTORY_GET] Getting recent %d commands, have %d total\n", 
           count, hm->count);
    fflush(stdout);
    
    buffer[0] = '\0';
    
    if (hm->count == 0) {
        strncpy(buffer, "No commands in history.\n", buffer_size - 1);
        return 0;
    }
    
    int num_to_show = (count < hm->count) ? count : hm->count;
    if (num_to_show > HISTORY_DISPLAY_SIZE) {
        num_to_show = HISTORY_DISPLAY_SIZE;
    }
    
    char line[MAX_COMMAND_LENGTH + 32];
    size_t current_len = 0;
    
    for (int i = 0; i < num_to_show; i++) {
        int idx = (hm->start_index + hm->count - 1 - i) % MAX_HISTORY_SIZE;
        
        snprintf(line, sizeof(line), "  [%d] %s\n", 
                 hm->count - i, hm->commands[idx]);
        
        size_t line_len = strlen(line);
        if (current_len + line_len >= buffer_size - 1) {
            break;
        }
        
        strcat(buffer, line);
        current_len += line_len;
    }
    
    printf("[HISTORY_GET] Formatted %d commands\n", num_to_show);
    fflush(stdout);
    
    return num_to_show;
}

int history_manager_search_exact(HistoryManager *hm, const char *search_term,
                                  char *result, size_t result_size) {
    if (!hm || !search_term || !result || result_size == 0) return 0;
    
    result[0] = '\0';
    
    if (strlen(search_term) == 0 || hm->count == 0) return 0;
    
    printf("[HISTORY_SEARCH] Exact search for: '%s' in %d commands\n", 
           search_term, hm->count);
    fflush(stdout);
    
    for (int i = 0; i < hm->count; i++) {
        int idx = (hm->start_index + hm->count - 1 - i) % MAX_HISTORY_SIZE;
        
        if (strcmp(hm->commands[idx], search_term) == 0) {
            strncpy(result, hm->commands[idx], result_size - 1);
            result[result_size - 1] = '\0';
            printf("[HISTORY_SEARCH] Found exact match at index %d\n", idx);
            fflush(stdout);
            return 1;
        }
    }
    
    printf("[HISTORY_SEARCH] No exact match found\n");
    fflush(stdout);
    return 0;
}

int calculate_lcs_length(const char *str1, const char *str2) {
    if (!str1 || !str2) return 0;
    
    int len1 = strlen(str1);
    int len2 = strlen(str2);
    
    if (len1 == 0 || len2 == 0) return 0;
    
    int **dp = malloc((len1 + 1) * sizeof(int*));
    if (!dp) return 0;
    
    for (int i = 0; i <= len1; i++) {
        dp[i] = calloc(len2 + 1, sizeof(int));
        if (!dp[i]) {
            for (int j = 0; j < i; j++) free(dp[j]);
            free(dp);
            return 0;
        }
    }
    
    int max_length = 0;
    
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
    
    printf("[HISTORY_SEARCH] Fuzzy search for: '%s' in %d commands\n", 
           search_term, hm->count);
    fflush(stdout);
    
    int num_results = 0;
    
    for (int i = 0; i < hm->count && num_results < max_results; i++) {
        int idx = (hm->start_index + hm->count - 1 - i) % MAX_HISTORY_SIZE;
        
        int lcs_len = calculate_lcs_length(search_term, hm->commands[idx]);
        
        if (lcs_len > 2) {
            strncpy(results[num_results].command, hm->commands[idx], 
                    MAX_COMMAND_LENGTH - 1);
            results[num_results].command[MAX_COMMAND_LENGTH - 1] = '\0';
            results[num_results].lcs_length = lcs_len;
            results[num_results].index = hm->count - i;
            num_results++;
            printf("[HISTORY_SEARCH] Fuzzy match: '%s' (LCS=%d)\n", 
                   hm->commands[idx], lcs_len);
            fflush(stdout);
        }
    }
    
    // Sort by LCS length (descending)
    for (int i = 0; i < num_results - 1; i++) {
        for (int j = 0; j < num_results - i - 1; j++) {
            if (results[j].lcs_length < results[j+1].lcs_length) {
                HistorySearchResult temp = results[j];
                results[j] = results[j+1];
                results[j+1] = temp;
            }
        }
    }
    
    printf("[HISTORY_SEARCH] Found %d fuzzy matches\n", num_results);
    fflush(stdout);
    
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
        printf("[HISTORY_LOAD] File doesn't exist yet: %s\n", hm->history_file);
        fflush(stdout);
        return 0;
    }
    
    printf("[HISTORY_LOAD] Loading from: %s\n", hm->history_file);
    fflush(stdout);
    
    char line[MAX_COMMAND_LENGTH];
    int loaded = 0;
    
    while (fgets(line, sizeof(line), fp) != NULL && loaded < MAX_HISTORY_SIZE) {
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
    
    printf("[HISTORY_LOAD] Loaded %d commands\n", loaded);
    fflush(stdout);
    
    return 0;
}

int history_manager_save_to_file(HistoryManager *hm) {
    if (!hm) return -1;
    
    printf("[HISTORY_SAVE] Saving %d commands to: %s\n", 
           hm->count, hm->history_file);
    fflush(stdout);
    
    char temp_file[PATH_MAX];
    snprintf(temp_file, PATH_MAX, "%s.tmp", hm->history_file);
    
    FILE *fp = fopen(temp_file, "w");
    if (!fp) {
        perror("fopen history file");
        return -1;
    }
    
    for (int i = 0; i < hm->count; i++) {
        int idx = (hm->start_index + i) % MAX_HISTORY_SIZE;
        fprintf(fp, "%s\n", hm->commands[idx]);
    }
    
    fclose(fp);
    
    if (rename(temp_file, hm->history_file) != 0) {
        perror("rename history file");
        unlink(temp_file);
        return -1;
    }
    
    chmod(hm->history_file, S_IRUSR | S_IWUSR);
    
    printf("[HISTORY_SAVE] Successfully saved\n");
    fflush(stdout);
    
    return 0;
}