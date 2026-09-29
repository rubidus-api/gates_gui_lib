/* gates_gui_lib — core-internal tree structures, shared by src/core modules
 * (tree, widget, layout, paint, hit). Never installed; public API stays in
 * include/gates. Platform-free. */
#ifndef GATES_TREE_INTERNAL_H
#define GATES_TREE_INTERNAL_H

#include <gates/tree.h>
#include <gates/geometry.h>
#include <gates/text_edit.h>
#include <gates/event.h>
#include <gates/clipboard.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include <gates/draw.h>
#include <gates/theme.h>
#include <gates/view.h>
#include <gates/post.h>
#include <gates/timer.h>
#include <gates/access.h>
#include <gates/input.h>
#include <gates/layout.h>
#include <gates/image.h>
#include <gates/task.h>

/* One queued notification: kind/origin plus the source's handle. The payload
 * is read from the widget when the event is delivered (latest state). */
typedef struct gates_i_event_t {
    gates_u32 node_index;
    gates_u32 generation;
    gates_u8 kind;           /* gates_event_kind_t, or GATES_I_EVENT_COMMAND */
    gates_u8 origin;         /* gates_event_origin_t */
    gates_u32 aux;           /* GATES_I_EVENT_COMMAND: the command id (node = scope);
                              * SORT_REQUESTED: the column id */
    gates_u64 item;          /* ACTIVATED from a view: the row's id at queue time */
} gates_i_event_t;

/* Queue-only kind for command invocations (never a public gates_event_kind_t). */
#define GATES_I_EVENT_COMMAND 0x40u

/* One open overlay (gates_overlay.c). */
#define GATES_I_OVERLAY_MAX 8u
typedef struct gates_i_overlay_t {
    gates_u32 index;
    gates_u32 generation;
    gates_node_kind_t kind;      /* GATES_NODE_DIALOG or GATES_NODE_MENU */
    gates_point_t at;            /* menu anchor */
    gates_i32 above_y;           /* menu: bottom edge when it must open upwards */
    gates_u32 prev_focus_index;  /* dialog: focus to restore (GATES_NONE = none) */
    gates_u32 prev_focus_generation;
    gates_u32 prev_scope;        /* dialog: focus_scope to restore */
    bool needs_focus;            /* dialog: focus its first control at the next layout */
    bool pressed_inside;         /* menu: a press started inside it */
} gates_i_overlay_t;

/* A node's message handler (gates_post.c, plan-0012). */
typedef struct gates_i_msg_handler_t {
    gates_u32 index;
    gates_u32 generation;
    gates_message_fn fn;
    void *user;
} gates_i_msg_handler_t;

/* One timer (gates_timer.c, plan-0012). */
typedef struct gates_i_timer_t {
    gates_timer_id_t id;
    gates_u32 index;
    gates_u32 generation;
    gates_u32 interval;
    bool repeat;
    bool alive;
    gates_u64 due;
    gates_timer_fn fn;
    void *user;
} gates_i_timer_t;

/* Accessibility properties the application set on a node (gates_access.c). */
typedef struct gates_i_access_prop_t {
    gates_u32 index;
    gates_u32 generation;
    gates_u8 *name;
    gates_u32 name_len;
    gates_u8 *id;
    gates_u32 id_len;
    gates_live_t live;
    gates_node_t labelled_by;
    gates_u8 *tip;               /* tooltip (plan-0018) */
    gates_u32 tip_len;
} gates_i_access_prop_t;

/* One registered command (gates_command.c). */
typedef struct gates_i_command_t {
    bool alive;
    gates_u32 scope_index;
    gates_u32 scope_generation;
    gates_command_id_t id;
    gates_u8 *label;         /* owned copy */
    gates_u32 label_len;
    gates_shortcut_t shortcut;
    gates_command_role_t role;
    bool enabled;
    bool checked;
    gates_command_fn invoke;
    void *user;
    gates_u32 icon;              /* plan-0020: 0 = none */
} gates_i_command_t;

#define GATES_NONE UINT32_MAX

/* Widget chrome geometry, shared by layout (intrinsic size), paint (drawing)
 * and hit testing (cell math) so the three cannot disagree. */
#define GATES_BUTTON_PAD_X    8
#define GATES_BUTTON_PAD_Y    4
#define GATES_BUTTON_BORDER   1
#define GATES_CHECK_BOX      12
#define GATES_CHECK_GAP       6
#define GATES_TEXTBOX_PAD_X   4
#define GATES_TEXTBOX_PAD_Y   3
#define GATES_TEXTBOX_BORDER  1
#define GATES_SEPARATOR_SPACE 4   /* space on each side of a separator's line */
#define GATES_PROGRESS_H     10

/* Dirty bits (RFC-0001 dirty layout / dirty paint; v1 resolves them as
 * full relayout / full repaint — see plan-0003 decision 6). */
#define GATES_DIRTY_LAYOUT 0x1u
#define GATES_DIRTY_PAINT  0x2u

/* Layout kinds (RFC-0001 §11). Values mirror the public gates_layout_t. */
typedef enum gates_layout_kind_i {
    GATES_LAYOUT_NONE = 0,
    GATES_LAYOUT_ABSOLUTE,
    GATES_LAYOUT_ROW,
    GATES_LAYOUT_COLUMN,
    GATES_LAYOUT_STACK,
    GATES_LAYOUT_SPLIT,
    GATES_LAYOUT_SCROLL,
    GATES_LAYOUT_FORM,
    GATES_LAYOUT_GRID,
    GATES_LAYOUT_WRAP,
} gates_layout_kind_i;

/* Non-widget drag targets (split handle, scroll thumb). */
typedef enum gates_drag_kind_i {
    GATES_DRAG_NONE = 0,
    GATES_DRAG_SPLIT,
    GATES_DRAG_SCROLL_THUMB,
    GATES_DRAG_TEXT_SELECT,
    GATES_DRAG_VIEW_VTHUMB,      /* plan-0011: a view's scrollbar thumbs and a header edge */
    GATES_DRAG_VIEW_HTHUMB,
    GATES_DRAG_VIEW_COLUMN,
    GATES_DRAG_SLIDER,           /* plan-0019: a slider's thumb */
    GATES_DRAG_EDITOR_SELECT,    /* plan-0022: selecting text in an editor, its scrollbar thumbs */
    GATES_DRAG_EDITOR_VTHUMB,
    GATES_DRAG_EDITOR_HTHUMB,
} gates_drag_kind_i;

typedef enum gates_align_i {
    GATES_ALIGN_STRETCH = 0,
    GATES_ALIGN_START,
    GATES_ALIGN_CENTER,
    GATES_ALIGN_END,
} gates_align_i;

/* One option of a radio group or choice (plan-0010); labels live in the same
 * block as the array. */
typedef struct gates_i_option_t {
    gates_u32 id;
    const gates_u8 *label;
    gates_u32 label_len;
    bool disabled;
} gates_i_option_t;

