/* app_logview - reference application: a live log fed by a worker thread.
 *
 * Interaction: a producer thread writes log lines in batches and posts them to
 * the UI (gates/post.h); the UI appends them to a bounded log (2000 lines,
 * 64 KiB), dropping and counting the oldest. The producer never waits for the
 * UI: when the queue is full the post answers FULL and the producer skips that
 * batch and counts it; when the app shuts down the post answers CLOSED and the
 * producer releases its sender and ends. While the view shows the end it
 * follows new lines; scrolling up stops following, End (or Follow) resumes.
 * Selecting a line shows it in full below. A repeating timer refreshes the
 * status line four times a second.
 *
 * Shows: Run/Pause (F5) and Flood (F6, no pause between batches: the queue
 * fills and batches are skipped) for the worker, Follow (F7, checked while
 * following), Clear (F8); the detail field; lines kept, dropped, batches the
 * producer had to skip; closing the window while the worker floods.
 *
 * Field check (T035): lines arrive by themselves and the view follows; F5
 * pauses and resumes; F6 floods: "skipped" grows, input (scrolling, clicking,
 * F-keys) still answers at once; wheel up stops following while lines keep
 * arriving; clicking a line shows it; End follows again; resizing the window
 * while lines arrive keeps them coming; closing the window while flooding ends
 * the program cleanly. */
#include <gates/app.h>
#include <gates/window.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/view.h>
#include <gates/post.h>
#include <gates/timer.h>
#include <gates/ui.h>
#include <gates/access.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>   /* the worker thread is the application's own business */
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)
#define BATCH 50
#define LINE_MAX_BYTES 96
#define MSG_LINES 1u

enum { CMD_RUN = 1, CMD_FLOOD, CMD_FOLLOW, CMD_CLEAR };

/* One batch of lines: owned by gates after a successful post, freed by release. */
typedef struct batch_t {
    unsigned count;
    unsigned short len[BATCH];
    char text[BATCH][LINE_MAX_BYTES];
} batch_t;

static void release_batch(void *payload, void *ctx) {
    (void)ctx;
    free(payload); /* may run on the worker or at shutdown: touches no UI state */
}

/* State shared with the worker: only these atomics and the sender. */
typedef struct worker_t {
    gates_sender_t *sender;
    gates_target_t target;
    atomic_bool running;
    atomic_bool flood;
    atomic_bool stop;
    atomic_uint skipped;         /* batches refused with FULL */
    atomic_uint produced;
} worker_t;

typedef struct app_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t root, log, detail, status;
    worker_t w;
    unsigned failures;           /* lines the log could not keep (out of memory) */
} app_t;

static const char *const levels[] = { "INFO ", "INFO ", "INFO ", "DEBUG", "WARN ", "ERROR" };
static const char *const what[] = {
    "request served", "cache refreshed", "user signed in", "slow query",
    "retrying connection", "disk usage above 80%", "job finished", "config reloaded",
};

/* -- the worker: formats batches and posts them, never touching the tree ------------------ */

static DWORD WINAPI worker_main(void *arg) {
    worker_t *w = arg;
    unsigned seq = 0, seed = 7u;
    while (!atomic_load(&w->stop)) {
        if (!atomic_load(&w->running)) {
            Sleep(20);
            continue;
        }
        batch_t *b = malloc(sizeof *b);
        if (b == nullptr) {
            Sleep(20);
            continue;
        }
        b->count = BATCH;
        for (unsigned i = 0; i < BATCH; i++) {
            seed = seed * 1103515245u + 12345u;
            unsigned x = (seed >> 8) & 0xFFFFu;
            int n = snprintf(b->text[i], LINE_MAX_BYTES, "%07u  %s  %s (worker %u)", ++seq,
                             levels[x % 6], what[(x >> 3) % 8], (x >> 6) % 16);
            b->len[i] = (unsigned short)(n > 0 ? n : 0);
        }
        gates_message_t m = { .target = w->target, .kind = MSG_LINES, .payload = b,
                              .bytes = sizeof *b, .release = release_batch };
        gates_err_t err = gates_sender_post(w->sender, &m);
        if (err == GATES_POST_CLOSED) {
            free(b);
            break; /* the app is shutting down */
        }
        if (!gates_is_ok(err)) {
            free(b); /* FULL: still ours; a log producer skips rather than waits */
            atomic_fetch_add(&w->skipped, 1);
        } else {
            atomic_fetch_add(&w->produced, BATCH);
        }
        if (!atomic_load(&w->flood)) {
            Sleep(40);
        }
    }
    gates_sender_release(w->sender);
    return 0;
}

