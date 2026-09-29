/* gates_gui_lib - accessibility model: roles, names, states, values, items,
 * actions through the input paths, a change log for platform adapters, and the
 * enforced rules (plan-0014, RFC-0003 10). Platform-free. */
#include <gates/access.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/ui.h>
#include <gates/frame.h>
#include <gates/editor.h>
#include "gates_tree_internal.h"

#include <string.h>

/* -- application-set properties --------------------------------------------------- */

static gates_i_access_prop_t *find_prop(const gates_tree_t *tree, gates_u32 idx) {
    gates_u32 gen = gates_i_slot(tree, idx)->generation;
    for (gates_u32 i = 0; i < tree->access_prop_count; i++) {
        gates_i_access_prop_t *p = &tree->access_props[i];
        if (p->index == idx && p->generation == gen) return p;
    }
    return nullptr;
}

static void free_prop_strings(gates_tree_t *tree, gates_i_access_prop_t *p) {
    if (p->name != nullptr) tree->alloc.free_fn(tree->alloc.ctx, p->name);
    if (p->id != nullptr) tree->alloc.free_fn(tree->alloc.ctx, p->id);
    if (p->tip != nullptr) tree->alloc.free_fn(tree->alloc.ctx, p->tip);
    p->name = p->id = p->tip = nullptr;
    p->name_len = p->id_len = p->tip_len = 0;
}

/* The node's entry, created on demand; entries of dead nodes are recycled. */
static gates_err_t prop_for(gates_tree_t *tree, gates_u32 idx, gates_i_access_prop_t **out) {
    gates_i_access_prop_t *p = find_prop(tree, idx);
    if (p != nullptr) {
        *out = p;
        return GATES_OK;
    }
    for (gates_u32 i = 0; i < tree->access_prop_count; i++) {
        gates_i_access_prop_t *q = &tree->access_props[i];
        gates_node_t n = { .index = q->index, .generation = q->generation };
        if (!gates_i_valid(tree, n)) { /* its node is gone: reuse the entry */
            free_prop_strings(tree, q);
            *q = (gates_i_access_prop_t){ .index = idx, .generation = gates_i_slot(tree, idx)->generation,
                                          .labelled_by = GATES_NODE_NULL };
            *out = q;
            return GATES_OK;
        }
    }
    if (tree->access_prop_count == tree->access_prop_cap) {
        gates_u32 cap = tree->access_prop_cap != 0 ? tree->access_prop_cap * 2 : 8;
        gates_allocator_t a = tree->alloc;
        proven_result_mem_mut_t r =
            tree->access_props == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof(gates_i_access_prop_t), alignof(gates_i_access_prop_t))
                : a.realloc_fn(a.ctx, tree->access_props,
                               tree->access_prop_cap * sizeof(gates_i_access_prop_t),
                               cap * sizeof(gates_i_access_prop_t), alignof(gates_i_access_prop_t));
        if (!proven_is_ok(r.err)) return r.err;
        tree->access_props = (gates_i_access_prop_t *)r.value.ptr;
        tree->access_prop_cap = cap;
    }
    p = &tree->access_props[tree->access_prop_count++];
    *p = (gates_i_access_prop_t){ .index = idx, .generation = gates_i_slot(tree, idx)->generation,
                                  .labelled_by = GATES_NODE_NULL };
    *out = p;
    return GATES_OK;
}

static gates_err_t copy_str(gates_tree_t *tree, gates_str_t s, gates_u8 **out, gates_u32 *len) {
    gates_u8 *c = nullptr;
    if (s.size > 0) {
        proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, s.size, 1);
        if (!proven_is_ok(r.err)) return r.err;
        c = (gates_u8 *)r.value.ptr;
        memcpy(c, s.ptr, s.size);
    }
    if (*out != nullptr) tree->alloc.free_fn(tree->alloc.ctx, *out);
    *out = c;
    *len = (gates_u32)s.size;
    return GATES_OK;
}

