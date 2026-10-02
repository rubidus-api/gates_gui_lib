/* gates_gui_lib - Win32 backend internals. Only files under src/platform/win32
 * may include this header (and windows.h). */
#ifndef GATES_WIN32_INTERNAL_H
#define GATES_WIN32_INTERNAL_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <gates/app.h>
#include <gates/window.h>
#include <gates/frame.h>
#include <gates/image.h>
#include <gates/editor.h>
#include <gates/task.h>
#include <gates/render.h>
#include <gates/text.h>
#include <gates/theme.h>
#include <gates/ui.h>
#include <gates/clipboard.h>
#include <gates/overlay.h>
#include <gates/post.h>
#include <gates/timer.h>
#include <gates/access.h>

struct gates_app {
    gates_allocator_t alloc;
    ATOM window_class;
    HINSTANCE hinstance;
    gates_u32 window_count;
    bool running;
    /* Posting: the app's sender and the message-only window that
     * wakes the UI thread for it; every window, for the after-delivery pass. */
    gates_sender_t *sender;
    ATOM post_class;
    HWND post_hwnd;
    bool post_pending;           /* a delivery-turn timer is set */
    struct gates_window *windows;
    bool com_init;               /* CoInitializeEx succeeded: undo at destroy */
};

struct gates_window {
    gates_app_t *app;
    HWND hwnd;
    gates_tree_t *tree;
    gates_window_callbacks_t cb;
    gates_draw_list_t draw_list;

    /* Present surface: top-down 32-bit BGRA DIB section. */
    HDC mem_dc;
    HBITMAP dib;
    HBITMAP dib_old;
    void *dib_pixels;
    gates_size_t dib_size;

    /* UI pipeline: tree drives layout/paint when it has widgets. */
    const gates_theme_t *theme;      /* points at theme_store */
    gates_theme_t theme_store;
    gates_theme_mode_t theme_mode;
    gates_u32 dpi;                   /* this window's monitor DPI (96 = 100%) */
    bool theme_custom;               /* set by gates_window_set_theme */
    const gates_text_backend_t *text;
    gates_size_t last_layout_size;

    /* Input state. */
    gates_point_t last_pos;
    bool have_last_pos;
    gates_u32 buttons;
    gates_u32 pending_lead;  /* UTF-16 high surrogate awaiting its low half */
    /* The last double click (message time, client pixels): a press soon after
     * it, near it, is the third of a triple click (0.8.0). */
    gates_u32 press_count;           /* quick presses in one place so far (0 = none) */
    DWORD press_time;
    POINT press_pos;

    /* IME composition in progress and the node it started in. */
    bool ime_active;
    gates_node_t ime_node;
    /* The dispatch timer is armed for leftover events (0.8.0; was a posted message). */
    bool dispatch_posted;
    /* IME detached while a read-only or password box has focus. */
    bool ime_off;
    HIMC ime_saved;
    /* Next window of the app (posting pass), and whether the tree timer is armed. */
    struct gates_window *next_window;
    bool shown;                  /* 0.8.0: shown once the program's UI is built (gates_app_run) */
    bool show_max;               /* a placement set before that asked for maximized */
    bool timer_armed;
    gates_u64 timer_due_at;      /* absolute due time the armed timer stands for */

    /* Accessibility: UI Automation providers handed out, the focus
     * and overlay count clients last heard of, the hidden system caret, and
     * the scale: monitor DPI x application zoom x Windows text size. */
    struct uia_el_t *uia_els;
    gates_access_ref_t uia_focus;
    gates_access_ref_t uia_opened;   /* a dialog to announce once laid out */
    gates_u32 uia_overlays;
    gates_u32 uia_menus;             /* open menus at the last drain (0.10.0: MenuClosed) */
    bool uia_draining;
    bool caret_made;
    gates_i32 caret_h;
    gates_u32 monitor_dpi;
    gates_u32 zoom;              /* percent, 100 = none */
    bool strict;                 /* GATES_ACCESS_STRICT: audit after every layout */
    gates_u64 perf_input_us;     /* first input since the last frame, 0 = none */
    bool leave_tracked;          /* WM_MOUSELEAVE requested for this hover */
    bool perf_first_done;
    gates_u32 strict_issues;     /* the count last reported */
};

