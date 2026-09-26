/* gates_gui_lib - textbox editing contract (plan-0008, RFC-0003 4.2 and 6.1):
 * one edit path, bounded undo/redo, maximum length with a pending offer,
 * read-only and password policies, and the safe public textbox API. */
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"

#include <string.h>

#define UNDO_DEFAULT_ENTRIES 64u
#define UNDO_DEFAULT_BYTES   16384u

typedef struct gates_i_undo_entry_t {
    gates_u8 *buf;           /* removed bytes, then inserted bytes */
    gates_u32 cap;
    gates_u32 pos;           /* byte offset of the change */
    gates_u32 rem_len;
    gates_u32 ins_len;
    gates_u32 anchor_before;
    gates_u32 caret_before;
    gates_u8 kind;           /* gates_i_unit_t */
} gates_i_undo_entry_t;

struct gates_i_undo {
    gates_i_undo_entry_t *entries;
    gates_u32 count;         /* stored entries */
    gates_u32 cap;
    gates_u32 pos;           /* entries[0..pos) can be undone, [pos..count) redone */
    gates_u32 bytes;         /* stored text bytes */
    gates_u32 max_entries;
    gates_u32 max_bytes;
    bool open;               /* the top entry may still grow (typing/deletion run) */
};

typedef struct gates_i_undo gates_i_undo_t;

/* -- helpers ------------------------------------------------------------------ */

static gates_widget_state_t *box_state(const gates_tree_t *tree, gates_node_t node) {
    if (!gates_i_valid(tree, node) || gates_i_slot(tree, node.index)->kind != GATES_NODE_TEXTBOX) {
        return nullptr;
    }
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, node.index)->state_index);
    return (st != nullptr && st->edit != nullptr) ? st : nullptr;
}

static bool on_boundary(gates_str_t text, gates_u32 off) {
    if (off > text.size) {
        return false;
    }
    return off == text.size || (text.ptr[off] & 0xC0) != 0x80;
}

/* Longest prefix of `s` not longer than `avail` bytes, ending on a codepoint. */
static gates_u32 fit_prefix(gates_str_t s, gates_u32 avail) {
    gates_u32 n = 0;
    while (n < s.size) {
        gates_u32 step = gates_text_decode(s, n, nullptr);
        if (n + step > avail) {
            break;
        }
        n += step;
    }
    return n;
}

static void entry_free(gates_tree_t *tree, gates_i_undo_entry_t *e) {
    if (e->buf != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, e->buf);
    }
    e->buf = nullptr;
}

static void undo_drop_oldest(gates_tree_t *tree, gates_i_undo_t *u) {
    if (u->count == 0) {
        return;
    }
    u->bytes -= u->entries[0].rem_len + u->entries[0].ins_len;
    entry_free(tree, &u->entries[0]);
    memmove(u->entries, u->entries + 1, (gates_usize_t)(u->count - 1) * sizeof *u->entries);
    u->count--;
    if (u->pos > 0) {
        u->pos--;
    }
    u->open = false;
}

static void undo_drop_redo(gates_tree_t *tree, gates_i_undo_t *u) {
    while (u->count > u->pos) {
        gates_i_undo_entry_t *e = &u->entries[--u->count];
        u->bytes -= e->rem_len + e->ins_len;
        entry_free(tree, e);
    }
}

static void undo_clear(gates_tree_t *tree, gates_i_undo_t *u) {
    if (u == nullptr) {
        return;
    }
    for (gates_u32 i = 0; i < u->count; i++) {
        entry_free(tree, &u->entries[i]);
    }
    u->count = 0;
    u->pos = 0;
    u->bytes = 0;
    u->open = false;
}

static void undo_trim(gates_tree_t *tree, gates_i_undo_t *u) {
    while (u->count > 0 && (u->count > u->max_entries || u->bytes > u->max_bytes)) {
        undo_drop_oldest(tree, u);
    }
}

static gates_i_undo_t *undo_get(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->undo == nullptr) {
        proven_result_mem_mut_t r =
            tree->alloc.alloc_fn(tree->alloc.ctx, sizeof(gates_i_undo_t), alignof(gates_i_undo_t));
        if (!proven_is_ok(r.err)) {
            return nullptr;
        }
        st->undo = (gates_i_undo_t *)r.value.ptr;
        memset(st->undo, 0, sizeof *st->undo);
        st->undo->max_entries = UNDO_DEFAULT_ENTRIES;
        st->undo->max_bytes = UNDO_DEFAULT_BYTES;
    }
    return st->undo;
}

