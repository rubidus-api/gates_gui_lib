/* gates_gui_lib - persisted UI state: split ratios, selected tabs, column
 * widths and scroll offsets of nodes with automation ids, as text. Platform-free. */
#include <gates/state.h>
#include <gates/layout.h>
#include <gates/frame.h>
#include "gates_tree_internal.h"

#include <string.h>

#define HEADER "# gates state 1\n"

typedef struct out_t {
    gates_u8 *buf;
    gates_usize_t cap;
    gates_usize_t len;
} out_t;

static void put(out_t *o, const char *s, gates_usize_t n) {
    for (gates_usize_t i = 0; i < n; i++, o->len++) {
        if (o->buf != nullptr && o->len < o->cap) o->buf[o->len] = (gates_u8)s[i];
    }
}

static void put_num(out_t *o, gates_i64 v) {
    char d[24];
    int n = 0;
    bool neg = v < 0;
    gates_u64 u = neg ? (gates_u64)(-v) : (gates_u64)v;
    do { d[n++] = (char)('0' + u % 10); u /= 10; } while (u != 0);
    if (neg) put(o, "-", 1);
    while (n > 0) put(o, &d[--n], 1);
}

static bool id_ok(const gates_i_access_prop_t *p) {
    for (gates_u32 i = 0; i < p->id_len; i++) {
        if (p->id[i] == '\n' || p->id[i] == '\r') return false;
    }
    return p->id_len > 0;
}

/* The kind a node saves as, or null. */
static const char *kind_of(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->kind == GATES_NODE_TABS) return "tabs";
    if (s->kind == GATES_NODE_VIEW) return gates_i_view_ncol(tree, idx) > 0 ? "columns" : nullptr;
    if (s->layout_kind == GATES_LAYOUT_SPLIT) return "split";
    if (s->layout_kind == GATES_LAYOUT_SCROLL) return "scroll";
    return nullptr;
}

gates_err_t gates_state_save(const gates_tree_t *tree, gates_u8 *buf, gates_usize_t cap, gates_usize_t *needed) {
    if (tree == nullptr || needed == nullptr) return PROVEN_ERR_INVALID_ARG;
    for (int pass = 0; pass < 2; pass++) {
        out_t o = { .buf = pass == 1 ? buf : nullptr, .cap = cap };
        put(&o, HEADER, strlen(HEADER));
        for (gates_u32 i = 0; i < tree->access_prop_count; i++) {
            const gates_i_access_prop_t *p = &tree->access_props[i];
            gates_node_t n = { .index = p->index, .generation = p->generation };
            if (!gates_i_valid(tree, n) || !id_ok(p)) continue;
            const char *kind = kind_of(tree, p->index);
            if (kind == nullptr) continue;
            const gates_node_slot_t *s = gates_i_slot(tree, p->index);
            put(&o, kind, strlen(kind));
            put(&o, " ", 1);
            if (s->kind == GATES_NODE_TABS) {
                put_num(&o, gates_i_tabs_selected(tree, p->index));
            } else if (s->kind == GATES_NODE_VIEW) {
                gates_column_id_t id;
                gates_i32 w;
                bool hidden;
                for (gates_u32 k = 0; gates_i_view_col_info(tree, p->index, k, &id, &w, &hidden); k++) {
                    if (k > 0) put(&o, ",", 1);
                    put_num(&o, (gates_i32)id);
                    put(&o, ":", 1);
                    put_num(&o, w);
                    if (hidden) put(&o, "h", 1);
                }
            } else if (s->layout_kind == GATES_LAYOUT_SPLIT) {
                put_num(&o, s->split_ratio);
            } else {
                put_num(&o, s->scroll_offset);
            }
            put(&o, " ", 1);
            put(&o, (const char *)p->id, p->id_len);
            put(&o, "\n", 1);
        }
        *needed = o.len;
        if (pass == 0) {
            if (buf == nullptr) return GATES_OK;
            if (o.len > cap) return PROVEN_ERR_OVERFLOW;
        }
    }
    return GATES_OK;
}

/* A non-negative decimal of at most 9 digits; false otherwise. */
static bool parse_num(const gates_u8 *p, gates_usize_t n, gates_i32 *out) {
    if (n == 0 || n > 9) return false;
    gates_i32 v = 0;
    for (gates_usize_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return false;
        v = v * 10 + (p[i] - '0');
    }
    *out = v;
    return true;
}

static bool word_is(const gates_u8 *p, gates_usize_t n, const char *w) {
    return strlen(w) == n && memcmp(p, w, n) == 0;
}

