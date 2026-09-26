/* gates_gui_lib - posting from worker threads to the UI (plan-0012, RFC-0003
 * section 9).
 *
 * A worker never touches the tree. It holds a sender (from gates_app_sender
 * on the UI thread, before the worker starts) and posts messages: a target
 * (a tree and a node in it), a kind, and a payload whose ownership passes to
 * gates when the post succeeds. The UI thread delivers messages at a safe
 * point, a bounded number per turn, to the target node's message handler, then
 * releases the payload. Every payload is released exactly once: after delivery,
 * when its target is gone (stale), when it is replaced, or when the app shuts
 * down. A post that fails (FULL, CLOSED, too large) leaves the payload with the
 * caller.
 *
 * Posting never blocks on the UI and never allocates. The queue has a limit
 * in messages and in payload bytes; a full queue answers GATES_POST_FULL
 * (try again later, drop, or coalesce); a closed one GATES_POST_CLOSED (stop
 * producing and release the sender). A sender stays valid on every thread
 * until its last reference is released, even after the app is destroyed.
 *
 * Release functions may run on a posting thread or on the thread that shuts
 * the app down: they must only free the payload, never touch UI state.
 * gates_sender_retain / _release / _post may be called from any thread; every
 * other function here is for the UI thread. The allocator given to the app
 * must be thread-safe (the default one is). Platform-free. */
#ifndef GATES_POST_H
#define GATES_POST_H

#include <gates/app.h>
#include <gates/tree.h>

#define GATES_POST_FULL   PROVEN_ERR_AGAIN   /* queue full: the caller keeps the payload */
#define GATES_POST_CLOSED PROVEN_ERR_EOF     /* app shutting down: stop and release the sender */

#define GATES_POST_DEFAULT_MESSAGES 1024u
#define GATES_POST_DEFAULT_BYTES    ((gates_usize_t)1 << 20)
#define GATES_POST_PER_TURN         64u      /* messages delivered per event turn */

typedef struct gates_sender gates_sender_t;

/* Where a message goes: a tree (by its serial number, unique for the life of
 * the process) and a node in it. Build it on the UI thread and give it to the
 * worker. */
typedef struct gates_target_t {
    gates_u64 tree;
    gates_node_t node;
} gates_target_t;

gates_u64 gates_tree_serial(const gates_tree_t *tree);
gates_target_t gates_target(const gates_tree_t *tree, gates_node_t node);

typedef void (*gates_payload_release_fn)(void *payload, void *ctx);

typedef struct gates_message_t {
    gates_target_t target;
    gates_u32 kind;              /* the application's own message kinds */
    void *payload;               /* may be null */
    gates_usize_t bytes;         /* counted against the byte limit */
    gates_payload_release_fn release; /* may be null (nothing to free) */
    void *release_ctx;
    /* A state snapshot: replaces a queued message with the same target and
     * kind (the replaced payload is released) instead of queueing another. */
    bool replaceable;
} gates_message_t;

/* The app's sender, retained for the caller (release it when done). */
[[nodiscard]] gates_err_t gates_app_sender(gates_app_t *app, gates_sender_t **out_sender);
void gates_sender_retain(gates_sender_t *sender);
void gates_sender_release(gates_sender_t *sender);
/* OK: gates owns the payload now. GATES_POST_FULL / GATES_POST_CLOSED, or
 * OUT_OF_BOUNDS for a message larger than the byte limit: the caller keeps it. */
[[nodiscard]] gates_err_t gates_sender_post(gates_sender_t *sender, const gates_message_t *msg);

/* The node's message handler (null removes it). The payload is borrowed for
 * the call and released right after it. Messages for a node without a handler
 * are released undelivered. A handler may post again: that message is
 * delivered in a later turn, never recursively. */
typedef void (*gates_message_fn)(gates_tree_t *tree, gates_node_t node, gates_u32 kind,
                                 void *payload, gates_usize_t bytes, void *user);
[[nodiscard]] gates_err_t gates_node_set_message_handler(gates_tree_t *tree, gates_node_t node,
                                                         gates_message_fn fn, void *user);

/* -- for platform backends and tests ---------------------------------------------
 *
 * The core has no threads of its own: the platform supplies a lock (kept
 * inside the sender, so it lives as long as the sender) and a wake-up that
 * makes the UI thread call gates_sender_dispatch soon. Wake is called with the
 * lock held, only when the queue goes from empty to non-empty, and never
 * after gates_sender_close returns. */
typedef struct gates_sync_t {
    gates_err_t (*lock_init)(void **lock);
    void (*lock_fini)(void *lock);
    void (*lock)(void *lock);
    void (*unlock)(void *lock);
} gates_sync_t;

typedef struct gates_sender_desc_t {
    gates_allocator_t allocator; /* zero -> proven heap (thread-safe) */
    gates_sync_t sync;           /* required */
    void (*wake)(void *ctx);     /* may be null (the UI polls) */
    void *wake_ctx;
    gates_u32 max_messages;      /* 0 -> GATES_POST_DEFAULT_MESSAGES */
    gates_usize_t max_bytes;     /* 0 -> GATES_POST_DEFAULT_BYTES */
} gates_sender_desc_t;

/* A new sender with one reference (the creator's). */
[[nodiscard]] gates_err_t gates_sender_create(const gates_sender_desc_t *desc,
                                              gates_sender_t **out_sender);
/* Trees that may receive messages (a tree is attached to at most one sender;
 * gates_tree_destroy detaches it, and its queued messages become stale). */
[[nodiscard]] gates_err_t gates_sender_attach(gates_sender_t *sender, gates_tree_t *tree);
void gates_sender_detach(gates_sender_t *sender, gates_tree_t *tree);
/* Delivers at most `max` messages (0 -> GATES_POST_PER_TURN); returns how many
 * remain queued (the platform schedules another turn when that is not 0). */
gates_u32 gates_sender_dispatch(gates_sender_t *sender, gates_u32 max);
/* Stops accepting posts (CLOSED from now on), releases every queued payload
 * on this thread, detaches all trees. The sender memory stays until the last
 * reference is released. */
void gates_sender_close(gates_sender_t *sender);
gates_u32 gates_sender_pending(gates_sender_t *sender);
gates_usize_t gates_sender_pending_bytes(gates_sender_t *sender);

#endif /* GATES_POST_H */