/* -- the UI side ---------------------------------------------------------------------------- */

static void status(app_t *a) {
    char buf[200];
    int n = snprintf(buf, sizeof buf, "%llu kept   %llu dropped   %u batches skipped   %s%s%s",
                     (unsigned long long)gates_log_count(a->tree, a->log),
                     (unsigned long long)gates_log_dropped(a->tree, a->log),
                     atomic_load(&a->w.skipped),
                     atomic_load(&a->w.running) ? (atomic_load(&a->w.flood) ? "flooding" : "running")
                                                : "paused",
                     gates_log_following(a->tree, a->log) ? ", following" : "",
                     a->failures > 0 ? "   (out of memory: lines lost)" : "");
    if (n > 0) {
        (void)gates_widget_set_text(a->tree, a->status,
                                    (gates_str_t){ .ptr = (const gates_u8 *)buf,
                                                   .size = (gates_usize_t)n });
    }
    (void)gates_command_set_checked(a->tree, a->root, CMD_FOLLOW, gates_log_following(a->tree, a->log));
    (void)gates_command_set_checked(a->tree, a->root, CMD_RUN, atomic_load(&a->w.running));
    (void)gates_command_set_checked(a->tree, a->root, CMD_FLOOD, atomic_load(&a->w.flood));
}

static void on_lines(gates_tree_t *tree, gates_node_t node, gates_u32 kind, void *payload,
                     gates_usize_t bytes, void *user) {
    (void)bytes;
    app_t *a = user;
    if (kind != MSG_LINES) return;
    const batch_t *b = payload; /* borrowed: gates frees it after this returns */
    for (unsigned i = 0; i < b->count; i++) {
        if (!gates_is_ok(gates_log_append(tree, node, (gates_str_t){ .ptr = (const gates_u8 *)b->text[i],
                                                                     .size = b->len[i] }))) {
            a->failures++;
        }
    }
}

static void on_tick(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)tree; (void)node; (void)id;
    status(user);
}

static void show_detail(app_t *a) {
    gates_str_t line = gates_log_line(a->tree, a->log, gates_view_selected(a->tree, a->log));
    (void)gates_textbox_set_text(a->tree, a->detail, line.size > 0 ? line : GATES_STR("(no line selected)"));
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    switch (id) {
    case CMD_RUN: atomic_store(&a->w.running, !atomic_load(&a->w.running)); break;
    case CMD_FLOOD:
        atomic_store(&a->w.flood, !atomic_load(&a->w.flood));
        if (atomic_load(&a->w.flood)) atomic_store(&a->w.running, true);
        break;
    case CMD_FOLLOW:
        (void)gates_log_set_following(tree, a->log, !gates_log_following(tree, a->log));
        break;
    case CMD_CLEAR: gates_log_clear(tree, a->log); a->failures = 0; show_detail(a); break;
    default: break;
    }
    status(a);
}

static void on_log(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    if (ev->kind == GATES_EVENT_SELECTION_CHANGED) show_detail(user);
    if (ev->kind == GATES_EVENT_FOLLOW_CHANGED) status(user); /* scrolled away from the end, or back */
}

