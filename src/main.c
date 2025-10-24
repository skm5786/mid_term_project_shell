// in src/main.c - UPDATED WITH SCROLLING SUPPORT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#include "gui/x11_window.h"
#include "gui/x11_render.h"
#include "gui/tab_manager.h"
#include "input/input_handler.h"
#include "input/line_edit.h"
#include "utils/unicode_handler.h"
#include "shell/multiwatch.h"
#include "shell/signal_handler.h"
#include "shell/process_manager.h"

static char *clipboard_content = NULL;
static TabManager *g_tab_mgr_for_callback = NULL;

static void multiwatch_output_callback(const char *output) {
    if (g_tab_mgr_for_callback) {
        Tab *active_tab = tab_manager_get_active(g_tab_mgr_for_callback);
        if (active_tab) {
            text_buffer_append(active_tab->buffer, output);
        }
    }
}

static void background_job_callback(const char *notification) {
    if (g_tab_mgr_for_callback) {
        Tab *active_tab = tab_manager_get_active(g_tab_mgr_for_callback);
        if (active_tab) {
            text_buffer_append(active_tab->buffer, notification);
        }
    }
}

void handle_mouse_click(XButtonEvent *event, TabManager *mgr, X11Context *ctx) {
    if (event->y < TAB_BAR_HEIGHT) {
        int tab_width = ctx->width / MAX_TABS;
        if (tab_width == 0) tab_width = 1;
        int clicked_tab_index = event->x / tab_width;
        tab_manager_switch_tab(mgr, clicked_tab_index);
    }
}

// NEW: Handle mouse scroll events
void handle_mouse_scroll(XButtonEvent *event, TabManager *mgr, X11Context *ctx) {
    Tab *active_tab = tab_manager_get_active(mgr);
    if (!active_tab) return;
    
    // Button 4 = Scroll Up, Button 5 = Scroll Down
    if (event->button == 4) {
        text_buffer_scroll_up(active_tab->buffer, 3);  // Scroll up 3 lines
    } else if (event->button == 5) {
        text_buffer_scroll_down(active_tab->buffer, 3);  // Scroll down 3 lines
    }
}

