#ifndef X11_WINDOW_H
#define X11_WINDOW_H

#include <X11/Xlib.h>

// This code is cross-platform and works on both Linux and macOS with XQuartz.

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

typedef struct {
    Display *display;
    Window window;
    GC gc;
    int screen;
    unsigned long black_pixel;
    unsigned long white_pixel;
    XFontStruct *font;
    int width, height;
} X11Context;

X11Context* x11_init(const char *title);
void x11_cleanup(X11Context *ctx);

#endif // X11_WINDOW_H