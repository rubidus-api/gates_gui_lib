/* T033: posting to the UI and timers (plan-0012 stage 1, RFC-0003 9):
 * sender lifetime, OK/FULL/CLOSED ownership, limits, replaceable snapshots,
 * bounded delivery, stale targets, exactly-once release, close, timers on a
 * fake clock. Single-threaded; the threaded stress test is T034. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/post.h>
#include <gates/timer.h>
#include "gates_test.h"

#include <string.h>

/* -- a counting lock that also catches re-entry (a release or wake under it) ---------- */

typedef struct fake_lock_t {
    bool held;
    int locks;
    int reentries;
    int fini;
} fake_lock_t;

static fake_lock_t g_lock;
static int g_wakes;
static bool g_wake_allowed = true;
static int g_wakes_after_close;

static gates_err_t fl_init(void **lock) { *lock = &g_lock; return GATES_OK; }
static void fl_fini(void *lock) { ((fake_lock_t *)lock)->fini++; }
static void fl_lock(void *lock) {
    fake_lock_t *l = lock;
    if (l->held) l->reentries++;
    l->held = true;
    l->locks++;
}
static void fl_unlock(void *lock) { ((fake_lock_t *)lock)->held = false; }
static void on_wake(void *ctx) {
    (void)ctx;
    g_wakes++;
    if (!g_wake_allowed) g_wakes_after_close++;
    if (!g_lock.held) g_lock.reentries += 100; /* wake must run under the lock */
}

static gates_sender_t *new_sender(gates_u32 msgs, gates_usize_t bytes) {
    memset(&g_lock, 0, sizeof g_lock);
    g_wakes = 0;
    g_wake_allowed = true;
    g_wakes_after_close = 0;
    gates_sender_desc_t d = { .sync = { fl_init, fl_fini, fl_lock, fl_unlock }, .wake = on_wake,
                              .max_messages = msgs, .max_bytes = bytes };
    gates_sender_t *s = nullptr;
    GT_ASSERT_OK(gates_sender_create(&d, &s));
    return s;
}

/* -- counting payloads ---------------------------------------------------------------- */

typedef struct payload_t {
    int releases;
    int value;
    bool released_under_lock;
} payload_t;

static void release_payload(void *p, void *ctx) {
    (void)ctx;
    payload_t *pl = p;
    pl->releases++;
    if (g_lock.held) pl->released_under_lock = true;
}

static gates_message_t msg(gates_target_t to, gates_u32 kind, payload_t *p, gates_usize_t bytes) {
    return (gates_message_t){ .target = to, .kind = kind, .payload = p, .bytes = bytes,
                              .release = release_payload };
}

typedef struct seen_t {
    gates_u32 kinds[256];
    int values[256];
    int n;
    gates_sender_t *sender;
    gates_target_t echo_to;      /* handler posts one more message here once */
    bool echo;
    bool close_in_handler;
    int during_dispatch;
} seen_t;

static payload_t g_echo_payload;

static void handler(gates_tree_t *tree, gates_node_t node, gates_u32 kind, void *payload,
                    gates_usize_t bytes, void *user) {
    (void)tree; (void)node; (void)bytes;
    seen_t *s = user;
    if (s->n < 256) {
        s->kinds[s->n] = kind;
        s->values[s->n] = payload != nullptr ? ((payload_t *)payload)->value : -1;
        s->n++;
    }
    if (s->echo) {
        s->echo = false;
        gates_message_t m = msg(s->echo_to, 99, &g_echo_payload, 1);
        GT_ASSERT_OK(gates_sender_post(s->sender, &m));
    }
    if (s->close_in_handler) {
        s->close_in_handler = false;
        gates_sender_close(s->sender);
    }
}

typedef struct app_t {
    gates_tree_t *t;
    gates_node_t a, b;
    seen_t seen;
} app_t;

static void make_tree(app_t *x) {
    memset(x, 0, sizeof *x);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &x->t));
    GT_ASSERT_OK(gates_panel_create(x->t, gates_tree_root(x->t), &x->a));
    GT_ASSERT_OK(gates_panel_create(x->t, gates_tree_root(x->t), &x->b));
    GT_ASSERT_OK(gates_node_set_message_handler(x->t, x->a, handler, &x->seen));
}