void handle_copy_to_clipboard(X11Context *ctx, const char *text_to_copy) {
    if (text_to_copy && strlen(text_to_copy) > 0) {
        if (clipboard_content) free(clipboard_content);
        clipboard_content = strdup(text_to_copy);
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
    Tab *active_tab = tab_manager_get_active(mgr);
    if (!active_tab) return;
    LineEdit *le = active_tab->line_edit;

    char buffer[32];
    KeySym keysym;
    Status status;
    int len = Xutf8LookupString(input_state->xic, &event->xkey, buffer, sizeof(buffer) - 1, &keysym, &status);
    buffer[len] = '\0';

    // NEW: Handle Page Up / Page Down for scrolling
    if (keysym == XK_Page_Up) {
        int visible_lines = text_buffer_get_visible_lines(ctx);
        text_buffer_scroll_up(active_tab->buffer, visible_lines - 1);
        return;
    }
    if (keysym == XK_Page_Down) {
        int visible_lines = text_buffer_get_visible_lines(ctx);
        text_buffer_scroll_down(active_tab->buffer, visible_lines - 1);
        return;
    }
    
    // NEW: Shift+Up/Down for line-by-line scrolling
    if ((event->xkey.state & ShiftMask) && keysym == XK_Up) {
        text_buffer_scroll_up(active_tab->buffer, 1);
        return;
    }
    if ((event->xkey.state & ShiftMask) && keysym == XK_Down) {
        text_buffer_scroll_down(active_tab->buffer, 1);
        return;
    }

    // Handle high-priority interrupts first
    if (event->xkey.state & ControlMask) {
        if (keysym == XK_c) {
            if (active_tab->multiwatch_session) {
                cleanup_multiwatch((MultiWatch *)active_tab->multiwatch_session);
                active_tab->multiwatch_session = NULL;
                line_edit_clear(le);
                text_buffer_append(active_tab->buffer, "\n[multiWatch stopped.]\n");
            } else if (active_tab->process_manager && 
                       process_manager_get_foreground(active_tab->process_manager)) {
                tab_manager_send_sigint(mgr);
            } else {
                handle_copy_to_clipboard(ctx, line_edit_get_line(le));
            }
            return;
        }
        
        if (keysym == XK_z) {
            if (active_tab->process_manager && 
                process_manager_get_foreground(active_tab->process_manager)) {
                tab_manager_send_sigtstp(mgr);
            }
            return;
        }
        
        // Ctrl+R for history search
        if (keysym == XK_r) {
            if (!active_tab->multiwatch_session && !active_tab->in_search_mode) {
                tab_manager_enter_search_mode(mgr);
            }
            return;
        }
        
        // NEW: Ctrl+Home/End for jump to top/bottom
        if (keysym == XK_Home) {
            text_buffer_scroll_up(active_tab->buffer, MAX_LINES);
            return;
        }
        if (keysym == XK_End) {
            text_buffer_scroll_to_bottom(active_tab->buffer);
            return;
        }
        
        // Other global shortcuts
        if (keysym == XK_n) { 
            tab_manager_create_tab(mgr); 
            return; 
        }
        if (keysym == XK_w) { 
            tab_manager_close_tab(mgr, mgr->active_tab); 
            return; 
        }
        if ((event->xkey.state & ShiftMask) && keysym == XK_v) { 
            handle_paste_from_clipboard(ctx); 
            return; 
        }
    }
    
    // If multiWatch is active, block all other text input
    if (active_tab->multiwatch_session) return;

    // Line Editing Controls
    if (event->xkey.state & ControlMask) {
        switch (keysym) {
            case XK_a: line_edit_move_to_start(le); return;
            case XK_e: line_edit_move_to_end(le); return;
        }
    }

    // Standard key handling
    switch (keysym) {
        case XK_Return:
            tab_manager_execute_command(mgr, line_edit_get_line(le));
            break;
        case XK_BackSpace:
            line_edit_delete_char_before_cursor(le);
            break;
        case XK_Left:
            line_edit_move_left(le);
            break;
        case XK_Right:
            line_edit_move_right(le);
            break;
        default:
            if (len > 0) {
                line_edit_insert_string(le, buffer);
            }
            break;
    }
}

static X11Context *g_ctx = NULL;
static TabManager *g_tab_mgr = NULL;
static InputState *g_input_state = NULL;

// Forward declarations
void process_keypress(XEvent *event, TabManager *mgr, InputState *input_state, X11Context *ctx);
void handle_mouse_click(XButtonEvent *event, TabManager *mgr, X11Context *ctx);
void handle_mouse_scroll(XButtonEvent *event, TabManager *mgr, X11Context *ctx);

// Function to process pending X11 events (called from command execution)
int process_pending_events(void) {
    if (!g_ctx || !g_tab_mgr || !g_input_state) return 0;
    
    while (XPending(g_ctx->display)) {
        XEvent event;
        XNextEvent(g_ctx->display, &event);
        if (XFilterEvent(&event, None)) continue;

        switch (event.type) {
            case KeyPress:
                process_keypress(&event, g_tab_mgr, g_input_state, g_ctx);
                break;
            case ButtonPress:
                if (event.xbutton.button == 4 || event.xbutton.button == 5) {
                    handle_mouse_scroll(&event.xbutton, g_tab_mgr, g_ctx);
                } else {
                    handle_mouse_click(&event.xbutton, g_tab_mgr, g_ctx);
                }
                break;
        }
    }
    
    return 0;
}

int main(void) {
    FILE *debug_out = fopen("/tmp/myterm_debug.log", "w");
    if (debug_out) {
        setvbuf(debug_out, NULL, _IONBF, 0);
        dup2(fileno(debug_out), STDERR_FILENO);
        dup2(fileno(debug_out), STDOUT_FILENO);
    }
    
    printf("=== MyTerm Starting ===\n");
    printf("PID: %d\n", getpid());
    fflush(stdout);
    
    if (setlocale(LC_ALL, "") == NULL) {
        fprintf(stderr, "Warning: could not set locale.\n");
    }

    printf("Initializing signal handlers...\n");
    fflush(stdout);
    if (signal_handler_init() == -1) {
        fprintf(stderr, "Warning: Failed to initialize signal handlers.\n");
    }
    printf("Signal handlers initialized\n");
    fflush(stdout);

    X11Context *ctx = x11_init("MyTerm");
    TabManager *tab_mgr = tab_manager_init();
    InputState *input_state = input_state_init(ctx->display, ctx->window);
    if (!ctx || !tab_mgr || !input_state) {
        fprintf(stderr, "Failed to initialize components\n");
        return 1;
    }

    g_ctx = ctx;
    g_tab_mgr = tab_mgr;
    g_input_state = input_state;
    g_tab_mgr_for_callback = tab_mgr;

    extern void set_event_processor_callback(int (*callback)(void));
    set_event_processor_callback(process_pending_events);

    Atom wm_delete_window = XInternAtom(ctx->display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ctx->display, ctx->window, &wm_delete_window, 1);

    printf("Entering main event loop\n");
    fflush(stdout);

    int running = 1;
    while (running) {
        Tab *active_tab = tab_manager_get_active(tab_mgr);

        if (active_tab && active_tab->multiwatch_session) {
            multiwatch_poll_output((MultiWatch *)active_tab->multiwatch_session, multiwatch_output_callback);
        }
        
        if (active_tab) {
            tab_manager_check_background_jobs(tab_mgr, background_job_callback);
        }
        
        while (XPending(ctx->display)) {
            XEvent event;
            XNextEvent(ctx->display, &event);
            if (XFilterEvent(&event, None)) continue;

            switch (event.type) {
                case KeyPress:
                    process_keypress(&event, tab_mgr, input_state, ctx);
                    break;
                case ButtonPress:
                    // NEW: Handle scroll wheel
                    if (event.xbutton.button == 4 || event.xbutton.button == 5) {
                        handle_mouse_scroll(&event.xbutton, tab_mgr, ctx);
                    } else {
                        handle_mouse_click(&event.xbutton, tab_mgr, ctx);
                    }
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
                    if (XGetWindowProperty(ctx->display, ctx->window, event.xselection.property, 0, 4096, False, AnyPropertyType, &type, &format, &nitems, &bytes_after, &data) == Success) {
                        if (data && active_tab && !active_tab->multiwatch_session) {
                            line_edit_insert_string(active_tab->line_edit, (char *)data);
                        }
                        if (data) XFree(data);
                    }
                    break;
                }
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
                            XChangeProperty(sev.display, sev.requestor, sev.property, utf8_atom, 8, PropModeReplace, (unsigned char *)clipboard_content, strlen(clipboard_content));
                        } else {
                            sev.property = None;
                        }
                        XSendEvent(ctx->display, sev.requestor, True, NoEventMask, (XEvent *)&sev);
                    }
                    break;
                }
            }
        }
        
        if (tab_mgr->num_tabs == 0) running = 0;
        
        if (active_tab) {
            render_tabs(ctx, tab_mgr);
            render_text_buffer(ctx, active_tab->buffer);

            const char *line = line_edit_get_line(active_tab->line_edit);
            int font_height = ctx->font->ascent + ctx->font->descent;
            
            // Only show input prompt if at bottom and not in multiwatch
            if (!active_tab->multiwatch_session && active_tab->buffer->scroll_offset == 0) {
                int visible_lines = text_buffer_get_visible_lines(ctx);
                int display_line = active_tab->buffer->cursor_line;
                int start_line = active_tab->buffer->line_count - visible_lines;
                if (start_line < 0) start_line = 0;
                
                int line_y = TAB_BAR_HEIGHT + ((display_line - start_line) * font_height) + ctx->font->ascent;
                
                XDrawString(ctx->display, ctx->window, ctx->gc, 10, line_y, "$ ", 2);
                XDrawString(ctx->display, ctx->window, ctx->gc, 10 + XTextWidth(ctx->font, "$ ", 2), line_y, line, strlen(line));
            
                int cursor_x = 10 + XTextWidth(ctx->font, "$ ", 2) +
                               XTextWidth(ctx->font, line, active_tab->line_edit->cursor_pos);
                int cursor_y = TAB_BAR_HEIGHT + ((display_line - start_line) * font_height);
                XFillRectangle(ctx->display, ctx->window, ctx->gc, cursor_x, cursor_y, 8, font_height);
            }
            
            XFlush(ctx->display);
        }

        usleep(10000);
    }

    printf("Cleaning up...\n");
    fflush(stdout);

    if (clipboard_content) free(clipboard_content);
    input_state_cleanup(input_state);
    tab_manager_cleanup(tab_mgr);
    x11_cleanup(ctx);
    
    if (debug_out) {
        fclose(debug_out);
    }
    
    return 0;
}