/* Grows an entry's buffer to `need` bytes (contents kept). */
static gates_err_t entry_reserve(gates_tree_t *tree, gates_i_undo_entry_t *e, gates_u32 need) {
    if (need <= e->cap) {
        return GATES_OK;
    }
    gates_u32 cap = e->cap < 16 ? 16 : e->cap;
    while (cap < need) {
        cap *= 2u;
    }
    proven_result_mem_mut_t r =
        e->buf == nullptr ? tree->alloc.alloc_fn(tree->alloc.ctx, cap, 1)
                          : tree->alloc.realloc_fn(tree->alloc.ctx, e->buf, e->cap, cap, 1);
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    e->buf = (gates_u8 *)r.value.ptr;
    e->cap = cap;
    return GATES_OK;
}

/* -- undo recording ----------------------------------------------------------- */

typedef struct undo_plan_t {
    gates_i_undo_t *u;       /* null: this edit is not recorded */
    bool merge;              /* grow the top entry instead of adding one */
    gates_i_undo_entry_t fresh; /* the new entry when !merge (buffer reserved) */
} undo_plan_t;

static bool can_merge(const gates_i_undo_t *u, gates_i_unit_t unit, gates_u32 b, gates_u32 e,
                      gates_str_t text) {
    if (!u->open || u->pos == 0 || u->pos != u->count || unit == GATES_I_UNIT_OTHER) {
        return false;
    }
    const gates_i_undo_entry_t *top = &u->entries[u->pos - 1];
    if (top->kind != unit) {
        return false;
    }
    switch (unit) {
    case GATES_I_UNIT_TYPE:
        return b == e && text.size > 0 && b == top->pos + top->ins_len;
    /* Deletion runs merge one codepoint (at most 4 bytes) at a time. */
    case GATES_I_UNIT_DEL_BACK:
        return text.size == 0 && e - b <= 4 && e == top->pos && top->ins_len == 0;
    case GATES_I_UNIT_DEL_FWD:
        return text.size == 0 && e - b <= 4 && b == top->pos && top->ins_len == 0;
    default:
        return false;
    }
}

/* Reserves everything the record needs. Owner rule (DECISIONS 2026-09-25):
 * when memory is short the oldest history is dropped first; if even an empty
 * history cannot hold the entry, the edit goes ahead unrecorded. */
static undo_plan_t undo_prepare(gates_tree_t *tree, gates_widget_state_t *st, gates_i_unit_t unit,
                                gates_u32 b, gates_u32 e, gates_str_t text) {
    undo_plan_t plan = {0};
    if (st->password) {
        return plan;
    }
    gates_i_undo_t *u = undo_get(tree, st);
    if (u == nullptr) {
        return plan;
    }
    gates_u32 rem = e - b;
    if (rem + (gates_u32)text.size > u->max_bytes) {
        undo_clear(tree, u); /* larger than the whole budget: history cannot span it */
        return plan;
    }
    for (;;) {
        gates_err_t err = GATES_OK;
        if (can_merge(u, unit, b, e, text)) {
            gates_i_undo_entry_t *top = &u->entries[u->pos - 1];
            err = entry_reserve(tree, top, top->rem_len + top->ins_len + rem + (gates_u32)text.size);
            if (gates_is_ok(err)) {
                plan.u = u;
                plan.merge = true;
                return plan;
            }
        } else {
            if (u->count >= u->cap) {
                gates_u32 cap = u->cap == 0 ? 8u : u->cap * 2u;
                proven_result_mem_mut_t r =
                    u->entries == nullptr
                        ? tree->alloc.alloc_fn(tree->alloc.ctx, cap * sizeof *u->entries,
                                               alignof(gates_i_undo_entry_t))
                        : tree->alloc.realloc_fn(tree->alloc.ctx, u->entries,
                                                 u->cap * sizeof *u->entries,
                                                 cap * sizeof *u->entries,
                                                 alignof(gates_i_undo_entry_t));
                err = r.err;
                if (gates_is_ok(err)) {
                    u->entries = (gates_i_undo_entry_t *)r.value.ptr;
                    u->cap = cap;
                }
            }
            if (gates_is_ok(err)) {
                gates_i_undo_entry_t fresh = {0};
                err = entry_reserve(tree, &fresh, rem + (gates_u32)text.size);
                if (gates_is_ok(err)) {
                    plan.u = u;
                    plan.fresh = fresh;
                    return plan;
                }
            }
        }
        if (u->count == 0) {
            u->open = false;
            return plan; /* nothing left to give up: the edit goes unrecorded */
        }
        undo_drop_oldest(tree, u);
    }
}