/* -- posting and delivery --------------------------------------------------------------- */

static void test_basic(void) {
    gates_sender_t *s = new_sender(8, 100);
    app_t x;
    make_tree(&x);
    GT_ASSERT_OK(gates_sender_attach(s, x.t));
    gates_target_t to = gates_target(x.t, x.a);
    GT_ASSERT(to.tree == gates_tree_serial(x.t) && to.tree != 0 && gates_node_eq(to.node, x.a));

    payload_t p[3] = { { .value = 1 }, { .value = 2 }, { .value = 3 } };
    for (int i = 0; i < 3; i++) {
        gates_message_t m = msg(to, (gates_u32)(10 + i), &p[i], 10);
        GT_ASSERT_OK(gates_sender_post(s, &m));
    }
    GT_ASSERT(gates_sender_pending(s) == 3 && gates_sender_pending_bytes(s) == 30);
    GT_ASSERT(g_wakes == 1);                                   /* empty -> non-empty only */
    GT_ASSERT(gates_sender_dispatch(s, 0) == 0);
    GT_ASSERT(x.seen.n == 3 && x.seen.kinds[0] == 10 && x.seen.kinds[2] == 12);
    GT_ASSERT(x.seen.values[0] == 1 && x.seen.values[2] == 3); /* in order */
    for (int i = 0; i < 3; i++) GT_ASSERT(p[i].releases == 1 && !p[i].released_under_lock);
    GT_ASSERT(gates_sender_pending(s) == 0 && gates_sender_pending_bytes(s) == 0);
    payload_t q = { .value = 4 };
    gates_message_t m = msg(to, 1, &q, 1);
    GT_ASSERT_OK(gates_sender_post(s, &m));
    GT_ASSERT(g_wakes == 2);
    (void)gates_sender_dispatch(s, 0);
    GT_ASSERT(q.releases == 1);
    /* A message without a payload or release function is fine. */
    gates_message_t empty = { .target = to, .kind = 7 };
    GT_ASSERT_OK(gates_sender_post(s, &empty));
    (void)gates_sender_dispatch(s, 0);
    GT_ASSERT(x.seen.n == 5 && x.seen.values[4] == -1);
    GT_ASSERT(g_lock.reentries == 0);
    gates_tree_destroy(x.t);
    gates_sender_close(s);
    gates_sender_release(s);
    GT_ASSERT(g_lock.fini == 1);
}

static void test_limits(void) {
    gates_sender_t *s = new_sender(4, 100);
    app_t x;
    make_tree(&x);
    GT_ASSERT_OK(gates_sender_attach(s, x.t));
    gates_target_t to = gates_target(x.t, x.a);
    payload_t p[6] = {0};
    for (int i = 0; i < 4; i++) {
        gates_message_t m = msg(to, 1, &p[i], 10);
        GT_ASSERT_OK(gates_sender_post(s, &m));
    }
    gates_message_t m = msg(to, 1, &p[4], 10);
    GT_ASSERT(gates_sender_post(s, &m) == GATES_POST_FULL);   /* message limit */
    GT_ASSERT(p[4].releases == 0);                            /* the caller keeps it */
    (void)gates_sender_dispatch(s, 0);
    m = msg(to, 1, &p[4], 60);
    GT_ASSERT_OK(gates_sender_post(s, &m));
    m = msg(to, 1, &p[5], 50);
    GT_ASSERT(gates_sender_post(s, &m) == GATES_POST_FULL);   /* byte limit */
    m = msg(to, 1, &p[5], 101);
    GT_ASSERT(gates_sender_post(s, &m) == PROVEN_ERR_OUT_OF_BOUNDS); /* could never fit */
    GT_ASSERT(gates_sender_post(s, nullptr) == PROVEN_ERR_INVALID_ARG);
    (void)gates_sender_dispatch(s, 0);
    for (int i = 0; i < 5; i++) GT_ASSERT(p[i].releases == 1);
    GT_ASSERT(p[5].releases == 0);
    gates_tree_destroy(x.t);
    gates_sender_close(s);
    gates_sender_release(s);
}

