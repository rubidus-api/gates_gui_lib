/* gates_gui_lib — window/surface boundary (RFC-0001 §3 gates_surface).
 * Opaque, platform-free header. A window owns its retained tree (§7); in
 * Phase 1 painting is driven by the paint callback emitting draw commands —
 * widgets take over draw-list generation in Phase 2. */
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
} gates_window_callbacks_t;

[[nodiscard]] gates_err_t gates_window_create(gates_app_t *app,
                                              const gates_window_desc_t *desc,
                                              const gates_window_callbacks_t *callbacks,
                                              gates_window_t **out_window);
void gates_window_destroy(gates_window_t *win);

/* The window's retained node tree (owned by the window; §7). */
gates_tree_t *gates_window_tree(gates_window_t *win);

/* Theme (plan-0013). SYSTEM (the default) follows the desktop: high contrast
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

/* Logical units (plan-0013): the window scales its drawing to the monitor's DPI
 * and converts pointer positions back, so the tree never sees device pixels.
 * GATES_FORCE_DPI=<dpi> in the environment forces a DPI (diagnostics). */
gates_size_t gates_window_client_size(const gates_window_t *win);
void gates_window_request_repaint(gates_window_t *win);

/* Accessibility (plan-0014). The window is a UI Automation provider: screen
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
/* GATES_ACCESS_STRICT=1 in the environment audits the tree after every layout
 * (gates_access_audit) and shows the issue count in the title. */

#endif /* GATES_WINDOW_H */