static void undo_abandon(gates_tree_t *tree, undo_plan_t *plan) {
    if (plan->u != nullptr && !plan->merge) {
        entry_free(tree, &plan->fresh);
    }
}

/* Called after the edit was applied; `removed` was copied before it. */
static void undo_commit(gates_tree_t *tree, undo_plan_t *plan, gates_i_unit_t unit, gates_u32 b,
                        gates_str_t removed, gates_str_t text, gates_u32 anchor_before,
                        gates_u32 caret_before, bool user) {
    gates_i_undo_t *u = plan->u;
    if (u == nullptr) {
        return;
    }
    if (plan->merge) {
        gates_i_undo_entry_t *top = &u->entries[u->pos - 1];
        if (unit == GATES_I_UNIT_TYPE) {
            memcpy(top->buf + top->rem_len + top->ins_len, text.ptr, text.size);
            top->ins_len += (gates_u32)text.size;
        } else if (unit == GATES_I_UNIT_DEL_BACK) {
            memmove(top->buf + removed.size, top->buf, top->rem_len);
            memcpy(top->buf, removed.ptr, removed.size);
            top->rem_len += (gates_u32)removed.size;
            top->pos = b;
        } else {
            memcpy(top->buf + top->rem_len, removed.ptr, removed.size);
            top->rem_len += (gates_u32)removed.size;
        }
        u->bytes += (gates_u32)(removed.size + text.size);
    } else {
        undo_drop_redo(tree, u);
        gates_i_undo_entry_t *ne = &u->entries[u->count++];
        *ne = plan->fresh;
        if (removed.size > 0 && removed.ptr != ne->buf) {
            memcpy(ne->buf, removed.ptr, removed.size);
        }
        if (text.size > 0) {
            memcpy(ne->buf + removed.size, text.ptr, text.size);
        }
        ne->pos = b;
        ne->rem_len = (gates_u32)removed.size;
        ne->ins_len = (gates_u32)text.size;
        ne->anchor_before = anchor_before;
        ne->caret_before = caret_before;
        ne->kind = (gates_u8)unit;
        u->pos = u->count;
        u->bytes += ne->rem_len + ne->ins_len;
    }
    u->open = user && unit != GATES_I_UNIT_OTHER;
    undo_trim(tree, u);
}

/* -- the edit path ------------------------------------------------------------ */

static void offer_clear(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->offer != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, st->offer);
    }
    st->offer = nullptr;
    st->offer_len = 0;
    st->offer_fit = 0;
}

/* Keeps the refused input so the application can ask the person. */
static void offer_store(gates_tree_t *tree, gates_u32 idx, gates_widget_state_t *st, gates_u32 b,
                        gates_u32 e, gates_str_t text, gates_u32 fixed_len) {
    offer_clear(tree, st);
    if (text.size > 0) {
        proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, text.size, 1);
        if (!proven_is_ok(r.err)) {
            return; /* no offer; the refusal itself is still reported */
        }
        st->offer = (gates_u8 *)r.value.ptr;
        memcpy(st->offer, text.ptr, text.size);
    }
    st->offer_len = (gates_u32)text.size;
    st->offer_begin = b;
    st->offer_end = e;
    st->offer_revision = st->revision;
    gates_u32 avail = st->max_bytes > fixed_len ? st->max_bytes - fixed_len : 0;
    st->offer_fit = fit_prefix(text, avail);
    gates_i_event_try_push(tree, idx, GATES_EVENT_LIMIT_EXCEEDED, (gates_u32)text.size);
}

