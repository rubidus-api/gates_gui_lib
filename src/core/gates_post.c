/* gates_gui_lib - sender endpoint: a bounded queue from any thread to the UI
 * thread (plan-0012, RFC-0003 9). The lock comes from the platform and lives in
 * the sender; payload release functions always run outside it; wake runs under
 * it and never after close. Platform-free. */
#include <gates/post.h>
#include <proven/heap.h>
#include "gates_tree_internal.h"

#include <string.h>

struct gates_sender {
    gates_allocator_t alloc;
    gates_sync_t sync;
    void *lock;
    void (*wake)(void *ctx);
    void *wake_ctx;
    gates_u32 refs;              /* under the lock */
    bool closed;                 /* under the lock */
    /* Ring of queued messages (under the lock); fixed at creation: posting never allocates. */
    gates_message_t *ring;
    gates_u32 cap;
    gates_u32 head;
    gates_u32 count;
    gates_usize_t bytes;
    gates_usize_t max_bytes;
    /* Attached trees (UI thread only). */
    gates_tree_t **trees;
    gates_u32 tree_count;
    gates_u32 tree_cap;
};

static void release_msg(const gates_message_t *m) {
    if (m->release != nullptr) {
        m->release(m->payload, m->release_ctx);
    }
}

gates_err_t gates_sender_create(const gates_sender_desc_t *desc, gates_sender_t **out_sender) {
    if (desc == nullptr || out_sender == nullptr || desc->sync.lock_init == nullptr ||
        desc->sync.lock_fini == nullptr || desc->sync.lock == nullptr || desc->sync.unlock == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_sender = nullptr;
    gates_allocator_t a = proven_alloc_is_valid(desc->allocator) ? desc->allocator
                                                                 : proven_heap_allocator();
    gates_u32 cap = desc->max_messages != 0 ? desc->max_messages : GATES_POST_DEFAULT_MESSAGES;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, sizeof(gates_sender_t), alignof(gates_sender_t));
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    gates_sender_t *s = (gates_sender_t *)r.value.ptr;
    memset(s, 0, sizeof *s);
    s->alloc = a;
    s->sync = desc->sync;
    s->wake = desc->wake;
    s->wake_ctx = desc->wake_ctx;
    s->refs = 1;
    s->cap = cap;
    s->max_bytes = desc->max_bytes != 0 ? desc->max_bytes : GATES_POST_DEFAULT_BYTES;
    proven_result_mem_mut_t rr = a.alloc_fn(a.ctx, (gates_usize_t)cap * sizeof(gates_message_t),
                                            alignof(gates_message_t));
    gates_err_t err = rr.err;
    if (gates_is_ok(err)) {
        s->ring = (gates_message_t *)rr.value.ptr;
        err = s->sync.lock_init(&s->lock);
        if (!gates_is_ok(err)) a.free_fn(a.ctx, s->ring);
    }
    if (!gates_is_ok(err)) {
        a.free_fn(a.ctx, s);
        return err;
    }
    *out_sender = s;
    return GATES_OK;
}

void gates_sender_retain(gates_sender_t *s) {
    if (s == nullptr) return;
    s->sync.lock(s->lock);
    s->refs++;
    s->sync.unlock(s->lock);
}

void gates_sender_release(gates_sender_t *s) {
    if (s == nullptr) return;
    s->sync.lock(s->lock);
    bool last = --s->refs == 0;
    s->sync.unlock(s->lock);
    if (!last) {
        return;
    }
    /* Nobody else can reach it now: no lock needed from here on. */
    for (gates_u32 i = 0; i < s->count; i++) {
        release_msg(&s->ring[(s->head + i) % s->cap]); /* never closed: still queued */
    }
    for (gates_u32 i = 0; i < s->tree_count; i++) {
        s->trees[i]->sender = nullptr;
    }
    gates_allocator_t a = s->alloc;
    s->sync.lock_fini(s->lock);
    if (s->trees != nullptr) a.free_fn(a.ctx, s->trees);
    a.free_fn(a.ctx, s->ring);
    a.free_fn(a.ctx, s);
}

static bool same_target(gates_target_t x, gates_target_t y) {
    return x.tree == y.tree && gates_node_eq(x.node, y.node);
}