/* Widget state (label/button/checkbox payload; RFC-0001 §6.2 state_index). */
typedef struct gates_widget_state_t {
    bool in_use;
    gates_u32 next_free;

    gates_u8 *text;          /* owned copy (tree allocator) */
    gates_u32 text_len;
    bool checked;
    bool disabled;

    /* Textbox only: edit core plus its single-line view state. */
    gates_text_edit_t *edit;
    gates_u32 cols;          /* intrinsic width in average character widths */
    gates_i32 view_x;        /* horizontal scroll, logical units (paint keeps it) */
    gates_u32 ime_cursor;    /* IME cursor, bytes into the preedit (plan-0006) */
    gates_rect_t caret_rect; /* last painted caret, window coordinates */
    bool caret_valid;        /* caret_rect is from a paint of the focused box */
    /* Editing policy (plan-0008). */
    bool read_only;
    bool password;
    gates_u32 max_bytes;     /* 0 = unlimited */
    struct gates_i_undo *undo; /* lazily allocated history, null = empty */
    /* Offer kept after LIMIT_EXCEEDED: the refused input and where it was to go. */
    gates_u8 *offer;
    gates_u32 offer_len;
    gates_u32 offer_begin;
    gates_u32 offer_end;
    gates_u32 offer_revision;
    gates_u32 offer_fit;

    gates_u32 revision;      /* committed text / checked changes (plan-0007) */
    bool not_focusable;      /* taken out of the Tab order (plan-0009) */
    /* Button bound to a command (plan-0009); cmd_id 0 = none. */
    gates_u32 cmd_scope_index;
    gates_u32 cmd_scope_generation;
    gates_command_id_t cmd_id;
    /* Menu overlay (plan-0009 stage 2): its command ids, their scope, selection. */
    gates_command_id_t *menu_ids;
    gates_u32 menu_count;
    gates_i32 menu_sel;          /* -1 = none */
    gates_u32 menu_scope_index;
    gates_u32 menu_scope_generation;
    bool menu_is_list;           /* a choice's option list: scope = the choice, ids = option ids */
    bool menu_from_bar;          /* opened from the menu bar (plan-0018): title menu_bar_title */
    gates_u32 menu_bar_title;
    gates_i32 menu_min_w;        /* a choice's list: at least as wide as the choice */
    /* Radio group / choice (plan-0010): one allocation holds array and labels. */
    bool has_options;
    gates_i_option_t *opts;
    gates_u32 opt_count;
    gates_u32 opt_sel;           /* selected id, 0 = none */
    gates_i32 opt_press;         /* radio: row a press started on, -1 = none */
    /* Virtual view (plan-0011): columns, model binding, scroll and selection. */
    struct gates_i_view *view;
    /* Label with a mnemonic target (plan-0018). */
    bool has_mn_target;
    gates_u32 mn_target_index;
    gates_u32 mn_target_generation;
    /* Menu bar (plan-0018): its titles and their command ids. */
    struct gates_i_menubar *mbar;
    /* Toolbar (plan-0018): its entries (command ids, 0 = separator). */
    struct gates_i_toolbar *tbar;
    /* Property grid (plan-0021): its properties and categories. */
    struct gates_i_propgrid *pgrid;
    /* Multi-line editor (plan-0022): its buffer, caret, view and history. */
    struct gates_i_editor *editor;
    /* Tabs (plan-0018): the titles; the pages are the stack's children. */
    struct gates_i_tabs *tabs;
    /* Spin box and slider (plan-0019): the range. */
    struct gates_i_range *rng;
    /* Group box (plan-0019): collapsible (checked = expanded). */
    bool group_fold;
    /* Images (plan-0020): an image node's image and set size; a button's icon. */
    gates_u32 image;
    gates_size_t image_size;
    gates_u32 icon;
    /* Form (plan-0010 stage 2): its field table. */
    struct gates_i_field *fields;
    gates_u32 field_count;
    gates_u32 field_cap;
    /* Progress (per-mille), textbox error state, label shown as an error (plan-0010). */
    gates_i32 value;
    bool invalid;
    gates_event_fn on_event; /* typed notifications; null = none queued */
    void *event_user;

    void (*on_click)(gates_tree_t *tree, gates_node_t node, void *user);
    void (*on_toggle)(gates_tree_t *tree, gates_node_t node, bool checked, void *user);
    void *cb_user;
} gates_widget_state_t;

typedef struct gates_node_slot_t {
    gates_u32 generation;
    gates_u32 next_free;
    bool alive;
    bool destroy_pending;

    gates_node_kind_t kind;
    gates_u32 state_index;   /* widget state pool index or GATES_NONE */

    gates_u32 parent;
    gates_u32 first_child;
    gates_u32 last_child;
    gates_u32 prev_sibling;
    gates_u32 next_sibling;
    gates_u32 child_count;

    /* Layout (container props + child props + results). */
    gates_i8 font;           /* a gates_font_t, or GATES_FONT_INHERIT (RFC-0004) */
    gates_u8 layout_kind;    /* gates_layout_kind_i */
    gates_u8 grow;           /* child main-axis weight (0 = fixed) */
    gates_u8 align;          /* gates_align_i, child cross-axis */
    gates_i32 padding;
    gates_i32 gap;
    gates_u32 active_child;  /* STACK: index into child order */
    gates_rect_t abs_rect;   /* ABSOLUTE child request */
    gates_size_t pref;       /* measure() result */
    gates_rect_t layout_rect;/* arrange() result */

    /* SPLIT: first-pane share in per-mille (no float in the core) + direction. */
    gates_i32 split_ratio;
    gates_u8 split_vertical;
    /* SCROLL: vertical offset (>=0) and measured content size. */
    gates_i32 scroll_offset;
    gates_i32 scroll_arranged; /* scroll_offset used by the last arrange */
    gates_size_t content_size;
    /* FORM: the label column width found by the last measure. */
    gates_i32 form_label_w;
    /* GRID (plan-0019): columns (0 = 2); a child's span (0 = 1). WRAP: the
     * width the last measure used (the line breaks depend on it). */
    gates_u8 grid_cols;
    gates_u8 span;
    gates_i32 wrap_w;

    gates_u32 dirty;         /* GATES_DIRTY_* */
    bool hidden;             /* plan-0010: no space, no paint, no hit, no focus */
    void *user_data;
} gates_node_slot_t;

struct gates_tree {
    gates_allocator_t alloc;

    gates_node_slot_t *slots;
    gates_u32 capacity;
    gates_u32 first_free;

    gates_u32 root;
    gates_u32 live_count;

    gates_u32 *pending;
    gates_u32 pending_len;
    gates_u32 pending_cap;

    /* Widget state pool. */
    gates_widget_state_t *states;
    gates_u32 state_cap;
    gates_u32 state_first_free; /* GATES_NONE when full */

    /* Interaction state (slot indices or GATES_NONE). */
    gates_u32 hover;
    gates_u32 pressed;