gates_err_t gates_i_box_edit(gates_tree_t *tree, gates_u32 idx, gates_u32 b, gates_u32 e,
                             gates_str_t text, gates_i_unit_t unit, bool user) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    gates_text_edit_t *ed = st->edit;
    gates_str_t cur = gates_text_edit_text(ed);
    gates_u32 fixed = (gates_u32)cur.size - (e - b);
    gates_u32 new_len = fixed + (gates_u32)text.size;

    if (st->max_bytes != 0 && new_len > st->max_bytes) {
        if (user) {
            offer_store(tree, idx, st, b, e, text, fixed);
        }
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    /* Reserve first: nothing below may fail once the text changes. */
    gates_err_t err = GATES_OK;
    if (st->on_event != nullptr) {
        err = gates_i_event_reserve(tree, user ? 1u : 0u, new_len);
    }
    if (gates_is_ok(err)) {
        err = gates_text_edit_reserve(ed, new_len);
    }
    if (!gates_is_ok(err)) {
        return err;
    }
    undo_plan_t plan = undo_prepare(tree, st, unit, b, e, text);

    /* The record needs the removed bytes; copy them before the edit (the text
     * may have moved during the reservation, so read it again). A merge is at
     * most one codepoint; a new entry has room reserved for them. */
    cur = gates_text_edit_text(ed);
    gates_u32 anchor_before = ed->anchor;
    gates_u32 caret_before = ed->caret;
    gates_str_t removed = { .ptr = cur.ptr + b, .size = e - b };
    gates_u8 small[4];
    if (plan.u != nullptr && removed.size > 0) {
        gates_u8 *dst = plan.merge ? small : plan.fresh.buf;
        memcpy(dst, removed.ptr, removed.size);
        removed.ptr = dst;
    }
    gates_text_edit_set_caret(ed, b, false);
    gates_text_edit_set_caret(ed, e, true);
    err = gates_text_edit_insert(ed, text); /* capacity reserved: cannot fail */
    if (!gates_is_ok(err)) {
        undo_abandon(tree, &plan);
        return err;
    }
    undo_commit(tree, &plan, unit, b, removed, text, anchor_before, caret_before, user);
    st->revision++;
    offer_clear(tree, st);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    if (user) {
        gates_i_event_push(tree, idx, GATES_EVENT_TEXT_CHANGED, GATES_ORIGIN_USER);
    }
    return GATES_OK;
}

void gates_i_box_seal(gates_widget_state_t *st) {
    if (st != nullptr && st->undo != nullptr) {
        st->undo->open = false;
    }
}

void gates_i_box_forget(gates_tree_t *tree, gates_widget_state_t *st) {
    undo_clear(tree, st->undo);
    offer_clear(tree, st);
}

void gates_i_box_free(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->undo != nullptr) {
        undo_clear(tree, st->undo);
        if (st->undo->entries != nullptr) {
            tree->alloc.free_fn(tree->alloc.ctx, st->undo->entries);
        }
        tree->alloc.free_fn(tree->alloc.ctx, st->undo);
        st->undo = nullptr;
    }
    offer_clear(tree, st);
}

/* Applies one history step: replaces [pos, pos+old_len) with `with`. */
static gates_err_t history_apply(gates_tree_t *tree, gates_u32 idx, gates_widget_state_t *st,
                                 gates_u32 pos, gates_u32 old_len, gates_str_t with) {
    gates_text_edit_t *ed = st->edit;
    gates_u32 new_len = (gates_u32)gates_text_edit_text(ed).size - old_len + (gates_u32)with.size;
    if (st->max_bytes != 0 && new_len > st->max_bytes) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    gates_err_t err = st->on_event != nullptr ? gates_i_event_reserve(tree, 1, new_len) : GATES_OK;
    if (gates_is_ok(err)) {
        err = gates_text_edit_reserve(ed, new_len);
    }
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_text_edit_set_caret(ed, pos, false);
    gates_text_edit_set_caret(ed, pos + old_len, true);
    err = gates_text_edit_insert(ed, with);
    if (!gates_is_ok(err)) {
        return err;
    }
    st->revision++;
    offer_clear(tree, st);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    gates_i_event_push(tree, idx, GATES_EVENT_TEXT_CHANGED, GATES_ORIGIN_USER);
    return GATES_OK;
}