/* Posted to itself when more events are queued than one turn delivers. */
#define GATES_WIN32_EVENTS_PER_TURN 64u
/* Sent to the app's message-only window when posted messages wait. */
#define GATES_WM_POST (WM_APP + 0x48)
/* The app's message-only window: the timer for a delivery turn that had to wait. */
#define GATES_WIN32_POST_TIMER_ID 0x6A7Fu
/* The one Win32 timer per window that stands for the tree's next due timer. */
#define GATES_WIN32_TIMER_ID 0x6A7Eu
#define GATES_WIN32_DISPATCH_TIMER_ID 0x6A7Du /* 0.8.0: leftover events continue on a timer */

/* gates_win32_app.c: DPI helpers that exist only on newer Windows (loaded at run time). */
gates_u32 gates_win32_dpi_for_window(HWND hwnd);
gates_u32 gates_win32_system_dpi(void);

/* gates_win32_window.c: re-arms the window's timer for the tree's next due one. */
void gates_win32_arm_timer(gates_window_t *win);

/* gates_win32_app.c */
const wchar_t *gates_win32_class_name(void);

/* gates_win32_window.c */
LRESULT CALLBACK gates_win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

/* gates_win32_input.c - returns true when the message was consumed. */
bool gates_win32_handle_input(gates_window_t *win, UINT msg, WPARAM wparam, LPARAM lparam);
/* IMM32 composition: true when handled, with the LRESULT in *result. */
bool gates_win32_handle_ime(gates_window_t *win, UINT msg, WPARAM wparam, LPARAM lparam,
                            LRESULT *result);
/* Moves the IME composition/candidate windows to the focused caret. */
void gates_win32_ime_place(gates_window_t *win);
/* Asks the IME to deliver an open composition as its result now (or drops it). */
void gates_win32_ime_complete(gates_window_t *win);
/* After every input message: deliver events, flush destroys, keep the IME in
 * step with focus, request a repaint when dirty. */
void gates_win32_show(gates_window_t *win);
void gates_win32_after_input(gates_window_t *win);
/* gates_win32_uia.c: WM_GETOBJECT (true when answered, with *result); UIA
 * events for what changed (after every input pass); providers let go when the
 * window goes; the hidden system caret that follows the text caret. */
bool gates_win32_uia_getobject(gates_window_t *win, WPARAM wparam, LPARAM lparam, LRESULT *result);
void gates_win32_uia_events(gates_window_t *win);
void gates_win32_uia_detach(gates_window_t *win);
void gates_win32_caret_follow(gates_window_t *win);
void gates_win32_caret_drop(gates_window_t *win);
/* gates_win32_window.c: the window's drawing DPI from its monitor, zoom and text size. */
void gates_win32_rescale(gates_window_t *win);
/* The system's UI font changed (0.10.0): drop every made face; the next
 * measure makes them again from the current settings. */
void gates_win32_text_refresh(void);
/* The GATES_FONT_UI face (0.12.0): null or empty = the system message font. Drops cached faces. */
void gates_win32_text_set_ui_face(const wchar_t *face);
/* gates_win32_perf.c (0.3.0): GATES_PERF=<file> field measurements. */
bool gates_win32_perf_on(void);
gates_u64 gates_win32_perf_now_us(void);
void gates_win32_perf_log(const char *fmt, ...);
gates_u64 gates_win32_perf_process_ms(void);
/* gates_win32_clipboard.c: CF_UNICODETEXT provider for the window's tree. */
void gates_win32_install_clipboard(gates_window_t *win);
/* gates_win32_image.c: the WIC image decoder for the window's tree (0.5.0). */
void gates_win32_install_image_decoder(gates_window_t *win);
void gates_win32_install_threads(gates_window_t *win); /* background tasks (0.6.0) */
/* Capture lost or mode cancelled: forget held buttons, press and drags. */
void gates_win32_cancel_pointer(gates_window_t *win);

#endif /* GATES_WIN32_INTERNAL_H */
