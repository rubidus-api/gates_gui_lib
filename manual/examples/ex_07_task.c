/* manual example (windows): a background task counts primes while the window
 * shows its progress and a Cancel button stops it. The work never touches the
 * tree: it reports, polls for cancellation and returns; the UI hears both. */
#include <gates/gates.h>

#include <stdio.h>

#define LIMIT 3000000u

typedef struct count_t {
    gates_u32 primes;            /* written by the work, read in on_done */
    gates_node_t bar, status, cancel;
    gates_task_t *task;
} count_t;

static bool is_prime(gates_u32 n) {
    if (n < 2) return false;
    for (gates_u32 d = 2; d * d <= n; d++) {
        if (n % d == 0) return false;
    }
    return true;
}

/* On the worker thread. */
static gates_err_t count_primes(gates_task_t *task, void *user) {
    count_t *c = user;
    for (gates_u32 n = 0; n < LIMIT; n++) {
        if (is_prime(n)) c->primes++;
        if (n % (LIMIT / 100) == 0) {
            if (gates_task_cancelled(task)) return PROVEN_ERR_EOF;
            char text[48];
            int len = snprintf(text, sizeof text, "%u primes so far", c->primes);
            (void)gates_task_report(task, (gates_u32)((gates_u64)n * 1000 / LIMIT),
                                    (gates_str_t){ .ptr = (const gates_u8 *)text, .size = (gates_usize_t)len });
        }
    }
    return GATES_OK;
}

/* On the UI thread. */
static void on_progress(gates_tree_t *tree, gates_task_t *task, gates_u32 permille, gates_str_t text, void *user) {
    (void)task;
    count_t *c = user;
    (void)gates_progress_set_value(tree, c->bar, (gates_i32)permille);
    (void)gates_widget_set_text(tree, c->status, text);
}

static void on_done(gates_tree_t *tree, gates_task_t *task, gates_err_t result, bool cancelled, void *user) {
    (void)task;
    count_t *c = user;
    char text[64];
    int len = cancelled ? snprintf(text, sizeof text, "Cancelled after %u primes", c->primes)
              : gates_is_ok(result) ? snprintf(text, sizeof text, "%u primes below %u", c->primes, LIMIT)
                                    : snprintf(text, sizeof text, "Stopped");
    (void)gates_widget_set_text(tree, c->status, (gates_str_t){ .ptr = (const gates_u8 *)text, .size = (gates_usize_t)len });
    (void)gates_widget_set_disabled(tree, c->cancel, true);
    c->task = nullptr;
}

static void on_cancel(gates_tree_t *tree, gates_node_t node, void *user) {
    (void)tree;
    (void)node;
    count_t *c = user;
    if (c->task != nullptr) gates_task_cancel(c->task);
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) return 1;
    gates_window_t *win = nullptr;
    gates_window_desc_t desc = { .title = GATES_STR("Primes"), .size = { 380, 160 } };
    if (!gates_is_ok(gates_window_create(app, &desc, nullptr, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    gates_tree_t *t = gates_window_tree(win);
    gates_node_t root = gates_tree_root(t);
    count_t c = {0};
    gates_err_t err = gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_layout_set_padding(t, root, 12);
    if (gates_is_ok(err)) err = gates_layout_set_gap(t, root, 8);
    if (gates_is_ok(err)) err = gates_progress_create(t, root, 0, &c.bar);
    if (gates_is_ok(err)) err = gates_node_set_access_name(t, c.bar, GATES_STR("Progress"));
    if (gates_is_ok(err)) err = gates_label_create(t, root, GATES_STR("Starting..."), &c.status);
    if (gates_is_ok(err)) err = gates_button_create(t, root, GATES_STR("&Cancel"), on_cancel, &c, &c.cancel);
    gates_task_desc_t task = { .work = count_primes, .work_user = &c, .on_progress = on_progress,
                               .on_done = on_done, .ui_user = &c };
    if (gates_is_ok(err)) err = gates_task_start(t, &task, &c.task);
    if (gates_is_ok(err)) err = gates_app_run(app);
    gates_window_destroy(win); /* a task still running is cancelled and waited for */
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
