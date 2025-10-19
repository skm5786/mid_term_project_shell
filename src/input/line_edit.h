// in src/input/line_edit.h
#ifndef LINE_EDIT_H
#define LINE_EDIT_H

#include <stddef.h>

#define MAX_INPUT_LENGTH 4096

// Line editing state
typedef struct {
    char buffer[MAX_INPUT_LENGTH];      // Input buffer
    int length;                         // Current length of input in bytes
    int cursor_pos;                     // Cursor position in bytes
} LineEdit;

// Function Prototypes
LineEdit* line_edit_init(void);
void line_edit_free(LineEdit *le);
int line_edit_insert_string(LineEdit *le, const char *str);
int line_edit_delete_char_before_cursor(LineEdit *le);
int line_edit_move_to_start(LineEdit *le);
int line_edit_move_to_end(LineEdit *le);
int line_edit_move_left(LineEdit *le);
int line_edit_move_right(LineEdit *le);
const char* line_edit_get_line(LineEdit *le);
void line_edit_clear(LineEdit *le);

#endif // LINE_EDIT_H