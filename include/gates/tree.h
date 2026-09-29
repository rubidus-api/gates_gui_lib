/* gates_gui_lib — retained node tree core (RFC-0001 §6-§8, Phase 0).
 *
 * Phase 0 scope: node pool with generation handles, O(1) tree links, subtree
 * destroy with deferred free at explicit safe points. The tree is owned by
 * gates_tree_t; from Phase 1 on, gates_window_t owns one of these.
 *
 * Threading: a gates_tree_t is single-thread-owned (UI thread; RFC-0001 §17). */
#ifndef GATES_TREE_H
#define GATES_TREE_H

#include <gates/types.h>

typedef enum gates_node_kind_t {
    GATES_NODE_CUSTOM = 0,
    GATES_NODE_PANEL,
    GATES_NODE_LABEL,
    GATES_NODE_BUTTON,
    GATES_NODE_CHECKBOX,
    GATES_NODE_TEXTBOX,
    GATES_NODE_DIALOG,       /* overlay: modal dialog (plan-0009 stage 2) */
    GATES_NODE_MENU,         /* overlay: context menu of commands */
    GATES_NODE_RADIO,        /* radio group: one node, one choice among options (plan-0010) */
    GATES_NODE_CHOICE,       /* dropdown choice: shows one option, opens a list */
    GATES_NODE_SEPARATOR,    /* a thin line between groups of controls */
    GATES_NODE_PROGRESS,     /* progress bar (per-mille) */
    GATES_NODE_FORM,         /* form: labelled field rows (gates/form.h) */
    GATES_NODE_VIEW,         /* virtual list/table over an application model (gates/view.h) */
    GATES_NODE_MENUBAR,      /* menu bar: titles over command menus (gates/frame.h) */
    GATES_NODE_TOOLBAR,      /* toolbar: buttons bound to commands */
    GATES_NODE_STATUSBAR,    /* status bar: a row of label segments */
    GATES_NODE_TABS,         /* tabs: a strip of titles over a stack of pages */
    GATES_NODE_TABSTRIP,     /* the strip of a tabs node (made by gates_tabs_create) */
    GATES_NODE_SPIN,         /* spin box: a text box and arrows (gates/inputs.h) */
    GATES_NODE_SPINARROWS,   /* the arrows of a spin box */
    GATES_NODE_SLIDER,       /* slider (gates/inputs.h) */
    GATES_NODE_GROUP,        /* group box: a header and a content panel (gates/widget.h) */
    GATES_NODE_GROUPHEAD,    /* a group box's title (a Tab stop when collapsible) */
    GATES_NODE_IMAGE,        /* an image (gates/image.h) */
    /* Semantic widgets arrive in Phase 4. */
} gates_node_kind_t;

typedef struct gates_tree gates_tree_t;

typedef struct gates_tree_desc_t {
    /* Zero-initialized allocator -> proven heap allocator. All tree memory
     * flows through this allocator; there is no hidden malloc. */
    gates_allocator_t allocator;
    /* 0 -> implementation default. */
    gates_u32 initial_capacity;
} gates_tree_desc_t;

typedef struct gates_node_desc_t {
    gates_node_kind_t kind;
    void *user_data;
} gates_node_desc_t;

/* -- lifecycle ----------------------------------------------------------- */

/* Creates a tree with a live root node (kind GATES_NODE_CUSTOM). */
[[nodiscard]] gates_err_t gates_tree_create(const gates_tree_desc_t *desc,
                                            gates_tree_t **out_tree);

/* Frees every slot and the tree itself. Handles become meaningless. */
void gates_tree_destroy(gates_tree_t *tree);

gates_node_t gates_tree_root(const gates_tree_t *tree);

/* True only for a live, non-destroy-pending node whose generation matches
 * (RFC-0001 §6.1, §30: a destroy_pending node is no longer a valid target). */
bool gates_node_is_valid(const gates_tree_t *tree, gates_node_t node);

/* -- node ops (RFC-0001 §7) ---------------------------------------------- */

/* parent == GATES_NODE_NULL creates a detached node (attach with append). */
[[nodiscard]] gates_err_t gates_node_create(gates_tree_t *tree, gates_node_t parent,
                                            const gates_node_desc_t *desc,
                                            gates_node_t *out_node);

/* Subtree destroy (§8): marks node + descendants destroy_pending and unlinks
 * from the parent. Slots are freed at the next gates_tree_flush_destroys().
 * The root cannot be destroyed. */
