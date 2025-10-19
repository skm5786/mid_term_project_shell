// in src/gui/x11_render.c
#include "x11_render.h"
#include "tab_manager.h" // This include is correct here.
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ... (your text_buffer functions are correct and unchanged) ...
TextBuffer* text_buffer_init() {
    TextBuffer *buf = malloc(sizeof(TextBuffer));
    if (!buf) { perror("malloc"); return NULL; }
    buf->line_count = 1;
    buf->cursor_line = 0;
    buf->cursor_col = 0;
    memset(buf->lines, 0, sizeof(buf->lines));
    return buf;
}

void text_buffer_free(TextBuffer *buf) {
    free(buf);
}

void text_buffer_append(TextBuffer *buf, const char *text) {
    for (int i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\n' || buf->cursor_col >= MAX_LINE_LENGTH - 1) {
            buf->cursor_line++;
            buf->cursor_col = 0;
            if (buf->cursor_line >= MAX_LINES) {
                memmove(buf->lines[0], buf->lines[1], (MAX_LINES - 1) * MAX_LINE_LENGTH);
                memset(buf->lines[MAX_LINES - 1], 0, MAX_LINE_LENGTH);
                buf->cursor_line = MAX_LINES - 1;
            }
            if (buf->cursor_line >= buf->line_count) {
                buf->line_count = buf->cursor_line + 1;
            }
        } else {
            buf->lines[buf->cursor_line][buf->cursor_col] = text[i];
            buf->cursor_col++;
        }
    }
}


// --- CHANGE ---
// The function signature here MUST match the new declaration in the header file.
void render_tabs(X11Context *ctx, struct TabManager *mgr) {
    XSetForeground(ctx->display, ctx->gc, 0xDDDDDD); // Light gray
    XFillRectangle(ctx->display, ctx->window, ctx->gc, 0, 0, ctx->width, TAB_BAR_HEIGHT);

    int tab_width = ctx->width / MAX_TABS;
    if (tab_width == 0) tab_width = 1; 

    for (int i = 0; i < MAX_TABS; ++i) {
        if (mgr->tabs[i].active) {
            int x_pos = i * tab_width;
            char tab_label[16];
            snprintf(tab_label, sizeof(tab_label), "Tab %d", i + 1);

            if (i == mgr->active_tab) {
                XSetForeground(ctx->display, ctx->gc, ctx->white_pixel);
                XFillRectangle(ctx->display, ctx->window, ctx->gc, x_pos + 2, 2, tab_width - 4, TAB_BAR_HEIGHT - 4);
            }

            XSetForeground(ctx->display, ctx->gc, ctx->black_pixel);
            XDrawRectangle(ctx->display, ctx->window, ctx->gc, x_pos + 2, 2, tab_width - 4, TAB_BAR_HEIGHT - 4);
            XDrawString(ctx->display, ctx->window, ctx->gc, x_pos + 10, 20, tab_label, strlen(tab_label));
        }
    }
}

// ... (render_text_buffer is correct and unchanged) ...
void render_text_buffer(X11Context *ctx, TextBuffer *buf) {
    XClearArea(ctx->display, ctx->window, 0, TAB_BAR_HEIGHT, ctx->width, ctx->height - TAB_BAR_HEIGHT, False);
    int font_height = ctx->font->ascent + ctx->font->descent;
    for (int i = 0; i < buf->line_count; ++i) {
        int y_pos = TAB_BAR_HEIGHT + (i * font_height) + ctx->font->ascent;
        if (y_pos > ctx->height + font_height) break;
        XDrawString(ctx->display, ctx->window, ctx->gc, 10, y_pos, buf->lines[i], strlen(buf->lines[i]));
    }
    int cursor_x = 10 + XTextWidth(ctx->font, buf->lines[buf->cursor_line], buf->cursor_col);
    int cursor_y = TAB_BAR_HEIGHT + (buf->cursor_line * font_height);
    XFillRectangle(ctx->display, ctx->window, ctx->gc, cursor_x, cursor_y, 8, font_height);
}