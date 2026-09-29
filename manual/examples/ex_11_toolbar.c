/* manual example (host): a toolbar, a status bar and a tooltip.
 * expect: pasted 1, focus kept; tooltip "Paste (Ctrl+V)" after 500 ms; status Pasted */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { CMD_CUT = 1, CMD_PASTE };

typedef struct app_t {
    gates_node_t status;
    int pasted;
} app_t;

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    if (id == CMD_PASTE) {
        a->pasted++;
        (void)gates_widget_set_text(tree, a->status, GATES_STR("Pasted"));
    }
}

/* A test clock: a window's tree has a real one. */
static gates_u64 now_ms;
static gates_u64 clock_now(void *ctx) { (void)ctx; return now_ms; }
static void clock_changed(void *ctx) { (void)ctx; }

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_tree_set_clock(t, clock_now, clock_changed, nullptr);
    gates_node_t root = gates_tree_root(t), bar, body, status_bar;
    app_t a = {0};
    gates_command_desc_t cmds[] = {
        { .id = CMD_CUT, .label = GATES_STR("Cu&t"), .shortcut = { .key = GATES_KEY_X, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = &a },
        { .id = CMD_PASTE, .label = GATES_STR("&Paste"), .shortcut = { .key = GATES_KEY_V, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = &a },
    };
    if (!gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_command_register(t, root, &cmds[0])) ||
        !gates_is_ok(gates_command_register(t, root, &cmds[1])) ||
        !gates_is_ok(gates_toolbar_create(t, root, root, &bar)) ||
        !gates_is_ok(gates_toolbar_add(t, bar, CMD_CUT)) ||
        !gates_is_ok(gates_toolbar_add(t, bar, CMD_PASTE)) ||
        !gates_is_ok(gates_textbox_create(t, root, GATES_STR(""), 30, &body)) ||
        !gates_is_ok(gates_node_set_access_name(t, body, GATES_STR("Text"))) ||
        !gates_is_ok(gates_layout_set_child_grow(t, body, 1)) ||
        !gates_is_ok(gates_statusbar_create(t, root, &status_bar)) ||
        !gates_is_ok(gates_statusbar_add(t, status_bar, GATES_STR("Ready"), 1, &a.status)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 300 }, gates_text_backend_builtin()))) {
        return 1;
    }
    gates_tree_set_focus(t, body);

    /* Find the Paste button through the accessibility model: item 2 of the bar. */
    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, bar, 2, &info))) return 1;
    gates_point_t p = { info.bounds.x + info.bounds.w / 2, info.bounds.y + info.bounds.h / 2 };

    /* Rest the pointer on it: the tooltip shows after GATES_TOOLTIP_DELAY_MS. */
    gates_pointer_event_t move = { .action = GATES_POINTER_MOVE, .pos = p };
    (void)gates_input_pointer(t, &move);
    now_ms += GATES_TOOLTIP_DELAY_MS;
    (void)gates_tree_run_timers(t);
    gates_str_t tip = {0};
    char tip_text[64] = "";
    if (gates_tooltip_shown(t, nullptr, nullptr, &tip, nullptr) && tip.size < sizeof tip_text) {
        memcpy(tip_text, tip.ptr, tip.size);
    }

    /* A click invokes the command; the focus stays in the text box. */
    gates_pointer_event_t down = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT, .pos = p };
    gates_pointer_event_t up = { .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT, .pos = p };
    (void)gates_input_pointer(t, &down);
    (void)gates_input_pointer(t, &up);
    (void)gates_tree_dispatch_events(t, 0);

    gates_str_t s = gates_widget_text(t, a.status);
    printf("pasted %d, focus %s; tooltip \"%s\" after %u ms; status %.*s\n", a.pasted,
           gates_node_eq(gates_tree_focus(t), body) ? "kept" : "moved", tip_text, GATES_TOOLTIP_DELAY_MS,
           (int)s.size, (const char *)s.ptr);
    gates_tree_destroy(t);
    return 0;
}
