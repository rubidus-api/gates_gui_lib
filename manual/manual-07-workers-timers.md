# Chapter 7 - Workers and timers

Headers: `gates/post.h`, `gates/timer.h`.

## Workers never touch the tree

The tree belongs to the UI thread. A worker thread - reading files, talking to a device,
computing - hands its results over by posting messages. On the UI thread, before the worker
starts, the program takes the app's sender (`gates_app_sender`) and builds a target: the tree
and the node that should receive the results (`gates_target`). The worker posts messages
carrying a kind and a payload; the UI thread delivers them at a safe point to the node's
message handler (`gates_node_set_message_handler`) and then releases the payload.

<!-- example: manual/examples/ex_08_worker.c -->
```c
/* manual example (windows): a worker thread feeds a log through the sender.
 * The worker never touches the tree; it posts lines, and the UI thread
 * delivers them to the log's message handler. */
#include <gates/gates.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum { MSG_LINE = 1 };

typedef struct job_t {
    gates_sender_t *sender;      /* retained for the worker */
    gates_target_t target;       /* built on the UI thread */
} job_t;

static void release_line(void *payload, void *ctx) {
    (void)ctx;
    free(payload); /* may run on any thread: only frees */
}

static DWORD WINAPI worker(void *arg) {
    job_t *job = arg;
    for (int i = 1; i <= 1000; i++) {
        char *line = malloc(40);
        if (line == nullptr) break;
        int n = snprintf(line, 40, "line %d from the worker", i);
        gates_message_t msg = { .target = job->target, .kind = MSG_LINE, .payload = line,
                                .bytes = (gates_usize_t)n, .release = release_line };
        gates_err_t err = gates_sender_post(job->sender, &msg);
        if (err == GATES_POST_FULL) { free(line); Sleep(5); i--; continue; } /* the caller keeps it */
        if (err == GATES_POST_CLOSED) { free(line); break; }                 /* the app is closing */
        if (!gates_is_ok(err)) { free(line); break; }
    }
    gates_sender_release(job->sender);
    return 0;
}

static void on_line(gates_tree_t *tree, gates_node_t node, gates_u32 kind, void *payload,
                    gates_usize_t bytes, void *user) {
    (void)user;
    if (kind == MSG_LINE) (void)gates_log_append(tree, node, (gates_str_t){ .ptr = payload, .size = bytes });
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) return 1;
    gates_window_t *win = nullptr;
    gates_window_desc_t desc = { .title = GATES_STR("Worker"), .size = { 420, 300 } };
    if (!gates_is_ok(gates_window_create(app, &desc, nullptr, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    gates_tree_t *t = gates_window_tree(win);
    gates_node_t root = gates_tree_root(t), log;
    job_t job = {0};
    gates_err_t err = gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_log_create(t, root, &(gates_log_desc_t){ .max_lines = 500 }, &log);
    if (gates_is_ok(err)) err = gates_layout_set_child_grow(t, log, 1);
    if (gates_is_ok(err)) err = gates_node_set_access_name(t, log, GATES_STR("Worker output"));
    if (gates_is_ok(err)) err = gates_node_set_message_handler(t, log, on_line, nullptr);
    if (gates_is_ok(err)) err = gates_app_sender(app, &job.sender);
    HANDLE thread = nullptr;
    if (gates_is_ok(err)) {
        job.target = gates_target(t, log);
        thread = CreateThread(nullptr, 0, worker, &job, 0, nullptr);
        if (thread == nullptr) gates_sender_release(job.sender);
    }
    if (gates_is_ok(err)) err = gates_app_run(app);
    gates_window_destroy(win);
    gates_app_destroy(app); /* closes the sender: the worker sees CLOSED and stops */
    if (thread != nullptr) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    return gates_is_ok(err) ? 0 : 1;
}
```

The rules that make this safe:

- Posting never blocks and never allocates. A full queue answers `GATES_POST_FULL`: the worker
  keeps its payload and tries later, drops it, or merges it into the next one. A closed queue
  answers `GATES_POST_CLOSED`: the app is shutting down, stop and release the sender.
- When a post succeeds, gates owns the payload and releases it exactly once - after delivery,
  when the target node is gone, when a newer snapshot replaces it (`replaceable`), or at
  shutdown. When a post fails, the worker still owns it.
- Release functions may run on any thread; they only free.
- The queue is bounded (1024 messages and 1 MiB of payload by default, `gates_app_desc_t`),
  and at most 64 messages are delivered per turn, so a flood never starves input and painting.
- A sender stays valid on every thread until its last reference is released, even after the
  app is gone.

## Timers

A timer belongs to a node and runs its callback on the UI thread after an interval, once or
repeatedly. Cancelling is immediate: a cancelled timer never fires again, and destroying its
node or tree cancels it. A late repeating timer fires once and is rescheduled from now, never
in a burst. An idle window runs no platform timer at all.

A window's tree has a clock. A bare tree - in a test - gets one from the program, which then
decides when time passes:

<!-- example: manual/examples/ex_08_timers.c -->
```c
/* manual example (host): a repeating timer, driven by a test clock.
 * expect: 3 ticks, then cancelled */
#include <gates/gates.h>

#include <stdio.h>

static gates_u64 now_ms;
static gates_u64 clock_now(void *ctx) { (void)ctx; return now_ms; }

static void on_tick(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)node;
    int *ticks = user;
    if (++*ticks == 3) (void)gates_timer_cancel(tree, id); /* a callback may cancel itself */
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    /* A window's tree has a clock; a bare tree gets one from the program. */
    gates_tree_set_clock(t, clock_now, nullptr, nullptr);
    int ticks = 0;
    gates_timer_id_t id;
    if (!gates_is_ok(gates_timer_start(t, gates_tree_root(t), 100, true, on_tick, &ticks, &id))) return 1;
    for (int step = 0; step < 10; step++) {
        now_ms += 100;
        (void)gates_tree_run_timers(t); /* the window does this when its timer fires */
    }
    printf("%d ticks, then %s\n", ticks, gates_timer_active(t, id) ? "still running" : "cancelled");
    gates_tree_destroy(t);
    return 0;
}
```

Before animating, ask `gates_window_reduced_motion`: when the person has turned animation effects
off, show the end state instead.