static void test_replaceable(void) {
    gates_sender_t *s = new_sender(8, 100);
    app_t x;
    make_tree(&x);
    GT_ASSERT_OK(gates_node_set_message_handler(x.t, x.b, handler, &x.seen));
    GT_ASSERT_OK(gates_sender_attach(s, x.t));
    payload_t a = { .value = 1 }, b = { .value = 2 }, c = { .value = 3 }, d = { .value = 4 };
    gates_message_t m = msg(gates_target(x.t, x.a), 5, &a, 10);
    m.replaceable = true;
    GT_ASSERT_OK(gates_sender_post(s, &m));
    m.payload = &b;
    m.bytes = 20;
    GT_ASSERT_OK(gates_sender_post(s, &m));                   /* replaces a */
    GT_ASSERT(a.releases == 1 && !a.released_under_lock);
    GT_ASSERT(gates_sender_pending(s) == 1 && gates_sender_pending_bytes(s) == 20);
    m.payload = &c;
    m.target = gates_target(x.t, x.b);                        /* another target: queued */
    GT_ASSERT_OK(gates_sender_post(s, &m));
    gates_message_t plain = msg(gates_target(x.t, x.a), 5, &d, 1); /* not replaceable */
    GT_ASSERT_OK(gates_sender_post(s, &plain));
    GT_ASSERT(gates_sender_pending(s) == 3);
    (void)gates_sender_dispatch(s, 0);
    GT_ASSERT(x.seen.n == 3 && x.seen.values[0] == 2 && x.seen.values[1] == 3 && x.seen.values[2] == 4);
    GT_ASSERT(a.releases == 1 && b.releases == 1 && c.releases == 1 && d.releases == 1);
    gates_tree_destroy(x.t);
    gates_sender_close(s);
    gates_sender_release(s);
}

static void test_stale(void) {
    gates_sender_t *s = new_sender(16, 1000);
    app_t x, y;
    make_tree(&x);
    make_tree(&y);
    GT_ASSERT(gates_tree_serial(y.t) != gates_tree_serial(x.t));
    GT_ASSERT_OK(gates_sender_attach(s, x.t));
    GT_ASSERT_OK(gates_sender_attach(s, y.t));
    payload_t p[5] = { { .value = 1 }, { .value = 2 }, { .value = 3 }, { .value = 4 }, { .value = 5 } };
    gates_message_t m;
    m = msg(gates_target(x.t, x.b), 1, &p[0], 1);             /* no handler on b */
    GT_ASSERT_OK(gates_sender_post(s, &m));
    m = msg(gates_target(x.t, x.a), 1, &p[1], 1);             /* node destroyed below */
    GT_ASSERT_OK(gates_sender_post(s, &m));
    m = msg(gates_target(y.t, y.a), 1, &p[2], 1);             /* tree destroyed below */
    GT_ASSERT_OK(gates_sender_post(s, &m));
    m = msg((gates_target_t){ .tree = 999999, .node = x.a }, 1, &p[3], 1); /* no such tree */
    GT_ASSERT_OK(gates_sender_post(s, &m));
    GT_ASSERT_OK(gates_node_destroy(x.t, x.a));
    gates_tree_destroy(y.t);
    (void)gates_sender_dispatch(s, 0);
    GT_ASSERT(x.seen.n == 0 && y.seen.n == 0);                /* nothing delivered */
    for (int i = 0; i < 4; i++) GT_ASSERT(p[i].releases == 1); /* each released once */
    /* Removing a handler: undelivered from then on. */
    GT_ASSERT_OK(gates_tree_flush_destroys(x.t));
    GT_ASSERT_OK(gates_node_set_message_handler(x.t, x.b, handler, &x.seen));
    GT_ASSERT_OK(gates_node_set_message_handler(x.t, x.b, nullptr, nullptr));
    m = msg(gates_target(x.t, x.b), 1, &p[4], 1);
    GT_ASSERT_OK(gates_sender_post(s, &m));
    (void)gates_sender_dispatch(s, 0);
    GT_ASSERT(x.seen.n == 0 && p[4].releases == 1);
    GT_ASSERT(gates_node_set_message_handler(x.t, (gates_node_t){ 9999, 1 }, handler, nullptr) ==
              PROVEN_ERR_INVALID_ARG);
    /* A tree is attached to one sender at a time. */
    gates_sender_t *other = new_sender(4, 100);
    GT_ASSERT(gates_sender_attach(other, x.t) == PROVEN_ERR_INVALID_STATE);
    gates_sender_detach(s, x.t);
    GT_ASSERT_OK(gates_sender_attach(other, x.t));
    gates_tree_destroy(x.t);                                  /* detaches itself */
    gates_sender_close(other);
    gates_sender_release(other);
    gates_sender_close(s);
    gates_sender_release(s);
}