static gates_u32 find_id(const gates_tree_t *tree, const gates_u8 *id, gates_usize_t n) {
    for (gates_u32 i = 0; i < tree->access_prop_count; i++) {
        const gates_i_access_prop_t *p = &tree->access_props[i];
        gates_node_t node = { .index = p->index, .generation = p->generation };
        if (p->id_len == n && n > 0 && memcmp(p->id, id, n) == 0 && gates_i_valid(tree, node)) return p->index;
    }
    return GATES_NONE;
}

/* One line: kind, value, id. true when it was applied. */
static bool apply(gates_tree_t *tree, const gates_u8 *line, gates_usize_t n) {
    gates_usize_t a = 0;
    while (a < n && line[a] != ' ') a++;
    gates_usize_t b = a + 1;
    while (b < n && line[b] != ' ') b++;
    if (a == 0 || a >= n || b >= n || b + 1 >= n) return false;
    const gates_u8 *kind = line, *val = line + a + 1, *id = line + b + 1;
    gates_usize_t kn = a, vn = b - a - 1, in = n - b - 1;
    gates_u32 idx = find_id(tree, id, in);
    if (idx == GATES_NONE) return false;
    const char *have = kind_of(tree, idx);
    if (have == nullptr || !word_is(kind, kn, have)) return false;
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    gates_i32 v = 0;
    if (s->kind == GATES_NODE_VIEW && memchr(val, ':', vn) != nullptr) {
        /* id:width[h], in display order (0.6.0). */
        gates_column_id_t ids[64];
        gates_i32 w[64];
        bool hid[64];
        gates_u32 k = 0;
        gates_usize_t start = 0;
        for (gates_usize_t i = 0; i <= vn; i++) {
            if (i < vn && val[i] != ',') continue;
            const gates_u8 *e = val + start;
            gates_usize_t en = i - start;
            gates_usize_t colon = 0;
            while (colon < en && e[colon] != ':') colon++;
            hid[k < 64 ? k : 0] = en > 0 && e[en - 1] == 'h';
            gates_usize_t wn = en - colon - 1 - (hid[k < 64 ? k : 0] ? 1 : 0);
            gates_i32 id = 0;
            if (k >= 64 || colon >= en || !parse_num(e, colon, &id) || colon + 1 + wn > en ||
                !parse_num(e + colon + 1, wn, &w[k])) {
                return false;
            }
            ids[k++] = (gates_column_id_t)id;
            start = i + 1;
        }
        return gates_i_view_apply_cols(tree, idx, ids, w, hid, k);
    }
    if (s->kind == GATES_NODE_VIEW) {
        /* The 0.3 format: widths only, by position. */
        gates_u32 ncol = gates_i_view_ncol(tree, idx);
        gates_i32 w[64];
        gates_u32 k = 0;
        gates_usize_t start = 0;
        for (gates_usize_t i = 0; i <= vn; i++) {
            if (i < vn && val[i] != ',') continue;
            if (k >= ncol || k >= 64 || !parse_num(val + start, i - start, &w[k])) return false;
            k++;
            start = i + 1;
        }
        if (k != ncol) return false;
        for (gates_u32 c = 0; c < ncol; c++) gates_i_view_set_col_width(tree, idx, c, w[c]);
        return true;
    }
    if (!parse_num(val, vn, &v)) return false;
    if (s->kind == GATES_NODE_TABS) {
        return gates_is_ok(gates_tabs_set_selected(tree, gates_i_handle(tree, idx), (gates_u32)v));
    }
    if (s->layout_kind == GATES_LAYOUT_SPLIT) {
        if (v < 1 || v > 999) return false;
        s->split_ratio = v;
    } else {
        s->scroll_offset = v; /* clamped by the next layout */
    }
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return true;
}

gates_err_t gates_state_load(gates_tree_t *tree, gates_str_t text, gates_u32 *applied) {
    if (tree == nullptr || (text.size > 0 && text.ptr == nullptr)) return PROVEN_ERR_INVALID_ARG;
    gates_u32 count = 0;
    gates_usize_t start = 0;
    for (gates_usize_t i = 0; i <= text.size; i++) {
        if (i < text.size && text.ptr[i] != '\n') continue;
        gates_usize_t n = i - start;
        if (n > 0 && text.ptr[start + n - 1] == '\r') n--;
        if (n > 0 && text.ptr[start] != '#' && apply(tree, text.ptr + start, n)) count++;
        start = i + 1;
    }
    if (applied != nullptr) *applied = count;
    return GATES_OK;
}
