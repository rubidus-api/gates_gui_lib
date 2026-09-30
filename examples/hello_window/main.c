/* hello_window - first pixels for gates_gui_lib.
 *
 * Demonstrates: window create, message pump, draw list -> software renderer ->
 * GDI present, unified pointer input, keyboard.
 *
 * Manual checklist:
 *   - window opens at 640x480 with a light background and a title;
 *   - a blue 40x40 square tracks the mouse; a border marks the client edge;
 *   - left click toggles the square color blue <-> orange;
 *   - wheel grows/shrinks the square (10..200 px);
 *   - resize keeps everything repainting without artifacts;
 *   - ESC (or closing the window) exits cleanly.
 */
#include <gates/app.h>
#include <gates/window.h>

typedef struct hello_state_t {
    gates_app_t *app;
    gates_point_t mouse;
    gates_i32 square;
    bool orange;
} hello_state_t;

static void on_paint(gates_window_t *win, gates_draw_list_t *dl, void *user) {
    hello_state_t *st = user;
    gates_size_t client = gates_window_client_size(win);

    /* Background. */
    (void)gates_draw_rect(dl, (gates_rect_t){ 0, 0, client.w, client.h },
                          GATES_RGB(240, 240, 235));
    /* Client-edge border + a diagonal to prove lines render. */
    (void)gates_draw_border(dl, (gates_rect_t){ 0, 0, client.w, client.h }, 4,
                            GATES_RGB(60, 60, 80));
    (void)gates_draw_line(dl, (gates_point_t){ 8, 8 },
                          (gates_point_t){ client.w - 9, client.h - 9 },
                          GATES_RGB(180, 180, 190));

    /* Mouse-tracking square, clipped to the client area. */
    gates_color_t fill = st->orange ? GATES_RGB(230, 140, 30) : GATES_RGB(40, 90, 220);
    gates_i32 half = st->square / 2;
    (void)gates_draw_clip_push(dl, (gates_rect_t){ 4, 4, client.w - 8, client.h - 8 });
    (void)gates_draw_rect(dl, (gates_rect_t){ st->mouse.x - half, st->mouse.y - half,
                                              st->square, st->square },
                          GATES_RGBA(fill.r, fill.g, fill.b, 200));
    (void)gates_draw_clip_pop(dl);
}

static void on_pointer(gates_window_t *win, const gates_pointer_event_t *ev, void *user) {
    hello_state_t *st = user;
    st->mouse = ev->pos;
    if (ev->action == GATES_POINTER_DOWN && ev->button == GATES_BUTTON_LEFT) {
        st->orange = !st->orange;
    }
    if (ev->action == GATES_POINTER_WHEEL) {
        st->square += (gates_i32)(ev->wheel.y * 10.0f);
        if (st->square < 10) st->square = 10;
        if (st->square > 200) st->square = 200;
    }
    gates_window_request_repaint(win);
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    hello_state_t *st = user;
    (void)win;
    if (ev->down && ev->vkey == 0x1B) { /* VK_ESCAPE */
        gates_app_quit(st->app);
    }
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) {
        return 1;
    }

    hello_state_t st = { .app = app, .mouse = { 320, 240 }, .square = 40 };
    gates_window_callbacks_t cb = {
        .on_paint = on_paint,
        .on_pointer = on_pointer,
        .on_key = on_key,
        .user_data = &st,
    };
    gates_window_t *win = nullptr;
    gates_window_desc_t desc = {
        .title = GATES_STR("gates hello_window"),
        .size = { 640, 480 },
    };
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }

    gates_err_t err = gates_app_run(app);

    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
