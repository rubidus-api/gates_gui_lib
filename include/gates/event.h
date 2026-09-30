/* gates_gui_lib - typed change notifications (RFC-0003 section 4.1, plan-0007).
 *
 * The application learns what a person did through events, not by polling
 * widgets during paint. User edits queue an event after the edit succeeded;
 * the window delivers queued events at a safe point after the input message
 * (gates_tree_dispatch_events). Programmatic setters are silent; call
 * gates_widget_notify to announce a programmatic change explicitly.
 *
 * Handlers run on the UI thread. A handler may change or destroy any node,
 * including its source; events for nodes that are gone are skipped. Nested
 * changes queue further events for the next dispatch call instead of being
 * delivered recursively.
 *
 * The older per-widget callbacks (gates_button_create's on_click,
 * gates_checkbox_create's on_toggle) still run synchronously inside pointer
 * routing. New code should use handlers. Platform-free. */
#ifndef GATES_EVENT_H
#define GATES_EVENT_H

#include <gates/tree.h>

typedef enum gates_event_kind_t {
    GATES_EVENT_NONE = 0,
    GATES_EVENT_TEXT_CHANGED,    /* textbox committed text changed */
    GATES_EVENT_PREEDIT_CHANGED, /* textbox IME composition changed (not committed text) */
    GATES_EVENT_VALUE_CHANGED,   /* checkbox checked state, or radio/choice selection, changed */
    GATES_EVENT_ACTIVATED,       /* button activated, or a view's row (ev->item) */
    /* textbox: an edit was refused because it would pass the maximum length
     * (plan-0008). The box keeps the input as an offer: ask the person, then
     * gates_textbox_accept_fit or gates_textbox_discard_rejected. */
    GATES_EVENT_LIMIT_EXCEEDED,
    /* dialog closed once, with ev->result = gates_dialog_result_t (plan-0009) */
    GATES_EVENT_DIALOG_CLOSED,
    /* menu closed once; ev->result = the command id it invoked, or 0 */
    GATES_EVENT_MENU_CLOSED,
    /* view (plan-0011): the selected item changed; ev->item = its id (0 = none) */
    GATES_EVENT_SELECTION_CHANGED,
    /* view: a header cell was clicked; ev->result = the column id (the model sorts) */
    GATES_EVENT_SORT_REQUESTED,
    /* tree view: open (ev->result = 1) or close (0) the row ev->item (the model decides) */
    GATES_EVENT_EXPAND_REQUESTED,
    /* view (plan-0021): a person changed a cell; ev->item = the row, ev->result = the column id */
    GATES_EVENT_CELL_EDITED,
    /* log view (0.8.0): a person's scrolling started or stopped following new
     * lines; ev->result = 1 following, 0 not (read at delivery) */
    GATES_EVENT_FOLLOW_CHANGED,
} gates_event_kind_t;

typedef enum gates_event_origin_t {
    GATES_ORIGIN_USER = 0,       /* pointer, keyboard or IME input */
    GATES_ORIGIN_PROGRAM,        /* gates_widget_notify */
} gates_event_origin_t;

typedef struct gates_event_t {
    gates_event_kind_t kind;
    gates_event_origin_t origin;
    gates_node_t source;
    gates_u32 revision;          /* the source's change counter when delivered */
    /* TEXT_CHANGED: committed text; PREEDIT_CHANGED: composition (empty when it
     * ended); LIMIT_EXCEEDED: the refused input. A copy owned by the
     * dispatcher, valid until the handler returns, even if the handler changes
     * the source. Always empty for a password box. */
    gates_str_t text;
    bool checked;                /* VALUE_CHANGED */
    gates_u32 limit;             /* LIMIT_EXCEEDED: the box's maximum, in UTF-8 bytes */
    gates_u32 fit_bytes;         /* LIMIT_EXCEEDED: bytes of the input that would fit */
    /* DIALOG_CLOSED: dialog result; MENU_CLOSED: command id or 0;
     * VALUE_CHANGED of a radio group or choice: the selected option id (0 = none);
     * SORT_REQUESTED and CELL_EDITED: the column id. */
    gates_u32 result;
    /* SELECTION_CHANGED: the selected item id; ACTIVATED from a view,
     * EXPAND_REQUESTED and CELL_EDITED: the row's id. */
    gates_u64 item;
    /* VALUE_CHANGED of a spin box or slider: the value (plan-0019). */
    gates_i64 value;
} gates_event_t;

typedef void (*gates_event_fn)(gates_tree_t *tree, const gates_event_t *ev, void *user);

/* One handler per widget (textbox, checkbox, button, radio, choice, view, dialog, menu); null removes it and drops
 * events it has not received yet. `user` is borrowed while registered.
 * Changes of the same kind and origin coalesce to the latest state before
 * delivery; activations never coalesce. Without a handler nothing is queued. */
[[nodiscard]] gates_err_t gates_widget_set_handler(gates_tree_t *tree, gates_node_t node,
                                                   gates_event_fn fn, void *user);

/* Queues a PROGRAM-origin event describing the widget's current state (text,
 * checked, or an activation for a button). OK and nothing queued when the
 * widget has no handler; INVALID_ARG for widgets without events. */
[[nodiscard]] gates_err_t gates_widget_notify(gates_tree_t *tree, gates_node_t node);

/* Change counter: increments whenever the committed text or checked state
 * changes, by the user or the program. Identical values do not count. */
gates_u32 gates_widget_revision(const gates_tree_t *tree, gates_node_t node);

/* Delivers queued events in order, at most `max_events` (0 = all that were
 * queued when the call started). Returns how many remain queued; the caller
 * schedules another call rather than looping. Calling it from inside a
 * handler delivers nothing. */
gates_u32 gates_tree_dispatch_events(gates_tree_t *tree, gates_u32 max_events);
gates_u32 gates_tree_pending_events(const gates_tree_t *tree);

/* -- bubbling (plan-0019, RFC-0005 A10) ---------------------------------------
 * A bubble handler on any node (a panel, a form, the root) receives the events
 * of its descendants that have no handler of their own - the nearest such
 * ancestor only; ev->source is the descendant. An overlay (a dialog or menu) is
 * a root of its own: its content bubbles to handlers inside it, never to the
 * window below. null removes it. `user` is borrowed while registered. */
[[nodiscard]] gates_err_t gates_node_set_bubble_handler(gates_tree_t *tree, gates_node_t node,
                                                        gates_event_fn fn, void *user);

/* -- deferred calls (plan-0019, RFC-0005 A8) ----------------------------------
 * gates_tree_defer asks for fn(tree, key, user) to run once at the next safe
 * point, however often it is asked before then: one call per key, with the
 * fn and user of the latest request ("recompute the total after any field
 * changed"). The safe point is gates_tree_dispatch_events, after the queued
 * events are delivered; a call asked for during a deferred call runs in the
 * next dispatch (its return value counts it as remaining work). */
typedef void (*gates_defer_fn)(gates_tree_t *tree, gates_u32 key, void *user);
[[nodiscard]] gates_err_t gates_tree_defer(gates_tree_t *tree, gates_u32 key, gates_defer_fn fn,
                                           void *user);
/* true when a call for `key` was waiting and is now dropped. */
bool gates_tree_cancel_defer(gates_tree_t *tree, gates_u32 key);

#endif /* GATES_EVENT_H */
