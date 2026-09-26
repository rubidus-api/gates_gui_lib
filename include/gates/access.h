/* gates_gui_lib - accessibility model (plan-0014, RFC-0003 section 10).
 *
 * gates describes every node to assistive technology and automation tools in
 * platform-free terms: a role, a name, a description, states, a value, the
 * actions it accepts, and virtual items for controls that draw their own rows
 * (radio options, choice options, menu entries, view rows). A platform adapter
 * (Win32: UI Automation) turns this into its own objects and events. Items are
 * addressed by (node, item id) - never by a node per row - and item 0 is the
 * node itself.
 *
 * Name, in this order (the first that gives text wins):
 *   1. an explicit name (gates_node_set_access_name);
 *   2. the text of a label tied to it (gates_node_set_labelled_by, like
 *      HTML's <label for>);
 *   3. for a form editor, its field's label (without the " *" required marker;
 *      the REQUIRED state says it);
 *   4. the node's own text: a label's text, a button's label (a bound command's
 *      label), a checkbox's caption, a dialog's title;
 *   5. for an item: its option label, command label, or the row's first cell.
 * Description: a form field's error, then its help, joined by ". ".
 * Automation id: explicit (gates_node_set_automation_id), else "field-<id>" for
 * a form editor, "cmd-<id>" for a button bound to a command, else empty; items
 * are "item-<id>". Ids are meant to be stable across runs of the application.
 *
 * Actions go through the same paths as input: events, commands, limits and
 * read-only rules apply, and they are refused exactly when input would be.
 * Setting a text value is a user edit (TEXT_CHANGED fires). Expanding is a
 * request (a choice opens its list; a tree row asks its model).
 * Views (list, table, tree, log): the rows shown now are the items (and the
 * selected row wherever it is), item id = row id; a row's name is its first
 * cell (a table row: its cells joined by ", "); gates reads cells of shown
 * rows only.
 * Everything here is for the UI thread. Platform-free. */
#ifndef GATES_ACCESS_H
#define GATES_ACCESS_H

#include <gates/tree.h>
#include <gates/geometry.h>
#include <gates/theme.h>

typedef enum gates_role_t {
    GATES_ROLE_NONE = 0,         /* not exposed (layout-only containers) */
    GATES_ROLE_WINDOW,           /* the tree root */
    GATES_ROLE_GROUP,            /* panels that group controls, form rows */
    GATES_ROLE_TEXT,             /* labels, help and error lines */
    GATES_ROLE_BUTTON,
    GATES_ROLE_CHECK_BOX,
    GATES_ROLE_RADIO_GROUP,
    GATES_ROLE_RADIO_ITEM,       /* item of a radio group */
    GATES_ROLE_COMBO_BOX,        /* choice */
    GATES_ROLE_LIST_ITEM,        /* item of a choice, a list or a log */
    GATES_ROLE_EDIT,             /* textbox */
    GATES_ROLE_PROGRESS_BAR,
    GATES_ROLE_SEPARATOR,
    GATES_ROLE_DIALOG,
    GATES_ROLE_MENU,
    GATES_ROLE_MENU_ITEM,
    GATES_ROLE_LIST,             /* view without columns, log */
    GATES_ROLE_TABLE,            /* view with columns */
    GATES_ROLE_TREE,             /* tree view */
    GATES_ROLE_ROW,              /* item of a table */
    GATES_ROLE_TREE_ITEM,        /* item of a tree */
    GATES_ROLE_FORM,
    GATES_ROLE_SCROLL_AREA,      /* scroll layout */
} gates_role_t;

