// in src/input/input_handler.h

#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "../gui/x11_window.h"

// Input state, including XIM/XIC for Unicode
//
typedef struct {
    int in_multiline_mode;
    XIM xim;
    XIC xic;
} InputState;

/**
 * @brief Initializes the input state, including opening the X Input Method.
 * @param display The X11 display connection.
 * @param window The window to associate with the input context.
 * @return A pointer to the new InputState.
 */
InputState* input_state_init(Display *display, Window window);

/**
 * @brief Cleans up the input state resources.
 * @param state The InputState to clean up.
 */
void input_state_cleanup(InputState *state);

#endif // INPUT_HANDLER_H