    /* Active drag on a split handle or scroll thumb. */
    gates_u32 drag_node;
    gates_u8 drag_kind;          /* gates_drag_kind_i */
    gates_point_t drag_start;    /* pointer position when the drag began */
    gates_i32 drag_start_value;  /* pane-A px (split) or offset px (scroll) */

    gates_u32 focus;         /* focused node index or GATES_NONE */

    /* Cached from the text backend at layout time so pointer routing can do
     * cell arithmetic without a backend handle. */
    gates_i32 line_height;
    gates_i32 advance;
    /* The backend of the last gates_layout_run (borrowed): hit testing and
     * accessibility measure text with it between layouts (RFC-0004). */
    const gates_text_backend_t *text_backend;

    gates_u32 dirty_bits;    /* aggregate of all marks since last clear */

    /* Typed notification queue (plan-0007). Entries before event_head are being
     * delivered; coalescing only merges into undelivered entries. */
    gates_i_event_t *events;
    gates_u32 event_len;
    gates_u32 event_cap;
    gates_u32 event_head;
    bool dispatching;
    /* Payload copies. While a handler holds event_text, growth allocates a new
     * block and parks the old one in event_text_retired until it returns. */
    gates_u8 *event_text;
    gates_u32 event_text_cap;
    gates_u8 *event_text_busy;
    gates_u8 *event_text_retired;
    gates_err_t input_error; /* last input-path failure, see gates_input_take_error */

    /* Clipboard provider (plan-0008); has_clipboard false = none. */
    gates_clipboard_t clipboard;
    bool has_clipboard;

    /* Keyboard (plan-0009): control held down by Space until key-up. */
    gates_u32 key_press;     /* slot index or GATES_NONE */
    gates_u32 focus_scope;   /* slot index of the scope root; GATES_NONE = tree root */
    gates_i_command_t *commands;
    gates_u32 command_count;
    gates_u32 command_cap;
    /* Open overlays, topmost last (plan-0009 stage 2). */
    gates_i_overlay_t overlays[GATES_I_OVERLAY_MAX];
    gates_u32 overlay_count;

    /* Posting and timers (plan-0012). */
    gates_u64 serial;            /* unique for the process, never reused */
    gates_sender_t *sender;      /* attached sender (not a reference) or null */
    /* Background tasks (plan-0021): the platform's threads and the running tasks. */
    gates_threads_t threads;
    bool has_threads;
    struct gates_task *tasks;
    gates_u32 task_count;
    gates_u32 next_task_id;
    gates_i_msg_handler_t *msg_handlers;
    gates_u32 msg_handler_count;
    gates_u32 msg_handler_cap;
    gates_i_timer_t *timers;
    gates_u32 timer_count;
    gates_u32 timer_cap;
    gates_timer_id_t next_timer_id;
    gates_clock_fn clock;
    void (*clock_changed)(void *ctx);
    void *clock_ctx;

    /* Accessibility (plan-0014). */
    bool access_on;
    gates_access_change_t access_changes[GATES_ACCESS_CHANGES_MAX];
    gates_u32 access_change_count;
    bool access_overflow;
    gates_i_access_prop_t *access_props;
    gates_u32 access_prop_count;
    gates_u32 access_prop_cap;
    gates_u8 *access_buf;        /* strings handed out by gates_access_info */
    gates_u32 access_buf_cap;
    gates_u8 *announce;
    gates_u32 announce_len;
    bool announce_assertive;

    /* Application frame (plan-0018): the menu bar and menu mode, keyboard cues. */
    gates_u32 menubar;           /* slot of the live menu bar, or GATES_NONE */
    gates_u8 mb_mode;            /* GATES_I_MB_OFF / _HIGHLIGHT / _OPEN */
    gates_u32 mb_sel;            /* highlighted or open title */
    gates_u32 mb_hover;          /* title under the pointer, GATES_NONE when none */
    bool cues_shown;             /* keyboard cues since the last Alt / menu mode */
    bool cues_always;            /* the platform always underlines access keys */
    bool eat_char;               /* a menu took the last key: drop the character it makes */
    gates_u32 tb_hover;          /* toolbar under the pointer (its hover entry), GATES_NONE */
    /* Images (plan-0020): the store and the platform's decoder. */
    struct gates_i_image_slot *images;
    gates_u32 image_count;
    gates_u32 image_cap;
    gates_u32 next_image_id;
    gates_image_decoder_t decoder;
    bool has_decoder;
    /* GRID column grow weights (plan-0019), per grid node. */
    struct gates_i_grid_grow *grid_grows;
    gates_u32 grid_grow_count;
    gates_u32 grid_grow_cap;
    bool wrap_changed;           /* a wrap container was arranged at a new width */
    /* Bubble handlers and deferred calls (plan-0019). */
    struct gates_i_bubble *bubbles;
    gates_u32 bubble_count;
    gates_u32 bubble_cap;
    struct gates_i_defer *defers;
    gates_u32 defer_count;
    gates_u32 defer_cap;
    /* Tooltips (plan-0018). */
    gates_u32 tip_index;         /* target node, GATES_NONE when none */
    gates_u32 tip_generation;
    gates_u64 tip_item;          /* a toolbar button: entry + 1, else 0 */
    gates_u8 tip_state;          /* GATES_I_TIP_OFF / _PENDING / _SHOWN */
    bool tip_by_focus;           /* armed by keyboard focus, not the pointer */
    gates_timer_id_t tip_timer;  /* 0 = none */
    gates_rect_t tip_box;
    gates_u8 *tip_text;          /* the shown text (a toolbar button's is composed) */
    gates_u32 tip_len;
    gates_u32 tip_cap;
};

#define GATES_I_TIP_OFF 0u
#define GATES_I_TIP_PENDING 1u
#define GATES_I_TIP_SHOWN 2u

#define GATES_I_MB_OFF 0u
#define GATES_I_MB_HIGHLIGHT 1u
#define GATES_I_MB_OPEN 2u

/* Images (gates_image.c, plan-0020). */
typedef struct gates_i_image_slot {
    gates_u32 id;                /* 0 = free */
    struct gates_image *image;
} gates_i_image_slot;
void gates_i_images_free(gates_tree_t *tree);
/* Draws image `id` fitted into `r` keeping its aspect ratio, centred (nothing for 0 or a removed id). */
gates_err_t gates_i_draw_image_fit(const gates_tree_t *tree, gates_draw_list_t *dl, gates_u32 id, gates_rect_t r);
gates_size_t gates_i_image_measure(const gates_tree_t *tree, const gates_node_slot_t *s);

/* GRID grow weights (gates_layout.c, plan-0019). */
typedef struct gates_i_grid_grow {
    gates_u32 index;
    gates_u32 generation;
    gates_u8 weight[GATES_GRID_MAX_COLUMNS];
} gates_i_grid_grow;
void gates_i_grid_free(gates_tree_t *tree);