/* State bits. */
#define GATES_ACCESS_FOCUSABLE   0x0001u
#define GATES_ACCESS_FOCUSED     0x0002u
#define GATES_ACCESS_DISABLED    0x0004u
#define GATES_ACCESS_CHECKED     0x0008u
#define GATES_ACCESS_SELECTED    0x0010u
#define GATES_ACCESS_EXPANDABLE  0x0020u
#define GATES_ACCESS_EXPANDED    0x0040u
#define GATES_ACCESS_READ_ONLY   0x0080u
#define GATES_ACCESS_INVALID     0x0100u
#define GATES_ACCESS_REQUIRED    0x0200u
#define GATES_ACCESS_PASSWORD    0x0400u
#define GATES_ACCESS_OFFSCREEN   0x0800u   /* hidden, on another page, scrolled out */
#define GATES_ACCESS_MODAL       0x1000u
#define GATES_ACCESS_BUSY        0x2000u   /* a loading row */

/* Action bits. */
#define GATES_ACCESS_INVOKE      0x01u
#define GATES_ACCESS_TOGGLE      0x02u
#define GATES_ACCESS_SELECT      0x04u
#define GATES_ACCESS_EXPAND      0x08u     /* expand and collapse */
#define GATES_ACCESS_SET_VALUE   0x10u
#define GATES_ACCESS_SCROLL      0x20u
#define GATES_ACCESS_FOCUS       0x40u

/* Live regions: how a change of a node's text is announced. */
typedef enum gates_live_t {
    GATES_LIVE_OFF = 0,
    GATES_LIVE_POLITE,           /* status lines: after the current speech */
    GATES_LIVE_ASSERTIVE,        /* errors: at once */
} gates_live_t;

typedef struct gates_access_info_t {
    gates_role_t role;
    /* Strings are borrowed from the tree: valid until the next
     * gates_access_info call on this tree or the next change to it. Copy
     * what must be kept (two infos cannot be held side by side). */
    gates_str_t name;
    gates_str_t description;     /* error then help (see above) */
    gates_str_t automation_id;
    gates_u32 states;            /* GATES_ACCESS_* state bits */
    gates_u32 actions;           /* GATES_ACCESS_* action bits */
    gates_str_t value;           /* text of an edit (empty for passwords),
                                    selected option of a radio group or choice */
    bool has_range;              /* progress: range_value in range_min..range_max */
    gates_i32 range_value, range_min, range_max;
    gates_rect_t bounds;         /* logical units, window coordinates */
    gates_node_t labelled_by;    /* the tied label (set_labelled_by, a form field), or null */
    gates_live_t live;
    /* Text (edits): caret and selection anchor, UTF-8 byte offsets into value. */
    gates_u32 caret, anchor;
    /* Items: how many the node has (options, entries, visible rows), and for a
     * table or a table row the number of columns. */
    gates_u64 item_count;
    gates_u32 column_count;
    /* An item's place in its whole set (1-based; for a view row its row in
     * the model, of set_size rows), 0 for nodes. */
    gates_u64 set_position, set_size;
    gates_u32 level;             /* a tree row's depth + 1, else 0 */
} gates_access_info_t;

/* Fills *out for (node, item). INVALID_ARG when the node is gone (a stale
 * handle never answers for the node that now uses its slot) or the item is
 * not one of the node's. See the note on the strings above. */
[[nodiscard]] gates_err_t gates_access_info(gates_tree_t *tree, gates_node_t node, gates_u64 item,
                                            gates_access_info_t *out);
/* The node's items in order (0 when it has none), and the item at an index. */
gates_u64 gates_access_item_count(gates_tree_t *tree, gates_node_t node);
gates_u64 gates_access_item_at(gates_tree_t *tree, gates_node_t node, gates_u64 index);

/* -- the accessible tree -------------------------------------------------------
 * What a platform adapter exposes, as (node, item) references: under a node,
 * its shown children in order (hidden nodes and inactive stack pages are left
 * out), then its items; under the root, after those, the open dialogs and
 * menus (a choice's open list is not an element of its own: its entries are
 * the choice's items). Every step answers the null reference (node null)
 * where there is nothing, or when the reference is stale. */
typedef struct gates_access_ref_t {
    gates_node_t node;
    gates_u64 item;              /* 0 = the node itself */
} gates_access_ref_t;