static void test_bounded_and_reentrant(void) {
    gates_sender_t *s = new_sender(256, 100000);
    app_t x;
    make_tree(&x);
    GT_ASSERT_OK(gates_sender_attach(s, x.t));
    gates_target_t to = gates_target(x.t, x.a);
    static payload_t p[100];
    memset(p, 0, sizeof p);
    for (int i = 0; i < 100; i++) {
        p[i].value = i;
        gates_message_t m = msg(to, 1, &p[i], 1);
        GT_ASSERT_OK(gates_sender_post(s, &m));
    }
    GT_ASSERT(gates_sender_dispatch(s, 0) == 100 - GATES_POST_PER_TURN);
    GT_ASSERT(x.seen.n == (int)GATES_POST_PER_TURN);
    GT_ASSERT(gates_sender_dispatch(s, 10) == 100 - GATES_POST_PER_TURN - 10);
    GT_ASSERT(gates_sender_dispatch(s, 0) == 0 && x.seen.n == 100);
    for (int i = 0; i < 100; i++) GT_ASSERT(p[i].releases == 1 && x.seen.values[i] == i);

    /* A handler posting again: delivered in the next turn, not recursively. */
    x.seen.n = 0;
    x.seen.sender = s;
    x.seen.echo = true;
    x.seen.echo_to = to;
    memset(&g_echo_payload, 0, sizeof g_echo_payload);
    payload_t one = { .value = 7 };
    gates_message_t m = msg(to, 1, &one, 1);
    GT_ASSERT_OK(gates_sender_post(s, &m));
    GT_ASSERT(gates_sender_dispatch(s, 0) == 1);              /* the echo waits */
    GT_ASSERT(x.seen.n == 1);
    GT_ASSERT(gates_sender_dispatch(s, 0) == 0 && x.seen.n == 2 && x.seen.kinds[1] == 99);
    GT_ASSERT(g_echo_payload.releases == 1);

    /* Closing from a handler: the rest is released, not delivered. */
    x.seen.n = 0;
    x.seen.close_in_handler = true;
    payload_t q[3] = {0};
    for (int i = 0; i < 3; i++) {
        m = msg(to, 1, &q[i], 1);
        GT_ASSERT_OK(gates_sender_post(s, &m));
    }
    GT_ASSERT(gates_sender_dispatch(s, 0) == 0);
    GT_ASSERT(x.seen.n == 1);
    for (int i = 0; i < 3; i++) GT_ASSERT(q[i].releases == 1);
    GT_ASSERT(g_lock.reentries == 0);
    gates_tree_destroy(x.t);
    gates_sender_release(s);
}

/* A release function that posts back while the sender closes must not deadlock. */
typedef struct echo_release_t {
    gates_sender_t *s;
    gates_target_t to;
    int calls;
    gates_err_t last;
} echo_release_t;

static void release_and_post(void *p, void *ctx) {
    (void)p;
    echo_release_t *e = ctx;
    e->calls++;
    gates_message_t m = { .target = e->to, .kind = 3 };
    e->last = gates_sender_post(e->s, &m);
}