/* Bubble handlers and deferred calls (gates_event.c, plan-0019). */
typedef struct gates_i_bubble {
    gates_u32 index;
    gates_u32 generation;
    gates_event_fn fn;
    void *user;
} gates_i_bubble;
typedef struct gates_i_defer {
    gates_u32 key;
    gates_defer_fn fn;
    void *user;
} gates_i_defer;
/* Who hears idx's events: its own handler, else the nearest ancestor's bubble
 * handler. false when nobody does (then nothing is queued). */
bool gates_i_handler(const gates_tree_t *tree, gates_u32 idx, gates_event_fn *fn, void **user);
static inline bool gates_i_wants_events(const gates_tree_t *tree, gates_u32 idx) {
    return idx != GATES_NONE && gates_i_handler(tree, idx, nullptr, nullptr);
}
void gates_i_bubble_free(gates_tree_t *tree);

/* Event queue helpers (gates_event.c). Reserve before mutating: `slots` more
 * entries and payload capacity for `text_bytes`. Push never allocates. */
gates_err_t gates_i_event_reserve(gates_tree_t *tree, gates_u32 slots, gates_u32 text_bytes);
void gates_i_event_push(gates_tree_t *tree, gates_u32 idx, gates_event_kind_t kind,
                        gates_event_origin_t origin);
/* Reserve + push in one step; failure drops the event and records input_error. */
void gates_i_event_try_push(gates_tree_t *tree, gates_u32 idx, gates_event_kind_t kind,
                            gates_u32 text_bytes);
/* Drops undelivered events for a slot (handler removed). */
void gates_i_event_purge(gates_tree_t *tree, gates_u32 idx);
void gates_i_event_free(gates_tree_t *tree);

/* Textbox editing (gates_textbox.c, plan-0008). */
typedef enum gates_i_unit_t {
    GATES_I_UNIT_TYPE,       /* typed characters: consecutive ones merge */
    GATES_I_UNIT_DEL_BACK,   /* Backspace run */
    GATES_I_UNIT_DEL_FWD,    /* Delete run */
    GATES_I_UNIT_OTHER,      /* paste, cut, IME commit, replace, accept: one unit each */
} gates_i_unit_t;
/* The one path every textbox text change takes: replaces bytes [b,e) with
 * text. Checks the maximum length (a user edit past it becomes an offer plus
 * LIMIT_EXCEEDED, and OUT_OF_BOUNDS is returned), reserves the event and the
 * text before touching anything, records undo (dropping the oldest history
 * when memory is short), applies, bumps the revision and, for user edits,
 * queues TEXT_CHANGED. Read-only and composition checks are the caller's. */
gates_err_t gates_i_box_edit(gates_tree_t *tree, gates_u32 idx, gates_u32 b, gates_u32 e,
                             gates_str_t text, gates_i_unit_t unit, bool user);
/* Ends a typing/deletion run so the next edit starts a new undo unit. */
void gates_i_box_seal(gates_widget_state_t *st);
/* Undo / redo on the box (user edits). OK and nothing done when empty. */
gates_err_t gates_i_box_undo(gates_tree_t *tree, gates_u32 idx);
gates_err_t gates_i_box_redo(gates_tree_t *tree, gates_u32 idx);
/* Drops history and the pending offer (programmatic set, raw edit bridge). */
void gates_i_box_forget(gates_tree_t *tree, gates_widget_state_t *st);
/* Frees history and offer when the widget state is released. */
void gates_i_box_free(gates_tree_t *tree, gates_widget_state_t *st);
/* The node's effective font: its own, else its nearest ancestor's, else
 * GATES_FONT_UI (gates_tree.c, RFC-0004). */
gates_i32 gates_i_font(const gates_tree_t *tree, gates_u32 idx);
static inline gates_i32 gates_i_slot_font(const gates_tree_t *tree, const gates_node_slot_t *s) {
    return gates_i_font(tree, (gates_u32)(s - tree->slots));
}
/* Text box geometry (RFC-0004): the x of a byte offset from the start of the
 * box's text, and the offset nearest to an x, in the box's font - a password
 * box by its stars. Everything that draws, hits or describes a text box uses
 * these two, so they agree. */
gates_i32 gates_i_box_x(const gates_text_backend_t *be, gates_i32 font, const gates_widget_state_t *st,
                        gates_u32 offset);
gates_u32 gates_i_box_offset_at_x(const gates_text_backend_t *be, gates_i32 font, const gates_widget_state_t *st,
                                  gates_i32 x);
/* Clipboard helpers (gates_clipboard.c). normalize: single-line paste policy
 * (CRLF/CR/LF/TAB -> one space, other controls dropped, invalid UTF-8 ->
 * U+FFFD) into a new block from `alloc`. */
gates_err_t gates_i_paste_normalize(gates_allocator_t alloc, gates_str_t in, bool keep_lines, gates_u8 **out,
                                    gates_u32 *out_len);

/* Focus (gates_focus.c, plan-0009). */
bool gates_i_focus_eligible(const gates_tree_t *tree, gates_u32 idx);
/* When the focused node is no longer eligible (disabled, hidden, command
 * gone), focus moves to the next eligible control in tree order, or to none. */
void gates_i_focus_check(gates_tree_t *tree);
/* Before `idx`'s subtree is unlinked: moves focus out of it if it is inside. */
void gates_i_focus_leave_subtree(gates_tree_t *tree, gates_u32 idx);
/* Nothing hidden on the way up, on the shown stack pages, inside the focus scope. */
bool gates_i_reachable(const gates_tree_t *tree, gates_u32 idx);
/* Scrolls every scroll ancestor so the node's rect is inside its viewport. */
void gates_i_scroll_into_view(gates_tree_t *tree, gates_u32 idx);
/* The scope root that commands and traversal use now. */
gates_u32 gates_i_scope_root(const gates_tree_t *tree);

/* Commands (gates_command.c). */
/* Inert: disabled, or bound to a command that is disabled or gone. */
bool gates_i_widget_inert(const gates_tree_t *tree, const gates_widget_state_t *st);
/* A widget's visible text: a bound button shows its command's label. */
gates_str_t gates_i_widget_label(const gates_tree_t *tree, const gates_widget_state_t *st);
/* Finds a live command (null when missing). */
gates_i_command_t *gates_i_command_find(const gates_tree_t *tree, gates_u32 scope_index,
                                        gates_u32 scope_generation, gates_command_id_t id);
/* The command whose shortcut this key is, in the active scope chain (or null). */
gates_i_command_t *gates_i_command_for_key(const gates_tree_t *tree,
                                           const gates_key_event_t *ev);
/* The active scope's command with this role (or null). */
gates_i_command_t *gates_i_command_with_role(const gates_tree_t *tree,
                                             gates_command_role_t role);
/* Queues an invocation of `c` (reserved slot required: push never allocates). */
void gates_i_command_push(gates_tree_t *tree, const gates_i_command_t *c,
                          gates_event_origin_t origin);
void gates_i_commands_free(gates_tree_t *tree);

/* Activation of a button/checkbox (gates_hit.c): reserve, toggle, queue, legacy callback. */
void gates_i_activate(gates_tree_t *tree, gates_u32 idx);