[[nodiscard]] gates_err_t gates_node_destroy(gates_tree_t *tree, gates_node_t node);

/* Detaches node from its parent; the subtree below it stays intact. */
[[nodiscard]] gates_err_t gates_node_remove(gates_tree_t *tree, gates_node_t node);

/* Appends a detached node as parent's last child. */
[[nodiscard]] gates_err_t gates_node_append(gates_tree_t *tree, gates_node_t parent,
                                            gates_node_t child);

/* Inserts a detached node before `before` (a child of parent).
 * before == GATES_NODE_NULL appends (DOM insertBefore semantics). */
[[nodiscard]] gates_err_t gates_node_insert_before(gates_tree_t *tree, gates_node_t parent,
                                                   gates_node_t child, gates_node_t before);

/* Moves node (attached or detached) under new_parent (append position).
 * Rejects making a node a descendant of itself (cycle prevention, §30);
 * link updates are O(1), the cycle check walks new_parent's ancestors. */
[[nodiscard]] gates_err_t gates_node_reparent(gates_tree_t *tree, gates_node_t node,
                                              gates_node_t new_parent);

/* Safe point (§8): frees all destroy_pending slots (generation bump + free
 * list). Callers from Phase 2 on: after event dispatch, before layout,
 * at frame end. */
[[nodiscard]] gates_err_t gates_tree_flush_destroys(gates_tree_t *tree);

/* -- introspection -------------------------------------------------------- */

gates_node_t gates_node_parent(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_node_first_child(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_node_last_child(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_node_prev_sibling(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_node_next_sibling(const gates_tree_t *tree, gates_node_t node);
gates_u32 gates_node_child_count(const gates_tree_t *tree, gates_node_t node);
gates_node_kind_t gates_node_kind(const gates_tree_t *tree, gates_node_t node);
void *gates_node_user_data(const gates_tree_t *tree, gates_node_t node);

/* Hidden (plan-0010): a hidden node and everything below it take no space in
 * layout, are not painted, not hit and not focusable; focus, a press or a drag
 * inside it are let go, and a choice's open list inside it closes. The flag is
 * the node's own; a child of a hidden node keeps its flag but is not shown.
 * A hidden page of a stack still counts as a page. The root and overlays
 * (dialogs, menus) cannot be hidden (INVALID_ARG). */
[[nodiscard]] gates_err_t gates_node_set_hidden(gates_tree_t *tree, gates_node_t node,
                                                bool hidden);
bool gates_node_hidden(const gates_tree_t *tree, gates_node_t node);

/* Font (RFC-0004): a gates_font_t from gates/text.h - GATES_FONT_UI (the
 * platform's proportional UI face, the default), GATES_FONT_MONO (fixed
 * pitch), or GATES_FONT_INHERIT (-1, take the parent's). Like CSS
 * font-family, a node's font applies to everything under it that does not
 * choose its own. Dialogs and menus are not under the root: they start from
 * GATES_FONT_UI unless set on them. INVALID_ARG for other values. */
[[nodiscard]] gates_err_t gates_node_set_font(gates_tree_t *tree, gates_node_t node, gates_i32 font);
/* The effective font: the node's own, else its nearest ancestor's, else GATES_FONT_UI. */
gates_i32 gates_node_font(const gates_tree_t *tree, gates_node_t node);

/* Counters for tests and diagnostics. */
gates_u32 gates_tree_live_count(const gates_tree_t *tree);     /* valid nodes */
gates_u32 gates_tree_pending_count(const gates_tree_t *tree);  /* awaiting flush */
gates_u32 gates_tree_capacity(const gates_tree_t *tree);       /* slot array size */

/* -- dirty tracking (v1: any layout-dirty relayouts from root; any
 *    paint-dirty repaints the window) ------------------------------------- */

#define GATES_TREE_DIRTY_LAYOUT 0x1u
#define GATES_TREE_DIRTY_PAINT  0x2u

/* -- keyboard focus (minimal; scopes and tab traversal are Phase 4) -------- */

gates_node_t gates_tree_focus(const gates_tree_t *tree);
/* GATES_NODE_NULL clears focus. Marks paint dirty when the focus changes. */
void gates_tree_set_focus(gates_tree_t *tree, gates_node_t node);

gates_u32 gates_tree_dirty(const gates_tree_t *tree);
void gates_tree_clear_dirty(gates_tree_t *tree, gates_u32 bits);

#endif /* GATES_TREE_H */