gates_err_t gates_node_set_access_name(gates_tree_t *tree, gates_node_t node, gates_str_t name) {
    if (tree == nullptr || !gates_i_valid(tree, node) || (name.size > 0 && name.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_access_prop_t *p = nullptr;
    gates_err_t err = prop_for(tree, node.index, &p);
    if (gates_is_ok(err)) err = copy_str(tree, name, &p->name, &p->name_len);
    if (gates_is_ok(err)) gates_i_access_log(tree, GATES_ACCESS_CHANGED, node.index, 0);
    return err;
}

gates_err_t gates_node_set_automation_id(gates_tree_t *tree, gates_node_t node, gates_str_t id) {
    if (tree == nullptr || !gates_i_valid(tree, node) || (id.size > 0 && id.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_access_prop_t *p = nullptr;
    gates_err_t err = prop_for(tree, node.index, &p);
    if (gates_is_ok(err)) err = copy_str(tree, id, &p->id, &p->id_len);
    return err;
}

gates_err_t gates_node_set_labelled_by(gates_tree_t *tree, gates_node_t node, gates_node_t label) {
    if (tree == nullptr || !gates_i_valid(tree, node) ||
        (!gates_node_eq(label, GATES_NODE_NULL) &&
         (!gates_i_valid(tree, label) || gates_i_slot(tree, label.index)->kind != GATES_NODE_LABEL))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_access_prop_t *p = nullptr;
    gates_err_t err = prop_for(tree, node.index, &p);
    if (gates_is_ok(err)) {
        p->labelled_by = label;
        gates_i_access_log(tree, GATES_ACCESS_CHANGED, node.index, 0);
    }
    return err;
}

gates_err_t gates_node_set_live(gates_tree_t *tree, gates_node_t node, gates_live_t live) {
    if (tree == nullptr || !gates_i_valid(tree, node) || live > GATES_LIVE_ASSERTIVE) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_access_prop_t *p = nullptr;
    gates_err_t err = prop_for(tree, node.index, &p);
    if (gates_is_ok(err)) p->live = live;
    return err;
}

/* -- tooltips (plan-0018): kept with the other per-node properties ---------------- */

gates_err_t gates_node_set_tooltip(gates_tree_t *tree, gates_node_t node, gates_str_t text) {
    if (tree == nullptr || !gates_i_valid(tree, node) || (text.size > 0 && text.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_access_prop_t *p = find_prop(tree, node.index);
    if (p == nullptr && text.size == 0) {
        return GATES_OK;
    }
    gates_err_t err = p != nullptr ? GATES_OK : prop_for(tree, node.index, &p);
    if (gates_is_ok(err)) err = copy_str(tree, text, &p->tip, &p->tip_len);
    if (gates_is_ok(err)) {
        gates_i_access_log(tree, GATES_ACCESS_CHANGED, node.index, 0);
        gates_i_tip_check(tree); /* a shown tooltip follows its text, or goes */
    }
    return err;
}

gates_str_t gates_i_tooltip_of(const gates_tree_t *tree, gates_u32 idx) {
    const gates_i_access_prop_t *p = find_prop(tree, idx);
    return p != nullptr && p->tip_len > 0 ? (gates_str_t){ .ptr = p->tip, .size = p->tip_len } : (gates_str_t){0};
}

gates_str_t gates_node_tooltip(const gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr || !gates_i_valid(tree, node)) return (gates_str_t){0};
    return gates_i_tooltip_of(tree, node.index);
}

gates_live_t gates_i_access_live(const gates_tree_t *tree, gates_u32 idx) {
    const gates_i_access_prop_t *p = find_prop(tree, idx);
    return p != nullptr ? p->live : GATES_LIVE_OFF;
}

/* -- change log ------------------------------------------------------------------------ */

void gates_access_enable(gates_tree_t *tree, bool enable) {
    if (tree == nullptr) return;
    tree->access_on = enable;
    if (!enable) {
        tree->access_change_count = 0;
        tree->access_overflow = false;
    }
}

bool gates_access_enabled(const gates_tree_t *tree) {
    return tree != nullptr && tree->access_on;
}

void gates_i_access_log(gates_tree_t *tree, gates_access_change_kind_t kind, gates_u32 idx,
                        gates_u64 item) {
    if (!tree->access_on || idx == GATES_NONE) return;
    gates_node_t node = { .index = idx, .generation = gates_i_slot(tree, idx)->generation };
    for (gates_u32 i = 0; i < tree->access_change_count; i++) {
        const gates_access_change_t *c = &tree->access_changes[i];
        if (c->kind == kind && gates_node_eq(c->node, node) && c->item == item) return; /* coalesced */
    }
    if (tree->access_change_count == GATES_ACCESS_CHANGES_MAX) {
        tree->access_overflow = true;
        return;
    }
    tree->access_changes[tree->access_change_count++] =
        (gates_access_change_t){ .kind = kind, .node = node, .item = item };
}

gates_u32 gates_access_take_changes(gates_tree_t *tree, gates_access_change_t *out, gates_u32 cap,
                                    bool *overflow) {
    if (overflow != nullptr) *overflow = tree != nullptr && tree->access_overflow;
    if (tree == nullptr || out == nullptr) return 0;
    gates_u32 n = tree->access_change_count < cap ? tree->access_change_count : cap;
    memcpy(out, tree->access_changes, n * sizeof *out);
    memmove(tree->access_changes, tree->access_changes + n,
            (tree->access_change_count - n) * sizeof *out);
    tree->access_change_count -= n;
    tree->access_overflow = false;
    return n;
}

gates_err_t gates_access_announce(gates_tree_t *tree, gates_str_t text, bool assertive) {
    if (tree == nullptr || (text.size > 0 && text.ptr == nullptr)) return PROVEN_ERR_INVALID_ARG;
    gates_err_t err = copy_str(tree, text, &tree->announce, &tree->announce_len);
    if (!gates_is_ok(err)) return err;
    tree->announce_assertive = assertive;
    gates_i_access_log(tree, GATES_ACCESS_ANNOUNCE, tree->root, 0);
    return GATES_OK;
}

gates_str_t gates_access_announcement(const gates_tree_t *tree, bool *assertive) {
    if (assertive != nullptr) *assertive = tree != nullptr && tree->announce_assertive;
    if (tree == nullptr) return (gates_str_t){0};
    return (gates_str_t){ .ptr = tree->announce, .size = tree->announce_len };
}

void gates_i_access_free(gates_tree_t *tree) {
    for (gates_u32 i = 0; i < tree->access_prop_count; i++) free_prop_strings(tree, &tree->access_props[i]);
    gates_allocator_t a = tree->alloc;
    if (tree->access_props != nullptr) a.free_fn(a.ctx, tree->access_props);
    if (tree->access_buf != nullptr) a.free_fn(a.ctx, tree->access_buf);
    if (tree->announce != nullptr) a.free_fn(a.ctx, tree->announce);
    tree->access_props = nullptr;
    tree->access_buf = nullptr;
    tree->announce = nullptr;
    tree->access_prop_count = tree->access_prop_cap = 0;
}

/* -- strings handed out ------------------------------------------------------------------ */

/* The scratch an info's strings live in. It may move while an info is being
 * filled (it grows), so strings are bound by offset and resolved at the end. */
#define SBUF_SLOTS 8
typedef struct sbuf_t {
    gates_tree_t *tree;
    gates_u32 len;
    bool failed;
    gates_str_t *slot[SBUF_SLOTS];
    gates_u32 off[SBUF_SLOTS];
    gates_u32 nslots;
} sbuf_t;

/* Appends to the tree's scratch; returns the offset of the piece. */
static gates_u32 put(sbuf_t *b, gates_str_t s) {
    gates_u32 at = b->len;
    if (s.size == 0 || b->failed) return at;
    gates_tree_t *t = b->tree;
    if (b->len + s.size > t->access_buf_cap) {
        gates_u32 cap = t->access_buf_cap != 0 ? t->access_buf_cap : 256;
        while (cap < b->len + s.size) cap *= 2;
        gates_allocator_t a = t->alloc;
        proven_result_mem_mut_t r = t->access_buf == nullptr
                                        ? a.alloc_fn(a.ctx, cap, 1)
                                        : a.realloc_fn(a.ctx, t->access_buf, t->access_buf_cap, cap, 1);
        if (!proven_is_ok(r.err)) {
            b->failed = true;
            return at;
        }
        t->access_buf = (gates_u8 *)r.value.ptr;
        t->access_buf_cap = cap;
    }
    memcpy(t->access_buf + b->len, s.ptr, s.size);
    b->len += (gates_u32)s.size;
    return at;
}

/* *s will be scratch[at, at + size) once resolved. */
static void bind(sbuf_t *b, gates_str_t *s, gates_u32 at, gates_usize_t size) {
    *s = (gates_str_t){ .ptr = nullptr, .size = size };
    if (b->nslots < SBUF_SLOTS) {
        b->slot[b->nslots] = s;
        b->off[b->nslots] = at;
        b->nslots++;
    }
}

/* The scratch no longer moves: point every bound string into it. */
static void resolve(sbuf_t *b) {
    for (gates_u32 k = 0; k < b->nslots; k++) {
        gates_str_t *s = b->slot[k];
        if (b->failed || s->size == 0 || b->tree->access_buf == nullptr) {
            *s = (gates_str_t){0};
        } else {
            s->ptr = b->tree->access_buf + b->off[k];
        }
    }
}

static gates_str_t cstr(const char *s) {
    return (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) };
}

static void num_id(sbuf_t *b, gates_str_t *out, const char *prefix, gates_u64 v) {
    char tmp[32];
    int n = 0;
    char digits[24];
    int d = 0;
    do { digits[d++] = (char)('0' + v % 10); v /= 10; } while (v != 0 && d < 24);
    for (const char *p = prefix; *p != '\0'; p++) tmp[n++] = *p;
    while (d > 0) tmp[n++] = digits[--d];
    gates_u32 at = put(b, (gates_str_t){ .ptr = (const gates_u8 *)tmp, .size = (gates_usize_t)n });
    bind(b, out, at, (gates_usize_t)n);
}

/* Puts shown text: mnemonic markup is removed in place (the stripped text is
 * never longer, and each byte is written at or before where it was read). */
static gates_u32 put_shown(sbuf_t *b, gates_str_t s, bool markup, gates_usize_t *size) {
    gates_u32 at = put(b, s);
    *size = s.size;
    if (markup && !b->failed && s.size > 0) {
        gates_u8 *p = b->tree->access_buf + at;
        gates_u32 n = gates_i_mn_strip((gates_str_t){ .ptr = p, .size = s.size }, p, (gates_u32)s.size);
        b->len = at + n;
        *size = n;
    }
    return at;
}

/* "Alt+X" (or "X" alone) for a mnemonic letter. */
static void key_str(sbuf_t *b, gates_str_t *out, gates_u8 letter, bool alt) {
    if (letter == 0) return;
    char tmp[6] = { 'A', 'l', 't', '+', (char)letter, 0 };
    gates_str_t k = alt ? (gates_str_t){ .ptr = (const gates_u8 *)tmp, .size = 5 }
                        : (gates_str_t){ .ptr = (const gates_u8 *)tmp + 4, .size = 1 };
    gates_u32 at = put(b, k);
    bind(b, out, at, k.size);
}

static void accel_str(sbuf_t *b, gates_str_t *out, const gates_shortcut_t *k) {
    char tmp[32];
    gates_usize_t n = gates_i_shortcut_text(k, tmp, sizeof tmp);
    if (n == 0 || n >= sizeof tmp) return;
    gates_u32 at = put(b, (gates_str_t){ .ptr = (const gates_u8 *)tmp, .size = n });
    bind(b, out, at, n);
}

/* -- helpers ------------------------------------------------------------------------------- */

static gates_widget_state_t *state_of(const gates_tree_t *tree, gates_u32 idx) {
    return gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
}

static gates_str_t own_text(const gates_tree_t *tree, gates_u32 idx) {
    const gates_widget_state_t *st = state_of(tree, idx);
    if (st == nullptr) return (gates_str_t){0};
    if (st->edit != nullptr) return (gates_str_t){0}; /* an edit's text is its value, not its name */
    return gates_i_widget_label(tree, st);
}

/* On screen: nothing hidden on the way up, every stack shows this branch. */
static bool shown(const gates_tree_t *tree, gates_u32 idx) {
    gates_u32 child = idx;
    for (gates_u32 p = idx; p != GATES_NONE; p = gates_i_slot(tree, p)->parent) {
        const gates_node_slot_t *s = gates_i_slot(tree, p);
        if (s->hidden) return false;
        if (p != idx && s->layout_kind == GATES_LAYOUT_STACK) {
            gates_u32 i = 0;
            for (gates_u32 c = s->first_child; c != GATES_NONE && c != child;
                 c = gates_i_slot(tree, c)->next_sibling) i++;
            if (i != s->active_child) return false;
        }
        child = p;
    }
    return true;
}

/* Scrolled out: the rect lies outside the viewport of a scroll area above it. */
static bool clipped(const gates_tree_t *tree, gates_u32 idx, gates_rect_t r) {
    for (gates_u32 p = gates_i_slot(tree, idx)->parent; p != GATES_NONE; p = gates_i_slot(tree, p)->parent) {
        const gates_node_slot_t *s = gates_i_slot(tree, p);
        if (s->layout_kind == GATES_LAYOUT_SCROLL && gates_rect_is_empty(gates_rect_intersect(r, s->layout_rect))) {
            return true;
        }
    }
    return false;
}

static gates_str_t strip_required(gates_str_t s, bool *required) {
    if (s.size >= 2 && s.ptr[s.size - 1] == '*' && s.ptr[s.size - 2] == ' ') {
        *required = true;
        s.size -= 2;
    }
    return s;
}

static gates_str_t label_text(const gates_tree_t *tree, gates_node_t n) {
    if (!gates_i_valid(tree, n)) return (gates_str_t){0};
    const gates_widget_state_t *st = state_of(tree, n.index);
    return st != nullptr && st->text != nullptr ? (gates_str_t){ .ptr = st->text, .size = st->text_len }
                                               : (gates_str_t){0};
}

static bool is_interactive(gates_node_kind_t k) {
    return k == GATES_NODE_BUTTON || k == GATES_NODE_CHECKBOX || k == GATES_NODE_TEXTBOX ||
           k == GATES_NODE_RADIO || k == GATES_NODE_CHOICE || k == GATES_NODE_VIEW ||
           k == GATES_NODE_TOOLBAR || k == GATES_NODE_TABSTRIP || k == GATES_NODE_SLIDER || k == GATES_NODE_EDITOR;
}

static gates_role_t role_of(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (idx == tree->root) return GATES_ROLE_WINDOW;
    switch (s->kind) {
    case GATES_NODE_LABEL: return GATES_ROLE_TEXT;
    case GATES_NODE_BUTTON: return GATES_ROLE_BUTTON;
    case GATES_NODE_CHECKBOX: return GATES_ROLE_CHECK_BOX;
    case GATES_NODE_TEXTBOX: return GATES_ROLE_EDIT;
    case GATES_NODE_EDITOR: return GATES_ROLE_EDIT; /* multi-line (plan-0022) */
    case GATES_NODE_RADIO: return GATES_ROLE_RADIO_GROUP;
    case GATES_NODE_CHOICE: return GATES_ROLE_COMBO_BOX;
    case GATES_NODE_SEPARATOR: return GATES_ROLE_SEPARATOR;
    case GATES_NODE_PROGRESS: return GATES_ROLE_PROGRESS_BAR;
    case GATES_NODE_DIALOG: return GATES_ROLE_DIALOG;
    case GATES_NODE_MENU: return GATES_ROLE_MENU;
    case GATES_NODE_FORM: return GATES_ROLE_FORM;
    case GATES_NODE_MENUBAR: return GATES_ROLE_MENU_BAR;
    case GATES_NODE_TOOLBAR: return GATES_ROLE_TOOL_BAR;
    case GATES_NODE_STATUSBAR: return GATES_ROLE_STATUS_BAR;
    case GATES_NODE_TABSTRIP: return GATES_ROLE_TAB;
    case GATES_NODE_SPIN: return GATES_ROLE_SPINNER;
    case GATES_NODE_GROUP: return GATES_ROLE_GROUP;
    case GATES_NODE_IMAGE: /* named: an Image; unnamed: decoration (plan-0020) */
        return find_prop(tree, idx) != nullptr && find_prop(tree, idx)->name_len > 0 ? GATES_ROLE_IMAGE
                                                                                   : GATES_ROLE_NONE;
    case GATES_NODE_SLIDER: return GATES_ROLE_SLIDER;
    case GATES_NODE_VIEW: {
        gates_u32 k = gates_i_view_kind(tree, idx);
        return k == GATES_I_VIEW_TREE ? GATES_ROLE_TREE : k == GATES_I_VIEW_TABLE ? GATES_ROLE_TABLE : GATES_ROLE_LIST;
    }
    case GATES_NODE_PANEL:
    case GATES_NODE_CUSTOM:
    default:
        if (s->layout_kind == GATES_LAYOUT_SCROLL) return GATES_ROLE_SCROLL_AREA;
        if (gates_i_tab_page_index(tree, idx, nullptr) >= 0) return GATES_ROLE_GROUP; /* a tab's page */
        return find_prop(tree, idx) != nullptr && find_prop(tree, idx)->name_len > 0 ? GATES_ROLE_GROUP
                                                                                   : GATES_ROLE_NONE;
    }
}

/* -- items ------------------------------------------------------------------------------------ */

gates_u64 gates_access_item_count(gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr || !gates_i_valid(tree, node)) return 0;
    const gates_node_slot_t *s = gates_i_slot(tree, node.index);
    const gates_widget_state_t *st = state_of(tree, node.index);
    if (st == nullptr) return 0;
    if (s->kind == GATES_NODE_RADIO || s->kind == GATES_NODE_CHOICE) return st->opt_count;
    if (s->kind == GATES_NODE_VIEW) return gates_i_view_item_count(tree, node.index);
    if (s->kind == GATES_NODE_MENUBAR) return st->mbar != nullptr ? st->mbar->count : 0;
    if (s->kind == GATES_NODE_TABSTRIP) return s->parent != GATES_NONE ? gates_i_tabs_count(tree, s->parent) : 0;
    if (s->kind == GATES_NODE_TOOLBAR && st->tbar != nullptr) {
        gates_u64 n = 0;
        for (gates_u32 k = 0; k < st->tbar->count; k++) n += st->tbar->ids[k] != 0 ? 1u : 0u;
        return n + (gates_i_toolbar_shown(tree, node.index) < st->tbar->count ? 1u : 0u);
    }
    if (s->kind == GATES_NODE_MENU) {
        if (st->menu_is_list) return 0; /* a choice's list: its rows are the choice's items */
        gates_u64 n = 0;
        for (gates_u32 r = 0; r < st->menu_count; r++) n += st->menu_ids[r] != 0 ? 1u : 0u;
        return n;
    }
    return 0;
}

gates_u64 gates_access_item_at(gates_tree_t *tree, gates_node_t node, gates_u64 index) {
    if (tree == nullptr || !gates_i_valid(tree, node)) return 0;
    const gates_node_slot_t *s = gates_i_slot(tree, node.index);
    const gates_widget_state_t *st = state_of(tree, node.index);
    if (st == nullptr) return 0;
    if (s->kind == GATES_NODE_RADIO || s->kind == GATES_NODE_CHOICE) {
        return index < st->opt_count ? st->opts[index].id : 0;
    }
    if (s->kind == GATES_NODE_VIEW) return gates_i_view_item_at(tree, node.index, index);
    if (s->kind == GATES_NODE_MENUBAR) {
        return st->mbar != nullptr && index < st->mbar->count ? index + 1 : 0; /* titles 1..n */
    }
    if (s->kind == GATES_NODE_TABSTRIP) {
        return s->parent != GATES_NONE && index < gates_i_tabs_count(tree, s->parent) ? index + 1 : 0;
    }
    if (s->kind == GATES_NODE_TOOLBAR && st->tbar != nullptr) { /* entry + 1; ">>" is count + 1 */
        gates_u64 k = 0;
        for (gates_u32 e = 0; e < st->tbar->count; e++) {
            if (st->tbar->ids[e] == 0) continue;
            if (k++ == index) return (gates_u64)e + 1;
        }
        return k == index && gates_i_toolbar_shown(tree, node.index) < st->tbar->count ? st->tbar->count + 1u : 0u;
    }
    if (s->kind == GATES_NODE_MENU) {
        gates_u64 k = 0;
        for (gates_u32 r = 0; r < st->menu_count; r++) {
            if (st->menu_ids[r] == 0) continue;
            if (k++ == index) return st->menu_ids[r];
        }
    }
    return 0;
}

static gates_i32 option_row(const gates_widget_state_t *st, gates_u64 id) {
    for (gates_u32 i = 0; i < st->opt_count; i++) if (st->opts[i].id == id) return (gates_i32)i;
    return -1;
}

static gates_i32 menu_row_of(const gates_widget_state_t *st, gates_u64 id) {
    for (gates_u32 r = 0; r < st->menu_count; r++) if (st->menu_ids[r] == id) return (gates_i32)r;
    return -1;
}

/* -- the accessible tree --------------------------------------------------------------------- */

static const gates_access_ref_t NO_REF = { .node = { .index = GATES_NONE, .generation = 0 }, .item = 0 };

static gates_access_ref_t ref_of(const gates_tree_t *tree, gates_u32 idx, gates_u64 item) {
    return idx == GATES_NONE ? NO_REF : (gates_access_ref_t){ gates_i_handle(tree, idx), item };
}

/* A choice's option list: drawn as an overlay, exposed as the choice's items. */
static bool is_list_overlay(const gates_tree_t *tree, gates_u32 idx) {
    const gates_widget_state_t *st = state_of(tree, idx);
    return gates_i_slot(tree, idx)->kind == GATES_NODE_MENU && st != nullptr && st->menu_is_list;
}

/* Position of an open dialog or menu among the overlays, or -1. */
static gates_i32 overlay_pos(const gates_tree_t *tree, gates_u32 idx) {
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        if (tree->overlays[i].index == idx) return (gates_i32)i;
    }
    return -1;
}

/* The first (step +1) or last (step -1) element overlay from position `from`. */
static gates_u32 overlay_from(const gates_tree_t *tree, gates_i32 from, gates_i32 step) {
    for (gates_i32 i = from; i >= 0 && i < (gates_i32)tree->overlay_count; i += step) {
        gates_u32 o = tree->overlays[i].index;
        if (!is_list_overlay(tree, o)) return o;
    }
    return GATES_NONE;
}

static gates_u32 shown_from(const gates_tree_t *tree, gates_u32 c, bool forward) {
    while (c != GATES_NONE && !shown(tree, c)) {
        const gates_node_slot_t *s = gates_i_slot(tree, c);
        c = forward ? s->next_sibling : s->prev_sibling;
    }
    return c;
}

/* The item's position among its node's items, or -1. */
static gates_i64 item_pos(gates_tree_t *tree, gates_node_t node, gates_u64 item) {
    gates_u64 n = gates_access_item_count(tree, node);
    for (gates_u64 i = 0; i < n; i++) {
        if (gates_access_item_at(tree, node, i) == item) return (gates_i64)i;
    }
    return -1;
}

static bool ref_ok(gates_tree_t *tree, gates_access_ref_t ref) {
    if (tree == nullptr || !gates_i_valid(tree, ref.node)) return false;
    if (ref.item == 0) return true;
    if (gates_i_slot(tree, ref.node.index)->kind == GATES_NODE_VIEW) {
        gates_i_view_item_t it; /* a shown row, or the selection wherever it is */
        return gates_i_view_item(tree, ref.node.index, ref.item, &it);
    }
    return item_pos(tree, ref.node, ref.item) >= 0;
}

gates_access_ref_t gates_access_parent(gates_tree_t *tree, gates_access_ref_t ref) {
    if (!ref_ok(tree, ref)) return NO_REF;
    if (ref.item != 0) return (gates_access_ref_t){ ref.node, 0 };
    if (overlay_pos(tree, ref.node.index) >= 0) return ref_of(tree, tree->root, 0);
    return ref_of(tree, gates_i_slot(tree, ref.node.index)->parent, 0);
}

gates_access_ref_t gates_access_first_child(gates_tree_t *tree, gates_access_ref_t ref) {
    if (!ref_ok(tree, ref) || ref.item != 0) return NO_REF;
    gates_u32 idx = ref.node.index;
    gates_u32 c = shown_from(tree, gates_i_slot(tree, idx)->first_child, true);
    if (c != GATES_NONE) return ref_of(tree, c, 0);
    if (gates_access_item_count(tree, ref.node) > 0) {
        return (gates_access_ref_t){ ref.node, gates_access_item_at(tree, ref.node, 0) };
    }
    return idx == tree->root ? ref_of(tree, overlay_from(tree, 0, 1), 0) : NO_REF;
}

gates_access_ref_t gates_access_last_child(gates_tree_t *tree, gates_access_ref_t ref) {
    if (!ref_ok(tree, ref) || ref.item != 0) return NO_REF;
    gates_u32 idx = ref.node.index;
    if (idx == tree->root) {
        gates_u32 o = overlay_from(tree, (gates_i32)tree->overlay_count - 1, -1);
        if (o != GATES_NONE) return ref_of(tree, o, 0);
    }
    gates_u64 n = gates_access_item_count(tree, ref.node);
    if (n > 0) return (gates_access_ref_t){ ref.node, gates_access_item_at(tree, ref.node, n - 1) };
    return ref_of(tree, shown_from(tree, gates_i_slot(tree, idx)->last_child, false), 0);
}

gates_access_ref_t gates_access_next(gates_tree_t *tree, gates_access_ref_t ref) {
    if (!ref_ok(tree, ref)) return NO_REF;
    if (ref.item != 0) {
        gates_i64 at = item_pos(tree, ref.node, ref.item);
        if (at < 0) return NO_REF; /* a selected row scrolled away has no siblings */
        gates_u64 i = (gates_u64)at + 1;
        return i < gates_access_item_count(tree, ref.node)
                   ? (gates_access_ref_t){ ref.node, gates_access_item_at(tree, ref.node, i) }
                   : NO_REF;
    }
    gates_i32 op = overlay_pos(tree, ref.node.index);
    if (op >= 0) return ref_of(tree, overlay_from(tree, op + 1, 1), 0);
    const gates_node_slot_t *s = gates_i_slot(tree, ref.node.index);
    if (s->parent == GATES_NONE) return NO_REF;
    gates_u32 c = shown_from(tree, s->next_sibling, true);
    if (c != GATES_NONE) return ref_of(tree, c, 0);
    gates_node_t parent = gates_i_handle(tree, s->parent);
    if (gates_access_item_count(tree, parent) > 0) {
        return (gates_access_ref_t){ parent, gates_access_item_at(tree, parent, 0) };
    }
    return s->parent == tree->root ? ref_of(tree, overlay_from(tree, 0, 1), 0) : NO_REF;
}

gates_access_ref_t gates_access_prev(gates_tree_t *tree, gates_access_ref_t ref) {
    if (!ref_ok(tree, ref)) return NO_REF;
    gates_u32 idx = ref.node.index;
    if (ref.item != 0) {
        gates_i64 i = item_pos(tree, ref.node, ref.item);
        if (i < 0) return NO_REF;
        if (i > 0) return (gates_access_ref_t){ ref.node, gates_access_item_at(tree, ref.node, (gates_u64)i - 1) };
        return ref_of(tree, shown_from(tree, gates_i_slot(tree, idx)->last_child, false), 0);
    }
    gates_i32 op = overlay_pos(tree, idx);
    if (op >= 0) {
        gates_u32 o = overlay_from(tree, op - 1, -1);
        if (o != GATES_NONE) return ref_of(tree, o, 0);
        return ref_of(tree, shown_from(tree, gates_i_slot(tree, tree->root)->last_child, false), 0);
    }
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->parent == GATES_NONE) return NO_REF;
    return ref_of(tree, shown_from(tree, s->prev_sibling, false), 0);
}

gates_access_ref_t gates_access_at_point(gates_tree_t *tree, gates_point_t p) {
    if (tree == nullptr) return NO_REF;
    gates_node_t n = gates_hit_test(tree, p);
    if (!gates_i_valid(tree, n)) return ref_of(tree, tree->root, 0);
    gates_u32 idx = n.index;
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = state_of(tree, idx);
    if (st == nullptr) return (gates_access_ref_t){ n, 0 };
    if (s->kind == GATES_NODE_MENU) {
        for (gates_u32 r = 0; r < st->menu_count; r++) {
            if (st->menu_ids[r] == 0 || !gates_rect_contains(gates_i_menu_row_rect(tree, idx, r), p)) continue;
            if (!st->menu_is_list) return (gates_access_ref_t){ n, st->menu_ids[r] };
            gates_node_t owner = { .index = st->menu_scope_index, .generation = st->menu_scope_generation };
            return gates_i_valid(tree, owner) ? (gates_access_ref_t){ owner, st->menu_ids[r] } : NO_REF;
        }
        if (st->menu_is_list) {
            gates_node_t owner = { .index = st->menu_scope_index, .generation = st->menu_scope_generation };
            return gates_i_valid(tree, owner) ? (gates_access_ref_t){ owner, 0 } : NO_REF;
        }
    } else if (s->kind == GATES_NODE_RADIO) {
        gates_i32 row = gates_i_radio_row_at(tree, idx, p);
        if (row >= 0) return (gates_access_ref_t){ n, st->opts[row].id };
    } else if (s->kind == GATES_NODE_VIEW) {
        return (gates_access_ref_t){ n, gates_i_view_row_at(tree, idx, p) };
    } else if (s->kind == GATES_NODE_MENUBAR) {
        gates_i32 t = gates_i_menubar_title_at(tree, idx, p);
        if (t >= 0) return (gates_access_ref_t){ n, (gates_u64)t + 1 };
    } else if (s->kind == GATES_NODE_TOOLBAR) {
        gates_i32 k = gates_i_toolbar_entry_at(tree, idx, p);
        if (k >= 0) return (gates_access_ref_t){ n, (gates_u64)k + 1 };
    } else if (s->kind == GATES_NODE_TABSTRIP) {
        gates_i32 k = gates_i_tab_at(tree, idx, p);
        if (k >= 0) return (gates_access_ref_t){ n, (gates_u64)k + 1 };
    }
    return (gates_access_ref_t){ n, 0 };
}

gates_access_ref_t gates_access_focus_ref(gates_tree_t *tree) {
    if (tree == nullptr) return NO_REF;
    if (tree->overlay_count > 0 && tree->overlays[tree->overlay_count - 1].kind == GATES_NODE_MENU) {
        gates_u32 m = tree->overlays[tree->overlay_count - 1].index;
        const gates_widget_state_t *st = state_of(tree, m);
        if (st != nullptr) {
            gates_u64 id = st->menu_sel >= 0 && (gates_u32)st->menu_sel < st->menu_count
                               ? st->menu_ids[st->menu_sel] : 0;
            if (!st->menu_is_list) return ref_of(tree, m, id);
            gates_node_t owner = { .index = st->menu_scope_index, .generation = st->menu_scope_generation };
            if (gates_i_valid(tree, owner)) return (gates_access_ref_t){ owner, id };
        }
    }
    if (tree->mb_mode == GATES_I_MB_HIGHLIGHT && gates_i_menubar_live(tree) != GATES_NONE) {
        return ref_of(tree, tree->menubar, (gates_u64)tree->mb_sel + 1); /* menu mode */
    }
    if (tree->focus == GATES_NONE) return NO_REF;
    const gates_widget_state_t *st = state_of(tree, tree->focus);
    if (gates_i_slot(tree, tree->focus)->kind == GATES_NODE_RADIO && st != nullptr &&
        gates_i_option_find(st, st->opt_sel) != nullptr) {
        return ref_of(tree, tree->focus, st->opt_sel);
    }
    if (gates_i_slot(tree, tree->focus)->kind == GATES_NODE_GROUPHEAD) {
        return ref_of(tree, gates_i_slot(tree, tree->focus)->parent, 0); /* the group (plan-0019) */
    }
    if (gates_i_slot(tree, tree->focus)->kind == GATES_NODE_TABSTRIP) {
        gates_u32 tabs = gates_i_slot(tree, tree->focus)->parent;
        return ref_of(tree, tree->focus, (gates_u64)gates_i_tabs_selected(tree, tabs) + 1);
    }
    if (gates_i_slot(tree, tree->focus)->kind == GATES_NODE_TOOLBAR && st != nullptr) {
        gates_i32 k = gates_i_toolbar_stop(tree, tree->focus); /* the button with the focus */
        return ref_of(tree, tree->focus, k >= 0 ? (gates_u64)k + 1 : 0);
    }
    if (gates_i_slot(tree, tree->focus)->kind == GATES_NODE_VIEW && st != nullptr) {
        return ref_of(tree, tree->focus, gates_i_view_selected(st)); /* the selected row */
    }
    return ref_of(tree, tree->focus, 0);
}

/* -- info -------------------------------------------------------------------------------------- */

static gates_err_t item_info(gates_tree_t *tree, gates_u32 idx, gates_u64 item, sbuf_t *b,
                             gates_access_info_t *out) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = state_of(tree, idx);
    if (st == nullptr) return PROVEN_ERR_INVALID_ARG;
    bool node_on = !gates_i_widget_inert(tree, st);
    if (s->kind == GATES_NODE_RADIO || s->kind == GATES_NODE_CHOICE) {
        gates_i32 row = option_row(st, item);
        if (row < 0) return PROVEN_ERR_INVALID_ARG;
        const gates_i_option_t *o = &st->opts[row];
        gates_u32 at = put(b, (gates_str_t){ .ptr = o->label, .size = o->label_len });
        bind(b, &out->name, at, o->label_len);
        out->role = s->kind == GATES_NODE_RADIO ? GATES_ROLE_RADIO_ITEM : GATES_ROLE_LIST_ITEM;
        out->set_position = (gates_u64)row + 1;
        out->set_size = st->opt_count;
        out->states = (o->id == st->opt_sel ? GATES_ACCESS_SELECTED : 0u) |
                      (o->disabled || !node_on ? GATES_ACCESS_DISABLED : 0u);
        out->actions = o->disabled || !node_on ? 0u : GATES_ACCESS_SELECT;
        if (s->kind == GATES_NODE_RADIO) {
            gates_i32 rh = gates_i_radio_row_h(tree->line_height > 0 ? tree->line_height : 16);
            out->bounds = (gates_rect_t){ s->layout_rect.x, s->layout_rect.y + row * rh, s->layout_rect.w, rh };
        } else {
            gates_i32 li = gates_i_choice_list_find(tree, idx);
            if (li >= 0) {
                out->bounds = gates_i_menu_row_rect(tree, tree->overlays[li].index, (gates_u32)row);
            } else {
                out->states |= GATES_ACCESS_OFFSCREEN; /* in the closed list */
            }
        }
        if (!shown(tree, idx) || clipped(tree, idx, out->bounds)) out->states |= GATES_ACCESS_OFFSCREEN;
    } else if (s->kind == GATES_NODE_MENU) {
        gates_i32 row = menu_row_of(st, item);
        if (row < 0 || item == 0) return PROVEN_ERR_INVALID_ARG;
        const gates_i_command_t *c = gates_i_command_find(tree, st->menu_scope_index,
                                                          st->menu_scope_generation,
                                                          (gates_command_id_t)item);
        if (st->menu_is_list) return PROVEN_ERR_INVALID_ARG; /* a choice's list: its items are the choice's */
        out->role = GATES_ROLE_MENU_ITEM;
        if (c != nullptr) {
            gates_str_t lab = { .ptr = c->label, .size = c->label_len };
            gates_usize_t n = 0;
            gates_u32 at = put_shown(b, lab, true, &n);
            bind(b, &out->name, at, n);
            key_str(b, &out->access_key, gates_mnemonic_of(lab), false);
            accel_str(b, &out->accelerator, &c->shortcut);
            out->states = (c->enabled ? 0u : GATES_ACCESS_DISABLED) | (c->checked ? GATES_ACCESS_CHECKED : 0u);
            out->actions = c->enabled ? GATES_ACCESS_INVOKE : 0u;
        } else {
            out->states = GATES_ACCESS_DISABLED;
        }
        out->bounds = gates_i_menu_row_rect(tree, idx, (gates_u32)row);
        out->set_size = gates_access_item_count(tree, gates_i_handle(tree, idx));
        out->set_position = (gates_u64)item_pos(tree, gates_i_handle(tree, idx), item) + 1;
    } else if (s->kind == GATES_NODE_TABSTRIP) {
        gates_u32 tabs = s->parent;
        if (tabs == GATES_NONE || item == 0 || item > gates_i_tabs_count(tree, tabs)) return PROVEN_ERR_INVALID_ARG;
        gates_u32 k = (gates_u32)item - 1;
        gates_str_t title = gates_i_tabs_title(tree, tabs, k);
        gates_usize_t nn = 0;
        gates_u32 at = put_shown(b, title, true, &nn);
        bind(b, &out->name, at, nn);
        key_str(b, &out->access_key, gates_mnemonic_of(title), true);
        out->role = GATES_ROLE_TAB_ITEM;
        out->states = (k == gates_i_tabs_selected(tree, tabs) ? GATES_ACCESS_SELECTED : 0u) |
                      (node_on ? 0u : GATES_ACCESS_DISABLED);
        out->actions = node_on ? GATES_ACCESS_SELECT : 0u;
        out->bounds = gates_i_tab_rect(tree, idx, k);
        if (!shown(tree, idx) || gates_rect_is_empty(out->bounds)) out->states |= GATES_ACCESS_OFFSCREEN;
        out->set_position = item;
        out->set_size = gates_i_tabs_count(tree, tabs);
    } else if (s->kind == GATES_NODE_TOOLBAR) {
        if (st->tbar == nullptr || item == 0 || item > (gates_u64)st->tbar->count + 1) return PROVEN_ERR_INVALID_ARG;
        gates_u32 k = (gates_u32)item - 1;
        gates_u32 n_shown = gates_i_toolbar_shown(tree, idx);
        out->role = GATES_ROLE_BUTTON;
        if (k == st->tbar->count) {                 /* ">>" */
            if (n_shown >= st->tbar->count) return PROVEN_ERR_INVALID_ARG;
            gates_u32 at = put(b, cstr("More"));
            bind(b, &out->name, at, 4);
            out->actions = node_on ? GATES_ACCESS_INVOKE : 0u;
            out->bounds = gates_i_toolbar_more_rect(tree, idx);
        } else {
            if (st->tbar->ids[k] == 0) return PROVEN_ERR_INVALID_ARG; /* separators are no items */
            const gates_i_command_t *c = gates_i_toolbar_command(tree, idx, k);
            bool on = c != nullptr && c->enabled;
            if (c != nullptr) {
                gates_usize_t nn = 0;
                gates_u32 at = put_shown(b, (gates_str_t){ .ptr = c->label, .size = c->label_len }, true, &nn);
                bind(b, &out->name, at, nn);
                accel_str(b, &out->accelerator, &c->shortcut);
                gates_u32 tn = gates_i_toolbar_tip(tree, idx, k, nullptr, 0);
                gates_u32 d0 = b->len;
                for (gates_u32 i = 0; i < tn && !b->failed; i++) put(b, cstr(" ")); /* room, then fill */
                if (!b->failed && tn > 0) {
                    (void)gates_i_toolbar_tip(tree, idx, k, b->tree->access_buf + d0, tn);
                    bind(b, &out->description, d0, tn);
                }
            }
            out->states = (on ? 0u : GATES_ACCESS_DISABLED) | (c != nullptr && c->checked ? GATES_ACCESS_CHECKED : 0u) |
                          (k >= n_shown ? GATES_ACCESS_OFFSCREEN : 0u);
            out->actions = on ? GATES_ACCESS_INVOKE : 0u;
            out->bounds = gates_i_toolbar_entry_rect(tree, idx, k);
        }
        if (!shown(tree, idx)) out->states |= GATES_ACCESS_OFFSCREEN;
        out->set_position = (gates_u64)item_pos(tree, gates_i_handle(tree, idx), item) + 1;
        out->set_size = gates_access_item_count(tree, gates_i_handle(tree, idx));
    } else if (s->kind == GATES_NODE_MENUBAR) {
        if (st->mbar == nullptr || item == 0 || item > st->mbar->count) return PROVEN_ERR_INVALID_ARG;
        gates_u32 t = (gates_u32)item - 1;
        gates_str_t title = { .ptr = st->mbar->items[t].title, .size = st->mbar->items[t].title_len };
        gates_usize_t n = 0;
        gates_u32 at = put_shown(b, title, true, &n);
        bind(b, &out->name, at, n);
        key_str(b, &out->access_key, gates_mnemonic_of(title), true);
        out->role = GATES_ROLE_MENU_ITEM;
        bool live = gates_i_menubar_live(tree) == idx;
        bool open = live && tree->mb_mode == GATES_I_MB_OPEN && tree->mb_sel == t;
        out->states = GATES_ACCESS_EXPANDABLE | (open ? GATES_ACCESS_EXPANDED : 0u) |
                      (live ? 0u : GATES_ACCESS_DISABLED);
        out->actions = live ? GATES_ACCESS_INVOKE | GATES_ACCESS_EXPAND : 0u;
        out->bounds = gates_i_menubar_title_rect(tree, idx, t);
        if (!shown(tree, idx)) out->states |= GATES_ACCESS_OFFSCREEN;
        out->set_position = item;
        out->set_size = st->mbar->count;
    } else if (s->kind == GATES_NODE_VIEW) {
        gates_i_view_item_t it;
        if (!gates_i_view_item(tree, idx, item, &it)) return PROVEN_ERR_INVALID_ARG;
        gates_u32 kind = gates_i_view_kind(tree, idx);
        out->role = kind == GATES_I_VIEW_TREE ? GATES_ROLE_TREE_ITEM
                  : kind == GATES_I_VIEW_TABLE ? GATES_ROLE_ROW : GATES_ROLE_LIST_ITEM;
        /* Name: the first cell; a table row reads all its cells. Cells are
         * borrowed until the next model call: copy each at once. */
        gates_u32 n0 = b->len;
        gates_u32 ncells = kind == GATES_I_VIEW_TABLE ? it.columns : 1;
        for (gates_u32 c = 0; c < ncells; c++) {
            gates_str_t cell = gates_i_view_cell(tree, idx, item, c);
            if (c > 0 && b->len > n0) put(b, cstr(", "));
            put(b, cell);
        }
        bind(b, &out->name, n0, b->len - n0);
        out->states = (it.selected ? GATES_ACCESS_SELECTED : 0u) | (node_on ? 0u : GATES_ACCESS_DISABLED);
        out->actions = node_on ? GATES_ACCESS_SELECT | GATES_ACCESS_INVOKE : 0u;
        if (it.has_info) {
            out->level = it.info.depth + 1;
            if (it.info.expandable) {
                out->states |= GATES_ACCESS_EXPANDABLE;
                if (node_on) out->actions |= GATES_ACCESS_EXPAND;
            }
            if (it.info.expanded) out->states |= GATES_ACCESS_EXPANDED;
            if (it.info.state == GATES_ROW_LOADING) out->states |= GATES_ACCESS_BUSY;
            if (it.info.state == GATES_ROW_ERROR) out->states |= GATES_ACCESS_INVALID;
        }
        out->bounds = it.rect;
        if (!it.shown || !shown(tree, idx) || clipped(tree, idx, it.rect)) out->states |= GATES_ACCESS_OFFSCREEN;
        out->set_position = it.row + 1;
        out->set_size = it.count;
        out->column_count = kind == GATES_I_VIEW_TABLE ? it.columns : 0;
    } else {
        return PROVEN_ERR_INVALID_ARG;
    }
    num_id(b, &out->automation_id, "item-", item);
    if (b->failed) return PROVEN_ERR_NOMEM;
    return GATES_OK;
}

gates_err_t gates_access_info(gates_tree_t *tree, gates_node_t node, gates_u64 item,
                              gates_access_info_t *out) {
    if (tree == nullptr || out == nullptr || !gates_i_valid(tree, node)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof *out);
    out->labelled_by = GATES_NODE_NULL;
    sbuf_t b = { .tree = tree };
    gates_u32 idx = node.index;
    if (item != 0) {
        gates_err_t err = item_info(tree, idx, item, &b, out);
        resolve(&b);
        return err;
    }
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    const gates_widget_state_t *st = state_of(tree, idx);
    const gates_i_access_prop_t *p = find_prop(tree, idx);
    out->role = role_of(tree, idx);
    out->bounds = s->layout_rect;
    out->live = p != nullptr ? p->live : GATES_LIVE_OFF;
    out->item_count = gates_access_item_count(tree, node);

    /* Name: explicit > form label > own text > dialog title. */
    /* A spin box's text box takes the spin box's name (plan-0019). */
    gates_u32 nidx = idx;
    gates_u32 box_spin = gates_i_spin_of_box(tree, idx);
    if (box_spin != GATES_NONE && (p == nullptr || (p->name_len == 0 && !gates_i_valid(tree, p->labelled_by)))) {
        nidx = box_spin;
    }
    const gates_i_access_prop_t *np = nidx == idx ? p : find_prop(tree, nidx);
    gates_i_field_info_t field;
    bool is_field = gates_i_form_field_info(tree, nidx, &field);
    bool required = false;
    gates_str_t name = {0};
    bool markup = false;          /* the name is mnemonic markup (plan-0018) */
    gates_node_t key_label = GATES_NODE_NULL;
    if (np != nullptr && np->name_len > 0) {
        name = (gates_str_t){ .ptr = np->name, .size = np->name_len };
    } else if (np != nullptr && gates_i_valid(tree, np->labelled_by)) {
        name = label_text(tree, np->labelled_by);
        out->labelled_by = np->labelled_by;
        key_label = np->labelled_by;
        markup = gates_i_mn_markup(tree, np->labelled_by.index);
    } else if (is_field) {
        name = strip_required(label_text(tree, field.label), &required);
        out->labelled_by = field.label;
        key_label = field.label;
        markup = gates_i_valid(tree, field.label) && gates_i_mn_markup(tree, field.label.index);
    } else if (s->kind == GATES_NODE_DIALOG && s->first_child != GATES_NONE) {
        name = label_text(tree, gates_i_handle(tree, s->first_child));
    } else if (s->kind == GATES_NODE_GROUP && s->first_child != GATES_NONE) {
        name = own_text(tree, s->first_child); /* its title (plan-0019) */
        markup = true;
    } else if (s->kind == GATES_NODE_TABSTRIP && s->parent != GATES_NONE) {
        name = gates_i_tabs_title(tree, s->parent, gates_i_tabs_selected(tree, s->parent));
        markup = true; /* the selected tab names the strip */
    } else if (gates_i_tab_page_index(tree, idx, nullptr) >= 0) {
        gates_u32 tabs = GATES_NONE;
        gates_i32 pi = gates_i_tab_page_index(tree, idx, &tabs);
        name = gates_i_tabs_title(tree, tabs, (gates_u32)pi);
        markup = true;
    } else {
        name = own_text(tree, idx);
        markup = gates_i_mn_markup(tree, idx) && s->kind != GATES_NODE_MENUBAR;
    }
    if (is_field) required = required || field.required;
    gates_usize_t name_n = 0;
    gates_u32 at = put_shown(&b, name, markup, &name_n);
    bind(&b, &out->name, at, name_n);
    /* Access key: the node's own mnemonic, or that of the label that targets it. */
    if (markup && gates_node_eq(key_label, GATES_NODE_NULL) &&
        (gates_i_mn_markup(tree, idx) || s->kind == GATES_NODE_GROUP)) {
        key_str(&b, &out->access_key, gates_mnemonic_of(name), true);
    } else if (gates_i_valid(tree, key_label) && gates_node_eq(gates_label_target(tree, key_label), node)) {
        key_str(&b, &out->access_key, gates_mnemonic_of(label_text(tree, key_label)), true);
    }
    if (st != nullptr && st->cmd_id != 0) {
        const gates_i_command_t *bc = gates_i_command_find(tree, st->cmd_scope_index,
                                                           st->cmd_scope_generation, st->cmd_id);
        if (bc != nullptr) accel_str(&b, &out->accelerator, &bc->shortcut);
    }

    /* Description: error, then help. */
    if (is_field) {
        gates_str_t err = gates_i_valid(tree, field.error) && !gates_i_slot(tree, field.error.index)->hidden
                              ? label_text(tree, field.error) : (gates_str_t){0};
        gates_str_t help = label_text(tree, field.help);
        gates_u32 d0 = b.len;
        put(&b, err);
        if (err.size > 0 && help.size > 0) put(&b, cstr(". "));
        put(&b, help);
        bind(&b, &out->description, d0, b.len - d0);
    }
    if (out->description.size == 0 && p != nullptr && p->tip_len > 0) {
        gates_u32 d0 = put(&b, (gates_str_t){ .ptr = p->tip, .size = p->tip_len }); /* the tooltip */
        bind(&b, &out->description, d0, p->tip_len);
    }

    /* Automation id: explicit > field-<id> > cmd-<id>. */
    if (p != nullptr && p->id_len > 0) {
        gates_u32 i0 = put(&b, (gates_str_t){ .ptr = p->id, .size = p->id_len });
        bind(&b, &out->automation_id, i0, p->id_len);
    } else if (is_field) {
        num_id(&b, &out->automation_id, "field-", field.id);
    } else if (st != nullptr && st->cmd_id != 0) {
        num_id(&b, &out->automation_id, "cmd-", st->cmd_id);
    }

    /* States and actions. */
    gates_u32 states = 0, actions = 0;
    bool inert = st != nullptr && gates_i_widget_inert(tree, st);
    if (gates_i_focus_eligible(tree, idx)) states |= GATES_ACCESS_FOCUSABLE;
    if (tree->focus == idx) states |= GATES_ACCESS_FOCUSED;
    if (inert) states |= GATES_ACCESS_DISABLED;
    if (required) states |= GATES_ACCESS_REQUIRED;
    if (!shown(tree, idx) || gates_rect_is_empty(s->layout_rect) || clipped(tree, idx, s->layout_rect)) {
        states |= GATES_ACCESS_OFFSCREEN;
    }
    if (is_interactive(s->kind) && !inert) actions |= GATES_ACCESS_FOCUS;
    switch (s->kind) {
    case GATES_NODE_BUTTON:
        if (!inert) actions |= GATES_ACCESS_INVOKE;
        break;
    case GATES_NODE_CHECKBOX:
        if (st != nullptr && st->checked) states |= GATES_ACCESS_CHECKED;
        if (!inert) actions |= GATES_ACCESS_TOGGLE;
        break;
    case GATES_NODE_TEXTBOX:
        if (st != nullptr && st->edit != nullptr) {
            if (st->read_only) states |= GATES_ACCESS_READ_ONLY;
            if (st->invalid) states |= GATES_ACCESS_INVALID;
            if (st->password) {
                states |= GATES_ACCESS_PASSWORD; /* the value is never exposed */
            } else {
                gates_str_t t = gates_text_edit_text(st->edit);
                gates_u32 v0 = put(&b, t);
                bind(&b, &out->value, v0, t.size);
                out->caret = gates_text_edit_caret(st->edit);
                out->anchor = st->edit->anchor;
            }
            if (!inert && !st->read_only) actions |= GATES_ACCESS_SET_VALUE;
        }
        break;
    case GATES_NODE_EDITOR:
        if (st != nullptr && st->editor != nullptr) {
            /* The text (both spans of the buffer), caret and selection (plan-0022). */
            gates_node_t h = gates_i_handle(tree, idx);
            const gates_text_buffer_t *tb = gates_editor_buffer(tree, h);
            gates_str_t s1, s2;
            gates_text_buffer_span(tb, 0, gates_text_buffer_length(tb), &s1, &s2);
            gates_u32 v0 = put(&b, s1);
            (void)put(&b, s2);
            bind(&b, &out->value, v0, s1.size + s2.size);
            gates_editor_selection(tree, h, &out->anchor, &out->caret);
            if (gates_editor_read_only(tree, h)) states |= GATES_ACCESS_READ_ONLY;
            else if (!inert) actions |= GATES_ACCESS_SET_VALUE;
        }
        break;
    case GATES_NODE_RADIO:
    case GATES_NODE_CHOICE:
        if (st != nullptr) {
            const gates_i_option_t *o = gates_i_option_find(st, st->opt_sel);
            if (o != nullptr) {
                gates_u32 v0 = put(&b, (gates_str_t){ .ptr = o->label, .size = o->label_len });
                bind(&b, &out->value, v0, o->label_len);
            }
            if (s->kind == GATES_NODE_CHOICE) {
                states |= GATES_ACCESS_EXPANDABLE;
                if (gates_i_choice_list_find(tree, idx) >= 0) states |= GATES_ACCESS_EXPANDED;
                if (!inert) actions |= GATES_ACCESS_EXPAND;
            }
        }
        break;
    case GATES_NODE_PROGRESS:
        out->has_range = true;
        out->range_min = 0;
        out->range_max = 1000;
        out->range_value = st != nullptr ? st->value : 0;
        break;
    case GATES_NODE_GROUP:
        if (gates_i_group_foldable(tree, idx)) {
            states |= GATES_ACCESS_EXPANDABLE | (st->checked ? GATES_ACCESS_EXPANDED : 0u);
            if (!inert) actions |= GATES_ACCESS_EXPAND;
            if (tree->focus != GATES_NONE && tree->focus == s->first_child) states |= GATES_ACCESS_FOCUSED;
        }
        break;
    case GATES_NODE_SPIN:
    case GATES_NODE_SLIDER: {
        gates_i64 lo, hi, v; /* plan-0019; the info's range is 32-bit: clamped into it */
        if (gates_i_range_info(tree, idx, &lo, &hi, &v)) {
            out->has_range = true;
            out->range_min = lo < INT32_MIN ? INT32_MIN : lo > INT32_MAX ? INT32_MAX : (gates_i32)lo;
            out->range_max = hi < INT32_MIN ? INT32_MIN : hi > INT32_MAX ? INT32_MAX : (gates_i32)hi;
            out->range_value = v < INT32_MIN ? INT32_MIN : v > INT32_MAX ? INT32_MAX : (gates_i32)v;
            if (!inert) actions |= GATES_ACCESS_SET_VALUE;
            if (s->kind == GATES_NODE_SPIN) {
                gates_str_t t = gates_textbox_text(tree, gates_i_handle(tree, s->first_child));
                gates_u32 v0 = put(&b, t);
                bind(&b, &out->value, v0, t.size);
            }
        }
        break;
    }
    case GATES_NODE_DIALOG:
        states |= GATES_ACCESS_MODAL;
        break;
    case GATES_NODE_VIEW: {
        gates_u32 pos, page;
        if (!inert && gates_i_view_scroll_info(tree, idx, &pos, &page)) actions |= GATES_ACCESS_SCROLL;
        if (gates_i_view_kind(tree, idx) == GATES_I_VIEW_TABLE) {
            gates_i_view_item_t it = {0};
            gates_item_id_t first = gates_i_view_item_at(tree, idx, 0);
            if (first != 0 && gates_i_view_item(tree, idx, first, &it)) out->column_count = it.columns;
        }
        break;
    }
    default:
        if (s->layout_kind == GATES_LAYOUT_SCROLL && gates_i_scrollable(tree, idx)) actions |= GATES_ACCESS_SCROLL;
        break;
    }
    out->states = states;
    out->actions = actions;
    resolve(&b);
    return b.failed ? PROVEN_ERR_NOMEM : GATES_OK;
}

/* -- actions: the input paths --------------------------------------------------------------- */

static gates_err_t usable(gates_tree_t *tree, gates_node_t node, gates_widget_state_t **st) {
    if (tree == nullptr || !gates_i_valid(tree, node)) return PROVEN_ERR_INVALID_ARG;
    *st = state_of(tree, node.index);
    if (*st == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (gates_i_widget_inert(tree, *st) || !shown(tree, node.index)) return PROVEN_ERR_INVALID_STATE;
    return GATES_OK;
}

/* Runs an input-path function and reports the error it left in input_error. */
static gates_err_t through_input(gates_tree_t *tree, void (*fn)(gates_tree_t *, gates_u32), gates_u32 idx) {
    gates_err_t before = tree->input_error;
    tree->input_error = GATES_OK;
    fn(tree, idx);
    gates_err_t err = tree->input_error;
    tree->input_error = before;
    return err;
}

gates_err_t gates_access_invoke(gates_tree_t *tree, gates_node_t node, gates_u64 item) {
    gates_widget_state_t *st = nullptr;
    gates_err_t err = usable(tree, node, &st);
    if (!gates_is_ok(err)) return err;
    gates_node_kind_t k = gates_i_slot(tree, node.index)->kind;
    if (k == GATES_NODE_MENU && item != 0) {
        return gates_i_menu_invoke(tree, node.index, (gates_command_id_t)item);
    }
    if (k == GATES_NODE_VIEW && item != 0) {
        return gates_i_view_activate_id(tree, node.index, item); /* as Enter */
    }
    if (k == GATES_NODE_TOOLBAR && item != 0) {
        const gates_i_toolbar *tb = st->tbar;
        if (tb == nullptr || item > (gates_u64)tb->count + 1 || (item <= tb->count && tb->ids[item - 1] == 0)) {
            return PROVEN_ERR_INVALID_ARG;
        }
        return gates_i_toolbar_activate(tree, node.index, (gates_u32)item - 1);
    }
    if (k == GATES_NODE_MENUBAR && item != 0) {
        return gates_access_expand(tree, node, item,
                                   !(tree->mb_mode == GATES_I_MB_OPEN && tree->mb_sel + 1 == item));
    }
    if (k != GATES_NODE_BUTTON || item != 0) return PROVEN_ERR_INVALID_ARG;
    return through_input(tree, gates_i_activate, node.index);
}

gates_err_t gates_access_toggle(gates_tree_t *tree, gates_node_t node) {
    gates_widget_state_t *st = nullptr;
    gates_err_t err = usable(tree, node, &st);
    if (!gates_is_ok(err)) return err;
    if (gates_i_slot(tree, node.index)->kind != GATES_NODE_CHECKBOX) return PROVEN_ERR_INVALID_ARG;
    return through_input(tree, gates_i_activate, node.index);
}

gates_err_t gates_access_select(gates_tree_t *tree, gates_node_t node, gates_u64 item) {
    gates_widget_state_t *st = nullptr;
    gates_err_t err = usable(tree, node, &st);
    if (!gates_is_ok(err)) return err;
    gates_node_kind_t k = gates_i_slot(tree, node.index)->kind;
    if (k == GATES_NODE_VIEW && item != 0) return gates_i_view_pick_id(tree, node.index, item);
    if (k == GATES_NODE_TABSTRIP && item != 0) {
        gates_u32 tabs = gates_i_slot(tree, node.index)->parent;
        if (item > gates_i_tabs_count(tree, tabs)) return PROVEN_ERR_INVALID_ARG;
        return gates_i_tabs_pick(tree, tabs, (gates_u32)item - 1);
    }
    if (k != GATES_NODE_RADIO && k != GATES_NODE_CHOICE) return PROVEN_ERR_INVALID_ARG;
    const gates_i_option_t *o = gates_i_option_find(st, (gates_u32)item);
    if (o == nullptr || item > UINT32_MAX) return PROVEN_ERR_INVALID_ARG;
    if (o->disabled) return PROVEN_ERR_INVALID_STATE;
    err = gates_i_option_pick(tree, node.index, (gates_u32)item);
    if (gates_is_ok(err) && k == GATES_NODE_CHOICE) {
        gates_i_choice_lists_check(tree, node.index); /* chosen: the list closes */
    }
    return err;
}

gates_err_t gates_access_expand(gates_tree_t *tree, gates_node_t node, gates_u64 item, bool expand) {
    gates_widget_state_t *st = nullptr;
    gates_err_t err = usable(tree, node, &st);
    if (!gates_is_ok(err)) return err;
    if (gates_i_slot(tree, node.index)->kind == GATES_NODE_VIEW && item != 0) {
        return gates_i_view_expand_id(tree, node.index, item, expand); /* a request */
    }
    if (gates_i_slot(tree, node.index)->kind == GATES_NODE_GROUP && item == 0) {
        if (!gates_i_group_foldable(tree, node.index)) return PROVEN_ERR_INVALID_ARG;
        return gates_i_group_toggle(tree, node.index, expand);
    }
    if (gates_i_slot(tree, node.index)->kind == GATES_NODE_MENUBAR && item != 0) {
        if (st->mbar == nullptr || item > st->mbar->count) return PROVEN_ERR_INVALID_ARG;
        if (gates_i_menubar_live(tree) != node.index) return PROVEN_ERR_INVALID_STATE;
        if (expand) return gates_i_menubar_open(tree, (gates_u32)item - 1, true);
        if (tree->mb_mode == GATES_I_MB_OPEN && tree->mb_sel + 1 == item) gates_i_menubar_leave(tree);
        return GATES_OK;
    }
    if (gates_i_slot(tree, node.index)->kind != GATES_NODE_CHOICE || item != 0) return PROVEN_ERR_INVALID_ARG;
    if (expand) {
        return through_input(tree, gates_i_choice_open, node.index);
    }
    gates_i_choice_lists_check(tree, node.index);
    return GATES_OK;
}

gates_err_t gates_access_set_range_value(gates_tree_t *tree, gates_node_t node, gates_i64 value) {
    gates_widget_state_t *st = nullptr;
    gates_err_t err = usable(tree, node, &st);
    if (!gates_is_ok(err)) return err;
    gates_node_kind_t k = gates_i_slot(tree, node.index)->kind;
    if (k != GATES_NODE_SPIN && k != GATES_NODE_SLIDER) return PROVEN_ERR_INVALID_ARG;
    return gates_i_range_user_set(tree, node.index, value);
}

gates_err_t gates_access_set_value(gates_tree_t *tree, gates_node_t node, gates_str_t text) {
    gates_widget_state_t *st = nullptr;
    gates_err_t err = usable(tree, node, &st);
    if (!gates_is_ok(err)) return err;
    if (gates_i_slot(tree, node.index)->kind == GATES_NODE_EDITOR) {
        return gates_i_editor_user_set(tree, node.index, text); /* plan-0022 */
    }
    if (gates_i_slot(tree, node.index)->kind != GATES_NODE_TEXTBOX || st->edit == nullptr ||
        (text.size > 0 && text.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (st->read_only) return PROVEN_ERR_PERMISSION;
    if (gates_text_edit_preedit(st->edit).size > 0) return PROVEN_ERR_BUSY;
    /* A user edit: limits, undo and TEXT_CHANGED as for typing. */
    gates_u32 len = (gates_u32)gates_text_edit_text(st->edit).size;
    return gates_i_box_edit(tree, node.index, 0, len, text, GATES_I_UNIT_OTHER, true);
}

gates_err_t gates_access_focus(gates_tree_t *tree, gates_node_t node, gates_u64 item) {
    if (tree == nullptr || !gates_i_valid(tree, node)) return PROVEN_ERR_INVALID_ARG;
    if (!gates_i_focus_eligible(tree, node.index)) return PROVEN_ERR_INVALID_STATE;
    if (item != 0) {
        gates_err_t err = gates_access_select(tree, node, item);
        if (!gates_is_ok(err)) return err;
    }
    gates_tree_set_focus(tree, node);
    gates_i_scroll_into_view(tree, node.index); /* as Tab does */
    return GATES_OK;
}

/* -- the enforced rules --------------------------------------------------------------------- */

/* sRGB channel -> linear light, scaled to 0..65535 (WCAG 2 formula, tabulated). */
static const gates_u32 lin[256] = {
    0, 20, 40, 60, 80, 99, 119, 139, 159, 179, 199, 219,
    241, 264, 288, 313, 340, 367, 396, 427, 458, 491, 526, 562,
    599, 637, 677, 718, 761, 805, 851, 898, 947, 997, 1048, 1101,
    1156, 1212, 1270, 1330, 1391, 1453, 1517, 1583, 1651, 1720, 1790, 1863,
    1937, 2013, 2090, 2170, 2250, 2333, 2418, 2504, 2592, 2681, 2773, 2866,
    2961, 3058, 3157, 3258, 3360, 3464, 3570, 3678, 3788, 3900, 4014, 4129,
    4247, 4366, 4488, 4611, 4736, 4864, 4993, 5124, 5257, 5392, 5530, 5669,
    5810, 5953, 6099, 6246, 6395, 6547, 6700, 6856, 7014, 7174, 7335, 7500,
    7666, 7834, 8004, 8177, 8352, 8528, 8708, 8889, 9072, 9258, 9445, 9635,
    9828, 10022, 10219, 10417, 10619, 10822, 11028, 11235, 11446, 11658, 11873, 12090,
    12309, 12530, 12754, 12980, 13209, 13440, 13673, 13909, 14146, 14387, 14629, 14874,
    15122, 15371, 15623, 15878, 16135, 16394, 16656, 16920, 17187, 17456, 17727, 18001,
    18277, 18556, 18837, 19121, 19407, 19696, 19987, 20281, 20577, 20876, 21177, 21481,
    21787, 22096, 22407, 22721, 23038, 23357, 23678, 24002, 24329, 24658, 24990, 25325,
    25662, 26001, 26344, 26688, 27036, 27386, 27739, 28094, 28452, 28813, 29176, 29542,
    29911, 30282, 30656, 31033, 31412, 31794, 32179, 32567, 32957, 33350, 33745, 34143,
    34544, 34948, 35355, 35764, 36176, 36591, 37008, 37429, 37852, 38278, 38706, 39138,
    39572, 40009, 40449, 40891, 41337, 41785, 42236, 42690, 43147, 43606, 44069, 44534,
    45002, 45473, 45947, 46423, 46903, 47385, 47871, 48359, 48850, 49344, 49841, 50341,
    50844, 51349, 51858, 52369, 52884, 53401, 53921, 54445, 54971, 55500, 56032, 56567,
    57105, 57646, 58190, 58737, 59287, 59840, 60396, 60955, 61517, 62082, 62650, 63221,
    63795, 64372, 64952, 65535,
};

static gates_u32 luminance(gates_color_t c) {
    return (2126u * lin[c.r] + 7152u * lin[c.g] + 722u * lin[c.b]) / 10000u;
}

/* contrast(a, b) >= ratio_x10 / 10, i.e. (Lmax + 0.05) * 10 >= r * (Lmin + 0.05). */
static bool enough(gates_color_t a, gates_color_t b, gates_u32 ratio_x10) {
    gates_u64 la = luminance(a), lb = luminance(b);
    gates_u64 hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    gates_u64 off = 3277; /* 0.05 of 65535 */
    return (hi + off) * 10 >= ratio_x10 * (lo + off);
}

typedef struct issues_t {
    gates_access_issue_t *out;
    gates_u32 cap;
    gates_u32 n;
} issues_t;

static void issue(issues_t *is, gates_access_rule_t rule, gates_node_t node, gates_u64 item) {
    if (is->n < is->cap) is->out[is->n] = (gates_access_issue_t){ .rule = rule, .node = node, .item = item };
    is->n++;
}

static void theme_rules(const gates_theme_t *t, issues_t *is) {
    static const struct { gates_color_token_t fg, bg; gates_u32 r; } pairs[] = {
        { GATES_COLOR_WINDOW_FG, GATES_COLOR_WINDOW_BG, 45 },
        { GATES_COLOR_PANEL_FG, GATES_COLOR_PANEL_BG, 45 },
        { GATES_COLOR_CONTROL_FG, GATES_COLOR_CONTROL_BG, 45 },
        { GATES_COLOR_CONTROL_FG, GATES_COLOR_CONTROL_HOVER_BG, 45 },
        { GATES_COLOR_CONTROL_FG, GATES_COLOR_CONTROL_PRESSED_BG, 45 },
        { GATES_COLOR_SELECTION_FG, GATES_COLOR_SELECTION_BG, 45 },
        { GATES_COLOR_PANEL_FG, GATES_COLOR_WINDOW_BG, 45 },
        { GATES_COLOR_ERROR, GATES_COLOR_WINDOW_BG, 45 },
        { GATES_COLOR_CONTROL_DISABLED_FG, GATES_COLOR_CONTROL_BG, 30 },
        { GATES_COLOR_CONTROL_DISABLED_FG, GATES_COLOR_PANEL_BG, 30 },
        { GATES_COLOR_ERROR, GATES_COLOR_CONTROL_BG, 30 },
        { GATES_COLOR_FOCUS_RING, GATES_COLOR_WINDOW_BG, 30 },
        { GATES_COLOR_FOCUS_RING, GATES_COLOR_CONTROL_BG, 30 },
        { GATES_COLOR_CONTROL_BORDER, GATES_COLOR_CONTROL_BG, 30 },
        { GATES_COLOR_CONTROL_BORDER, GATES_COLOR_WINDOW_BG, 30 },
    };
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        if (!enough(t->colors[pairs[i].fg], t->colors[pairs[i].bg], pairs[i].r)) {
            issue(is, GATES_RULE_CONTRAST, GATES_NODE_NULL, (gates_u64)i);
        }
    }
    if (gates_theme_focus_width(t) < 2 || gates_theme_error_width(t) < 2) {
        issue(is, GATES_RULE_FOCUS_CUE, GATES_NODE_NULL, 0);
    }
}

gates_u32 gates_theme_audit(const gates_theme_t *theme, gates_access_issue_t *out, gates_u32 cap) {
    issues_t is = { .out = out, .cap = out != nullptr ? cap : 0 };
    if (theme != nullptr) theme_rules(theme, &is);
    return is.n;
}

/* Per-node rules; interactive nodes are also collected for the id pass. */
typedef struct collect_t {
    gates_u32 *idx;
    gates_u32 n, cap;
    bool failed;
} collect_t;

static void audit_node(gates_tree_t *tree, gates_u32 idx, issues_t *is, collect_t *col) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->hidden) return;
    gates_node_t node = gates_i_handle(tree, idx);
    const gates_widget_state_t *st = state_of(tree, idx);
    if (is_interactive(s->kind) && st != nullptr && shown(tree, idx)) {
        gates_access_info_t info;
        if (gates_is_ok(gates_access_info(tree, node, 0, &info)) && info.name.size == 0) {
            issue(is, GATES_RULE_NO_NAME, node, 0);
        }
        /* Pointer targets: the node, or each row of a radio group. */
        gates_rect_t r = s->layout_rect;
        gates_i32 h = r.h;
        if (s->kind == GATES_NODE_RADIO) h = gates_i_radio_row_h(tree->line_height > 0 ? tree->line_height : 16);
        if (!gates_rect_is_empty(r) && (r.w < GATES_ACCESS_MIN_TARGET || h < GATES_ACCESS_MIN_TARGET)) {
            issue(is, GATES_RULE_TARGET_SIZE, node, 0);
        }
        /* Keyboard: enabled and shown, but taken out of the Tab order. */
        if (st->not_focusable && !gates_i_widget_inert(tree, st)) issue(is, GATES_RULE_KEYBOARD, node, 0);
        if (!col->failed) {
            if (col->n == col->cap) {
                gates_u32 cap = col->cap != 0 ? col->cap * 2 : 32;
                gates_allocator_t a = tree->alloc;
                proven_result_mem_mut_t m = col->idx == nullptr
                    ? a.alloc_fn(a.ctx, cap * sizeof(gates_u32), alignof(gates_u32))
                    : a.realloc_fn(a.ctx, col->idx, col->cap * sizeof(gates_u32), cap * sizeof(gates_u32),
                                   alignof(gates_u32));
                if (!proven_is_ok(m.err)) {
                    col->failed = true;
                } else {
                    col->idx = (gates_u32 *)m.value.ptr;
                    col->cap = cap;
                }
            }
            if (!col->failed) col->idx[col->n++] = idx;
        }
    }
    for (gates_u32 c = s->first_child; c != GATES_NONE; c = gates_i_slot(tree, c)->next_sibling) {
        audit_node(tree, c, is, col);
    }
}

gates_u32 gates_access_audit(gates_tree_t *tree, const gates_theme_t *theme,
                             gates_access_issue_t *out, gates_u32 cap) {
    issues_t is = { .out = out, .cap = out != nullptr ? cap : 0 };
    if (tree == nullptr) return 0;
    if (theme != nullptr) theme_rules(theme, &is);
    collect_t col = {0};
    audit_node(tree, tree->root, &is, &col);
    for (gates_u32 i = 0; i < tree->overlay_count; i++) audit_node(tree, tree->overlays[i].index, &is, &col);
    /* Duplicate automation ids (explicit or default) among the interactive nodes.
     * Info strings are rewritten by every call, so each id is copied first. */
    for (gates_u32 i = 0; i < col.n; i++) {
        gates_access_info_t a;
        if (!gates_is_ok(gates_access_info(tree, gates_i_handle(tree, col.idx[i]), 0, &a)) ||
            a.automation_id.size == 0 || a.automation_id.size > 128) continue;
        gates_u8 mine[128];
        gates_usize_t mine_len = a.automation_id.size;
        memcpy(mine, a.automation_id.ptr, mine_len);
        for (gates_u32 j = i + 1; j < col.n; j++) {
            gates_access_info_t b;
            if (gates_is_ok(gates_access_info(tree, gates_i_handle(tree, col.idx[j]), 0, &b)) &&
                b.automation_id.size == mine_len && memcmp(b.automation_id.ptr, mine, mine_len) == 0) {
                issue(&is, GATES_RULE_DUPLICATE_ID, gates_i_handle(tree, col.idx[j]), 0);
            }
        }
    }
    if (col.idx != nullptr) tree->alloc.free_fn(tree->alloc.ctx, col.idx);
    return is.n;
}

/* -- scrolling ---------------------------------------------------------------------------------- */

/* A scroll area: its offset over the part of its content that does not fit. */
static bool area_scroll(gates_tree_t *tree, gates_node_t node, gates_i32 *max) {
    const gates_node_slot_t *s = gates_i_slot(tree, node.index);
    if (s->layout_kind != GATES_LAYOUT_SCROLL || !gates_i_scrollable(tree, node.index)) return false;
    gates_i32 m = gates_layout_scroll_content(tree, node).h - s->layout_rect.h;
    *max = m > 0 ? m : 0;
    return *max > 0;
}

bool gates_access_scroll_info(gates_tree_t *tree, gates_node_t node, gates_u32 *pos, gates_u32 *page) {
    gates_u32 p = 0, g = 0;
    bool ok = false;
    if (tree != nullptr && gates_i_valid(tree, node)) {
        gates_i32 max = 0;
        if (gates_i_slot(tree, node.index)->kind == GATES_NODE_VIEW) {
            ok = gates_i_view_scroll_info(tree, node.index, &p, &g);
        } else if (area_scroll(tree, node, &max)) {
            gates_i32 off = gates_layout_scroll_offset(tree, node);
            gates_i32 content = gates_layout_scroll_content(tree, node).h;
            p = (gates_u32)(((gates_u64)(off > 0 ? off : 0) * GATES_ACCESS_SCROLL_MAX) / (gates_u64)max);
            g = (gates_u32)(((gates_u64)gates_i_slot(tree, node.index)->layout_rect.h * GATES_ACCESS_SCROLL_MAX) /
                            (gates_u64)content);
            ok = true;
        }
    }
    if (pos != nullptr) *pos = p;
    if (page != nullptr) *page = g;
    return ok;
}

gates_err_t gates_access_scroll_to(gates_tree_t *tree, gates_node_t node, gates_u32 pos) {
    gates_widget_state_t *st = nullptr;
    if (tree == nullptr || !gates_i_valid(tree, node)) return PROVEN_ERR_INVALID_ARG;
    if (pos > GATES_ACCESS_SCROLL_MAX) pos = GATES_ACCESS_SCROLL_MAX;
    if (gates_i_slot(tree, node.index)->kind == GATES_NODE_VIEW) {
        gates_err_t err = usable(tree, node, &st);
        return gates_is_ok(err) ? gates_i_view_scroll_set(tree, node.index, pos) : err;
    }
    gates_i32 max = 0;
    if (!area_scroll(tree, node, &max)) return PROVEN_ERR_INVALID_ARG;
    return gates_layout_set_scroll_offset(tree, node, (gates_i32)(((gates_u64)max * pos) / GATES_ACCESS_SCROLL_MAX));
}

gates_err_t gates_access_scroll_by(gates_tree_t *tree, gates_node_t node, gates_i32 amount, bool page) {
    gates_widget_state_t *st = nullptr;
    if (tree == nullptr || !gates_i_valid(tree, node)) return PROVEN_ERR_INVALID_ARG;
    if (gates_i_slot(tree, node.index)->kind == GATES_NODE_VIEW) {
        gates_err_t err = usable(tree, node, &st);
        return gates_is_ok(err) ? gates_i_view_scroll_step(tree, node.index, amount, page) : err;
    }
    gates_i32 max = 0;
    if (!area_scroll(tree, node, &max)) return PROVEN_ERR_INVALID_ARG;
    gates_i32 line = tree->line_height > 0 ? tree->line_height : 16;
    gates_i32 step = page ? gates_i_slot(tree, node.index)->layout_rect.h : line;
    gates_i64 off = (gates_i64)gates_layout_scroll_offset(tree, node) + (gates_i64)amount * step;
    if (off < 0) off = 0;
    if (off > max) off = max;
    return gates_layout_set_scroll_offset(tree, node, (gates_i32)off);
}

/* -- text ----------------------------------------------------------------------------------- */

static const gates_widget_state_t *text_box(const gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr || !gates_i_valid(tree, node) || gates_i_slot(tree, node.index)->kind != GATES_NODE_TEXTBOX) {
        return nullptr;
    }
    const gates_widget_state_t *st = state_of(tree, node.index);
    return st != nullptr && st->edit != nullptr && !st->password ? st : nullptr;
}

