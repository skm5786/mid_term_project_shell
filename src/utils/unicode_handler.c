// in src/utils/unicode_handler.c

#include "unicode_handler.h"
#include <locale.h>
#include <stdio.h>
#include <string.h>

int unicode_init(void) {
    if (setlocale(LC_ALL, "") == NULL) {
        fprintf(stderr, "Warning: could not set locale from environment.\n");
        return -1;
    }
    return 0;
}

int is_multiline_continuation(const char *line) {
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\\') {
        if (len > 1 && line[len - 2] == '\\') return 0; // Escaped backslash
        return 1;
    }
    return 0;
}

// THIS IS THE CORRECTED, ROBUST VERSION
void process_escape_sequences(const char *input, char *output, size_t max_len) {
    size_t out_idx = 0;
    for (size_t in_idx = 0; input[in_idx] != '\0' && out_idx < max_len - 1; ) {
        if (input[in_idx] == '\\' && input[in_idx + 1] != '\0') {
            in_idx++; // Move to the character to be escaped
            switch (input[in_idx]) {
                case 'n': output[out_idx++] = '\n'; break;
                case 't': output[out_idx++] = '\t'; break;
                case '\\': output[out_idx++] = '\\'; break;
                default:
                    // Not a recognized escape, output both characters literally
                    output[out_idx++] = '\\';
                    if (out_idx < max_len - 1) {
                        output[out_idx++] = input[in_idx];
                    }
                    break;
            }
            in_idx++; // Move past the character we just processed
        } else {
            // Not a backslash, just copy the character
            output[out_idx++] = input[in_idx++];
        }
    }
    output[out_idx] = '\0';
}

int get_last_utf8_char_len(const char *str, int length) {
    if (length <= 0) return 0;
    int pos = length - 1;
    // Move backwards past any continuation bytes (10xxxxxx)
    while (pos > 0 && (str[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return length - pos;
}