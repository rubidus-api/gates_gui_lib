# Chapter 1 - Concepts

Headers: `gates/tree.h`, `gates/types.h`, `gates/event.h`.

## The node tree

Everything a window shows is a node in its tree: panels that group, labels, buttons, text
boxes, whole views. The program creates nodes, puts them under parents, and changes them; gates
lays them out, paints them, routes input to them, and describes them to assistive technology.
The tree is retained: the program does not redraw anything by hand, it changes the tree and
the window repaints what changed.

A node is named by a handle, `gates_node_t`: a slot index and a generation. When a node is
destroyed its slot can later hold another node with a new generation, so an old handle never
reaches the new node - every call checks, and a stale handle is refused (`PROVEN_ERR_INVALID_ARG`),
never obeyed. Destroying marks a whole subtree at once; the slots are freed at a safe point
(`gates_tree_flush_destroys`, which a window calls after each input turn), so a handler can
destroy the node that sent its event.

<!-- example: manual/examples/ex_01_handles.c -->
```c
/* manual example (host): handles, generations and copies.
 * expect: stale handle refused; label kept its copy */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t root = gates_tree_root(tree), old, fresh;

    /* Text is copied: the caller's buffer may change or go away at once. */
    char buf[16] = "first";
    if (!gates_is_ok(gates_label_create(tree, root, (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = strlen(buf) }, &old))) return 1;
    strcpy(buf, "XXXXX");

    /* Destroying marks the node; its slot is freed at a safe point. */
    if (!gates_is_ok(gates_node_destroy(tree, old))) return 1;
    (void)gates_tree_flush_destroys(tree);

    /* A new node may reuse the slot, with a new generation: the old handle
     * never reaches it. */
    if (!gates_is_ok(gates_label_create(tree, root, GATES_STR("second"), &fresh))) return 1;
    bool stale_refused = !gates_node_is_valid(tree, old) &&
                         gates_widget_set_text(tree, old, GATES_STR("oops")) == PROVEN_ERR_INVALID_ARG;
    gates_str_t now = gates_widget_text(tree, fresh);
    bool kept = now.size == 6 && memcmp(now.ptr, "second", 6) == 0;
    printf("%s; %s\n", stale_refused ? "stale handle refused" : "STALE HANDLE WORKED",
           kept ? "label kept its copy" : "label changed");
    gates_tree_destroy(tree);
    return stale_refused && kept ? 0 : 1;
}
```

## Ownership: copied or borrowed

Every string the program gives gates is copied when the call returns successfully: labels,
texts, command labels, option labels, names. The caller's buffer is free to change at once.
What gates gives back is borrowed:

| Returned | Valid until |
|---|---|
| `gates_widget_text`, `gates_textbox_text` | the next change of that node |
| `ev->text` in an event | the handler returns |
| a view cell (`gates_cell_t.text`, from your model) | the next call into your model |
| `gates_access_info_t` strings | the next `gates_access_info` call on the tree, or any change |

Callbacks and `user` pointers registered with gates (handlers, models, command functions) are
borrowed for as long as they are registered: keep them alive, or unregister first.

## Errors

Functions that can fail return `gates_err_t` and are marked `[[nodiscard]]`. `GATES_OK` (test it
with `gates_is_ok`) means the whole change happened; any other value means none of it did -
a node that could not be created leaves nothing half-built behind. The codes are proven's:
`PROVEN_ERR_INVALID_ARG` (a bad or stale argument), `PROVEN_ERR_INVALID_STATE` (not now: a
disabled control, a closed dialog), `PROVEN_ERR_NOMEM`, `PROVEN_ERR_OUT_OF_BOUNDS` (a limit).
Out of memory is an ordinary answer, not a crash: the reference applications handle it.

## Events and safe points

A program learns what the person did through events. Each widget has at most one handler
(`gates_widget_set_handler`); when the person changes something, gates queues an event
describing it, and the window delivers the queue at a safe point after the input message has
been handled (`gates_tree_dispatch_events`). Nothing is delivered in the middle of an input
operation, so a handler may change or destroy anything, including its source.

Changes the program makes itself are silent: it already knows. When the rest of the program
should hear about one, `gates_widget_notify` queues an event with origin
`GATES_ORIGIN_PROGRAM`.

<!-- example: manual/examples/ex_02_events.c -->
```c
/* manual example (host): events - what the person did, delivered at a safe point.
 * expect: user checked=1, program checked=0 */
#include <gates/gates.h>

#include <stdio.h>

static char log_text[128];
static int log_len;

static void on_value(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind != GATES_EVENT_VALUE_CHANGED) return;
    log_len += snprintf(log_text + log_len, sizeof log_text - (size_t)log_len, "%s%s checked=%d",
                        log_len > 0 ? ", " : "", ev->origin == GATES_ORIGIN_USER ? "user" : "program",
                        ev->checked ? 1 : 0);
}

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t box;
    if (!gates_is_ok(gates_checkbox_create(tree, gates_tree_root(tree), GATES_STR("Send news"), false, nullptr, nullptr, &box)) ||
        !gates_is_ok(gates_widget_set_handler(tree, box, on_value, nullptr))) {
        return 1;
    }
    /* A person ticks the box (here through the accessibility action, which
     * takes the same path as a click): an event is queued, not delivered. */
    if (!gates_is_ok(gates_access_toggle(tree, box))) return 1;
    /* The window delivers queued events after each input message; without a
     * window, the program does it. */
    (void)gates_tree_dispatch_events(tree, 0);

    /* Setters are silent: the program knows what it did. Announce it
     * explicitly when the rest of the program should hear it. */
    if (!gates_is_ok(gates_checkbox_set_checked(tree, box, false))) return 1;
    if (!gates_is_ok(gates_widget_notify(tree, box))) return 1;
    (void)gates_tree_dispatch_events(tree, 0);

    printf("%s\n", log_text);
    gates_tree_destroy(tree);
    return 0;
}
```

Two rules follow. Never read widget state while painting to find out what changed - follow
events. And never change a model from inside a paint callback; paint only draws.

## Units

Every coordinate and size in the API is a logical unit, 1/96 inch. A window converts once, at
its edge. Chapter 8 has the details; until then, read "units" where you would expect pixels.
