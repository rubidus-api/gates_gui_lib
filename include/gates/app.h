/* gates_gui_lib - application/platform boundary.
 * Opaque, platform-free header; the implementation lives in src/platform/.
 * Win32 backend: message pump; posting from workers and timers (gates/post.h,
 * gates/timer.h). */
#ifndef GATES_APP_H
#define GATES_APP_H

#include <gates/types.h>

typedef struct gates_app gates_app_t;

typedef struct gates_app_desc_t {
    /* Zeroed allocator -> proven heap allocator. With workers posting it must
     * be thread-safe (the default is). */
    gates_allocator_t allocator;
    /* Posting queue limits (gates/post.h); 0 -> 1024 messages, 1 MiB. */
    gates_u32 post_max_messages;
    gates_usize_t post_max_bytes;
    /* The face of GATES_FONT_UI (0.12.0), UTF-8, e.g. "Malgun Gothic"; copied. Empty: the
     * platform's UI font (Win32: the system message font). The size stays the platform's;
     * a face that is not installed falls back to the platform's. GATES_FONT_MONO is not
     * affected. */
    gates_str_t ui_font;
} gates_app_desc_t;

/* The application: one per process, made and used on the UI thread. */
[[nodiscard]] gates_err_t gates_app_create(const gates_app_desc_t *desc,
                                           gates_app_t **out_app);
void gates_app_destroy(gates_app_t *app);

/* Runs the platform event pump on the calling (UI) thread; returns after
 * gates_app_quit() or when the last window closes. */
[[nodiscard]] gates_err_t gates_app_run(gates_app_t *app);
void gates_app_quit(gates_app_t *app);

/* Changes the face of GATES_FONT_UI while running (0.12.0), as ui_font does at
 * creation - a settings screen's "font" choice; empty goes back to the
 * platform's. Every window measures and lays out again. NOT_FOUND when the face
 * is not installed (the platform's is used then); INVALID_ARG for a name that is
 * not UTF-8 or too long for the platform. A named face (gates_font_named) set on
 * a node stays as it is. */
[[nodiscard]] gates_err_t gates_app_set_ui_font(gates_app_t *app, gates_str_t face);

#endif /* GATES_APP_H */
