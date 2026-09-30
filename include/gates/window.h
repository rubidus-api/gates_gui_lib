/* gates_gui_lib - window/surface boundary.
 * Opaque, platform-free header. A window owns its retained tree; the tree lays
 * out and paints its widgets, and the paint callback may add draw commands. */
#ifndef GATES_WINDOW_H
#define GATES_WINDOW_H

#include <gates/app.h>
#include <gates/draw.h>
#include <gates/input.h>
#include <gates/tree.h>
#include <gates/theme.h>

typedef struct gates_window gates_window_t;

typedef struct gates_window_desc_t {
    gates_str_t title;               /* UTF-8 */
    gates_size_t size;               /* client size in logical units (1/96 inch); {0,0} -> default */
} gates_window_desc_t;

typedef struct gates_window_callbacks_t {
    /* Fill the (already reset) draw list; target is the client area. */
    void (*on_paint)(gates_window_t *win, gates_draw_list_t *dl, void *user);
    void (*on_pointer)(gates_window_t *win, const gates_pointer_event_t *ev, void *user);
    void (*on_key)(gates_window_t *win, const gates_key_event_t *ev, void *user);
    void (*on_char)(gates_window_t *win, gates_u32 utf16_unit, void *user);
    void (*on_resize)(gates_window_t *win, gates_size_t client, void *user);
    /* Return false to keep the window open. Absent -> close allowed. */
    bool (*on_close)(gates_window_t *win, void *user);
    void *user_data;
    /* 0.8.0: an input-path failure (gates_input_take_error, gates/ui.h), taken
     * after each input turn - an edit or activation skipped for want of memory,
     * a clipboard that would not answer. Absent: the error waits for the
     * program to take it. Show it; the tree is still consistent. */
    void (*on_input_error)(gates_window_t *win, gates_err_t err, void *user);
} gates_window_callbacks_t;

/* A top-level window with its tree; shown when gates_app_run starts (or at
 * once while it runs). Destroying it cancels its running jobs and waits. */
[[nodiscard]] gates_err_t gates_window_create(gates_app_t *app,
                                              const gates_window_desc_t *desc,
                                              const gates_window_callbacks_t *callbacks,
                                              gates_window_t **out_window);
void gates_window_destroy(gates_window_t *win);

/* The window's retained node tree (owned by the window). */
gates_tree_t *gates_window_tree(gates_window_t *win);

/* Theme. SYSTEM (the default) follows the desktop: high contrast
 * when it is on, else dark or light as the Windows app mode says, switching
 * live when the user changes it; the title bar follows too. */
typedef enum gates_theme_mode_t {
    GATES_THEME_SYSTEM = 0,
    GATES_THEME_LIGHT,
    GATES_THEME_DARK,
    GATES_THEME_HIGH_CONTRAST,   /* from the system's high-contrast colours */
} gates_theme_mode_t;
void gates_window_set_theme_mode(gates_window_t *win, gates_theme_mode_t mode);
gates_theme_mode_t gates_window_theme_mode(const gates_window_t *win);
/* An application theme (copied); it stays until set_theme_mode is called. */
[[nodiscard]] gates_err_t gates_window_set_theme(gates_window_t *win, const gates_theme_t *theme);
/* The theme in use now. */
const gates_theme_t *gates_window_theme(const gates_window_t *win);

/* Logical units: the window scales its drawing to the monitor's DPI
 * and converts pointer positions back, so the tree never sees device pixels.
 * GATES_FORCE_DPI=<dpi> in the environment forces a DPI (diagnostics). */
gates_size_t gates_window_client_size(const gates_window_t *win);
void gates_window_request_repaint(gates_window_t *win);

/* Accessibility. The window is a UI Automation provider: screen
 * readers and automation tools see the tree as access.h describes it.
 * Zoom (percent, 25..400, 100 = none) scales the whole interface on top of the
 * monitor's DPI and the Windows "Text size" setting, which it also follows. */
void gates_window_set_zoom(gates_window_t *win, gates_u32 percent);
gates_u32 gates_window_zoom(const gates_window_t *win);
/* The user turned animation effects off: show end states, skip motion. */
bool gates_window_reduced_motion(const gates_window_t *win);
/* Spoken by screen readers (a UI Automation notification); assertive
 * interrupts what is being read. The text is copied. */