gates_access_ref_t gates_access_parent(gates_tree_t *tree, gates_access_ref_t ref);
gates_access_ref_t gates_access_first_child(gates_tree_t *tree, gates_access_ref_t ref);
gates_access_ref_t gates_access_last_child(gates_tree_t *tree, gates_access_ref_t ref);
gates_access_ref_t gates_access_next(gates_tree_t *tree, gates_access_ref_t ref);
gates_access_ref_t gates_access_prev(gates_tree_t *tree, gates_access_ref_t ref);
/* The deepest element at a point (logical, window coordinates): overlays
 * first, as for the pointer; an item when the point is on one. */
gates_access_ref_t gates_access_at_point(gates_tree_t *tree, gates_point_t p);

/* Where keyboard focus is, as an element: the highlighted entry of the open
 * menu (or of a choice's open list, as the choice's item), else the focused
 * node - for a radio group, its selected option. Null when nothing has focus. */
gates_access_ref_t gates_access_focus_ref(gates_tree_t *tree);

/* Application-supplied properties (copied). An empty string clears. */
[[nodiscard]] gates_err_t gates_node_set_access_name(gates_tree_t *tree, gates_node_t node,
                                                     gates_str_t name);
/* Ties a visible label to a control: the label's text becomes the control's
 * name, and assistive technology can move between them. Null unties. */
[[nodiscard]] gates_err_t gates_node_set_labelled_by(gates_tree_t *tree, gates_node_t node,
                                                     gates_node_t label);
[[nodiscard]] gates_err_t gates_node_set_automation_id(gates_tree_t *tree, gates_node_t node,
                                                       gates_str_t id);
[[nodiscard]] gates_err_t gates_node_set_live(gates_tree_t *tree, gates_node_t node,
                                              gates_live_t live);

/* -- actions (refused as input would be: INVALID_STATE disabled/inert,
 *    PERMISSION read-only, INVALID_ARG not accepted by the node) -------------- */
[[nodiscard]] gates_err_t gates_access_invoke(gates_tree_t *tree, gates_node_t node, gates_u64 item);
[[nodiscard]] gates_err_t gates_access_toggle(gates_tree_t *tree, gates_node_t node);
[[nodiscard]] gates_err_t gates_access_select(gates_tree_t *tree, gates_node_t node, gates_u64 item);
[[nodiscard]] gates_err_t gates_access_expand(gates_tree_t *tree, gates_node_t node, gates_u64 item,
                                              bool expand);
[[nodiscard]] gates_err_t gates_access_set_value(gates_tree_t *tree, gates_node_t node,
                                                 gates_str_t text);
/* Focus a node; for an item, selecting it is how it takes focus. */
[[nodiscard]] gates_err_t gates_access_focus(gates_tree_t *tree, gates_node_t node, gates_u64 item);

/* Text of an edit (for text APIs such as the UIA Text pattern). Offsets are
 * UTF-8 bytes into the info's value. The window rectangle of text[start, end)
 * on screen, clipped to the visible part (false when none of it shows; an
 * empty range gives a zero-width rectangle at its place); the offset nearest
 * a point; and a selection made through the model (anchor and caret; no text
 * changes; refused as input would be). Password boxes answer nothing. */
bool gates_access_text_rect(gates_tree_t *tree, gates_node_t node, gates_u32 start, gates_u32 end,
                            gates_rect_t *out);
gates_u32 gates_access_text_offset_at(gates_tree_t *tree, gates_node_t node, gates_point_t p);
[[nodiscard]] gates_err_t gates_access_select_text(gates_tree_t *tree, gates_node_t node, gates_u32 anchor,
                                                   gates_u32 caret);

/* Vertical scrolling of a view or a scroll area, in 1/GATES_ACCESS_SCROLL_MAX
 * of its range: *pos is where it is, *page how much of the whole shows.
 * False when the node cannot scroll (everything fits). */
