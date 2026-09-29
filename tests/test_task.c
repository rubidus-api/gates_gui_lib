/* T053: background tasks (plan-0021 stage 3) - work on a thread, progress
 * coalesced to the UI thread, the result and the end reported once,
 * cancellation, a full queue, start failures, and a tree destroyed while
 * tasks run (cancelled and joined). Built with -pthread; also run under
 * ThreadSanitizer. */
#include <gates/task.h>
#include <gates/post.h>
#include "gates_test.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* -- pthreads behind the sender's sync and the task threads ---------------------------- */

static gates_err_t px_init(void **lock) {
    pthread_mutex_t *m = malloc(sizeof *m);
    if (m == nullptr) return PROVEN_ERR_NOMEM;
    pthread_mutex_init(m, nullptr);
    *lock = m;
    return GATES_OK;
}
static void px_fini(void *lock) { pthread_mutex_destroy(lock); free(lock); }
static void px_lock(void *lock) { pthread_mutex_lock(lock); }
static void px_unlock(void *lock) { pthread_mutex_unlock(lock); }

typedef struct thread_box_t {
    pthread_t id;
    void (*fn)(void *);
    void *arg;
} thread_box_t;

static atomic_int started, joined;
static bool refuse_start;

static void *run_box(void *p) {
    thread_box_t *b = p;
    b->fn(b->arg);
    return nullptr;
}

static gates_err_t th_start(void *ctx, void (*fn)(void *), void *arg, void **out) {
    (void)ctx;
    if (refuse_start) return PROVEN_ERR_IO;
    thread_box_t *b = malloc(sizeof *b);
    if (b == nullptr) return PROVEN_ERR_NOMEM;
    b->fn = fn;
    b->arg = arg;
    if (pthread_create(&b->id, nullptr, run_box, b) != 0) {
        free(b);
        return PROVEN_ERR_IO;
    }
    atomic_fetch_add(&started, 1);
    *out = b;
    return GATES_OK;
}

static void th_join(void *ctx, void *thread) {
    (void)ctx;
    thread_box_t *b = thread;
    pthread_join(b->id, nullptr);
    free(b);
    atomic_fetch_add(&joined, 1);
}

static void th_yield(void *ctx) {
    (void)ctx;
    sched_yield();
}

static const gates_threads_t threads = { .start = th_start, .join = th_join, .yield = th_yield };

static void nap(void) {
    struct timespec ts = { 0, 200000 }; /* 0.2 ms */
    nanosleep(&ts, nullptr);
}

typedef struct world_t {
    gates_sender_t *sender;
    gates_tree_t *tree;
} world_t;

static void world(world_t *w, gates_u32 max_messages) {
    gates_sender_desc_t d = { .sync = { px_init, px_fini, px_lock, px_unlock }, .max_messages = max_messages };
    GT_ASSERT_OK(gates_sender_create(&d, &w->sender));
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &w->tree));
    GT_ASSERT_OK(gates_sender_attach(w->sender, w->tree));
    gates_tree_set_threads(w->tree, &threads);
}

static void world_end(world_t *w) {
    gates_tree_destroy(w->tree);
    gates_sender_close(w->sender);
    gates_sender_release(w->sender);
}

/* The UI loop: deliver until the tree has no tasks left (bounded). */
static void run_until_done(world_t *w) {
    for (int i = 0; i < 200000 && gates_tree_task_count(w->tree) > 0; i++) {
        if (gates_sender_dispatch(w->sender, 0) == 0) nap();
    }
    (void)gates_sender_dispatch(w->sender, 0);
}

/* -- a counting job ---------------------------------------------------------------------- */

typedef struct job_t {
    int steps;
    bool wait_for_cancel;
    gates_err_t result;
    int produced;                /* written by the work, read in on_done */
    atomic_bool ran;
    atomic_bool finished;        /* the work is about to return */
    /* UI side */
    int progress_calls;
    gates_u32 last_permille;
    gates_u32 prev_permille;
    bool monotonic;
    char last_text[32];
    int done_calls;
    gates_err_t done_result;
    bool done_cancelled;
    int produced_seen;
    gates_task_t *task;
    gates_tree_t *tree_seen;
} job_t;

static gates_err_t count_work(gates_task_t *task, void *user) {
    job_t *j = user;
    atomic_store(&j->ran, true);
    if (j->wait_for_cancel) {
        while (!gates_task_cancelled(task)) sched_yield();
        return PROVEN_ERR_EOF;
    }
    for (int i = 1; i <= j->steps; i++) {
        char text[32];
        int n = snprintf(text, sizeof text, "step %d", i);
        (void)gates_task_report(task, (gates_u32)(i * 1000 / j->steps), (gates_str_t){ (const gates_u8 *)text, (gates_usize_t)n });
        j->produced = i;
    }
    (void)gates_task_report(task, 5000, GATES_STR("over")); /* reported as 1000 */
    atomic_store(&j->finished, true);
    return j->result;
}