[[nodiscard]] gates_err_t gates_window_announce(gates_window_t *win, gates_str_t text, bool assertive);
/* Placement (0.3.0): where the window is, as text to keep in a
 * file - "x,y,w,h,state" with the normal (restored) rectangle in screen pixels
 * and state "normal" or "maximized". *needed always receives its length; with
 * a buffer too small nothing is written (OVERFLOW). set_placement takes that
 * text back; a rectangle that no longer meets any monitor moves onto the
 * nearest one; malformed text is INVALID_ARG and changes nothing. */
[[nodiscard]] gates_err_t gates_window_placement(const gates_window_t *win, gates_u8 *buf,
                                                 gates_usize_t cap, gates_usize_t *needed);
[[nodiscard]] gates_err_t gates_window_set_placement(gates_window_t *win, gates_str_t text);
/* Native dialogs (0.5.0): the platform's own modal dialogs. The call
 * returns when the person answers; the window's menus close first. A cancel is
 * GATES_OK with *len = 0 (or chosen = false). Paths are UTF-8; *len receives the
 * path's length, and a too small cap returns OVERFLOW and writes nothing.
 * filters: "Text files|*.txt|All files|*.*" (name|patterns pairs, patterns
 * separated by ';'). folder: where it starts (empty: the platform's choice);
 * name: a save dialog's suggested file name. A save dialog asks before
 * overwriting. Run them from a command or event handler, never while painting. */
typedef struct gates_file_dialog_t {
    gates_str_t title;
    gates_str_t filters;
    gates_str_t folder;
    gates_str_t name;
} gates_file_dialog_t;
[[nodiscard]] gates_err_t gates_window_open_file(gates_window_t *win, const gates_file_dialog_t *desc,
                                                 gates_u8 *buf, gates_usize_t cap, gates_usize_t *len);
/* Several files at once (0.8.0): each path is followed by a NUL byte in buf,
 * *len receives all of their bytes (OVERFLOW writes nothing), *count how many
 * paths; a cancel gives *len = 0 and *count = 0. */
[[nodiscard]] gates_err_t gates_window_open_files(gates_window_t *win, const gates_file_dialog_t *desc,
                                                  gates_u8 *buf, gates_usize_t cap, gates_usize_t *len,
                                                  gates_u32 *count);
[[nodiscard]] gates_err_t gates_window_save_file(gates_window_t *win, const gates_file_dialog_t *desc,
                                                 gates_u8 *buf, gates_usize_t cap, gates_usize_t *len);
[[nodiscard]] gates_err_t gates_window_choose_folder(gates_window_t *win, const gates_file_dialog_t *desc,
                                                     gates_u8 *buf, gates_usize_t cap, gates_usize_t *len);
/* *color is the starting colour and, when chosen, the answer (alpha kept). */
[[nodiscard]] gates_err_t gates_window_choose_color(gates_window_t *win, gates_color_t *color, bool *chosen);

typedef enum gates_message_buttons_t {
    GATES_MESSAGE_OK = 0,
    GATES_MESSAGE_OK_CANCEL,
    GATES_MESSAGE_YES_NO,
    GATES_MESSAGE_YES_NO_CANCEL,
} gates_message_buttons_t;
typedef enum gates_message_icon_t {
    GATES_MESSAGE_INFO = 0,
    GATES_MESSAGE_WARNING,
    GATES_MESSAGE_ERROR,
    GATES_MESSAGE_QUESTION,
} gates_message_icon_t;
typedef enum gates_answer_t {
    GATES_ANSWER_NONE = 0,       /* the message could not be shown */
    GATES_ANSWER_OK,
    GATES_ANSWER_CANCEL,         /* also Escape or the close box */
    GATES_ANSWER_YES,
    GATES_ANSWER_NO,
} gates_answer_t;
/* A message box, modal to the window: the button pressed, or
 * GATES_ANSWER_NONE when it could not be shown. */
gates_answer_t gates_window_message(gates_window_t *win, gates_str_t title, gates_str_t text,
                                    gates_message_buttons_t buttons, gates_message_icon_t icon);

/* GATES_ACCESS_STRICT=1 in the environment audits the tree after every layout
 * (gates_access_audit) and shows the issue count in the title. */

#endif /* GATES_WINDOW_H */
