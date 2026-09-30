/* gates_gui_lib - background tasks (0.6.0): a work function
 * on a platform thread; progress and the end travel back through the tree's
 * sender as gates' own message kinds and are handled before any node's
 * message handler. A task is shared by the UI and its worker and freed by
 * whichever lets go last; its memory comes from the thread-safe heap.
 * Platform-free. */
#include <gates/task.h>
#include <gates/post.h>
#include <proven/heap.h>
#include "gates_tree_internal.h"

#include <stdatomic.h>
#include <string.h>

#define TASK_DONE_BIT 0x8000u

struct gates_task {
    atomic_int refs;             /* the UI's and the worker's */
    atomic_bool cancel;
    gates_task_work_fn work;
    void *work_user;
    gates_task_progress_fn on_progress;
    gates_task_done_fn on_done;
    void *ui_user;
    gates_sender_t *sender;      /* retained */
    gates_target_t target;
    gates_u32 id;
    gates_threads_t threads;
    void *thread;
    gates_err_t result;          /* written by the worker before its end is posted */
    struct gates_task *next;     /* the tree's list (UI thread) */
};

typedef struct progress_t {
    gates_task_t *task;          /* compared, never followed, until found in the tree's list */
    gates_u32 id;
    gates_u32 permille;
    gates_u32 len;
    gates_u8 text[];
} progress_t;

static void heap_free(void *p) {
    gates_allocator_t h = proven_heap_allocator();
    h.free_fn(h.ctx, p);
}

static void task_unref(gates_task_t *t) {
    if (atomic_fetch_sub(&t->refs, 1) == 1) {
        gates_sender_release(t->sender);
        heap_free(t);
    }
}

static void release_task(void *payload, void *ctx) {
    (void)ctx;
    task_unref((gates_task_t *)payload);
}

static void release_progress(void *payload, void *ctx) {
    (void)ctx;
    heap_free(payload);
}

static gates_u32 kind_of(const gates_task_t *t, bool done) {
    return GATES_I_TASK_KIND_BASE | (done ? TASK_DONE_BIT : 0u) | (t->id & 0x7FFFu);
}

/* -- worker side ---------------------------------------------------------------------- */

static void task_main(void *arg) {
    gates_task_t *t = arg;
    t->result = t->work(t, t->work_user);
    /* The end must arrive: wait for room; a closed queue means nobody listens. */
    gates_message_t m = { .target = t->target, .kind = kind_of(t, true), .payload = t, .release = release_task };
    for (;;) {
        gates_err_t err = gates_sender_post(t->sender, &m);
        if (gates_is_ok(err)) return; /* the message holds the worker's reference now */
        if (err != GATES_POST_FULL) break;
        t->threads.yield(t->threads.ctx);
    }
    task_unref(t);
}

gates_err_t gates_task_report(gates_task_t *t, gates_u32 permille, gates_str_t text) {
    if (t == nullptr || (text.size > 0 && text.ptr == nullptr) || text.size > 0xFFFFu) return PROVEN_ERR_INVALID_ARG;
    gates_allocator_t h = proven_heap_allocator();
    proven_result_mem_mut_t r = h.alloc_fn(h.ctx, sizeof(progress_t) + text.size, alignof(progress_t));
    if (!proven_is_ok(r.err)) return r.err;
    progress_t *p = (progress_t *)r.value.ptr;
    *p = (progress_t){ .task = t, .id = t->id, .permille = permille < 1000u ? permille : 1000u,
                       .len = (gates_u32)text.size };
    if (text.size > 0) memcpy(p->text, text.ptr, text.size);
    gates_message_t m = { .target = t->target, .kind = kind_of(t, false), .payload = p,
                          .bytes = sizeof(progress_t) + text.size, .release = release_progress, .replaceable = true };
    gates_err_t err = gates_sender_post(t->sender, &m);
    if (!gates_is_ok(err)) heap_free(p);
    return err;
}

bool gates_task_cancelled(const gates_task_t *t) {
    return t == nullptr || atomic_load(&((gates_task_t *)t)->cancel);
}

/* -- UI side -------------------------------------------------------------------------- */

