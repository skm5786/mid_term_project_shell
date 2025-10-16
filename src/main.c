// in src/main.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#include "gui/x11_window.h"
#include "gui/x11_render.h"
#include "gui/tab_manager.h"
#include "input/input_handler.h"
#include "utils/unicode_handler.h"

static char *clipboard_content = NULL;

void handle_mouse_click(XButtonEvent *event, TabManager *mgr, X11Context *ctx) {
    if (event->y < TAB_BAR_HEIGHT) {
        int tab_width = ctx->width / MAX_TABS;
        if (tab_width == 0) tab_width = 1;
        int clicked_tab_index = event->x / tab_width;
        tab_manager_switch_tab(mgr, clicked_tab_index);
    }
}

void handle_copy_to_clipboard(X11Context *ctx, Tab *tab) {
    if (tab->input_length > 0) {
        if (clipboard_content) free(clipboard_content);
        clipboard_content = strdup(tab->current_input);
        Atom clipboard_atom = XInternAtom(ctx->display, "CLIPBOARD", False);
        XSetSelectionOwner(ctx->display, clipboard_atom, ctx->window, CurrentTime);
    }
}

void handle_paste_from_clipboard(X11Context *ctx) {
    Atom clipboard_atom = XInternAtom(ctx->display, "CLIPBOARD", False);
    Atom utf8_atom = XInternAtom(ctx->display, "UTF8_STRING", True);
    XConvertSelection(ctx->display, clipboard_atom, utf8_atom, clipboard_atom, ctx->window, CurrentTime);
}

void process_keypress(XEvent *event, TabManager *mgr, InputState *input_state, X11Context *ctx) {
    if (mgr->active_tab == -1) return;
    Tab *active_tab = tab_manager_get_active(mgr);
    if (!active_tab) return;
    
    char buffer[32];
    KeySym keysym;
    Status status;
    int len = Xutf8LookupString(input_state->xic, &event->xkey, buffer, sizeof(buffer) - 1, &keysym, &status);
    buffer[len] = '\0';
    
    if (event->xkey.state & ControlMask) {
        if (event->xkey.state & ShiftMask) {
            if (keysym == XK_c) { handle_copy_to_clipboard(ctx, active_tab); return; }
            if (keysym == XK_v) { handle_paste_from_clipboard(ctx); return; }
        }
        if (keysym == XK_n) { tab_manager_create_tab(mgr); return; }
        if (keysym == XK_w) { tab_manager_close_tab(mgr, mgr->active_tab); return; }
        if (keysym == XK_j) {
            if (active_tab->input_length + 1 < MAX_LINE_LENGTH) {
                strcat(active_tab->current_input, "\n");
                active_tab->input_length++;
                text_buffer_append(active_tab->buffer, "\n");
            }
            return;
        }
    }

    switch (keysym) {
        case XK_Return:
            if (is_multiline_continuation(active_tab->current_input)) {
                active_tab->current_input[active_tab->input_length - 1] = '\0';
                strcat(active_tab->current_input, "\n");
                text_buffer_append(active_tab->buffer, "\\\n> ");
                input_state->in_multiline_mode = 1;
            } else {
                char final_input_processed[MAX_MULTILINE_INPUT];
                process_escape_sequences(active_tab->current_input, final_input_processed, sizeof(final_input_processed));
                
                #if USE_BASH_MODE
                    strcat(final_input_processed, "\n");
                    tab_manager_send_input(mgr, final_input_processed);
                #else
                    tab_manager_execute_command(mgr, final_input_processed);
                #endif
                
                // Reset for the next command
                input_state->in_multiline_mode = 0;
                memset(active_tab->current_input, 0, MAX_LINE_LENGTH);
                active_tab->input_length = 0;
            }
            break;
        case XK_BackSpace:
             if (active_tab->input_length > 0) {
                int char_len = get_last_utf8_char_len(active_tab->current_input, active_tab->input_length);
                active_tab->input_length -= char_len;
                active_tab->current_input[active_tab->input_length] = '\0';
                if (active_tab->buffer->cursor_col > 0) {
                    active_tab->buffer->cursor_col--;
                    active_tab->buffer->lines[active_tab->buffer->cursor_line][active_tab->buffer->cursor_col] = '\0';
                }
            }
            break;
        default:
            if (len > 0 && active_tab->input_length + len < MAX_LINE_LENGTH) {
                strcat(active_tab->current_input, buffer);
                active_tab->input_length += len;
                text_buffer_append(active_tab->buffer, buffer);
            }
            break;
    }
}

