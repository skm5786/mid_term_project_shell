// in src/input/input_handler.c

#include "input_handler.h"
#include <stdio.h>
#include <stdlib.h>

InputState* input_state_init(Display *display, Window window) {
    InputState *state = malloc(sizeof(InputState));
    if (!state) {
        perror("malloc");
        return NULL;
    }

    state->in_multiline_mode = 0;

    // Open the X Input Method to handle international keyboard input
    //
    state->xim = XOpenIM(display, NULL, NULL, NULL);
    if (!state->xim) {
        fprintf(stderr, "Warning: XOpenIM failed. Unicode input may not work correctly.\n");
        state->xic = NULL;
        return state;
    }

    // Create the X Input Context, associating the input method with our window
    //
    state->xic = XCreateIC(state->xim,
        XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
        XNClientWindow, window,
        XNFocusWindow, window,
        NULL);
    if (!state->xic) {
        fprintf(stderr, "Warning: XCreateIC failed. Unicode input may not work correctly.\n");
    }

    return state;
}

void input_state_cleanup(InputState *state) {
    if (state) {
        if (state->xic) {
            XDestroyIC(state->xic);
        }
        if (state->xim) {
            XCloseIM(state->xim);
        }
        free(state);
    }
}