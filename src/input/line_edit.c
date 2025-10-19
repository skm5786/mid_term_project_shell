// in src/input/line_edit.c
#include "line_edit.h"
#include <stdlib.h>
#include <string.h>

// Helper to check if a byte is a UTF-8 continuation byte (starts with 10xx xxxx)
static int is_utf8_continuation(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

// Helper to get the length of a UTF-8 character starting at the given position
static int utf8_char_len(const char *str) {
    unsigned char c = *str;
    if (c < 0x80) return 1; // 0xxxxxxx
    if ((c & 0xE0) == 0xC0) return 2; // 110xxxxx
    if ((c & 0xF0) == 0xE0) return 3; // 1110xxxx
    if ((c & 0xF8) == 0xF0) return 4; // 11110xxx
    return 1; // Malformed, treat as a single byte
}

LineEdit* line_edit_init(void) {
    LineEdit *le = calloc(1, sizeof(LineEdit));
    return le;
}

void line_edit_free(LineEdit *le) {
    free(le);
}

int line_edit_insert_string(LineEdit *le, const char *str) {
    int str_len = strlen(str);
    if (!le || !str || le->length + str_len >= MAX_INPUT_LENGTH) {
        return -1;
    }

    // If cursor is not at the end, make space for the new string
    if (le->cursor_pos < le->length) {
        memmove(&le->buffer[le->cursor_pos + str_len],
                &le->buffer[le->cursor_pos],
                le->length - le->cursor_pos);
    }

    // Copy the new string into the buffer at the cursor position
    memcpy(&le->buffer[le->cursor_pos], str, str_len);

    le->length += str_len;
    le->cursor_pos += str_len;
    le->buffer[le->length] = '\0';
    return 0;
}

int line_edit_delete_char_before_cursor(LineEdit *le) {
    if (!le || le->cursor_pos == 0) return -1;

    // Find the start of the UTF-8 character before the cursor
    int prev_pos = le->cursor_pos - 1;
    while (prev_pos > 0 && is_utf8_continuation(le->buffer[prev_pos])) {
        prev_pos--;
    }

    int char_len = le->cursor_pos - prev_pos;

    // Shift the rest of the string left
    memmove(&le->buffer[prev_pos],
            &le->buffer[le->cursor_pos],
            le->length - le->cursor_pos);
    
    le->length -= char_len;
    le->cursor_pos = prev_pos;
    le->buffer[le->length] = '\0';
    return 0;
}

int line_edit_move_to_start(LineEdit *le) {
    if (!le) return -1;
    le->cursor_pos = 0;
    return 0;
}

int line_edit_move_to_end(LineEdit *le) {
    if (!le) return -1;
    le->cursor_pos = le->length;
    return 0;
}

int line_edit_move_left(LineEdit *le) {
    if (!le || le->cursor_pos == 0) return -1;
    // Move to the beginning of the previous UTF-8 character
    le->cursor_pos--;
    while (le->cursor_pos > 0 && is_utf8_continuation(le->buffer[le->cursor_pos])) {
        le->cursor_pos--;
    }
    return 0;
}

int line_edit_move_right(LineEdit *le) {
    if (!le || le->cursor_pos >= le->length) return -1;
    // Move to the end of the current UTF-8 character
    le->cursor_pos += utf8_char_len(&le->buffer[le->cursor_pos]);
    return 0;
}

const char* line_edit_get_line(LineEdit *le) {
    return le ? le->buffer : "";
}

void line_edit_clear(LineEdit *le) {
    if (le) {
        le->length = 0;
        le->cursor_pos = 0;
        le->buffer[0] = '\0';
    }
}