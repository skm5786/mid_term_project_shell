#include "x11_window.h"
#include <stdio.h>
#include <stdlib.h>

// This code is cross-platform and works on both Linux and macOS with XQuartz.

X11Context* x11_init(const char *title) {
    X11Context *ctx = malloc(sizeof(X11Context));
    if (!ctx) {
        perror("malloc");
        return NULL;
    }

    ctx->display = XOpenDisplay(NULL);
    if (ctx->display == NULL) {
        fprintf(stderr, "Error: Cannot open display. Make sure XQuartz is running.\n");
        free(ctx);
        return NULL;
    }

    ctx->screen = DefaultScreen(ctx->display);
    ctx->black_pixel = BlackPixel(ctx->display, ctx->screen);
    ctx->white_pixel = WhitePixel(ctx->display, ctx->screen);

    ctx->width = WINDOW_WIDTH;
    ctx->height = WINDOW_HEIGHT;
    ctx->window = XCreateSimpleWindow(ctx->display, RootWindow(ctx->display, ctx->screen), 0, 0, ctx->width, ctx->height, 2, ctx->black_pixel, ctx->white_pixel);
    XStoreName(ctx->display, ctx->window, title);

    ctx->font = XLoadQueryFont(ctx->display, "fixed");
    if (ctx->font == NULL) {
        fprintf(stderr, "Warning: 'fixed' font not found, falling back to '9x15'.\n");
        ctx->font = XLoadQueryFont(ctx->display, "9x15");
        if (ctx->font == NULL) {
            fprintf(stderr, "Error: Could not load fallback font '9x15'.\n");
            XCloseDisplay(ctx->display);
            free(ctx);
            return NULL;
        }
    }

    ctx->gc = XCreateGC(ctx->display, ctx->window, 0, NULL);
    XSetFont(ctx->display, ctx->gc, ctx->font->fid);
    XSetForeground(ctx->display, ctx->gc, ctx->black_pixel);

    XSelectInput(ctx->display, ctx->window, ExposureMask | KeyPressMask | ButtonPressMask);
    XMapWindow(ctx->display, ctx->window);
    XFlush(ctx->display);

    return ctx;
}

void x11_cleanup(X11Context *ctx) {
    if (ctx == NULL) return;
    if (ctx->font) XFreeFont(ctx->display, ctx->font);
    if (ctx->gc) XFreeGC(ctx->display, ctx->gc);
    if (ctx->display) {
        XDestroyWindow(ctx->display, ctx->window);
        XCloseDisplay(ctx->display);
    }
    free(ctx);
}