int main(void) {
    if (setlocale(LC_ALL, "") == NULL) fprintf(stderr, "Warning: could not set locale.\n");

    X11Context *ctx = x11_init("MyTerm");
    TabManager *tab_mgr = tab_manager_init();
    InputState *input_state = input_state_init(ctx->display, ctx->window);
    if (!ctx || !tab_mgr || !input_state) return 1;

    Atom wm_delete_window = XInternAtom(ctx->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ctx->display, ctx->window, &wm_delete_window, 1);

    int running = 1;
    while (running) {
        #if USE_BASH_MODE
            tab_manager_read_output(tab_mgr);
        #endif
        
        while (XPending(ctx->display)) {
            XEvent event;
            XNextEvent(ctx->display, &event);
            if (XFilterEvent(&event, None)) continue;

            switch (event.type) {
                case KeyPress:
                    process_keypress(&event, tab_mgr, input_state, ctx);
                    break;
                case ButtonPress:
                    handle_mouse_click(&event.xbutton, tab_mgr, ctx);
                    break;
                case ClientMessage:
                    if ((Atom)event.xclient.data.l[0] == wm_delete_window) running = 0;
                    break;
                case SelectionNotify: {
                    if (event.xselection.property == None) break;
                    Atom type;
                    int format;
                    unsigned long nitems, bytes_after;
                    unsigned char *data = NULL;
                    if (XGetWindowProperty(ctx->display, ctx->window, event.xselection.property,
                                           0, 1024, False, AnyPropertyType,
                                           &type, &format, &nitems, &bytes_after, &data) == Success) {
                        if (data) {
                            Tab *active_tab = tab_manager_get_active(tab_mgr);
                            if (active_tab) {
                                strcat(active_tab->current_input, (char *)data);
                                active_tab->input_length += strlen((char *)data);
                                text_buffer_append(active_tab->buffer, (char *)data);
                            }
                            XFree(data);
                        }
                    }
                    break;
                }
                // THIS IS THE MISSING HANDLER FOR SERVING COPIED TEXT
                case SelectionRequest: {
                    XSelectionRequestEvent *req = &event.xselectionrequest;
                    if (req->selection == XInternAtom(ctx->display, "CLIPBOARD", False)) {
                        XSelectionEvent sev = {0};
                        sev.type = SelectionNotify;
                        sev.display = req->display;
                        sev.requestor = req->requestor;
                        sev.selection = req->selection;
                        sev.target = req->target;
                        sev.property = req->property;
                        sev.time = req->time;
                        
                        Atom utf8_atom = XInternAtom(ctx->display, "UTF8_STRING", True);
                        if (sev.target == utf8_atom && clipboard_content) {
                            XChangeProperty(sev.display, sev.requestor, sev.property, utf8_atom, 8,
                                            PropModeReplace, (unsigned char *)clipboard_content,
                                            strlen(clipboard_content));
                        } else {
                            sev.property = None; // Can't provide the requested format
                        }
                        XSendEvent(ctx->display, sev.requestor, True, NoEventMask, (XEvent *)&sev);
                    }
                    break;
                }
            }
        }
        
        if (tab_mgr->num_tabs == 0) running = 0;
        
        Tab *active_tab = tab_manager_get_active(tab_mgr);
        if (active_tab) {
            render_tabs(ctx, tab_mgr);
            render_text_buffer(ctx, active_tab->buffer);
            XFlush(ctx->display);
        }
        usleep(10000);
    }

    if (clipboard_content) free(clipboard_content);
    input_state_cleanup(input_state);
    tab_manager_cleanup(tab_mgr);
    x11_cleanup(ctx);
    return 0;
}