/* Overlays (gates_overlay.c). */
/* Where overlay i goes for a viewport, given its measured preferred size. */
gates_rect_t gates_i_overlay_place(const gates_tree_t *tree, gates_u32 i, gates_size_t pref,
                                   gates_size_t viewport);
/* After layout: give a newly opened dialog's first control the focus. */
void gates_i_overlays_after_layout(gates_tree_t *tree);
/* Pointer routing: the node to hit-test from (a modal dialog, or the root);
 * true in *consumed when the top menu took the event entirely. */
gates_u32 gates_i_overlay_pointer(gates_tree_t *tree, const gates_pointer_event_t *ev,
                                  bool *consumed);
/* Keys while a menu is on top (true: consumed). */
bool gates_i_overlay_key(gates_tree_t *tree, const gates_key_event_t *ev);
/* Escape with no cancel command while a dialog is on top: cancels it. */
bool gates_i_overlay_escape(gates_tree_t *tree);
/* gates_node_destroy on an open overlay: forget it silently (no event). */
void gates_i_overlay_node_destroyed(gates_tree_t *tree, gates_u32 idx);
bool gates_i_overlay_is_dialog_scope(const gates_tree_t *tree, gates_u32 scope);
gates_size_t gates_i_menu_measure(const gates_tree_t *tree, const gates_widget_state_t *st,
                                  const gates_text_backend_t *text, gates_i32 font_size);
gates_err_t gates_i_menu_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                               const gates_theme_t *theme, const gates_text_backend_t *text);
void gates_i_menu_free(gates_tree_t *tree, gates_widget_state_t *st);

/* Options (gates_choice.c, plan-0010). */
#define GATES_RADIO_ROW_GAP 4
#define GATES_CHOICE_ARROW_CELLS 2
/* Height of one radio row for a line height. */
gates_i32 gates_i_radio_row_h(gates_i32 line_height);
/* The radio row under p (by the cached line height), or -1. */
gates_i32 gates_i_radio_row_at(const gates_tree_t *tree, gates_u32 idx, gates_point_t p);
const gates_i_option_t *gates_i_option_find(const gates_widget_state_t *st, gates_u32 id);
bool gates_i_options_any_enabled(const gates_widget_state_t *st);
/* A person selects `id` (enabled option): reserves the event first (no
 * change without it), selects, bumps the revision, queues VALUE_CHANGED.
 * OK and nothing done when it is already selected. */
gates_err_t gates_i_option_pick(gates_tree_t *tree, gates_u32 idx, gates_u32 id);
/* Radio keys on the focused group (true: consumed). */
bool gates_i_radio_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev);
/* Space on a radio group: selects the first enabled option when none is. */
void gates_i_radio_activate(gates_tree_t *tree, gates_u32 idx);
gates_size_t gates_i_options_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                     const gates_text_backend_t *text);
gates_err_t gates_i_options_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                  const gates_theme_t *theme, const gates_text_backend_t *text,
                                  bool pressed, bool hovered);
void gates_i_options_free(gates_tree_t *tree, gates_widget_state_t *st);
/* Choice list (gates_overlay.c): opens it (errors go to input_error); the
 * list's overlay index for a choice, or -1; closes lists whose choice can no
 * longer be used (disabled, hidden, unreachable) or, with `idx`, that one. */
void gates_i_choice_open(gates_tree_t *tree, gates_u32 idx);
gates_i32 gates_i_choice_list_find(const gates_tree_t *tree, gates_u32 idx);
void gates_i_choice_lists_check(gates_tree_t *tree, gates_u32 only_idx);

/* Queues an event carrying `aux` / `item` (reserved slot required). */
void gates_i_event_push_ex(gates_tree_t *tree, gates_u32 idx, gates_event_kind_t kind,
                           gates_event_origin_t origin, gates_u32 aux, gates_u64 item);

/* Virtual views (gates_view.c, plan-0011). */
void gates_i_view_free(gates_tree_t *tree, gates_widget_state_t *st);
gates_item_id_t gates_i_view_selected(const gates_widget_state_t *st);
/* Accessibility (gates_view.c, plan-0014 stage 2): rows as items - the rows
 * shown now plus the selection; cells only for shown rows. */
#define GATES_I_VIEW_LIST 0u
#define GATES_I_VIEW_TABLE 1u
#define GATES_I_VIEW_TREE 2u
typedef struct gates_i_view_item_t {
    gates_u64 row, count;        /* position in the model, model rows */
    bool shown, selected, has_info;
    gates_rect_t rect;           /* when shown */
    gates_row_info_t info;       /* tree rows */
    gates_u32 columns;
} gates_i_view_item_t;
gates_u32 gates_i_view_kind(const gates_tree_t *tree, gates_u32 idx);
gates_u64 gates_i_view_item_count(gates_tree_t *tree, gates_u32 idx);
gates_item_id_t gates_i_view_item_at(gates_tree_t *tree, gates_u32 idx, gates_u64 k);
bool gates_i_view_item(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id, gates_i_view_item_t *out);
gates_str_t gates_i_view_cell(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id, gates_u32 col);
gates_item_id_t gates_i_view_row_at(gates_tree_t *tree, gates_u32 idx, gates_point_t p);
gates_err_t gates_i_view_pick_id(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id);
gates_err_t gates_i_view_activate_id(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id);
gates_err_t gates_i_view_expand_id(gates_tree_t *tree, gates_u32 idx, gates_item_id_t id, bool open);
bool gates_i_view_scroll_info(gates_tree_t *tree, gates_u32 idx, gates_u32 *pos, gates_u32 *page);
gates_err_t gates_i_view_scroll_set(gates_tree_t *tree, gates_u32 idx, gates_u32 pos);
gates_err_t gates_i_view_scroll_step(gates_tree_t *tree, gates_u32 idx, gates_i32 amount, bool page);
gates_size_t gates_i_view_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                  const gates_text_backend_t *text);
gates_err_t gates_i_view_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                               const gates_theme_t *theme, const gates_text_backend_t *text);
bool gates_i_view_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev);
/* Left press on the view (true: taken, possibly starting a drag). */
bool gates_i_view_pointer_down(gates_tree_t *tree, gates_u32 idx, gates_point_t p,
                               gates_u32 clicks);
void gates_i_view_pointer_up(gates_tree_t *tree, gates_u32 idx, gates_point_t p);
void gates_i_view_drag(gates_tree_t *tree, gates_point_t p);
bool gates_i_view_wheel(gates_tree_t *tree, gates_u32 idx, gates_vec2_t wheel);
/* In-place editing (plan-0021): the editor child is placed by the view's arrange;
 * the view owning editor text box `box` (or GATES_NONE); focus left the editor;
 * Enter/Escape in the editor; cancel any open edit (a view being disabled). */