static bool is_editor(const gates_tree_t *tree, gates_node_t node) {
    return tree != nullptr && gates_i_valid(tree, node) && gates_i_slot(tree, node.index)->kind == GATES_NODE_EDITOR;
}

gates_u32 gates_access_text_rects(gates_tree_t *tree, gates_node_t node, gates_u32 start, gates_u32 end, gates_rect_t *out,
                                  gates_u32 cap) {
    if (out == nullptr || cap == 0) return 0;
    if (is_editor(tree, node)) {
        if (!shown(tree, node.index)) return 0;
        gates_u32 n = gates_i_editor_text_rects(tree, node.index, start, end, out, cap);
        gates_u32 kept = 0;
        for (gates_u32 i = 0; i < n; i++) {
            if (!clipped(tree, node.index, out[i])) out[kept++] = out[i];
        }
        return kept;
    }
    return gates_access_text_rect(tree, node, start, end, out) ? 1u : 0u;
}

bool gates_access_text_rect(gates_tree_t *tree, gates_node_t node, gates_u32 start, gates_u32 end,
                            gates_rect_t *out) {
    if (is_editor(tree, node)) return out != nullptr && gates_access_text_rects(tree, node, start, end, out, 1) == 1;
    const gates_widget_state_t *st = text_box(tree, node);
    if (st == nullptr || out == nullptr || !shown(tree, node.index)) return false;
    gates_u32 len = (gates_u32)gates_text_edit_text(st->edit).size;
    if (start > len) start = len;
    if (end > len) end = len;
    if (end < start) end = start;
    gates_rect_t inner = gates_i_textbox_inner(tree, node.index);
    gates_i32 lh = tree->line_height > 0 ? tree->line_height : 16;
    gates_i32 font = gates_i_font(tree, node.index);
    gates_i32 x0 = inner.x + gates_i_box_x(tree->text_backend, font, st, start) - st->view_x;
    gates_i32 x1 = inner.x + gates_i_box_x(tree->text_backend, font, st, end) - st->view_x;
    gates_i32 y = inner.y + (inner.h - lh) / 2;
    if (x0 < inner.x) x0 = inner.x;
    if (x1 > inner.x + inner.w) x1 = inner.x + inner.w;
    if (x0 > inner.x + inner.w || x1 < inner.x || (x1 <= x0 && end > start)) return false;
    *out = (gates_rect_t){ x0, y, x1 > x0 ? x1 - x0 : 0, lh };
    return !clipped(tree, node.index, *out);
}

