/* gates_gui_lib - UI timers (plan-0012, RFC-0003 section 9).
 *
 * A timer belongs to a node of a tree and runs its callback on the UI thread
 * after `interval_ms`, once or repeatedly. Cancelling is immediate: a
 * cancelled timer never fires afterwards, and destroying its node or tree
 * cancels it. A repeating timer that is late fires once and is rescheduled
 * from now (no burst of missed ticks). A callback may start and cancel timers,
 * its own included; a timer started in a callback fires on a later run, never
 * in the same one. Resolution is the platform's (Win32: about 10-16 ms).
 * Idle windows have no platform timer running. Platform-free. */
#ifndef GATES_TIMER_H
#define GATES_TIMER_H

#include <gates/tree.h>

typedef gates_u32 gates_timer_id_t;      /* nonzero, never reused within a tree */
#define GATES_TIMER_NONE UINT32_MAX      /* "no timer due" from gates_tree_run_timers */

typedef void (*gates_timer_fn)(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id,
                               void *user);

/* interval_ms >= 1. INVALID_STATE when the tree has no clock (a window's tree
 * has one). */
[[nodiscard]] gates_err_t gates_timer_start(gates_tree_t *tree, gates_node_t node,
                                            gates_u32 interval_ms, bool repeat,
                                            gates_timer_fn fn, void *user,
                                            gates_timer_id_t *out_id);
/* INVALID_ARG when the id is not an active timer of this tree. */
[[nodiscard]] gates_err_t gates_timer_cancel(gates_tree_t *tree, gates_timer_id_t id);
bool gates_timer_active(const gates_tree_t *tree, gates_timer_id_t id);

/* -- for platform backends and tests ------------------------------------------ */

typedef gates_u64 (*gates_clock_fn)(void *ctx);           /* milliseconds, monotonic */
/* The clock, and `changed`, called whenever the next due time may have moved
 * (a timer started or cancelled) so the platform can re-arm its own timer. */
void gates_tree_set_clock(gates_tree_t *tree, gates_clock_fn now, void (*changed)(void *ctx),
                          void *ctx);
/* Fires every timer that is due now (each at most once) and returns the
 * milliseconds until the next one is due, or GATES_TIMER_NONE. */
gates_u32 gates_tree_run_timers(gates_tree_t *tree);
/* Milliseconds until the next timer is due (0 = overdue), or GATES_TIMER_NONE. */
gates_u32 gates_tree_next_timer(const gates_tree_t *tree);
gates_u32 gates_tree_timer_count(const gates_tree_t *tree);

#endif /* GATES_TIMER_H */