static void test_close(void) {
    gates_sender_t *s = new_sender(8, 100);
    app_t x;
    make_tree(&x);
    GT_ASSERT_OK(gates_sender_attach(s, x.t));
    gates_target_t to = gates_target(x.t, x.a);
    payload_t p[3] = {0};
    for (int i = 0; i < 3; i++) {
        gates_message_t m = msg(to, 1, &p[i], 5);
        GT_ASSERT_OK(gates_sender_post(s, &m));
    }
    echo_release_t e = { .s = s, .to = to };
    gates_message_t m = { .target = to, .kind = 2, .payload = &e, .release = release_and_post,
                          .release_ctx = &e };
    GT_ASSERT_OK(gates_sender_post(s, &m));
    gates_sender_retain(s);                                   /* a worker's reference */
    gates_sender_close(s);
    g_wake_allowed = false;
    for (int i = 0; i < 3; i++) GT_ASSERT(p[i].releases == 1);
    GT_ASSERT(e.calls == 1 && e.last == GATES_POST_CLOSED);   /* no deadlock, CLOSED */
    GT_ASSERT(gates_sender_pending(s) == 0 && gates_sender_pending_bytes(s) == 0);
    GT_ASSERT(x.seen.n == 0);
    payload_t late = {0};
    m = msg(to, 1, &late, 1);
    GT_ASSERT(gates_sender_post(s, &m) == GATES_POST_CLOSED);
    GT_ASSERT(late.releases == 0 && g_wakes_after_close == 0);
    GT_ASSERT(gates_sender_attach(s, x.t) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT(gates_sender_dispatch(s, 0) == 0);
    gates_tree_destroy(x.t);                                  /* already detached: fine */
    gates_sender_release(s);                                  /* the app's */
    GT_ASSERT(g_lock.fini == 0);                              /* the worker still holds one */
    GT_ASSERT(gates_sender_post(s, &m) == GATES_POST_CLOSED);
    gates_sender_release(s);                                  /* the worker's: freed now */
    GT_ASSERT(g_lock.fini == 1 && g_lock.reentries == 0);

    /* Dropping the last reference without closing releases what was queued. */
    s = new_sender(8, 100);
    payload_t left = {0};
    m = msg((gates_target_t){ .tree = 1, .node = x.a }, 1, &left, 1);
    GT_ASSERT_OK(gates_sender_post(s, &m));
    gates_sender_release(s);
    GT_ASSERT(left.releases == 1);
    gates_sender_desc_t bad = {0};
    gates_sender_t *none = nullptr;
    GT_ASSERT(gates_sender_create(&bad, &none) == PROVEN_ERR_INVALID_ARG); /* needs a lock */
}

/* -- timers on a fake clock ----------------------------------------------------------------- */

typedef struct clock_t_ {
    gates_u64 now;
    int changed;
} fake_clock_t;

static gates_u64 fc_now(void *ctx) { return ((fake_clock_t *)ctx)->now; }
static void fc_changed(void *ctx) { ((fake_clock_t *)ctx)->changed++; }

typedef struct fired_t {
    gates_timer_id_t ids[64];
    int n;
    gates_tree_t *t;
    gates_node_t node;
    gates_timer_id_t cancel_this;    /* cancelled from inside the callback */
    bool start_new;
    gates_timer_id_t started;
} fired_t;

static void on_timer(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)node;
    fired_t *f = user;
    if (f->n < 64) f->ids[f->n++] = id;
    if (f->cancel_this != 0) {
        GT_ASSERT_OK(gates_timer_cancel(tree, f->cancel_this));
        f->cancel_this = 0;
    }
    if (f->start_new) {
        f->start_new = false;
        GT_ASSERT_OK(gates_timer_start(tree, f->node, 1, false, on_timer, f, &f->started));
    }
}