gates_u32 gates_access_text_offset_at(gates_tree_t *tree, gates_node_t node, gates_point_t p) {
    if (is_editor(tree, node)) return gates_i_editor_offset_at_point(tree, node.index, p);
    const gates_widget_state_t *st = text_box(tree, node);
    if (st == nullptr) return 0;
    gates_rect_t inner = gates_i_textbox_inner(tree, node.index);
    gates_i32 rel = p.x - inner.x;
    if (rel < 0) rel = 0;
    return gates_i_box_offset_at_x(tree->text_backend, gates_i_font(tree, node.index), st, rel + st->view_x);
}

gates_err_t gates_access_select_text(gates_tree_t *tree, gates_node_t node, gates_u32 anchor, gates_u32 caret) {
    if (is_editor(tree, node)) {
        gates_widget_state_t *es = nullptr;
        gates_err_t err = usable(tree, node, &es);
        if (!gates_is_ok(err)) return err;
        gates_u32 len = gates_editor_length(tree, node);
        if (anchor > len || caret > len) return PROVEN_ERR_OUT_OF_BOUNDS;
        return gates_editor_set_selection(tree, node, anchor, caret); /* plan-0022 */
    }
    gates_widget_state_t *st = nullptr;
    gates_err_t err = usable(tree, node, &st);
    if (!gates_is_ok(err)) return err;
    if (text_box(tree, node) == nullptr) return PROVEN_ERR_INVALID_ARG;
    return gates_textbox_set_selection(tree, node, anchor, caret);
}