static void on_progress(gates_tree_t *tree, gates_task_t *task, gates_u32 permille, gates_str_t text, void *user) {
    job_t *j = user;
    (void)tree;
    GT_ASSERT(task == j->task);
    if (j->progress_calls > 0 && permille < j->prev_permille) j->monotonic = false;
    j->prev_permille = permille;
    j->last_permille = permille;
    size_t n = text.size < 31 ? text.size : 31;
    memcpy(j->last_text, text.ptr, n);
    j->last_text[n] = 0;
    j->progress_calls++;
}

static void on_done(gates_tree_t *tree, gates_task_t *task, gates_err_t result, bool cancelled, void *user) {
    job_t *j = user;
    GT_ASSERT(task == j->task);
    j->tree_seen = tree;
    j->done_calls++;
    j->done_result = result;
    j->done_cancelled = cancelled;
    j->produced_seen = j->produced;
}

static gates_task_desc_t desc_for(job_t *j) {
    return (gates_task_desc_t){ .work = count_work, .work_user = j, .on_progress = on_progress, .on_done = on_done,
                                .ui_user = j };
}

static void test_progress_and_done(void) {
    world_t w;
    world(&w, 0);
    job_t j = { .steps = 50, .result = PROVEN_ERR_OVERFLOW, .monotonic = true };
    gates_task_desc_t d = desc_for(&j);
    GT_ASSERT_OK(gates_task_start(w.tree, &d, &j.task));
    GT_ASSERT(gates_tree_task_count(w.tree) == 1);
    run_until_done(&w);
    GT_ASSERT(gates_tree_task_count(w.tree) == 0);
    GT_ASSERT(j.done_calls == 1 && j.tree_seen == w.tree);
    GT_ASSERT(j.done_result == PROVEN_ERR_OVERFLOW && !j.done_cancelled);
    GT_ASSERT(j.produced_seen == 50); /* what the work wrote is visible */
    GT_ASSERT(j.progress_calls >= 1 && j.progress_calls <= 51); /* coalesced: never more than reported */
    GT_ASSERT(j.monotonic);
    /* The last report is shown before the end, clamped to 1000. */
    GT_ASSERT(j.last_permille == 1000 && strcmp(j.last_text, "over") == 0);
    GT_ASSERT(atomic_load(&joined) == atomic_load(&started));
    world_end(&w);
}

/* Reports the UI has not shown yet are replaced by the latest: one call. */
static void test_coalescing(void) {
    world_t w;
    world(&w, 0);
    job_t j = { .steps = 500, .result = GATES_OK, .monotonic = true };
    gates_task_desc_t d = desc_for(&j);
    GT_ASSERT_OK(gates_task_start(w.tree, &d, &j.task));
    while (!atomic_load(&j.finished)) sched_yield(); /* the UI is busy elsewhere */
    run_until_done(&w);
    GT_ASSERT(j.progress_calls == 1 && j.last_permille == 1000 && strcmp(j.last_text, "over") == 0);
    GT_ASSERT(j.done_calls == 1);
    world_end(&w);
}

static void test_cancel(void) {
    world_t w;
    world(&w, 0);
    job_t j = { .wait_for_cancel = true, .monotonic = true };
    gates_task_desc_t d = desc_for(&j);
    GT_ASSERT_OK(gates_task_start(w.tree, &d, &j.task));
    while (!atomic_load(&j.ran)) sched_yield();
    for (int i = 0; i < 50; i++) (void)gates_sender_dispatch(w.sender, 0);
    GT_ASSERT(j.done_calls == 0 && gates_tree_task_count(w.tree) == 1); /* still working */
    gates_task_cancel(j.task);
    run_until_done(&w);
    GT_ASSERT(j.done_calls == 1 && j.done_cancelled && j.done_result == PROVEN_ERR_EOF);
    gates_task_cancel(nullptr);
    GT_ASSERT(gates_task_cancelled(nullptr));
    world_end(&w);
}

/* Several tasks through a tiny queue: every end still arrives (the worker waits for room). */
static void test_full_queue(void) {
    world_t w;
    world(&w, 2);
    job_t jobs[6];
    for (int i = 0; i < 6; i++) {
        jobs[i] = (job_t){ .steps = 20 + i, .result = GATES_OK, .monotonic = true };
        gates_task_desc_t d = desc_for(&jobs[i]);
        GT_ASSERT_OK(gates_task_start(w.tree, &d, &jobs[i].task));
    }
    GT_ASSERT(gates_tree_task_count(w.tree) == 6);
    run_until_done(&w);
    for (int i = 0; i < 6; i++) {
        GT_ASSERT(jobs[i].done_calls == 1 && gates_is_ok(jobs[i].done_result));
        GT_ASSERT(jobs[i].produced_seen == 20 + i);
    }
    world_end(&w);
}