gates_err_t gates_sender_post(gates_sender_t *s, const gates_message_t *msg) {
    if (s == nullptr || msg == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (msg->bytes > s->max_bytes) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    gates_message_t replaced = {0};
    bool have_replaced = false;
    s->sync.lock(s->lock);
    if (s->closed) {
        s->sync.unlock(s->lock);
        return GATES_POST_CLOSED;
    }
    if (msg->replaceable) {
        for (gates_u32 i = 0; i < s->count; i++) {
            gates_message_t *q = &s->ring[(s->head + i) % s->cap];
            if (q->replaceable && q->kind == msg->kind && same_target(q->target, msg->target)) {
                if (s->bytes - q->bytes + msg->bytes > s->max_bytes) {
                    s->sync.unlock(s->lock);
                    return GATES_POST_FULL;
                }
                replaced = *q;
                have_replaced = true;
                s->bytes = s->bytes - q->bytes + msg->bytes;
                *q = *msg;
                break;
            }
        }
    }
    if (!have_replaced) {
        if (s->count == s->cap || s->bytes + msg->bytes > s->max_bytes) {
            s->sync.unlock(s->lock);
            return GATES_POST_FULL;
        }
        bool was_empty = s->count == 0;
        s->ring[(s->head + s->count) % s->cap] = *msg;
        s->count++;
        s->bytes += msg->bytes;
        if (was_empty && s->wake != nullptr) {
            s->wake(s->wake_ctx); /* under the lock: close cannot slip in between */
        }
    }
    s->sync.unlock(s->lock);
    if (have_replaced) {
        release_msg(&replaced); /* outside the lock: it may post again */
    }
    return GATES_OK;
}

gates_err_t gates_sender_attach(gates_sender_t *s, gates_tree_t *tree) {
    if (s == nullptr || tree == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    s->sync.lock(s->lock);
    bool closed = s->closed;
    s->sync.unlock(s->lock);
    if (closed || tree->sender != nullptr) {
        return PROVEN_ERR_INVALID_STATE;
    }
    if (s->tree_count == s->tree_cap) {
        gates_u32 cap = s->tree_cap != 0 ? s->tree_cap * 2 : 4;
        gates_allocator_t a = s->alloc;
        proven_result_mem_mut_t r =
            s->trees == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof *s->trees, alignof(gates_tree_t *))
                : a.realloc_fn(a.ctx, s->trees, s->tree_cap * sizeof *s->trees,
                               cap * sizeof *s->trees, alignof(gates_tree_t *));
        if (!proven_is_ok(r.err)) {
            return r.err;
        }
        s->trees = (gates_tree_t **)r.value.ptr;
        s->tree_cap = cap;
    }
    s->trees[s->tree_count++] = tree;
    tree->sender = s;
    return GATES_OK;
}

void gates_sender_detach(gates_sender_t *s, gates_tree_t *tree) {
    if (s == nullptr || tree == nullptr) return;
    for (gates_u32 i = 0; i < s->tree_count; i++) {
        if (s->trees[i] == tree) {
            s->trees[i] = s->trees[--s->tree_count];
            break;
        }
    }
    if (tree->sender == s) {
        tree->sender = nullptr;
    }
}

static gates_tree_t *find_tree(const gates_sender_t *s, gates_u64 serial) {
    for (gates_u32 i = 0; i < s->tree_count; i++) {
        if (s->trees[i]->serial == serial) return s->trees[i];
    }
    return nullptr;
}

static const gates_i_msg_handler_t *find_handler(const gates_tree_t *tree, gates_node_t node) {
    for (gates_u32 i = 0; i < tree->msg_handler_count; i++) {
        const gates_i_msg_handler_t *h = &tree->msg_handlers[i];
        if (h->index == node.index && h->generation == node.generation) return h;
    }
    return nullptr;
}

gates_u32 gates_sender_dispatch(gates_sender_t *s, gates_u32 max) {
    if (s == nullptr) return 0;
    if (max == 0 || max > GATES_POST_PER_TURN) max = GATES_POST_PER_TURN;
    gates_sender_retain(s); /* a handler may drop the last outside reference */
    /* One batch per turn: later posts (from handlers too) wait for the next one. */
    gates_message_t batch[GATES_POST_PER_TURN];
    s->sync.lock(s->lock);
    gates_u32 n = s->count < max ? s->count : max;
    for (gates_u32 i = 0; i < n; i++) {
        batch[i] = s->ring[s->head];
        s->head = (s->head + 1) % s->cap;
        s->count--;
        s->bytes -= batch[i].bytes;
    }
    s->sync.unlock(s->lock);
    for (gates_u32 i = 0; i < n; i++) {
        const gates_message_t *m = &batch[i];
        gates_tree_t *tree = find_tree(s, m->target.tree); /* looked up again each time */
        if (tree != nullptr && m->kind >= GATES_I_TASK_KIND_BASE) {
            gates_i_task_message(tree, m->kind, m->payload); /* a task's progress or end (plan-0021) */
        } else if (tree != nullptr && gates_i_valid(tree, m->target.node)) {
            const gates_i_msg_handler_t *h = find_handler(tree, m->target.node);
            if (h != nullptr && h->fn != nullptr) {
                gates_message_fn fn = h->fn;
                fn(tree, m->target.node, m->kind, m->payload, m->bytes, h->user);
            }
        }
        release_msg(m); /* delivered or stale: exactly once, outside the lock */
    }
    s->sync.lock(s->lock);
    gates_u32 left = s->count;
    s->sync.unlock(s->lock);
    gates_sender_release(s);
    return left;
}

void gates_sender_close(gates_sender_t *s) {
    if (s == nullptr) return;
    s->sync.lock(s->lock);
    s->closed = true;
    s->sync.unlock(s->lock);
    /* Closed: no poster touches the ring any more, and this is the UI thread. */
    while (s->count > 0) {
        gates_message_t m = s->ring[s->head];
        s->head = (s->head + 1) % s->cap;
        s->count--;
        s->bytes -= m.bytes;
        release_msg(&m);
    }
    for (gates_u32 i = 0; i < s->tree_count; i++) {
        s->trees[i]->sender = nullptr;
    }
    s->tree_count = 0;
}

gates_u32 gates_sender_pending(gates_sender_t *s) {
    if (s == nullptr) return 0;
    s->sync.lock(s->lock);
    gates_u32 n = s->count;
    s->sync.unlock(s->lock);
    return n;
}

gates_usize_t gates_sender_pending_bytes(gates_sender_t *s) {
    if (s == nullptr) return 0;
    s->sync.lock(s->lock);
    gates_usize_t n = s->bytes;
    s->sync.unlock(s->lock);
    return n;
}

/* -- trees: serial, targets, handlers ------------------------------------------------- */

gates_u64 gates_tree_serial(const gates_tree_t *tree) {
    return tree != nullptr ? tree->serial : 0;
}

gates_target_t gates_target(const gates_tree_t *tree, gates_node_t node) {
    return (gates_target_t){ .tree = gates_tree_serial(tree), .node = node };
}

gates_err_t gates_node_set_message_handler(gates_tree_t *tree, gates_node_t node,
                                           gates_message_fn fn, void *user) {
    if (tree == nullptr || !gates_i_valid(tree, node)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    /* Entries of destroyed nodes are dropped here as well. */
    gates_u32 out = 0;
    for (gates_u32 i = 0; i < tree->msg_handler_count; i++) {
        gates_i_msg_handler_t *h = &tree->msg_handlers[i];
        gates_node_t hn = { .index = h->index, .generation = h->generation };
        if (gates_i_valid(tree, hn) && !(h->index == node.index && h->generation == node.generation)) {
            tree->msg_handlers[out++] = *h;
        }
    }
    tree->msg_handler_count = out;
    if (fn == nullptr) {
        return GATES_OK;
    }
    if (tree->msg_handler_count == tree->msg_handler_cap) {
        gates_u32 cap = tree->msg_handler_cap != 0 ? tree->msg_handler_cap * 2 : 4;
        gates_allocator_t a = tree->alloc;
        proven_result_mem_mut_t r =
            tree->msg_handlers == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof(gates_i_msg_handler_t), alignof(gates_i_msg_handler_t))
                : a.realloc_fn(a.ctx, tree->msg_handlers,
                               tree->msg_handler_cap * sizeof(gates_i_msg_handler_t),
                               cap * sizeof(gates_i_msg_handler_t), alignof(gates_i_msg_handler_t));
        if (!proven_is_ok(r.err)) {
            return r.err;
        }
        tree->msg_handlers = (gates_i_msg_handler_t *)r.value.ptr;
        tree->msg_handler_cap = cap;
    }
    tree->msg_handlers[tree->msg_handler_count++] =
        (gates_i_msg_handler_t){ .index = node.index, .generation = node.generation, .fn = fn,
                                 .user = user };
    return GATES_OK;
}

void gates_i_post_tree_free(gates_tree_t *tree) {
    if (tree->msg_handlers != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, tree->msg_handlers);
    }
    tree->msg_handlers = nullptr;
    tree->msg_handler_count = 0;
    tree->msg_handler_cap = 0;
}