void gates_i_view_place_editor(gates_tree_t *tree, gates_u32 idx);
gates_u32 gates_i_view_of_editor(const gates_tree_t *tree, gates_u32 box);
void gates_i_view_editor_left(gates_tree_t *tree, gates_u32 box);
bool gates_i_view_editor_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev);
void gates_i_view_cancel_edit(gates_tree_t *tree, gates_u32 idx);
/* Columns for the saved state (all columns, in order) and the header menu's right press. */
bool gates_i_view_col_info(const gates_tree_t *tree, gates_u32 idx, gates_u32 k, gates_column_id_t *id,
                           gates_i32 *width, bool *hidden);
bool gates_i_view_apply_cols(gates_tree_t *tree, gates_u32 idx, const gates_column_id_t *ids, const gates_i32 *widths,
                             const bool *hidden, gates_u32 n);
bool gates_i_view_context(gates_tree_t *tree, gates_u32 idx, gates_point_t p);
/* Frees the commands whose scope is the node in slot idx (its slot is being released). */
void gates_i_commands_drop_scope(gates_tree_t *tree, gates_u32 idx, gates_u32 generation);

/* Posting and timers: freed with the tree (gates_post.c / gates_timer.c). */
void gates_i_post_tree_free(gates_tree_t *tree);
void gates_i_timers_free(gates_tree_t *tree);

/* Accessibility (gates_access.c): records a change when enabled; frees. */
void gates_i_access_log(gates_tree_t *tree, gates_access_change_kind_t kind, gates_u32 idx,
                        gates_u64 item);
void gates_i_access_free(gates_tree_t *tree);
gates_live_t gates_i_access_live(const gates_tree_t *tree, gates_u32 idx);

/* What a form knows about one of its editors (gates_form.c): false when the
 * node is not a form editor. */
typedef struct gates_i_field_info_t {
    gates_u32 id;
    gates_node_t label, help, error;
    bool required;
} gates_i_field_info_t;
bool gates_i_form_field_info(const gates_tree_t *tree, gates_u32 editor_idx,
                             gates_i_field_info_t *out);
/* Chooses and invokes a menu entry by command id, like a click (gates_overlay.c). */
gates_err_t gates_i_menu_invoke(gates_tree_t *tree, gates_u32 menu_idx, gates_command_id_t id);
/* Menu row geometry for accessibility bounds. */
gates_rect_t gates_i_menu_row_rect(const gates_tree_t *tree, gates_u32 menu_idx, gates_u32 row);

/* Mnemonics (gates_mnemonic.c, plan-0018). A text is markup when its node
 * parses it: buttons, check boxes, labels with a target, command labels in
 * menus, menu bar titles. Widths add up exactly (RFC-0004), so markup text is
 * measured and drawn in pieces around each removed '&'. */
bool gates_i_mn_markup(const gates_tree_t *tree, gates_u32 idx);
/* The width of the shown text (markup removed). */
gates_i32 gates_i_mn_width(const gates_text_backend_t *be, gates_i32 font, gates_str_t text);
/* Draws markup text with its top-left at rect.x/y (rect.h the line), underlining
 * the mnemonic when `cues`. */
gates_err_t gates_i_mn_draw(gates_draw_list_t *dl, const gates_text_backend_t *be, gates_rect_t rect,
                            gates_str_t text, gates_i32 font, gates_color_t color, bool cues);
/* Copies the shown text into buf (cut to cap); returns its full length. */
gates_u32 gates_i_mn_strip(gates_str_t text, gates_u8 *buf, gates_u32 cap);
/* Keyboard cues visible now. */
bool gates_i_cues(const gates_tree_t *tree);
/* A node's text as shown by paint and layout: markup or not. */
static inline gates_i32 gates_i_text_w(const gates_tree_t *tree, gates_u32 idx,
                                       const gates_text_backend_t *be, gates_i32 font,
                                       gates_str_t text) {
    return gates_i_mn_markup(tree, idx) ? gates_i_mn_width(be, font, text)
                                        : be->measure(be->ctx, font, text).w;
}
/* Shortcut text for menus and accessibility (gates_command.c). */
gates_usize_t gates_i_shortcut_text(const gates_shortcut_t *k, char *buf, gates_usize_t cap);

/* Menu bar (gates_menubar.c, plan-0018). */
typedef struct gates_i_menubar_item {
    gates_u8 *title;
    gates_u32 title_len;
    gates_command_id_t *ids;
    gates_u32 count;
} gates_i_menubar_item;
typedef struct gates_i_menubar {
    gates_u32 scope_index;
    gates_u32 scope_generation;
    gates_i_menubar_item *items;
    gates_u32 count;
    gates_u32 cap;
} gates_i_menubar;
void gates_i_menubar_free(gates_tree_t *tree, gates_widget_state_t *st);
gates_size_t gates_i_menubar_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                     const gates_text_backend_t *text);
gates_err_t gates_i_menubar_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                  const gates_theme_t *theme, const gates_text_backend_t *text);
/* Title geometry (window coordinates, after layout). */
gates_rect_t gates_i_menubar_title_rect(const gates_tree_t *tree, gates_u32 idx, gates_u32 title);
gates_i32 gates_i_menubar_title_at(const gates_tree_t *tree, gates_u32 idx, gates_point_t p);
/* The live, reachable menu bar, or GATES_NONE. */
gates_u32 gates_i_menubar_live(const gates_tree_t *tree);
/* Opens title t's menu (keyboard: first entry selected). Leaves menu mode and
 * returns the error when the menu cannot be built. */
gates_err_t gates_i_menubar_open(gates_tree_t *tree, gates_u32 t, bool keyboard);
/* Leaves menu mode, closing the bar's menu. */
void gates_i_menubar_leave(gates_tree_t *tree);
/* F10 (gates_input.c): enters or leaves menu mode; false without a reachable bar. */
bool gates_i_menubar_f10(gates_tree_t *tree);
/* Keys in menu mode with no bar menu open (true: consumed). */
bool gates_i_menubar_key(gates_tree_t *tree, const gates_key_event_t *ev);
/* A left press on the bar (not while a bar menu is open). */
void gates_i_menubar_press(gates_tree_t *tree, gates_u32 idx, gates_point_t p);
/* A bar menu was removed (gates_overlay.c): menu mode ends (callers that
 * switch menus or go back to the title set the mode again). */
void gates_i_menubar_menu_closed(gates_tree_t *tree);
/* Destroy / hide / unreachable: forget or leave (gates_tree.c, gates_focus.c). */
void gates_i_menubar_check(gates_tree_t *tree);
void gates_i_menubar_destroying(gates_tree_t *tree, gates_u32 top);
/* The overlay record index of the bar's open menu, or -1 (gates_overlay.c). */
gates_i32 gates_i_bar_menu_overlay(const gates_tree_t *tree);
/* Opens a menu overlay for the bar (gates_overlay.c). */
gates_err_t gates_i_menu_open_for_bar(gates_tree_t *tree, gates_point_t at, gates_i32 above_y,
                                      gates_node_t scope, const gates_command_id_t *ids,
                                      gates_u32 count, gates_u32 title, bool keyboard,
                                      gates_node_t *out_menu);
/* Closes menu overlay record i (reported with result 0). */
void gates_i_menu_close_record(gates_tree_t *tree, gates_u32 i);

