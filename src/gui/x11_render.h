// in src/gui/x11_render.h
#ifndef X11_RENDER_H
#define X11_RENDER_H

#include "x11_window.h"

// Forward declaration of the struct.
struct TabManager;

#define MAX_LINES 1000
#define MAX_LINE_LENGTH 256
#define TAB_BAR_HEIGHT 30

typedef struct TextBuffer{
    char lines[MAX_LINES][MAX_LINE_LENGTH];
    int line_count;
    int cursor_line;
    int cursor_col;
} TextBuffer;

// --- Function prototypes ---
TextBuffer* text_buffer_init();
void text_buffer_free(TextBuffer *buf);
void text_buffer_append(TextBuffer *buf, const char *text);

// --- CHANGE ---
// Use "struct TabManager" because this header only has a forward declaration.
void render_tabs(X11Context *ctx, struct TabManager *mgr);

void render_text_buffer(X11Context *ctx, TextBuffer *buf);

#endif // X11_RENDER_H