gates_err_t gates_i_box_undo(gates_tree_t *tree, gates_u32 idx) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    gates_i_undo_t *u = st->undo;
    if (st->read_only) {
        return PROVEN_ERR_PERMISSION; /* design 6: no undo in a read-only box */
    }
    if (u == nullptr || u->pos == 0) {
        return GATES_OK;
    }
    gates_i_undo_entry_t *en = &u->entries[u->pos - 1];
    gates_err_t err = history_apply(tree, idx, st, en->pos, en->ins_len,
                                    (gates_str_t){ .ptr = en->buf, .size = en->rem_len });
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_text_edit_set_caret(st->edit, en->anchor_before, false);
    gates_text_edit_set_caret(st->edit, en->caret_before, true);
    u->pos--;
    u->open = false;
    return GATES_OK;
}

gates_err_t gates_i_box_redo(gates_tree_t *tree, gates_u32 idx) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    gates_i_undo_t *u = st->undo;
    if (st->read_only) {
        return PROVEN_ERR_PERMISSION;
    }
    if (u == nullptr || u->pos >= u->count) {
        return GATES_OK;
    }
    gates_i_undo_entry_t *en = &u->entries[u->pos];
    gates_err_t err = history_apply(tree, idx, st, en->pos, en->rem_len,
                                    (gates_str_t){ .ptr = en->buf + en->rem_len,
                                                   .size = en->ins_len });
    if (!gates_is_ok(err)) {
        return err;
    }
    u->pos++;
    u->open = false;
    return GATES_OK;
}

/* -- geometry (RFC-0004) ------------------------------------------------------ */

static gates_i32 star_width(const gates_text_backend_t *be, gates_i32 font) {
    return be != nullptr && be->glyph_advance != nullptr ? be->glyph_advance(be->ctx, font, '*') : 0;
}

gates_i32 gates_i_box_x(const gates_text_backend_t *be, gates_i32 font, const gates_widget_state_t *st,
                        gates_u32 offset) {
    gates_str_t t = gates_text_edit_text(st->edit);
    if (offset > t.size) offset = (gates_u32)t.size;
    if (!st->password) return gates_text_width(be, font, (gates_str_t){ .ptr = t.ptr, .size = offset });
    gates_i32 n = 0; /* one star per code point */
    for (gates_u32 at = 0; at < offset; at += gates_text_decode(t, at, nullptr)) n++;
    return n * star_width(be, font);
}

gates_u32 gates_i_box_offset_at_x(const gates_text_backend_t *be, gates_i32 font, const gates_widget_state_t *st,
                                  gates_i32 x) {
    gates_str_t t = gates_text_edit_text(st->edit);
    if (!st->password) return gates_text_offset_at_x(be, font, t, x);
    gates_i32 w = star_width(be, font);
    gates_u32 at = 0;
    for (gates_i32 left = 0; at < t.size && w > 0 && (x - left) * 2 >= w; left += w) {
        at += gates_text_decode(t, at, nullptr);
    }
    return at;
}

/* -- public API --------------------------------------------------------------- */

gates_err_t gates_textbox_copy_text(const gates_tree_t *tree, gates_node_t node, gates_u8 *buf,
                                    gates_usize_t cap, gates_usize_t *needed) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr || needed == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_str_t t = gates_text_edit_text(st->edit);
    *needed = t.size;
    if (buf == nullptr) {
        return GATES_OK;
    }
    if (cap < t.size) {
        return PROVEN_ERR_OVERFLOW;
    }
    if (t.size > 0) {
        memcpy(buf, t.ptr, t.size);
    }
    return GATES_OK;
}

gates_err_t gates_textbox_selection(const gates_tree_t *tree, gates_node_t node,
                                    gates_u32 *anchor, gates_u32 *caret) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr || anchor == nullptr || caret == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *anchor = st->edit->anchor;
    *caret = st->edit->caret;
    return GATES_OK;
}