#define GATES_ACCESS_SCROLL_MAX 10000u
bool gates_access_scroll_info(gates_tree_t *tree, gates_node_t node, gates_u32 *pos, gates_u32 *page);
[[nodiscard]] gates_err_t gates_access_scroll_to(gates_tree_t *tree, gates_node_t node, gates_u32 pos);
/* By `amount` lines (rows), or pages when `page`; negative goes up. */
[[nodiscard]] gates_err_t gates_access_scroll_by(gates_tree_t *tree, gates_node_t node, gates_i32 amount,
                                                 bool page);

/* -- changes, for the platform adapter ----------------------------------------
 * While enabled, the tree records what changed: a node whose state, text or
 * items changed (CHANGED), a parent whose children changed (STRUCTURE), focus
 * leaving and reaching a node (FOCUS_LOST then FOCUS_GAINED), a node destroyed
 * (REMOVED, recorded before its slot can be reused), a live region's new text
 * (LIVE), and announcements. One entry per (node, item, kind) until taken; at
 * most GATES_ACCESS_CHANGES_MAX, beyond that `overflow` is set and the adapter
 * should treat the whole tree as changed. */
#define GATES_ACCESS_CHANGES_MAX 256u
typedef enum gates_access_change_kind_t {
    GATES_ACCESS_CHANGED = 1,
    GATES_ACCESS_STRUCTURE,
    GATES_ACCESS_FOCUS_LOST,
    GATES_ACCESS_FOCUS_GAINED,
    GATES_ACCESS_REMOVED,
    GATES_ACCESS_LIVE,
    GATES_ACCESS_ANNOUNCE,       /* gates_access_announce; node = root */
} gates_access_change_kind_t;

typedef struct gates_access_change_t {
    gates_access_change_kind_t kind;
    gates_node_t node;
    gates_u64 item;
} gates_access_change_t;

void gates_access_enable(gates_tree_t *tree, bool enable);
bool gates_access_enabled(const gates_tree_t *tree);
/* Moves up to `cap` recorded changes into out (oldest first); returns how
 * many. *overflow (may be null) reports lost changes since the last take. */
gates_u32 gates_access_take_changes(gates_tree_t *tree, gates_access_change_t *out, gates_u32 cap,
                                    bool *overflow);
/* An announcement for screen readers (copied); the adapter speaks it with its
 * platform's notification. The latest text is gates_access_announcement. */
[[nodiscard]] gates_err_t gates_access_announce(gates_tree_t *tree, gates_str_t text, bool assertive);
gates_str_t gates_access_announcement(const gates_tree_t *tree, bool *assertive);

/* -- the enforced rules --------------------------------------------------------
 * gates_access_audit checks a laid-out tree against the rules gates holds its
 * applications and examples to; tests and the examples run it. */
typedef enum gates_access_rule_t {
    GATES_RULE_NO_NAME = 1,          /* an interactive node without an accessible name */
    GATES_RULE_TARGET_SIZE,          /* a pointer target below 24 x 24 units (WCAG 2.2 2.5.8) */
    GATES_RULE_KEYBOARD,             /* an interactive node the keyboard cannot reach */
    GATES_RULE_DUPLICATE_ID,         /* two nodes with the same automation id in a tree */
    GATES_RULE_CONTRAST,             /* a theme text/cue pair below its ratio (node = null) */
    GATES_RULE_FOCUS_CUE,            /* a theme focus or error cue thinner than 2 units */
} gates_access_rule_t;

typedef struct gates_access_issue_t {
    gates_access_rule_t rule;
    gates_node_t node;
    gates_u64 item;
} gates_access_issue_t;

#define GATES_ACCESS_MIN_TARGET 24

/* Returns the number of issues found (all of them, even beyond `cap`). Theme
 * may be null (theme rules skipped). The tree must have been laid out. */
gates_u32 gates_access_audit(gates_tree_t *tree, const gates_theme_t *theme,
                             gates_access_issue_t *out, gates_u32 cap);
/* The theme rules alone. */
gates_u32 gates_theme_audit(const gates_theme_t *theme, gates_access_issue_t *out, gates_u32 cap);

#endif /* GATES_ACCESS_H */
