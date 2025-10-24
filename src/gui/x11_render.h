// in src/gui/x11_render.h
#ifndef X11_RENDER_H
#define X11_RENDER_H

#include "x11_window.h"

// Forward declaration of the struct.
struct TabManager;

#define MAX_LINES 10000
#define MAX_LINE_LENGTH 256
#define TAB_BAR_HEIGHT 30

typedef struct TextBuffer{
    char lines[MAX_LINES][MAX_LINE_LENGTH];
    int line_count;
    int cursor_line;
    int cursor_col;
    int scroll_offset;  // NEW: Tracks how many lines we've scrolled up
} TextBuffer;

// --- Function prototypes ---
TextBuffer* text_buffer_init();
void text_buffer_free(TextBuffer *buf);
void text_buffer_append(TextBuffer *buf, const char *text);

// NEW: Scrolling functions
void text_buffer_scroll_up(TextBuffer *buf, int lines);
void text_buffer_scroll_down(TextBuffer *buf, int lines);
void text_buffer_scroll_to_bottom(TextBuffer *buf);
int text_buffer_get_visible_lines(X11Context *ctx);

void render_tabs(X11Context *ctx, struct TabManager *mgr);
void render_text_buffer(X11Context *ctx, TextBuffer *buf);

#endif // X11_RENDER_H