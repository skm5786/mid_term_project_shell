// src/shell/history_manager.h
#ifndef HISTORY_MANAGER_H
#define HISTORY_MANAGER_H

#include <stddef.h>
#include <limits.h>

#define MAX_HISTORY_SIZE 10000
#define HISTORY_DISPLAY_SIZE 1000
#define MAX_COMMAND_LENGTH 512
#define MAX_SEARCH_RESULTS 10

// Structure to hold a single history search result
typedef struct {
    char command[MAX_COMMAND_LENGTH];
    int lcs_length;  // Length of longest common substring (for fuzzy matching)
    int index;       // Position in history
} HistorySearchResult;

// Main history manager structure
typedef struct {
    char commands[MAX_HISTORY_SIZE][MAX_COMMAND_LENGTH];
    int start_index;  // Ring buffer start position
    int count;        // Current number of commands (up to MAX_HISTORY_SIZE)
    char history_file[PATH_MAX];
} HistoryManager;

/**
 * @brief Initialize the history manager
 * @return Pointer to new HistoryManager, or NULL on failure
 */
HistoryManager* history_manager_init(void);

/**
 * @brief Clean up history manager and save to disk
 * @param hm History manager to clean up
 */
void history_manager_cleanup(HistoryManager *hm);

/**
 * @brief Add a command to the history
 * @param hm History manager
 * @param command Command string to add
 * @return 0 on success, -1 on failure
 */
int history_manager_add_command(HistoryManager *hm, const char *command);

/**
 * @brief Get the most recent N commands
 * @param hm History manager
 * @param buffer Output buffer for formatted history
 * @param buffer_size Size of output buffer
 * @param count Number of commands to retrieve (max HISTORY_DISPLAY_SIZE)
 * @return Number of commands retrieved
 */
int history_manager_get_recent(HistoryManager *hm, char *buffer, 
                                size_t buffer_size, int count);

/**
 * @brief Search for exact match in history
 * @param hm History manager
 * @param search_term Search string
 * @param result Output buffer for result
 * @param result_size Size of result buffer
 * @return 1 if exact match found, 0 otherwise
 */
int history_manager_search_exact(HistoryManager *hm, const char *search_term,
                                  char *result, size_t result_size);

/**
 * @brief Search for fuzzy matches using LCS algorithm
 * @param hm History manager
 * @param search_term Search string
 * @param results Array to store results
 * @param max_results Maximum number of results to return
 * @return Number of results found
 */
int history_manager_search_fuzzy(HistoryManager *hm, const char *search_term,
                                  HistorySearchResult *results, int max_results);

/**
 * @brief Load history from file
 * @param hm History manager
 * @return 0 on success, -1 on failure
 */
int history_manager_load_from_file(HistoryManager *hm);

/**
 * @brief Save history to file
 * @param hm History manager
 * @return 0 on success, -1 on failure
 */
int history_manager_save_to_file(HistoryManager *hm);

/**
 * @brief Calculate longest common substring length between two strings
 * @param str1 First string
 * @param str2 Second string
 * @return Length of longest common substring
 */
int calculate_lcs_length(const char *str1, const char *str2);

/**
 * @brief Format search results for display
 * @param results Array of search results
 * @param num_results Number of results
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 */
void format_search_results(HistorySearchResult *results, int num_results,
                           char *buffer, size_t buffer_size);

#endif // HISTORY_MANAGER_H