/* gates_gui_lib - background tasks (plan-0021, RFC-0005 A9): work on a
 * thread of its own, with progress, a result and cancellation reported on
 * the UI thread.
 *
 * gates_task_start runs `work` on a new thread. The work function never
 * touches the tree: it reports progress with gates_task_report (the latest
 * report replaces one not yet shown), polls gates_task_cancelled, and returns
 * its result. On the UI thread, in the tree's normal event turn, gates calls
 * on_progress for reports and on_done once when the work has returned (then
 * the thread is joined and the task handle ends after on_done). Data the work
 * produced can be left in the program's own structure (`user`): everything
 * the work wrote before returning is visible to on_done.
 *
 * gates_task_cancel only asks: the work decides when to stop, and on_done
 * still comes, with `cancelled` true. Destroying the tree (closing its window)
 * cancels every task and waits for each work function to return; on_done is
 * not called then. A work function that never looks at gates_task_cancelled
 * keeps its window from closing until it ends.
 *
 * The tree needs a sender (gates/post.h: a window has one) and threads from
 * the platform (the Win32 window installs them); otherwise UNSUPPORTED.
 * Message kinds from 0xFFFF0000 up are gates' own for this: a program's
 * messages use lower kinds. */
#ifndef GATES_TASK_H
#define GATES_TASK_H

#include <gates/tree.h>

typedef struct gates_task gates_task_t;

/* Worker side (any thread). */
typedef gates_err_t (*gates_task_work_fn)(gates_task_t *task, void *user);
/* permille 0..1000 (larger is reported as 1000); text is copied (may be empty).
 * OK, or the queue's answer (GATES_POST_FULL / GATES_POST_CLOSED: that report
 * is dropped; the work may go on). */
[[nodiscard]] gates_err_t gates_task_report(gates_task_t *task, gates_u32 permille, gates_str_t text);
bool gates_task_cancelled(const gates_task_t *task);

/* UI side. */
typedef void (*gates_task_progress_fn)(gates_tree_t *tree, gates_task_t *task, gates_u32 permille,
                                       gates_str_t text, void *user);
typedef void (*gates_task_done_fn)(gates_tree_t *tree, gates_task_t *task, gates_err_t result, bool cancelled,
                                   void *user);

typedef struct gates_task_desc_t {
    gates_task_work_fn work;         /* required */
    void *work_user;
    gates_task_progress_fn on_progress; /* may be null */
    gates_task_done_fn on_done;      /* may be null */
    void *ui_user;
} gates_task_desc_t;

/* *out_task (may be null) is valid until on_done returns or the tree is
 * destroyed. UNSUPPORTED without a sender or threads; the platform's error
 * when the thread cannot start (nothing is left behind). */
[[nodiscard]] gates_err_t gates_task_start(gates_tree_t *tree, const gates_task_desc_t *desc,
                                           gates_task_t **out_task);
/* Asks the work to stop (it sees gates_task_cancelled). */
void gates_task_cancel(gates_task_t *task);
/* Tasks started and not yet done (their on_done not yet called). */
gates_u32 gates_tree_task_count(const gates_tree_t *tree);

/* -- for platform backends and tests ------------------------------------------------ */

typedef struct gates_threads_t {
    void *ctx;
    /* Starts fn(arg) on a new thread; *out_thread is joined exactly once. */
    gates_err_t (*start)(void *ctx, void (*fn)(void *arg), void *arg, void **out_thread);
    void (*join)(void *ctx, void *thread);
    /* Gives up the processor for a moment (a worker waiting for queue space). */
    void (*yield)(void *ctx);
} gates_threads_t;

/* Installs (copies) the platform's threads; null removes them (tasks already
 * running are unaffected). */
void gates_tree_set_threads(gates_tree_t *tree, const gates_threads_t *threads);

#endif /* GATES_TASK_H */