static gates_err_t quiet_work(gates_task_t *task, void *user) {
    (void)task;
    (void)user;
    return GATES_OK;
}

static void test_start_refusals(void) {
    world_t w;
    world(&w, 0);
    job_t j = {0};
    gates_task_desc_t d = desc_for(&j);
    gates_task_t *t = (gates_task_t *)&j;
    GT_ASSERT(gates_task_start(nullptr, &d, &t) == PROVEN_ERR_INVALID_ARG && t == nullptr);
    GT_ASSERT(gates_task_start(w.tree, nullptr, nullptr) == PROVEN_ERR_INVALID_ARG);
    gates_task_desc_t nowork = { .on_done = on_done };
    GT_ASSERT(gates_task_start(w.tree, &nowork, nullptr) == PROVEN_ERR_INVALID_ARG);
    /* The platform refuses the thread: nothing is left behind. */
    refuse_start = true;
    GT_ASSERT(gates_task_start(w.tree, &d, &t) == PROVEN_ERR_IO && t == nullptr);
    refuse_start = false;
    GT_ASSERT(gates_tree_task_count(w.tree) == 0);
    /* Without threads, or without a sender: UNSUPPORTED. */
    gates_tree_set_threads(w.tree, nullptr);
    GT_ASSERT(gates_task_start(w.tree, &d, nullptr) == PROVEN_ERR_UNSUPPORTED);
    gates_threads_t half = { .start = th_start, .join = th_join }; /* no yield: not installed */
    gates_tree_set_threads(w.tree, &half);
    GT_ASSERT(gates_task_start(w.tree, &d, nullptr) == PROVEN_ERR_UNSUPPORTED);
    gates_tree_set_threads(nullptr, &threads);
    gates_tree_t *lone = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &lone));
    gates_tree_set_threads(lone, &threads);
    GT_ASSERT(gates_task_start(lone, &d, nullptr) == PROVEN_ERR_UNSUPPORTED);
    GT_ASSERT(gates_tree_task_count(lone) == 0 && gates_tree_task_count(nullptr) == 0);
    gates_tree_destroy(lone);
    /* A task without UI callbacks still ends and is joined. */
    gates_tree_set_threads(w.tree, &threads);
    gates_task_desc_t bare = { .work = quiet_work };
    int before = atomic_load(&joined);
    GT_ASSERT_OK(gates_task_start(w.tree, &bare, nullptr));
    run_until_done(&w);
    GT_ASSERT(atomic_load(&joined) == before + 1);
    /* Reports with bad text are refused. */
    GT_ASSERT(gates_task_report(nullptr, 0, GATES_STR("")) == PROVEN_ERR_INVALID_ARG);
    world_end(&w);
}

/* Closing the window while tasks run: cancelled, joined, no on_done, nothing leaked. */
static void test_destroy_while_running(void) {
    world_t w;
    world(&w, 0);
    job_t a = { .wait_for_cancel = true }, b = { .wait_for_cancel = true };
    gates_task_desc_t da = desc_for(&a), db = desc_for(&b);
    GT_ASSERT_OK(gates_task_start(w.tree, &da, &a.task));
    GT_ASSERT_OK(gates_task_start(w.tree, &db, &b.task));
    while (!atomic_load(&a.ran) || !atomic_load(&b.ran)) sched_yield();
    int before = atomic_load(&joined);
    gates_tree_destroy(w.tree); /* returns only when both works have returned */
    GT_ASSERT(atomic_load(&joined) == before + 2);
    GT_ASSERT(a.done_calls == 0 && b.done_calls == 0);
    /* Their ends may still be queued: stale, released once. */
    for (int i = 0; i < 10; i++) (void)gates_sender_dispatch(w.sender, 0);
    GT_ASSERT(a.done_calls == 0 && b.done_calls == 0);
    gates_sender_close(w.sender);
    gates_sender_release(w.sender);
}

/* The app shuts the queue down while a task finishes: its end is dropped quietly. */
static void test_closed_queue(void) {
    world_t w;
    world(&w, 0);
    job_t j = { .wait_for_cancel = true };
    gates_task_desc_t d = desc_for(&j);
    GT_ASSERT_OK(gates_task_start(w.tree, &d, &j.task));
    while (!atomic_load(&j.ran)) sched_yield();
    gates_sender_close(w.sender);
    GT_ASSERT(gates_task_report(j.task, 1, GATES_STR("late")) == GATES_POST_CLOSED);
    gates_tree_destroy(w.tree);
    GT_ASSERT(j.done_calls == 0);
    gates_sender_release(w.sender);
}

int main(void) {
    test_progress_and_done();
    test_coalescing();
    test_cancel();
    test_full_queue();
    test_start_refusals();
    test_destroy_while_running();
    test_closed_queue();
    return gt_report("test_task");
}