/* Toolbar and status bar (gates_toolbar.c, plan-0018). */
typedef struct gates_i_toolbar {
    gates_u32 scope_index;
    gates_u32 scope_generation;
    gates_command_id_t *ids;     /* 0 = separator */
    gates_u32 count;
    gates_u32 cap;
    gates_i32 sel;               /* keyboard stop: entry index, count = ">>", -1 = none */
    gates_i32 press;             /* entry pressed by the pointer, count = ">>", -1 = none */
    gates_i32 hover;             /* entry under the pointer, -1 = none */
    bool icons_only;             /* plan-0020: buttons with an icon show only it */
} gates_i_toolbar;
void gates_i_toolbar_free(gates_tree_t *tree, gates_widget_state_t *st);
bool gates_i_toolbar_any_enabled(const gates_tree_t *tree, const gates_widget_state_t *st);
gates_size_t gates_i_toolbar_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                     const gates_text_backend_t *text);
gates_err_t gates_i_toolbar_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                  const gates_theme_t *theme, const gates_text_backend_t *text);
/* Entries shown after the last layout; an entry's rect (empty when not shown);
 * the ">>" rect (empty without overflow); the entry under p (count = ">>", -1). */
gates_u32 gates_i_toolbar_shown(const gates_tree_t *tree, gates_u32 idx);
gates_rect_t gates_i_toolbar_entry_rect(const gates_tree_t *tree, gates_u32 idx, gates_u32 k);
gates_rect_t gates_i_toolbar_more_rect(const gates_tree_t *tree, gates_u32 idx);
gates_i32 gates_i_toolbar_entry_at(const gates_tree_t *tree, gates_u32 idx, gates_point_t p);
/* The command of entry k (null for a separator or a missing command). */
const gates_i_command_t *gates_i_toolbar_command(const gates_tree_t *tree, gates_u32 idx, gates_u32 k);
/* The current keyboard stop (repaired to an enabled one), or -1. */
gates_i32 gates_i_toolbar_stop(const gates_tree_t *tree, gates_u32 idx);
/* Invokes entry k (count = opens the ">>" menu); INVALID_STATE when disabled. */
gates_err_t gates_i_toolbar_activate(gates_tree_t *tree, gates_u32 idx, gates_u32 k);
bool gates_i_toolbar_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev);
void gates_i_toolbar_down(gates_tree_t *tree, gates_u32 idx, gates_point_t p);
void gates_i_toolbar_up(gates_tree_t *tree, gates_u32 idx, gates_point_t p);
/* "Cut (Ctrl+X)" for entry k into buf; returns the length it needs. */
gates_u32 gates_i_toolbar_tip(const gates_tree_t *tree, gates_u32 idx, gates_u32 k, gates_u8 *buf,
                              gates_u32 cap);
gates_err_t gates_i_statusbar_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                    const gates_theme_t *theme);

/* Tabs (gates_tabs.c, plan-0018): a TABS column holds the strip (TABSTRIP) and
 * a stack panel whose children are the pages. */
typedef struct gates_i_tab_title {
    gates_u8 *text;
    gates_u32 len;
} gates_i_tab_title;
typedef struct gates_i_tabs {
    gates_i_tab_title *titles;
    gates_u32 count;
    gates_u32 cap;
} gates_i_tabs;
void gates_i_tabs_free(gates_tree_t *tree, gates_widget_state_t *st);
gates_u32 gates_i_tabs_count(const gates_tree_t *tree, gates_u32 tabs);
gates_str_t gates_i_tabs_title(const gates_tree_t *tree, gates_u32 tabs, gates_u32 index);
gates_u32 gates_i_tabs_selected(const gates_tree_t *tree, gates_u32 tabs);
/* A person switches to `index`: reserves the event, switches, moves the focus
 * into the new page when it was in the old one, queues VALUE_CHANGED. */
gates_err_t gates_i_tabs_pick(gates_tree_t *tree, gates_u32 tabs, gates_u32 index);
gates_i32 gates_i_tab_page_index(const gates_tree_t *tree, gates_u32 idx, gates_u32 *out_tabs);
gates_size_t gates_i_tabstrip_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                      const gates_text_backend_t *text);
gates_err_t gates_i_tabstrip_paint(const gates_tree_t *tree, gates_u32 strip, gates_draw_list_t *dl,
                                   const gates_theme_t *theme, const gates_text_backend_t *text);
gates_rect_t gates_i_tab_rect(const gates_tree_t *tree, gates_u32 strip, gates_u32 index);
gates_i32 gates_i_tab_at(const gates_tree_t *tree, gates_u32 strip, gates_point_t p);
bool gates_i_tabstrip_key(gates_tree_t *tree, gates_u32 strip, const gates_key_event_t *ev);
bool gates_i_tabs_ctrl_key(gates_tree_t *tree, const gates_key_event_t *ev);
void gates_i_tabstrip_press(gates_tree_t *tree, gates_u32 strip, gates_point_t p);
bool gates_i_tabs_mnemonic(gates_tree_t *tree, gates_u8 letter);

/* View columns by position (gates_view.c) for persisted state. */
gates_u32 gates_i_view_ncol(const gates_tree_t *tree, gates_u32 idx);
gates_i32 gates_i_view_col_width(const gates_tree_t *tree, gates_u32 idx, gates_u32 k);
void gates_i_view_set_col_width(gates_tree_t *tree, gates_u32 idx, gates_u32 k, gates_i32 width);

/* Spin box and slider (gates_inputs.c, plan-0019). */
typedef struct gates_i_range {
    gates_i64 min, max, step, page, value;
    gates_u32 scale;
    gates_u32 ticks;             /* slider: tick marks every n steps, 0 = none */
    bool vertical;               /* slider */
} gates_i_range;
void gates_i_range_free(gates_tree_t *tree, gates_widget_state_t *st);
void gates_i_propgrid_free(gates_tree_t *tree, gates_widget_state_t *st);
/* Multi-line editor (plan-0022). */
void gates_i_editor_free(gates_tree_t *tree, gates_widget_state_t *st);
gates_u32 gates_i_editor_caret(const gates_widget_state_t *st);
gates_size_t gates_i_editor_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                    const gates_text_backend_t *text);
gates_err_t gates_i_editor_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                 const gates_theme_t *theme, const gates_text_backend_t *text);
bool gates_i_editor_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev);
bool gates_i_editor_char(gates_tree_t *tree, gates_u32 idx, gates_str_t utf8);
bool gates_i_editor_press(gates_tree_t *tree, gates_u32 idx, gates_point_t p, gates_u32 clicks, bool shift);
void gates_i_editor_drag(gates_tree_t *tree, gates_point_t p);
bool gates_i_editor_wheel(gates_tree_t *tree, gates_u32 idx, gates_vec2_t wheel);
bool gates_i_editor_composing(const gates_widget_state_t *st);
gates_str_t gates_i_editor_preedit(const gates_widget_state_t *st);
gates_err_t gates_i_editor_set_preedit(gates_tree_t *tree, gates_u32 idx, gates_str_t text, gates_u32 cursor);
gates_err_t gates_i_editor_commit(gates_tree_t *tree, gates_u32 idx, gates_str_t text);
void gates_i_editor_preedit_cancel(gates_tree_t *tree, gates_u32 idx);
void gates_i_editor_blur(gates_tree_t *tree, gates_u32 idx);
gates_u32 gates_i_editor_text_rects(gates_tree_t *tree, gates_u32 idx, gates_u32 start, gates_u32 end, gates_rect_t *out,
                                    gates_u32 cap);
