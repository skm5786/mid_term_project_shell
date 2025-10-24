// in src/gui/x11_render.c
#include "x11_render.h"
#include "tab_manager.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

TextBuffer* text_buffer_init() {
    TextBuffer *buf = malloc(sizeof(TextBuffer));
    if (!buf) { perror("malloc"); return NULL; }
    buf->line_count = 1;
    buf->cursor_line = 0;
    buf->cursor_col = 0;
    buf->scroll_offset = 0;  // NEW: Initialize scroll offset
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
    
    // NEW: Auto-scroll to bottom when new content is added
    buf->scroll_offset = 0;
}

// NEW: Get number of visible lines in the window
int text_buffer_get_visible_lines(X11Context *ctx) {
    int font_height = ctx->font->ascent + ctx->font->descent;
    int available_height = ctx->height - TAB_BAR_HEIGHT;
    return available_height / font_height;
}

// NEW: Scroll up by specified number of lines
void text_buffer_scroll_up(TextBuffer *buf, int lines) {
    if (!buf) return;
    
    buf->scroll_offset += lines;
    
    // Limit scroll offset to prevent scrolling beyond the buffer
    int max_scroll = buf->line_count - 1;
    if (buf->scroll_offset > max_scroll) {
        buf->scroll_offset = max_scroll;
    }
}

// NEW: Scroll down by specified number of lines
void text_buffer_scroll_down(TextBuffer *buf, int lines) {
    if (!buf) return;
    
    buf->scroll_offset -= lines;
    
    // Don't scroll below the bottom
    if (buf->scroll_offset < 0) {
        buf->scroll_offset = 0;
    }
}

// NEW: Jump to the bottom of the buffer
void text_buffer_scroll_to_bottom(TextBuffer *buf) {
    if (!buf) return;
    buf->scroll_offset = 0;
}

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

void render_text_buffer(X11Context *ctx, TextBuffer *buf) {
    XClearArea(ctx->display, ctx->window, 0, TAB_BAR_HEIGHT, ctx->width, ctx->height - TAB_BAR_HEIGHT, False);
    
    int font_height = ctx->font->ascent + ctx->font->descent;
    int visible_lines = text_buffer_get_visible_lines(ctx);
    
    // Calculate which lines to display based on scroll offset
    int start_line = buf->line_count - visible_lines - buf->scroll_offset;
    if (start_line < 0) start_line = 0;
    
    int end_line = start_line + visible_lines;
    if (end_line > buf->line_count) end_line = buf->line_count;
    
    // Render visible lines
    for (int i = start_line; i < end_line; ++i) {
        int display_row = i - start_line;
        int y_pos = TAB_BAR_HEIGHT + (display_row * font_height) + ctx->font->ascent;
        
        if (y_pos > ctx->height + font_height) break;
        
        XDrawString(ctx->display, ctx->window, ctx->gc, 10, y_pos, 
                   buf->lines[i], strlen(buf->lines[i]));
    }
    
    // NEW: Draw scroll indicator if scrolled up
    if (buf->scroll_offset > 0) {
        char scroll_indicator[64];
        snprintf(scroll_indicator, sizeof(scroll_indicator), 
                "[Scrolled up %d lines]", buf->scroll_offset);
        
        XSetForeground(ctx->display, ctx->gc, 0x888888); // Gray text
        int indicator_y = TAB_BAR_HEIGHT + ctx->font->ascent + 5;
        XDrawString(ctx->display, ctx->window, ctx->gc, 
                   ctx->width - 200, indicator_y, 
                   scroll_indicator, strlen(scroll_indicator));
        XSetForeground(ctx->display, ctx->gc, ctx->black_pixel);
    }
    
    // Draw cursor (only if at bottom)
    if (buf->scroll_offset == 0) {
        int cursor_display_line = buf->cursor_line - start_line;
        if (cursor_display_line >= 0 && cursor_display_line < visible_lines) {
            int cursor_x = 10 + XTextWidth(ctx->font, buf->lines[buf->cursor_line], buf->cursor_col);
            int cursor_y = TAB_BAR_HEIGHT + (cursor_display_line * font_height);
            XFillRectangle(ctx->display, ctx->window, ctx->gc, cursor_x, cursor_y, 8, font_height);
        }
    }
}