/* T034: posting under real threads (plan-0012 stage 2, RFC-0003 9): many
 * producers into small queues with FULL retries, per-producer order, shutdown
 * while producers post, replaceable floods, trees destroyed under traffic.
 * Every accepted payload is released exactly once, every refused one stays
 * with its producer. Built with -pthread; also run under ThreadSanitizer. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/post.h>
#include "gates_test.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* -- pthread sync for the sender ------------------------------------------------------ */

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

static atomic_int g_wakes;
static void on_wake(void *ctx) { (void)ctx; atomic_fetch_add(&g_wakes, 1); }

static gates_sender_t *new_sender(gates_u32 msgs, gates_usize_t bytes) {
    gates_sender_desc_t d = { .sync = { px_init, px_fini, px_lock, px_unlock }, .wake = on_wake,
                              .max_messages = msgs, .max_bytes = bytes };
    gates_sender_t *s = nullptr;
    GT_ASSERT_OK(gates_sender_create(&d, &s));
    return s;
}

/* -- payloads: one release counter each ---------------------------------------------------- */

typedef struct item_t {
    atomic_int releases;
    int producer;
    int seq;
} item_t;

static void release_item(void *p, void *ctx) {
    (void)ctx;
    atomic_fetch_add(&((item_t *)p)->releases, 1);
}

#define PRODUCERS 4
#define PER_PRODUCER 20000

typedef struct producer_t {
    gates_sender_t *s;
    gates_target_t to;
    item_t *items;
    int id;
    int count;
    int posted;          /* accepted */
    int fulls;
    int closed_at;       /* index of the first CLOSED, or -1 */
    bool stop_on_closed;
    bool replaceable;
} producer_t;

static void *produce(void *arg) {
    producer_t *p = arg;
    p->closed_at = -1;
    for (int i = 0; i < p->count; i++) {
        item_t *it = &p->items[i];
        it->producer = p->id;
        it->seq = i;
        gates_message_t m = { .target = p->to, .kind = (gates_u32)p->id, .payload = it, .bytes = 8,
                              .release = release_item, .replaceable = p->replaceable };
        for (;;) {
            gates_err_t err = gates_sender_post(p->s, &m);
            if (gates_is_ok(err)) {
                p->posted++;
                break;
            }
            if (err == GATES_POST_CLOSED) {
                p->closed_at = i;
                break;
            }
            p->fulls++;          /* FULL: the item is still ours; try again */
            sched_yield();
        }
        if (p->closed_at >= 0) break;
    }
    gates_sender_release(p->s);  /* the worker's reference */
    return nullptr;
}

typedef struct ui_t {
    int next_seq[PRODUCERS];     /* per-producer order */
    int out_of_order;
    int delivered;
    int last_value;
} ui_t;

static void handler(gates_tree_t *tree, gates_node_t node, gates_u32 kind, void *payload,
                    gates_usize_t bytes, void *user) {
    (void)tree; (void)node; (void)bytes;
    ui_t *u = user;
    item_t *it = payload;
    u->delivered++;
    u->last_value = it->seq;
    if (kind < PRODUCERS) {
        if (it->seq != u->next_seq[kind] && u->next_seq[kind] >= 0) u->out_of_order++;
        u->next_seq[kind] = it->seq + 1;
    }
}

static void start(producer_t *p, pthread_t *th, gates_sender_t *s, gates_target_t to, int id,
                  int count) {
    memset(p, 0, sizeof *p);
    p->s = s;
    p->to = to;
    p->id = id;
    p->count = count;
    p->items = calloc((size_t)count, sizeof(item_t));
    gates_sender_retain(s);      /* before the worker starts */
    pthread_create(th, nullptr, produce, p);
}

static bool all_done(const producer_t *p, int n, atomic_int *finished) {
    (void)p; (void)n;
    return atomic_load(finished) == n;
}

/* -- many producers, small queue --------------------------------------------------------- */

static atomic_int g_finished;

static void *produce_counted(void *arg) {
    produce(arg);
    atomic_fetch_add(&g_finished, 1);
    return nullptr;
}

static void test_flood_order(void) {
    gates_sender_t *s = new_sender(16, 1u << 20);
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    ui_t u = {0};
    GT_ASSERT_OK(gates_node_set_message_handler(t, gates_tree_root(t), handler, &u));
    GT_ASSERT_OK(gates_sender_attach(s, t));
    producer_t p[PRODUCERS];
    pthread_t th[PRODUCERS];
    atomic_store(&g_finished, 0);
    for (int i = 0; i < PRODUCERS; i++) {
        memset(&p[i], 0, sizeof p[i]);
        p[i].s = s;
        p[i].to = gates_target(t, gates_tree_root(t));
        p[i].id = i;
        p[i].count = PER_PRODUCER;
        p[i].items = calloc(PER_PRODUCER, sizeof(item_t));
        gates_sender_retain(s);
        pthread_create(&th[i], nullptr, produce_counted, &p[i]);
    }
    /* The UI thread: deliver bounded turns until every producer is done and drained. */
    while (!all_done(p, PRODUCERS, &g_finished) || gates_sender_pending(s) > 0) {
        if (gates_sender_dispatch(s, 0) == 0) sched_yield();
    }
    for (int i = 0; i < PRODUCERS; i++) pthread_join(th[i], nullptr);
    (void)gates_sender_dispatch(s, 0);
    GT_ASSERT(u.delivered == PRODUCERS * PER_PRODUCER);
    GT_ASSERT(u.out_of_order == 0);                       /* FIFO per producer */
    int fulls = 0;
    bool once = true;
    for (int i = 0; i < PRODUCERS; i++) {
        GT_ASSERT(p[i].posted == PER_PRODUCER && p[i].closed_at < 0);
        fulls += p[i].fulls;
        for (int k = 0; k < PER_PRODUCER; k++) {
            if (atomic_load(&p[i].items[k].releases) != 1) once = false;
        }
        free(p[i].items);
    }
    GT_ASSERT(once);                                      /* exactly once, all of them */
    GT_ASSERT(fulls > 0);                                 /* the small queue did fill */
    GT_ASSERT(atomic_load(&g_wakes) > 0);
    GT_ASSERT(gates_sender_pending_bytes(s) == 0);
    gates_tree_destroy(t);
    gates_sender_close(s);
    gates_sender_release(s);
}