gates_u32 gates_i_editor_offset_at_point(gates_tree_t *tree, gates_u32 idx, gates_point_t p);
gates_err_t gates_i_editor_user_set(gates_tree_t *tree, gates_u32 idx, gates_str_t text);
void gates_i_node_undo(gates_tree_t *tree, gates_node_t node);
/* Background tasks (plan-0021): message kinds from GATES_I_TASK_KIND_BASE are
 * the tasks' own, handled by gates_i_task_message before node handlers; tree
 * destroy cancels and joins every task first. */
#define GATES_I_TASK_KIND_BASE 0xFFFF0000u
void gates_i_task_message(gates_tree_t *tree, gates_u32 kind, void *payload);
void gates_i_tasks_shutdown(gates_tree_t *tree);
gates_i64 gates_i_range_value(const gates_tree_t *tree, gates_u32 idx);
bool gates_i_range_info(const gates_tree_t *tree, gates_u32 idx, gates_i64 *min, gates_i64 *max, gates_i64 *value);
/* A person's change (accessibility, keys): reserved report, clamped, VALUE_CHANGED. */
gates_err_t gates_i_range_user_set(gates_tree_t *tree, gates_u32 idx, gates_i64 v);
/* The spin box a text box belongs to, or GATES_NONE. */
gates_u32 gates_i_spin_of_box(const gates_tree_t *tree, gates_u32 box);
void gates_i_spin_typed(gates_tree_t *tree, gates_u32 spin);
void gates_i_spin_commit(gates_tree_t *tree, gates_u32 spin);
bool gates_i_spin_key(gates_tree_t *tree, gates_u32 spin, const gates_key_event_t *ev);
void gates_i_spin_arrows_press(gates_tree_t *tree, gates_u32 arrows, gates_point_t p);
gates_err_t gates_i_spin_arrows_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                      const gates_theme_t *theme);
gates_size_t gates_i_spin_arrows_measure(void);
gates_size_t gates_i_slider_measure(const gates_tree_t *tree, const gates_node_slot_t *s);
gates_err_t gates_i_slider_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                 const gates_theme_t *theme);
bool gates_i_slider_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev);
bool gates_i_slider_press(gates_tree_t *tree, gates_u32 idx, gates_point_t p);
void gates_i_slider_drag(gates_tree_t *tree, gates_point_t p, bool end);

/* Group box (gates_group.c, plan-0019). */
bool gates_i_group_foldable(const gates_tree_t *tree, gates_u32 group);
gates_err_t gates_i_group_toggle(gates_tree_t *tree, gates_u32 group, bool expanded);
void gates_i_group_head_activate(gates_tree_t *tree, gates_u32 head);
gates_u32 gates_i_group_first(const gates_tree_t *tree, gates_u32 group);
gates_size_t gates_i_group_head_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                        const gates_text_backend_t *text);
gates_err_t gates_i_group_paint(const gates_tree_t *tree, gates_u32 group, gates_draw_list_t *dl,
                                const gates_theme_t *theme);
gates_err_t gates_i_group_head_paint(const gates_tree_t *tree, gates_u32 head, gates_draw_list_t *dl,
                                     const gates_theme_t *theme, const gates_text_backend_t *text);

/* Tooltips (gates_tooltip.c, plan-0018). */
gates_str_t gates_i_tooltip_of(const gates_tree_t *tree, gates_u32 idx);
/* The pointer rests over `idx`/`item` (GATES_NONE: over nothing with a tooltip). */
void gates_i_tip_hover(gates_tree_t *tree, gates_point_t p, gates_u32 hit);
/* A press or a key: hide; the target stays, so it does not come back until it changes. */
void gates_i_tip_dismiss(gates_tree_t *tree);
/* Keyboard focus reached `idx` (or GATES_NONE). */
void gates_i_tip_focus(gates_tree_t *tree, gates_u32 idx);
/* The target became unusable (disabled, hidden, unreachable) or its text changed. */
void gates_i_tip_check(gates_tree_t *tree);
void gates_i_tip_destroying(gates_tree_t *tree, gates_u32 top);
gates_err_t gates_i_tip_paint(const gates_tree_t *tree, gates_draw_list_t *dl, const gates_theme_t *theme,
                              const gates_text_backend_t *text);
void gates_i_tip_free(gates_tree_t *tree);

/* Form (gates_form.c). */
void gates_i_form_free(gates_tree_t *tree, gates_widget_state_t *st);

/* Frees a detached, never-exposed subtree at once, without allocating. */
void gates_i_discard_detached(gates_tree_t *tree, gates_node_t node);

/* Shared helpers implemented in gates_tree.c. */
gates_node_slot_t *gates_i_slot(const gates_tree_t *tree, gates_u32 idx);
bool gates_i_valid(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_i_handle(const gates_tree_t *tree, gates_u32 idx);
void gates_i_mark_dirty(gates_tree_t *tree, gates_u32 idx, gates_u32 bits);

/* Widget state pool (gates_tree.c owns storage; gates_widget.c uses it). */
gates_err_t gates_i_state_acquire(gates_tree_t *tree, gates_u32 *out_index);
void gates_i_state_release(gates_tree_t *tree, gates_u32 state_index);
gates_widget_state_t *gates_i_state(const gates_tree_t *tree, gates_u32 state_index);

/* Split/scroll chrome geometry, derived from layout results.
 * Owned by gates_layout.c; used by paint and hit testing so the three agree.
 * All return empty rects when the node is not that kind (or not scrollable). */
gates_rect_t gates_i_split_handle(const gates_tree_t *tree, gates_u32 idx);
bool gates_i_scrollable(const gates_tree_t *tree, gates_u32 idx);
gates_rect_t gates_i_scroll_viewport(const gates_tree_t *tree, gates_u32 idx);
gates_rect_t gates_i_scroll_track(const gates_tree_t *tree, gates_u32 idx);
gates_rect_t gates_i_scroll_thumb(const gates_tree_t *tree, gates_u32 idx);
/* Clamps a scroll offset into the legal range; returns the clamped value. */
gates_i32 gates_i_scroll_clamp(const gates_tree_t *tree, gates_u32 idx, gates_i32 offset);

/* Text area inside a textbox's border and padding (empty for other kinds). */
gates_rect_t gates_i_textbox_inner(const gates_tree_t *tree, gates_u32 idx);

#endif /* GATES_TREE_INTERNAL_H */