void gates_tree_set_threads(gates_tree_t *tree, const gates_threads_t *threads) {
    if (tree == nullptr) return;
    bool ok = threads != nullptr && threads->start != nullptr && threads->join != nullptr && threads->yield != nullptr;
    tree->has_threads = ok;
    tree->threads = ok ? *threads : (gates_threads_t){0};
}

gates_err_t gates_task_start(gates_tree_t *tree, const gates_task_desc_t *desc, gates_task_t **out_task) {
    if (out_task != nullptr) *out_task = nullptr;
    if (tree == nullptr || desc == nullptr || desc->work == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (tree->sender == nullptr || !tree->has_threads) return PROVEN_ERR_UNSUPPORTED;
    gates_allocator_t h = proven_heap_allocator();
    proven_result_mem_mut_t r = h.alloc_fn(h.ctx, sizeof(gates_task_t), alignof(gates_task_t));
    if (!proven_is_ok(r.err)) return r.err;
    gates_task_t *t = (gates_task_t *)r.value.ptr;
    memset(t, 0, sizeof *t);
    atomic_init(&t->refs, 2);
    atomic_init(&t->cancel, false);
    t->work = desc->work;
    t->work_user = desc->work_user;
    t->on_progress = desc->on_progress;
    t->on_done = desc->on_done;
    t->ui_user = desc->ui_user;
    t->sender = tree->sender;
    gates_sender_retain(t->sender);
    t->target = gates_target(tree, gates_tree_root(tree));
    t->id = tree->next_task_id++;
    t->threads = tree->threads;
    gates_err_t err = t->threads.start(t->threads.ctx, task_main, t, &t->thread);
    if (!gates_is_ok(err)) {
        gates_sender_release(t->sender);
        heap_free(t);
        return err;
    }
    t->next = tree->tasks;
    tree->tasks = t;
    tree->task_count++;
    if (out_task != nullptr) *out_task = t;
    return GATES_OK;
}

void gates_task_cancel(gates_task_t *t) {
    if (t != nullptr) atomic_store(&t->cancel, true);
}

gates_u32 gates_tree_task_count(const gates_tree_t *tree) {
    return tree != nullptr ? tree->task_count : 0;
}

/* Takes t out of the tree's list; false when it is not there (already done). */
static bool unlink_task(gates_tree_t *tree, const gates_task_t *t) {
    for (gates_task_t **p = &tree->tasks; *p != nullptr; p = &(*p)->next) {
        if (*p == t) {
            *p = t->next;
            tree->task_count--;
            return true;
        }
    }
    return false;
}

static gates_task_t *find_task(gates_tree_t *tree, const gates_task_t *t, gates_u32 id) {
    for (gates_task_t *q = tree->tasks; q != nullptr; q = q->next) {
        if (q == t && q->id == id) return q;
    }
    return nullptr;
}

void gates_i_task_message(gates_tree_t *tree, gates_u32 kind, void *payload) {
    if ((kind & TASK_DONE_BIT) != 0) {
        gates_task_t *t = payload; /* the message holds a reference: t is alive */
        if (!unlink_task(tree, t)) return; /* defensive: an end is delivered at most once */
        t->threads.join(t->threads.ctx, t->thread); /* the work has returned; the thread ends */
        if (t->on_done != nullptr) t->on_done(tree, t, t->result, atomic_load(&t->cancel), t->ui_user);
        task_unref(t); /* the UI's reference; the message's goes after delivery */
        return;
    }
    const progress_t *p = payload;
    gates_task_t *t = find_task(tree, p->task, p->id);
    if (t != nullptr && t->on_progress != nullptr) {
        t->on_progress(tree, t, p->permille, (gates_str_t){ .ptr = p->text, .size = p->len }, t->ui_user);
    }
}

void gates_i_tasks_shutdown(gates_tree_t *tree) {
    for (gates_task_t *t = tree->tasks; t != nullptr; t = t->next) atomic_store(&t->cancel, true);
    while (tree->tasks != nullptr) {
        gates_task_t *t = tree->tasks;
        tree->tasks = t->next;
        tree->task_count--;
        t->threads.join(t->threads.ctx, t->thread); /* waits for the work to notice */
        task_unref(t);
    }
}