/* -- shutdown while producers post ---------------------------------------------------------- */

static void test_shutdown_race(void) {
    for (int round = 0; round < 20; round++) {
        gates_sender_t *s = new_sender(8, 1u << 20);
        gates_tree_t *t = nullptr;
        GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
        ui_t u = {0};
        GT_ASSERT_OK(gates_node_set_message_handler(t, gates_tree_root(t), handler, &u));
        GT_ASSERT_OK(gates_sender_attach(s, t));
        producer_t p[PRODUCERS];
        pthread_t th[PRODUCERS];
        for (int i = 0; i < PRODUCERS; i++) {
            start(&p[i], &th[i], s, gates_target(t, gates_tree_root(t)), i, 5000);
        }
        for (int k = 0; k < 20 + round * 5; k++) (void)gates_sender_dispatch(s, 0);
        /* App shutdown: close, destroy the tree, drop the app's reference, while
         * producers are still posting; the last worker frees the sender. */
        gates_sender_close(s);
        gates_tree_destroy(t);
        gates_sender_release(s);
        for (int i = 0; i < PRODUCERS; i++) pthread_join(th[i], nullptr);
        bool ok = true;
        for (int i = 0; i < PRODUCERS; i++) {
            for (int k = 0; k < p[i].count; k++) {
                int r = atomic_load(&p[i].items[k].releases);
                bool accepted = k < p[i].posted;          /* posts are in order */
                if (accepted ? r != 1 : r != 0) ok = false;
            }
            if (p[i].posted < p[i].count && p[i].closed_at != p[i].posted) ok = false;
            free(p[i].items);
        }
        GT_ASSERT(ok);
    }
}

/* -- replaceable snapshots and a tree destroyed under traffic --------------------------------- */

static void test_snapshots_and_stale(void) {
    gates_sender_t *s = new_sender(4, 1u << 20);
    gates_tree_t *t = nullptr, *doomed = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &doomed));
    ui_t u = {0}, v = {0};
    for (int i = 0; i < PRODUCERS; i++) u.next_seq[i] = -1; /* snapshots skip values */
    GT_ASSERT_OK(gates_node_set_message_handler(t, gates_tree_root(t), handler, &u));
    GT_ASSERT_OK(gates_node_set_message_handler(doomed, gates_tree_root(doomed), handler, &v));
    GT_ASSERT_OK(gates_sender_attach(s, t));
    GT_ASSERT_OK(gates_sender_attach(s, doomed));
    producer_t snap, stale;
    pthread_t th_snap, th_stale;
    memset(&snap, 0, sizeof snap);
    snap.s = s;
    snap.to = gates_target(t, gates_tree_root(t));
    snap.id = 100;                   /* kind outside the ordered range */
    snap.count = 20000;
    snap.replaceable = true;
    snap.items = calloc(20000, sizeof(item_t));
    atomic_store(&g_finished, 0);
    gates_sender_retain(s);
    pthread_create(&th_snap, nullptr, produce_counted, &snap);
    memset(&stale, 0, sizeof stale);
    stale.s = s;
    stale.to = gates_target(doomed, gates_tree_root(doomed));
    stale.id = 101;
    stale.count = 20000;
    stale.items = calloc(20000, sizeof(item_t));
    gates_sender_retain(s);
    pthread_create(&th_stale, nullptr, produce_counted, &stale);
    /* Keep delivering while they post (a full queue only drains here); destroy
     * the second tree part way: its messages turn stale from then on. */
    long turns = 0;
    while (atomic_load(&g_finished) < 2 || gates_sender_pending(s) > 0) {
        if (gates_sender_dispatch(s, 0) == 0) sched_yield();
        if (++turns == 50) gates_tree_destroy(doomed);
    }
    if (turns < 50) gates_tree_destroy(doomed);
    pthread_join(th_snap, nullptr);
    pthread_join(th_stale, nullptr);
    while (gates_sender_dispatch(s, 0) > 0) {}
    GT_ASSERT(u.last_value == 19999);                     /* the latest snapshot arrived */
    GT_ASSERT(u.delivered <= 20000);
    bool once = true;
    for (int k = 0; k < 20000; k++) {
        if (atomic_load(&snap.items[k].releases) != 1) once = false;
        if (atomic_load(&stale.items[k].releases) != 1) once = false;
    }
    GT_ASSERT(once);                 /* replaced, delivered or stale: once each */
    free(snap.items);
    free(stale.items);
    gates_tree_destroy(t);
    gates_sender_close(s);
    gates_sender_release(s);
}

int main(void) {
    test_flood_order();
    test_shutdown_race();
    test_snapshots_and_stale();
    return gt_report("test_post_stress");
}