static gates_err_t build_ui(app_t *a) {
    gates_tree_t *t = a->tree;
    a->root = gates_tree_root(t);
    TRY(gates_layout_set(t, a->root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, a->root, 12));
    TRY(gates_layout_set_gap(t, a->root, 8));
    gates_log_desc_t d = { .max_lines = 2000, .max_bytes = 64 * 1024 };
    TRY(gates_log_create(t, a->root, &d, &a->log));
    TRY(gates_node_set_font(t, a->log, GATES_FONT_MONO)); /* log lines read best aligned */
    TRY(gates_node_set_access_name(t, a->log, GATES_STR("Log")));
    TRY(gates_layout_set_child_grow(t, a->log, 1));
    TRY(gates_widget_set_handler(t, a->log, on_log, a));
    TRY(gates_node_set_message_handler(t, a->log, on_lines, a));
    TRY(gates_textbox_create(t, a->root, GATES_STR(""), 50, &a->detail));
    TRY(gates_node_set_access_name(t, a->detail, GATES_STR("Selected line")));
    TRY(gates_textbox_set_read_only(t, a->detail, true));

    struct { gates_command_id_t id; const char *label; gates_key_t key; } cmds[] = {
        { CMD_RUN, "Run (F5)", GATES_KEY_F5 },
        { CMD_FLOOD, "Flood (F6)", GATES_KEY_F6 },
        { CMD_FOLLOW, "Follow (F7)", GATES_KEY_F7 },
        { CMD_CLEAR, "Clear (F8)", GATES_KEY_F8 },
    };
    gates_node_t row = GATES_NODE_NULL;
    TRY(gates_panel_create(t, a->root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        gates_command_desc_t c = { .id = cmds[i].id,
                                   .label = { .ptr = (const gates_u8 *)cmds[i].label,
                                              .size = strlen(cmds[i].label) },
                                   .shortcut = { .key = cmds[i].key },
                                   .enabled = true, .invoke = on_command, .user = a };
        TRY(gates_command_register(t, a->root, &c));
        gates_node_t b = GATES_NODE_NULL;
        TRY(gates_button_create(t, row, GATES_STR(""), nullptr, nullptr, &b));
        TRY(gates_button_set_command(t, b, a->root, cmds[i].id));
    }
    TRY(gates_label_create(t, a->root, GATES_STR(""), &a->status));
    gates_timer_id_t tick = 0;
    TRY(gates_timer_start(t, a->root, 250, true, on_tick, a, &tick));
    show_detail(a);
    status(a);
    gates_tree_set_focus(t, a->log);
    return GATES_OK;
}

int main(void) {
    static app_t a;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &a.app))) {
        return 1;
    }
    gates_window_desc_t desc = { .title = GATES_STR("gates: log viewer"), .size = { 640, 520 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(a.app, &desc, &(gates_window_callbacks_t){0}, &win))) {
        gates_app_destroy(a.app);
        return 1;
    }
    a.tree = gates_window_tree(win);
    gates_err_t err = build_ui(&a);
    HANDLE thread = nullptr;
    if (gates_is_ok(err)) {
        /* The worker gets its own sender reference before it starts. */
        err = gates_app_sender(a.app, &a.w.sender);
    }
    if (gates_is_ok(err)) {
        a.w.target = gates_target(a.tree, a.log);
        atomic_store(&a.w.running, true);
        thread = CreateThread(nullptr, 0, worker_main, &a.w, 0, nullptr);
        if (thread == nullptr) {
            gates_sender_release(a.w.sender);
            err = PROVEN_ERR_INVALID_STATE;
        }
    }
    if (!gates_is_ok(err)) {
        fprintf(stderr, "app_logview: start failed (%d)\n", (int)err);
    } else {
        gates_window_request_repaint(win);
        err = gates_app_run(a.app);
    }
    /* Shutdown: the worker may still be posting. Destroying the app closes the
     * sender (its posts answer CLOSED); stop covers a paused worker. */
    atomic_store(&a.w.stop, true);
    gates_window_destroy(win);
    gates_app_destroy(a.app);
    if (thread != nullptr) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    return gates_is_ok(err) ? 0 : 1;
}