static void test_timers(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t n, m;
    GT_ASSERT_OK(gates_panel_create(t, gates_tree_root(t), &n));
    GT_ASSERT_OK(gates_panel_create(t, gates_tree_root(t), &m));
    fired_t f = { .t = t, .node = n };
    gates_timer_id_t a = 0, b = 0, c = 0;
    GT_ASSERT(gates_timer_start(t, n, 100, false, on_timer, &f, &a) == PROVEN_ERR_INVALID_STATE);
    fake_clock_t clk = {0};
    gates_tree_set_clock(t, fc_now, fc_changed, &clk);
    GT_ASSERT(gates_tree_next_timer(t) == GATES_TIMER_NONE);
    GT_ASSERT(gates_timer_start(t, n, 0, false, on_timer, &f, &a) == PROVEN_ERR_INVALID_ARG);

    /* One-shot. */
    GT_ASSERT_OK(gates_timer_start(t, n, 100, false, on_timer, &f, &a));
    GT_ASSERT(a != 0 && gates_timer_active(t, a) && clk.changed >= 1);
    GT_ASSERT(gates_tree_next_timer(t) == 100);
    clk.now = 50;
    GT_ASSERT(gates_tree_run_timers(t) == 50 && f.n == 0);
    clk.now = 100;
    GT_ASSERT(gates_tree_run_timers(t) == GATES_TIMER_NONE);
    GT_ASSERT(f.n == 1 && f.ids[0] == a && !gates_timer_active(t, a));
    GT_ASSERT(gates_tree_timer_count(t) == 0);

    /* Repeating: a late timer fires once and is rescheduled from now. */
    f.n = 0;
    GT_ASSERT_OK(gates_timer_start(t, n, 10, true, on_timer, &f, &b));
    GT_ASSERT(b != a);
    clk.now = 110;
    (void)gates_tree_run_timers(t);
    clk.now = 165;                                            /* five intervals late */
    GT_ASSERT(gates_tree_run_timers(t) == 10);
    GT_ASSERT(f.n == 2);
    clk.now = 175;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(f.n == 3);

    /* Cancel: never fires again; unknown ids refused; ids are not reused. */
    int changed = clk.changed;
    GT_ASSERT_OK(gates_timer_cancel(t, b));
    GT_ASSERT(clk.changed > changed);
    GT_ASSERT(gates_timer_cancel(t, b) == PROVEN_ERR_INVALID_ARG);
    clk.now = 500;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(f.n == 3);
    GT_ASSERT_OK(gates_timer_start(t, n, 5, false, on_timer, &f, &c));
    GT_ASSERT(c != a && c != b);

    /* Same round: due timers fire in start order; one cancels a later one, which
     * then does not fire; a timer started in a callback waits for a later run. */
    gates_timer_id_t d = 0, e = 0;
    GT_ASSERT_OK(gates_timer_start(t, n, 5, false, on_timer, &f, &d));
    GT_ASSERT_OK(gates_timer_start(t, n, 5, false, on_timer, &f, &e));
    f.n = 0;
    f.cancel_this = e;
    f.start_new = true;
    clk.now = 505;
    GT_ASSERT(gates_tree_run_timers(t) == 1);                  /* the new one: 1 ms */
    GT_ASSERT(f.n == 2 && f.ids[0] == c && f.ids[1] == d);
    GT_ASSERT(!gates_timer_active(t, e) && gates_timer_active(t, f.started));
    clk.now = 506;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(f.n == 3 && f.ids[2] == f.started);

    /* A repeating timer cancelling itself stops. */
    gates_timer_id_t r = 0;
    GT_ASSERT_OK(gates_timer_start(t, n, 10, true, on_timer, &f, &r));
    f.n = 0;
    f.cancel_this = r;
    clk.now = 516;
    (void)gates_tree_run_timers(t);
    clk.now = 600;
    (void)gates_tree_run_timers(t);
    GT_ASSERT(f.n == 1 && !gates_timer_active(t, r));

    /* A destroyed node's timer never fires and its entry is reclaimed. */
    gates_timer_id_t z = 0;
    GT_ASSERT_OK(gates_timer_start(t, m, 10, true, on_timer, &f, &z));
    GT_ASSERT(gates_tree_timer_count(t) == 1);
    GT_ASSERT_OK(gates_node_destroy(t, m));
    f.n = 0;
    clk.now = 700;
    GT_ASSERT(gates_tree_run_timers(t) == GATES_TIMER_NONE);
    GT_ASSERT(f.n == 0 && gates_tree_timer_count(t) == 0 && !gates_timer_active(t, z));

    /* Destroying the tree with timers pending frees them (ASan checks). */
    GT_ASSERT_OK(gates_timer_start(t, n, 10, true, on_timer, &f, &z));
    gates_tree_destroy(t);
}

int main(void) {
    test_basic();
    test_limits();
    test_replaceable();
    test_stale();
    test_bounded_and_reentrant();
    test_close();
    test_timers();
    return gt_report("test_post");
}