gates_err_t gates_textbox_set_selection(gates_tree_t *tree, gates_node_t node, gates_u32 anchor,
                                        gates_u32 caret) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_str_t t = gates_text_edit_text(st->edit);
    if (!on_boundary(t, anchor) || !on_boundary(t, caret)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_text_edit_preedit(st->edit).size > 0) {
        return PROVEN_ERR_BUSY;
    }
    gates_text_edit_set_caret(st->edit, anchor, false);
    gates_text_edit_set_caret(st->edit, caret, true);
    gates_i_box_seal(st);
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_textbox_replace_selection(gates_tree_t *tree, gates_node_t node,
                                            gates_str_t text) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr || (text.size > 0 && text.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_text_edit_preedit(st->edit).size > 0) {
        return PROVEN_ERR_BUSY;
    }
    return gates_i_box_edit(tree, node.index, gates_text_edit_sel_begin(st->edit),
                            gates_text_edit_sel_end(st->edit), text, GATES_I_UNIT_OTHER, false);
}

gates_err_t gates_textbox_set_read_only(gates_tree_t *tree, gates_node_t node, bool read_only) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (st->read_only != read_only) {
        st->read_only = read_only;
        gates_i_box_seal(st);
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

bool gates_textbox_read_only(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = box_state(tree, node);
    return st != nullptr && st->read_only;
}

gates_err_t gates_textbox_set_max_bytes(gates_tree_t *tree, gates_node_t node,
                                        gates_u32 max_bytes) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (max_bytes != 0 && gates_text_edit_text(st->edit).size > max_bytes) {
        return PROVEN_ERR_OUT_OF_BOUNDS;
    }
    st->max_bytes = max_bytes;
    return GATES_OK;
}

gates_u32 gates_textbox_max_bytes(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = box_state(tree, node);
    return st != nullptr ? st->max_bytes : 0;
}

gates_err_t gates_textbox_set_password(gates_tree_t *tree, gates_node_t node, bool password) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (st->password != password) {
        st->password = password;
        undo_clear(tree, st->undo); /* no plaintext history either way */
        offer_clear(tree, st);
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
    }
    return GATES_OK;
}

bool gates_textbox_password(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = box_state(tree, node);
    return st != nullptr && st->password;
}

gates_err_t gates_textbox_set_undo_limits(gates_tree_t *tree, gates_node_t node,
                                          gates_u32 max_entries, gates_u32 max_bytes) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr || max_entries == 0) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_undo_t *u = undo_get(tree, st);
    if (u == nullptr) {
        return PROVEN_ERR_NOMEM;
    }
    u->max_entries = max_entries;
    u->max_bytes = max_bytes;
    undo_drop_redo(tree, u);
    undo_trim(tree, u);
    return GATES_OK;
}

bool gates_textbox_can_undo(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = box_state(tree, node);
    return st != nullptr && !st->read_only && st->undo != nullptr && st->undo->pos > 0;
}

bool gates_textbox_can_redo(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = box_state(tree, node);
    return st != nullptr && !st->read_only && st->undo != nullptr &&
           st->undo->pos < st->undo->count;
}

gates_err_t gates_textbox_accept_fit(gates_tree_t *tree, gates_node_t node) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (st->offer_len == 0 || st->offer_revision != st->revision || st->read_only ||
        gates_text_edit_preedit(st->edit).size > 0) {
        return PROVEN_ERR_INVALID_STATE;
    }
    gates_str_t part = { .ptr = st->offer, .size = st->offer_fit };
    gates_err_t err = gates_i_box_edit(tree, node.index, st->offer_begin, st->offer_end, part,
                                       GATES_I_UNIT_OTHER, true);
    offer_clear(tree, st); /* used (or no longer valid) either way */
    return err;
}

void gates_textbox_discard_rejected(gates_tree_t *tree, gates_node_t node) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st != nullptr) {
        offer_clear(tree, st);
    }
}

void gates_textbox_edit_commit(gates_tree_t *tree, gates_node_t node) {
    gates_widget_state_t *st = box_state(tree, node);
    if (st == nullptr) {
        return;
    }
    gates_text_edit_t *ed = st->edit;
    gates_u32 caret = ed->caret, anchor = ed->anchor;
    gates_text_edit_set_caret(ed, anchor, false); /* snaps onto codepoint boundaries */
    gates_text_edit_set_caret(ed, caret, true);
    gates_i_box_forget(tree, st);
    st->revision++;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
}
