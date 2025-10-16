#ifndef UNICODE_HANDLER_H
#define UNICODE_HANDLER_H

#include <stddef.h>

#define MAX_MULTILINE_INPUT 4096

/**
 * @brief Initializes locale settings for Unicode support.
 * @return 0 on success, -1 on failure.
 *
 */
int unicode_init(void);

/**
 * @brief Checks if a line ends with an unescaped backslash, indicating multiline input.
 * @param line The input line to check.
 * @return 1 if it's a continuation, 0 otherwise.
 *
 */
int is_multiline_continuation(const char *line);

/**
 * @brief Processes escape sequences like \n, \t in a string.
 * @param input The raw input string.
 * @param output The buffer to store the processed string.
 * @param max_len The size of the output buffer.
 *
 */
void process_escape_sequences(const char *input, char *output, size_t max_len);

/**
 * @brief Finds the starting byte of the last UTF-8 character in a string.
 * @param str The string to search.
 * @param length The current length of the string in bytes.
 * @return The number of bytes in the last UTF-8 character (1-4).
 */
int get_last_utf8_char_len(const char *str, int length);

#endif // UNICODE_HANDLER_H