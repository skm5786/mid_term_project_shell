// src/input/autocomplete.h
#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

#include <stddef.h>

#define MAX_MATCHES 256
#define MAX_FILENAME_LENGTH 256

/**
 * Structure to hold autocomplete results
 */
typedef struct {
    char matches[MAX_MATCHES][MAX_FILENAME_LENGTH];
    int num_matches;
    char longest_common_prefix[MAX_FILENAME_LENGTH];
    int prefix_length;
} AutocompleteResult;

/**
 * @brief Find files matching a prefix in the current directory
 * 
 * This function searches the current working directory for files
 * that start with the given prefix. It populates the result structure
 * with all matching filenames and calculates the longest common prefix.
 * 
 * @param prefix The prefix to match (e.g., "ab" matches "abc.txt", "abcd.txt")
 * @param result Pointer to AutocompleteResult structure to fill
 * @return 0 on success, -1 on failure
 */
int autocomplete_find_matches(const char *prefix, AutocompleteResult *result);

/**
 * @brief Calculate the longest common prefix among multiple strings
 * 
 * Given an array of strings, finds the longest prefix that all strings share.
 * For example: ["abc.txt", "abcd.txt"] -> "abc"
 * 
 * @param strings Array of strings to compare
 * @param count Number of strings in the array
 * @param output Buffer to store the result
 * @param max_len Maximum length of output buffer
 * @return Length of the common prefix
 */
int autocomplete_longest_common_prefix(char **strings, int count, 
                                       char *output, size_t max_len);

/**
 * @brief Extract the last token (filename) from a command line
 * 
 * This function extracts the last "word" from a command line, which is
 * the filename we want to autocomplete. It handles spaces and quotes.
 * 
 * For example:
 * - "./myprog de" -> "de"
 * - "ls -la ab" -> "ab"
 * - "cat 'file with spaces' ne" -> "ne"
 * 
 * @param command_line The full command line
 * @param token_start Output: pointer to start of last token in command_line
 * @param token_end Output: pointer to end of last token in command_line
 * @return 0 on success, -1 if no token found
 */
int autocomplete_extract_last_token(const char *command_line,
                                     const char **token_start,
                                     const char **token_end);

/**
 * @brief Format autocomplete matches for display
 * 
 * Creates a formatted string showing all matches with numbers for selection.
 * Example output: "1. abc.txt  2. abcd.txt  3. abcde.txt"
 * 
 * @param result The autocomplete result to format
 * @param output Buffer to store formatted output
 * @param max_len Maximum length of output buffer
 * @return Number of characters written
 */
int autocomplete_format_matches(const AutocompleteResult *result,
                                char *output, size_t max_len);

/**
 * @brief Replace the last token in command line with a new value
 * 
 * This function replaces the last token (filename) in the command line
 * with a new value (the autocompleted filename).
 * 
 * For example:
 * - "./myprog de" + "def.txt" -> "./myprog def.txt"
 * 
 * @param command_line Original command line
 * @param new_token New token to insert
 * @param output Buffer to store result
 * @param max_len Maximum length of output buffer
 * @return 0 on success, -1 on failure
 */
int autocomplete_replace_last_token(const char *command_line,
                                    const char *new_token,
                                    char *output, size_t max_len);

#endif // AUTOCOMPLETE_H