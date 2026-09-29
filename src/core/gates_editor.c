/* gates_gui_lib - multi-line text editor (RFC-0006, plan-0022): one node over
 * a text buffer; lays out and paints only the lines it shows; its own history
 * of edits. Platform-free. */
#include <gates/editor.h>
#include <gates/event.h>
#include <gates/layout.h>
#include <gates/text.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"

#include <stdio.h>
#include <string.h>

#define ED_PAD 3                     /* text inset inside the border */
#define ED_WHEEL_LINES 3
#define ED_HISTORY_BYTES (1u << 20)  /* text kept for undo */

typedef enum { RUN_NONE, RUN_TYPE, RUN_DEL_BACK, RUN_DEL_FWD, RUN_OTHER } run_t;

typedef struct hist_t {
    gates_u32 at;
    gates_u8 *removed;
    gates_u32 rlen;
    gates_u8 *inserted;
    gates_u32 ilen;
    gates_u32 caret0, anchor0;       /* where the caret was before */
    run_t run;
} hist_t;

struct gates_i_editor {
    gates_text_buffer_t *buf;
    gates_u32 caret, anchor;
    gates_i32 pref_x;                /* Up/Down keep this column; -1 = take the caret's */
    gates_u32 top;                   /* first line shown */
    gates_i32 scroll_x;
    gates_i32 max_w;                 /* the widest line laid out so far */
    bool tab_inserts, read_only;
    gates_u32 tab_width, max_bytes, rows, cols;
    hist_t *hist;
    gates_u32 nhist, hist_cap, hist_pos;
    gates_i64 clean;                 /* hist_pos of the unmodified text, -1 = gone */
    gates_usize_t hist_bytes;
    bool run_open;                   /* the next typing may join the last entry */
    gates_u8 *scratch;               /* one line's bytes for layout */
    gates_u32 scratch_cap;
    gates_u32 drag_top;
    gates_i32 drag_x;
    /* Stage 3: wrapping, the gutter, highlighting. */
    bool wrap, line_numbers, auto_indent;
    gates_u32 top_row;               /* the row of `top` shown first (wrap) */
    gates_u8 *sscratch;              /* one line's style bytes for painting */
    gates_u32 sscratch_cap;
    gates_editor_style_t styles[256];
    gates_u32 nstyles;
    gates_editor_styler_fn styler;
    void *styler_user;
    gates_u32 styled_end;            /* styles before this (a line start) are up to date */
    /* Stage 4: an input method's composition, shown at the caret. */
    gates_u8 *pre;
    gates_u32 pre_len, pre_cap, ime_cursor;
};

typedef struct ed_geom {
    gates_rect_t inner, text, gutter, vtrack, vthumb, htrack, hthumb;
    gates_i32 row_h;
    gates_u32 lines, visible, top, max_top;
    gates_u32 top_row, max_top_row;  /* with wrap: rows inside the top lines */
    gates_i32 max_x, scroll_x;
} ed_geom;

/* A place on screen: a line and one of its rows (always row 0 without wrap). */
typedef struct dpos {
    gates_u32 line, row;
} dpos;

/* How rows are laid out: the backend, the font and the wrapping width. */
typedef struct lay_t {
    const gates_text_backend_t *be;
    gates_i32 font;
    gates_i32 w;
} lay_t;

/* -- state ------------------------------------------------------------------------------- */

static void hist_free_entry(gates_allocator_t a, hist_t *h) {
    if (h->removed != nullptr) a.free_fn(a.ctx, h->removed);
    if (h->inserted != nullptr) a.free_fn(a.ctx, h->inserted);
    *h = (hist_t){0};
}

void gates_i_editor_free(gates_tree_t *tree, gates_widget_state_t *st) {
    struct gates_i_editor *e = st->editor;
    if (e == nullptr) return;
    gates_allocator_t a = tree->alloc;
    for (gates_u32 i = 0; i < e->nhist; i++) hist_free_entry(a, &e->hist[i]);
    if (e->hist != nullptr) a.free_fn(a.ctx, e->hist);
    if (e->scratch != nullptr) a.free_fn(a.ctx, e->scratch);
    if (e->sscratch != nullptr) a.free_fn(a.ctx, e->sscratch);
    if (e->pre != nullptr) a.free_fn(a.ctx, e->pre);
    gates_text_buffer_destroy(e->buf);
    a.free_fn(a.ctx, e);
    st->editor = nullptr;
}

static struct gates_i_editor *ed_at(const gates_tree_t *tree, gates_u32 idx) {
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    return st != nullptr ? st->editor : nullptr;
}

static struct gates_i_editor *ed_of(const gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr || !gates_i_valid(tree, node) || gates_i_slot(tree, node.index)->kind != GATES_NODE_EDITOR) {
        return nullptr;
    }
    return ed_at(tree, node.index);
}

gates_u32 gates_i_editor_caret(const gates_widget_state_t *st) {
    return st != nullptr && st->editor != nullptr ? st->editor->caret : 0;
}

/* -- bytes and code points ------------------------------------------------------------------ */

static gates_u8 byte_at(const struct gates_i_editor *e, gates_u32 off) {
    return gates_text_buffer_byte(e->buf, off);
}

static gates_u32 len_of(const struct gates_i_editor *e) {
    return gates_text_buffer_length(e->buf);
}

/* The code point boundary at or before off. */
static gates_u32 snap(const struct gates_i_editor *e, gates_u32 off) {
    gates_u32 len = len_of(e);
    if (off > len) off = len;
    while (off > 0 && off < len && (byte_at(e, off) & 0xC0) == 0x80) off--;
    return off;
}

/* The next boundary ("\r\n" is one step). */
static gates_u32 next_cp(const struct gates_i_editor *e, gates_u32 off) {
    gates_u32 len = len_of(e);
    if (off >= len) return len;
    gates_u8 c = byte_at(e, off);
    if (c == '\r' && byte_at(e, off + 1) == '\n' && off + 1 < len) return off + 2;
    gates_u32 n = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    return off + n <= len ? off + n : len;
}

static gates_u32 prev_cp(const struct gates_i_editor *e, gates_u32 off) {
    if (off == 0) return 0;
    if (byte_at(e, off - 1) == '\n' && off >= 2 && byte_at(e, off - 2) == '\r') return off - 2;
    gates_u32 p = off - 1;
    while (p > 0 && off - p < 4 && (byte_at(e, p) & 0xC0) == 0x80) p--;
    return p;
}

typedef enum { CL_SPACE, CL_BREAK, CL_WORD, CL_PUNCT } cls_t;

static cls_t cls_at(const struct gates_i_editor *e, gates_u32 off) {
    gates_u8 c = byte_at(e, off);
    if (c == ' ' || c == '\t') return CL_SPACE;
    if (c == '\n' || c == '\r') return CL_BREAK;
    if (c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z')) return CL_WORD;
    return CL_PUNCT;
}

/* Ctrl+Right: past the run the caret is in, then past blanks. */
static gates_u32 word_right(const struct gates_i_editor *e, gates_u32 off) {
    gates_u32 len = len_of(e);
    if (off >= len) return len;
    cls_t c = cls_at(e, off);
    if (c == CL_BREAK) return next_cp(e, off);
    while (off < len && cls_at(e, off) == c) off = next_cp(e, off);
    while (off < len && cls_at(e, off) == CL_SPACE) off = next_cp(e, off);
    return off;
}

/* Ctrl+Left: back over blanks, then to the start of the run before them. */
static gates_u32 word_left(const struct gates_i_editor *e, gates_u32 off) {
    if (off == 0) return 0;
    gates_u32 p = prev_cp(e, off);
    while (p > 0 && cls_at(e, p) == CL_SPACE) p = prev_cp(e, p);
    cls_t c = cls_at(e, p);
    if (c == CL_BREAK || c == CL_SPACE) return p;
    while (p > 0) {
        gates_u32 q = prev_cp(e, p);
        if (cls_at(e, q) != c) break;
        p = q;
    }
    return p;
}

/* True when text is well-formed UTF-8 (no overlongs, surrogates or values past U+10FFFF). */
static bool utf8_ok(gates_str_t t) {
    for (gates_usize_t i = 0; i < t.size;) {
        gates_u8 c = t.ptr[i];
        gates_u32 n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (n == 0 || i + n > t.size) return false;
        gates_u32 cp = n == 1 ? c : n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (gates_u32 k = 1; k < n; k++) {
            if ((t.ptr[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (t.ptr[i + k] & 0x3Fu);
        }
        if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += n;
    }
    return true;
}

static bool on_boundary(const struct gates_i_editor *e, gates_u32 off) {
    return off <= len_of(e) && (off == len_of(e) || (byte_at(e, off) & 0xC0) != 0x80);
}

/* -- one line's layout ---------------------------------------------------------------------- */

/* The line's text (without its line end) in the scratch buffer; empty when it cannot be held. */
static gates_str_t line_text(const gates_tree_t *tree, struct gates_i_editor *e, gates_u32 line, gates_err_t *err) {
    gates_u32 s = gates_text_buffer_line_start(e->buf, line), en = gates_text_buffer_line_end(e->buf, line);
    gates_u32 n = en - s;
    if (n > e->scratch_cap) {
        gates_u32 cap = e->scratch_cap < 256 ? 256 : e->scratch_cap;
        while (cap < n) cap *= 2;
        proven_result_mem_mut_t r = e->scratch == nullptr
                                        ? tree->alloc.alloc_fn(tree->alloc.ctx, cap, 1)
                                        : tree->alloc.realloc_fn(tree->alloc.ctx, e->scratch, e->scratch_cap, cap, 1);
        if (!proven_is_ok(r.err)) {
            if (err != nullptr) *err = r.err;
            return (gates_str_t){0};
        }
        e->scratch = (gates_u8 *)r.value.ptr;
        e->scratch_cap = cap;
    }
    gates_text_buffer_copy(e->buf, s, en, e->scratch, n);
    return (gates_str_t){ .ptr = e->scratch, .size = n };
}

static gates_i32 tab_px(const gates_text_backend_t *be, gates_i32 font, const struct gates_i_editor *e) {
    gates_i32 sp = be->glyph_advance != nullptr ? be->glyph_advance(be->ctx, font, ' ') : 8;
    gates_i32 t = sp * (gates_i32)e->tab_width;
    return t > 0 ? t : 8;
}

static gates_i32 advance(const gates_text_backend_t *be, gates_i32 font, const struct gates_i_editor *e, gates_i32 x,
                         gates_u32 cp) {
    if (cp == '\t') {
        gates_i32 t = tab_px(be, font, e);
        return (x / t + 1) * t - x;
    }
    return be->glyph_advance != nullptr ? be->glyph_advance(be->ctx, font, cp) : 8;
}

/* x (from the line's left edge) of byte `upto` of the line. */
static gates_i32 x_in(const gates_text_backend_t *be, gates_i32 font, const struct gates_i_editor *e, gates_str_t t,
                      gates_u32 upto) {
    gates_i32 x = 0;
    for (gates_u32 i = 0; i < t.size && i < upto;) {
        gates_u32 cp;
        i += gates_text_decode(t, i, &cp);
        x += advance(be, font, e, x, cp);
    }
    return x;
}

/* The boundary nearest x in the line. */
static gates_u32 at_x(const gates_text_backend_t *be, gates_i32 font, const struct gates_i_editor *e, gates_str_t t,
                      gates_i32 x) {
    gates_i32 left = 0;
    for (gates_u32 i = 0; i < t.size;) {
        gates_u32 cp;
        gates_u32 next = i + gates_text_decode(t, i, &cp);
        gates_i32 right = left + advance(be, font, e, left, cp);
        if (x < right) return (x - left) * 2 < right - left ? i : next;
        left = right;
        i = next;
    }
    return (gates_u32)t.size;
}

static const gates_text_backend_t *backend(const gates_tree_t *tree) {
    return tree->text_backend;
}

/* -- rows ------------------------------------------------------------------------------------ */

/* The end of the row that starts at `start` in line text t: without wrap the
 * line's end; with it, after the last blank that fits, else the last code
 * point that fits (at least one per row; blanks may hang past the edge). */
static gates_u32 row_end(const lay_t *L, const struct gates_i_editor *e, gates_str_t t, gates_u32 start) {
    if (!e->wrap || L->w <= 0 || L->be == nullptr) return (gates_u32)t.size;
    gates_i32 x = 0;
    gates_u32 brk = start;
    for (gates_u32 i = start; i < t.size;) {
        gates_u32 cp;
        gates_u32 next = i + gates_text_decode(t, i, &cp);
        bool blank = cp == ' ' || cp == '\t';
        gates_i32 a = advance(L->be, L->font, e, x, cp);
        if (x + a > L->w && i > start && !blank) return brk > start ? brk : i;
        x += a;
        i = next;
        if (blank) brk = i;
    }
    return (gates_u32)t.size;
}

/* Row `row` of the line (clamped to its last): its range; returns its index. */
static gates_u32 row_range(const lay_t *L, const struct gates_i_editor *e, gates_str_t t, gates_u32 row, gates_u32 *rs,
                           gates_u32 *re) {
    gates_u32 s = 0;
    for (gates_u32 r = 0;; r++) {
        gates_u32 en = row_end(L, e, t, s);
        if (r == row || en >= t.size) {
            *rs = s;
            *re = en;
            return r;
        }
        s = en;
    }
}

/* The row holding byte `rel` of the line (a row boundary belongs to the row after it). */
static gates_u32 row_at(const lay_t *L, const struct gates_i_editor *e, gates_str_t t, gates_u32 rel, gates_u32 *rs,
                        gates_u32 *re) {
    gates_u32 s = 0;
    for (gates_u32 r = 0;; r++) {
        gates_u32 en = row_end(L, e, t, s);
        if (rel < en || en >= t.size) {
            *rs = s;
            *re = en;
            return r;
        }
        s = en;
    }
}

static gates_u32 rows_in(const gates_tree_t *tree, const lay_t *L, struct gates_i_editor *e, gates_u32 line) {
    if (!e->wrap) return 1;
    gates_str_t t = line_text(tree, e, line, nullptr);
    gates_u32 rs, re;
    return row_range(L, e, t, UINT32_MAX, &rs, &re) + 1;
}

static bool dpos_lt(dpos a, dpos b) { return a.line < b.line || (a.line == b.line && a.row < b.row); }
static bool dpos_eq(dpos a, dpos b) { return a.line == b.line && a.row == b.row; }

/* n rows down (up when negative), stopping at the first and last rows; *moved (may be null) how many. */
static dpos advance_rows(const gates_tree_t *tree, const lay_t *L, struct gates_i_editor *e, dpos p, gates_i64 n,
                         gates_i64 *moved) {
    gates_u32 lines = gates_text_buffer_line_count(e->buf);
    gates_i64 done = 0;
    for (; n > 0; n--, done++) {
        if (p.row + 1 < rows_in(tree, L, e, p.line)) p.row++;
        else if (p.line + 1 < lines) p = (dpos){ p.line + 1, 0 };
        else break;
    }
    for (; n < 0; n++, done--) {
        if (p.row > 0) p.row--;
        else if (p.line > 0) p = (dpos){ p.line - 1, rows_in(tree, L, e, p.line - 1) - 1 };
        else break;
    }
    if (moved != nullptr) *moved = done;
    return p;
}

/* Where an offset is shown. */
static dpos pos_of(const gates_tree_t *tree, const lay_t *L, struct gates_i_editor *e, gates_u32 off) {
    gates_u32 line = gates_text_buffer_line_of(e->buf, off);
    if (!e->wrap) return (dpos){ line, 0 };
    gates_str_t t = line_text(tree, e, line, nullptr);
    gates_u32 rs, re;
    return (dpos){ line, row_at(L, e, t, off - gates_text_buffer_line_start(e->buf, line), &rs, &re) };
}

/* The caret's x in its row (0 when the line cannot be laid out). */
static gates_i32 caret_x(const gates_tree_t *tree, const lay_t *L, struct gates_i_editor *e, gates_u32 off) {
    if (L->be == nullptr) return 0;
    gates_u32 line = gates_text_buffer_line_of(e->buf, off);
    gates_str_t t = line_text(tree, e, line, nullptr);
    gates_u32 rs, re, rel = off - gates_text_buffer_line_start(e->buf, line);
    (void)row_at(L, e, t, rel, &rs, &re);
    gates_i32 x = x_in(L->be, L->font, e, (gates_str_t){ .ptr = t.ptr + rs, .size = t.size - rs }, rel - rs);
    if (!e->wrap && x + 1 > e->max_w) e->max_w = x + 1;
    return x;
}

/* The offset in row p nearest x; never the row's end unless it is the line's end
 * (that place is shown at the start of the next row). */
static gates_u32 at_row_x(const gates_tree_t *tree, const lay_t *L, struct gates_i_editor *e, dpos p, gates_i32 x) {
    gates_str_t t = line_text(tree, e, p.line, nullptr);
    gates_u32 rs, re;
    (void)row_range(L, e, t, p.row, &rs, &re);
    gates_u32 rel = rs + (L->be != nullptr ? at_x(L->be, L->font, e, (gates_str_t){ .ptr = t.ptr + rs, .size = re - rs }, x) : 0);
    if (rel == re && re < t.size && re > rs) {
        do rel--; while (rel > rs && (t.ptr[rel] & 0xC0) == 0x80);
    }
    return gates_text_buffer_line_start(e->buf, p.line) + rel;
}

/* -- geometry -------------------------------------------------------------------------------- */

static gates_u32 digits(gates_u32 n) {
    gates_u32 d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

static void geom(const gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e, gates_i32 line_height, ed_geom *g) {
    memset(g, 0, sizeof *g);
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    g->inner = (gates_rect_t){ r.x + 1, r.y + 1, r.w - 2 > 0 ? r.w - 2 : 0, r.h - 2 > 0 ? r.h - 2 : 0 };
    g->row_h = line_height > 0 ? line_height : 16;
    g->lines = gates_text_buffer_line_count(e->buf);
    const gates_text_backend_t *be = backend(tree);
    gates_i32 font = gates_i_font(tree, idx);
    gates_i32 zero = be != nullptr && be->glyph_advance != nullptr ? be->glyph_advance(be->ctx, font, '0')
                                                                     : (tree->advance > 0 ? tree->advance : 8);
    gates_i32 gw = e->line_numbers ? (gates_i32)digits(g->lines) * zero + 2 * ED_PAD : 0;
    gates_i32 h = g->inner.h - 2 * ED_PAD, w = g->inner.w - gw - 2 * ED_PAD;
    bool vbar = e->wrap || (gates_i64)g->lines * g->row_h > h;
    bool hbar = !e->wrap && e->max_w > w - (vbar ? GATES_SCROLLBAR_PX : 0);
    if (hbar && !vbar) vbar = (gates_i64)g->lines * g->row_h > h - GATES_SCROLLBAR_PX;
    g->text = (gates_rect_t){ g->inner.x + gw + ED_PAD, g->inner.y + ED_PAD, w - (vbar ? GATES_SCROLLBAR_PX : 0),
                              h - (hbar ? GATES_SCROLLBAR_PX : 0) };
    if (g->text.w < 0) g->text.w = 0;
    if (g->text.h < 0) g->text.h = 0;
    if (gw > 0) g->gutter = (gates_rect_t){ g->inner.x, g->inner.y, gw, g->inner.h - (hbar ? GATES_SCROLLBAR_PX : 0) };
    gates_u32 fit = (gates_u32)(g->text.h / g->row_h);
    g->visible = fit > 0 ? fit : 1;
    if (!e->wrap) {
        g->max_top = g->lines > g->visible ? g->lines - g->visible : 0;
        g->top = e->top < g->max_top ? e->top : g->max_top;
    } else {
        /* The last place the view may start: `visible` rows before the end. */
        lay_t L = { be, font, g->text.w };
        dpos last = { g->lines - 1, rows_in(tree, &L, e, g->lines - 1) - 1 };
        dpos m = advance_rows(tree, &L, e, last, -(gates_i64)(g->visible - 1), nullptr);
        g->max_top = m.line;
        g->max_top_row = m.row;
        dpos top = { e->top, e->top_row };
        if (top.line >= g->lines) top = (dpos){ g->lines - 1, 0 };
        gates_u32 rows = rows_in(tree, &L, e, top.line);
        if (top.row >= rows) top.row = rows - 1;
        if (dpos_lt(m, top)) top = m;
        g->top = top.line;
        g->top_row = top.row;
    }
    g->max_x = e->wrap ? 0 : e->max_w > g->text.w ? e->max_w - g->text.w : 0;
    g->scroll_x = e->scroll_x < 0 ? 0 : e->scroll_x > g->max_x ? g->max_x : e->scroll_x;
    if (vbar) {
        g->vtrack = (gates_rect_t){ g->inner.x + g->inner.w - GATES_SCROLLBAR_PX, g->inner.y, GATES_SCROLLBAR_PX,
                                    g->inner.h - (hbar ? GATES_SCROLLBAR_PX : 0) };
        gates_i64 th = g->lines > 0 ? (gates_i64)g->vtrack.h * g->visible / g->lines : g->vtrack.h;
        if (th < GATES_SCROLLBAR_PX) th = GATES_SCROLLBAR_PX;
        if (th > g->vtrack.h) th = g->vtrack.h;
        gates_i32 travel = g->vtrack.h - (gates_i32)th;
        gates_i32 y = g->max_top > 0 ? (gates_i32)((gates_i64)travel * g->top / g->max_top) : 0;
        g->vthumb = (gates_rect_t){ g->vtrack.x, g->vtrack.y + y, GATES_SCROLLBAR_PX, (gates_i32)th };
    }
    if (hbar) {
        g->htrack = (gates_rect_t){ g->inner.x, g->inner.y + g->inner.h - GATES_SCROLLBAR_PX,
                                    g->inner.w - (vbar ? GATES_SCROLLBAR_PX : 0), GATES_SCROLLBAR_PX };
        gates_i32 tw = e->max_w > 0 ? (gates_i32)((gates_i64)g->htrack.w * g->text.w / e->max_w) : g->htrack.w;
        if (tw < GATES_SCROLLBAR_PX) tw = GATES_SCROLLBAR_PX;
        if (tw > g->htrack.w) tw = g->htrack.w;
        gates_i32 travel = g->htrack.w - tw;
        gates_i32 x = g->max_x > 0 ? (gates_i32)((gates_i64)travel * g->scroll_x / g->max_x) : 0;
        g->hthumb = (gates_rect_t){ g->htrack.x + x, g->htrack.y, tw, GATES_SCROLLBAR_PX };
    }
}

static void geom_now(const gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e, ed_geom *g) {
    geom(tree, idx, e, tree->line_height, g);
}

static lay_t lay_of(const gates_tree_t *tree, gates_u32 idx, const ed_geom *g) {
    return (lay_t){ backend(tree), gates_i_font(tree, idx), g->text.w };
}

/* Scrolls so the caret is shown (its row, and its column without wrap). */
static void keep_caret(gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e) {
    ed_geom g;
    gates_i32 x = 0;
    if (!e->wrap) {
        lay_t L0 = { backend(tree), gates_i_font(tree, idx), 0 };
        x = caret_x(tree, &L0, e, e->caret); /* may widen max_w: before the geometry */
    }
    geom_now(tree, idx, e, &g);
    lay_t L = lay_of(tree, idx, &g);
    dpos c = pos_of(tree, &L, e, e->caret);
    dpos top = { g.top, g.top_row };
    if (dpos_lt(c, top)) {
        top = c;
    } else {
        dpos p = top;
        bool seen = false;
        for (gates_u32 k = 0; k < g.visible && !seen; k++) {
            if (dpos_eq(p, c)) {
                seen = true;
            } else {
                gates_i64 moved = 0;
                p = advance_rows(tree, &L, e, p, 1, &moved);
                if (moved == 0) break;
            }
        }
        if (!seen) top = advance_rows(tree, &L, e, c, -(gates_i64)(g.visible - 1), nullptr);
    }
    e->top = top.line;
    e->top_row = top.row;
    if (e->wrap) {
        e->scroll_x = 0;
        return;
    }
    gates_i32 sx = g.scroll_x;
    if (x < sx) sx = x;
    else if (x + 1 > sx + g.text.w) sx = x + 1 - g.text.w;
    e->scroll_x = sx < 0 ? 0 : sx;
}

/* -- creation and the public surface ---------------------------------------------------------- */

gates_err_t gates_editor_create(gates_tree_t *tree, gates_node_t parent, const gates_editor_desc_t *desc,
                                gates_node_t *out_editor) {
    if (tree == nullptr || desc == nullptr || out_editor == nullptr ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_editor = GATES_NODE_NULL;
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, sizeof(struct gates_i_editor), alignof(struct gates_i_editor));
    if (!proven_is_ok(r.err)) return r.err;
    struct gates_i_editor *e = (struct gates_i_editor *)r.value.ptr;
    memset(e, 0, sizeof *e);
    e->pref_x = -1;
    e->tab_inserts = desc->tab_inserts;
    e->read_only = desc->read_only;
    e->tab_width = desc->tab_width != 0 ? desc->tab_width : 4;
    e->max_bytes = desc->max_bytes != 0 && desc->max_bytes < GATES_TEXT_BUFFER_MAX ? desc->max_bytes
                                                                                    : GATES_TEXT_BUFFER_MAX;
    e->rows = desc->rows != 0 ? desc->rows : 10;
    e->cols = desc->cols != 0 ? desc->cols : 40;
    e->wrap = desc->wrap;
    e->line_numbers = desc->line_numbers;
    e->auto_indent = desc->auto_indent;
    gates_err_t err = gates_text_buffer_create(a, &e->buf);
    if (!gates_is_ok(err)) {
        a.free_fn(a.ctx, e);
        return err;
    }
    gates_node_t node = GATES_NODE_NULL;
    gates_node_desc_t nd = { .kind = GATES_NODE_EDITOR };
    err = gates_node_create(tree, GATES_NODE_NULL, &nd, &node);
    gates_u32 state = GATES_NONE;
    bool owned = false;
    if (gates_is_ok(err)) {
        err = gates_i_state_acquire(tree, &state);
        if (gates_is_ok(err)) {
            gates_i_slot(tree, node.index)->state_index = state;
            gates_i_state(tree, state)->editor = e;
            owned = true;
        }
    }
    if (gates_is_ok(err) && !gates_node_eq(parent, GATES_NODE_NULL)) err = gates_node_append(tree, parent, node);
    if (!gates_is_ok(err)) {
        if (!owned) {
            gates_text_buffer_destroy(e->buf);
            a.free_fn(a.ctx, e);
        }
        if (gates_i_valid(tree, node)) gates_i_node_undo(tree, node);
        return err;
    }
    *out_editor = node;
    return GATES_OK;
}

const gates_text_buffer_t *gates_editor_buffer(const gates_tree_t *tree, gates_node_t editor) {
    const struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr ? e->buf : nullptr;
}

gates_u32 gates_editor_length(const gates_tree_t *tree, gates_node_t editor) {
    const struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr ? len_of(e) : 0;
}

static void hist_clear(gates_tree_t *tree, struct gates_i_editor *e) {
    for (gates_u32 i = 0; i < e->nhist; i++) hist_free_entry(tree->alloc, &e->hist[i]);
    e->nhist = e->hist_pos = 0;
    e->hist_bytes = 0;
    e->clean = 0;
    e->run_open = false;
}

static void changed(gates_tree_t *tree, gates_u32 idx) {
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    st->revision++;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    gates_i_access_log(tree, GATES_ACCESS_CHANGED, idx, 0);
}

gates_err_t gates_editor_set_text(gates_tree_t *tree, gates_node_t editor, gates_str_t text) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr || (text.size > 0 && text.ptr == nullptr) || !utf8_ok(text)) return PROVEN_ERR_INVALID_ARG;
    gates_err_t err = gates_text_buffer_set_text(e->buf, text);
    if (!gates_is_ok(err)) return err;
    hist_clear(tree, e);
    e->caret = e->anchor = 0;
    e->top = 0;
    e->top_row = 0;
    e->scroll_x = 0;
    e->max_w = 0;
    e->pref_x = -1;
    e->styled_end = 0;
    changed(tree, editor.index);
    return GATES_OK;
}

/* -- history ------------------------------------------------------------------------------------ */

static gates_err_t dup(gates_allocator_t a, const gates_u8 *p, gates_u32 n, gates_u8 **out) {
    *out = nullptr;
    if (n == 0) return GATES_OK;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, n, 1);
    if (!proven_is_ok(r.err)) return r.err;
    *out = (gates_u8 *)r.value.ptr;
    memcpy(*out, p, n);
    return GATES_OK;
}

/* Drops the oldest entries while the history holds too much text. */
static void hist_trim(gates_tree_t *tree, struct gates_i_editor *e) {
    while (e->nhist > 1 && e->hist_bytes > ED_HISTORY_BYTES) {
        e->hist_bytes -= e->hist[0].rlen + e->hist[0].ilen;
        hist_free_entry(tree->alloc, &e->hist[0]);
        memmove(&e->hist[0], &e->hist[1], (gates_usize_t)(e->nhist - 1) * sizeof(hist_t));
        e->nhist--;
        e->hist_pos--;
        if (e->clean >= 0) e->clean--;
    }
}

/* Appends `add` to a history text (at its end, or its start). */
static gates_err_t grow_text(gates_allocator_t a, gates_u8 **p, gates_u32 *n, gates_str_t add, bool front) {
    if (add.size == 0) return GATES_OK;
    proven_result_mem_mut_t r = *p == nullptr ? a.alloc_fn(a.ctx, *n + add.size, 1)
                                              : a.realloc_fn(a.ctx, *p, *n, *n + add.size, 1);
    if (!proven_is_ok(r.err)) return r.err;
    gates_u8 *q = (gates_u8 *)r.value.ptr;
    if (front) {
        memmove(q + add.size, q, *n);
        memcpy(q, add.ptr, add.size);
    } else {
        memcpy(q + *n, add.ptr, add.size);
    }
    *p = q;
    *n += (gates_u32)add.size;
    return GATES_OK;
}

/* Records an edit already made: joins the last entry for a run, else a new entry. */
static gates_err_t hist_record(gates_tree_t *tree, struct gates_i_editor *e, gates_u32 at, gates_str_t removed,
                               gates_str_t inserted, gates_u32 caret0, gates_u32 anchor0, run_t run) {
    gates_allocator_t a = tree->alloc;
    hist_t *top = e->hist_pos > 0 && e->hist_pos == e->nhist ? &e->hist[e->hist_pos - 1] : nullptr;
    if (e->run_open && top != nullptr && run == top->run && e->clean != (gates_i64)e->hist_pos) {
        if (run == RUN_TYPE && removed.size == 0 && top->at + top->ilen == at) {
            gates_err_t err = grow_text(a, &top->inserted, &top->ilen, inserted, false);
            if (gates_is_ok(err)) e->hist_bytes += inserted.size;
            return err;
        }
        if (run == RUN_DEL_BACK && inserted.size == 0 && top->at == at + removed.size) {
            gates_err_t err = grow_text(a, &top->removed, &top->rlen, removed, true);
            if (gates_is_ok(err)) {
                top->at = at;
                e->hist_bytes += removed.size;
            }
            return err;
        }
        if (run == RUN_DEL_FWD && inserted.size == 0 && top->at == at) {
            gates_err_t err = grow_text(a, &top->removed, &top->rlen, removed, false);
            if (gates_is_ok(err)) e->hist_bytes += removed.size;
            return err;
        }
    }
    /* A new entry: what could have been redone is gone. */
    for (gates_u32 i = e->hist_pos; i < e->nhist; i++) {
        e->hist_bytes -= e->hist[i].rlen + e->hist[i].ilen;
        hist_free_entry(a, &e->hist[i]);
    }
    e->nhist = e->hist_pos;
    if (e->clean > (gates_i64)e->hist_pos) e->clean = -1;
    if (e->nhist == e->hist_cap) {
        gates_u32 nc = e->hist_cap == 0 ? 16 : e->hist_cap * 2;
        proven_result_mem_mut_t r = e->hist == nullptr
                                        ? a.alloc_fn(a.ctx, nc * sizeof(hist_t), alignof(hist_t))
                                        : a.realloc_fn(a.ctx, e->hist, e->hist_cap * sizeof(hist_t), nc * sizeof(hist_t),
                                                       alignof(hist_t));
        if (!proven_is_ok(r.err)) return r.err;
        e->hist = (hist_t *)r.value.ptr;
        e->hist_cap = nc;
    }
    hist_t h = { .at = at, .rlen = (gates_u32)removed.size, .ilen = (gates_u32)inserted.size, .caret0 = caret0,
                 .anchor0 = anchor0, .run = run };
    gates_err_t err = dup(a, removed.ptr, h.rlen, &h.removed);
    if (gates_is_ok(err)) err = dup(a, inserted.ptr, h.ilen, &h.inserted);
    if (!gates_is_ok(err)) {
        hist_free_entry(a, &h);
        return err;
    }
    e->hist[e->nhist++] = h;
    e->hist_pos = e->nhist;
    e->hist_bytes += h.rlen + h.ilen;
    hist_trim(tree, e);
    return GATES_OK;
}

/* -- editing ------------------------------------------------------------------------------------- */

/* Styles from the edited line on may be stale. */
static void restyle_from(struct gates_i_editor *e, gates_u32 off) {
    gates_u32 start = gates_text_buffer_line_start(e->buf, gates_text_buffer_line_of(e->buf, off));
    if (start < e->styled_end) e->styled_end = start;
}

/* Announces a person's change (reserved by the caller). */
static void report(gates_tree_t *tree, gates_u32 idx, bool text) {
    if (text) gates_i_event_push(tree, idx, GATES_EVENT_TEXT_CHANGED, GATES_ORIGIN_USER);
    gates_i_event_push(tree, idx, GATES_EVENT_SELECTION_CHANGED, GATES_ORIGIN_USER);
}

/* One edit: [b, en) becomes text. A person's edit (user) is reported; `record`
 * puts it in the history (a failure to record leaves the edit made and the
 * history cleared, so undo never replays a wrong step). */
static gates_err_t edit(gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e, gates_u32 b, gates_u32 en,
                        gates_str_t text, run_t run, bool user, bool record) {
    if (user && e->read_only) return PROVEN_ERR_PERMISSION;
    if ((gates_u64)len_of(e) - (en - b) + text.size > e->max_bytes) return PROVEN_ERR_OUT_OF_BOUNDS;
    if (user && gates_i_wants_events(tree, idx)) {
        gates_err_t err = gates_i_event_reserve(tree, 2, 0);
        if (!gates_is_ok(err)) return err;
    }
    gates_u8 *removed = nullptr;
    gates_err_t err = GATES_OK;
    if (record && en > b) {
        proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, en - b, 1);
        if (!proven_is_ok(r.err)) return r.err;
        removed = (gates_u8 *)r.value.ptr;
        gates_text_buffer_copy(e->buf, b, en, removed, en - b);
    }
    gates_u32 caret0 = e->caret, anchor0 = e->anchor;
    err = gates_text_buffer_replace(e->buf, b, en, text);
    if (!gates_is_ok(err)) {
        if (removed != nullptr) tree->alloc.free_fn(tree->alloc.ctx, removed);
        return err;
    }
    restyle_from(e, b);
    if (record) {
        gates_err_t herr = hist_record(tree, e, b, (gates_str_t){ .ptr = removed, .size = en - b }, text, caret0, anchor0,
                                       run);
        if (!gates_is_ok(herr)) {
            hist_clear(tree, e);
            e->clean = -1;
            tree->input_error = herr;
        }
    }
    if (removed != nullptr) tree->alloc.free_fn(tree->alloc.ctx, removed);
    e->run_open = run == RUN_TYPE || run == RUN_DEL_BACK || run == RUN_DEL_FWD;
    changed(tree, idx);
    if (user) report(tree, idx, true);
    return GATES_OK;
}

/* A person's edit at the selection or a range; the caret lands after the new text. */
static gates_err_t user_edit(gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e, gates_u32 b, gates_u32 en,
                             gates_str_t text, run_t run) {
    gates_err_t err = edit(tree, idx, e, b, en, text, run, true, true);
    if (!gates_is_ok(err)) {
        if (err != PROVEN_ERR_PERMISSION) tree->input_error = err;
        return err;
    }
    e->caret = e->anchor = b + (gates_u32)text.size;
    e->pref_x = -1;
    keep_caret(tree, idx, e);
    return GATES_OK;
}

gates_err_t gates_editor_replace(gates_tree_t *tree, gates_node_t editor, gates_u32 begin, gates_u32 end,
                                 gates_str_t text, bool undoable) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr || begin > end || end > len_of(e) || !on_boundary(e, begin) || !on_boundary(e, end) ||
        (text.size > 0 && text.ptr == nullptr) || !utf8_ok(text)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_u32 caret = e->caret, anchor = e->anchor;
    e->run_open = false;
    gates_err_t err = edit(tree, editor.index, e, begin, end, text, RUN_OTHER, false, undoable);
    if (!gates_is_ok(err)) return err;
    e->run_open = false;
    /* The caret keeps its place in the text: a point in the replaced range goes after it. */
    gates_u32 k = (gates_u32)text.size, del = end - begin;
    e->caret = caret < begin ? caret : caret >= end ? caret - del + k : begin + k;
    e->anchor = anchor < begin ? anchor : anchor >= end ? anchor - del + k : begin + k;
    if (!undoable) {
        hist_clear(tree, e); /* the history no longer describes this text */
        e->clean = -1;
    }
    return GATES_OK;
}

static gates_err_t replay(gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e, bool redo, bool user) {
    if (redo ? e->hist_pos >= e->nhist : e->hist_pos == 0) return PROVEN_ERR_INVALID_STATE;
    if (e->read_only && user) return PROVEN_ERR_PERMISSION;
    hist_t *h = &e->hist[redo ? e->hist_pos : e->hist_pos - 1];
    if (user && gates_i_wants_events(tree, idx)) {
        gates_err_t err = gates_i_event_reserve(tree, 2, 0);
        if (!gates_is_ok(err)) return err;
    }
    gates_str_t put = redo ? (gates_str_t){ .ptr = h->inserted, .size = h->ilen }
                           : (gates_str_t){ .ptr = h->removed, .size = h->rlen };
    gates_u32 take = redo ? h->rlen : h->ilen;
    gates_err_t err = gates_text_buffer_replace(e->buf, h->at, h->at + take, put);
    if (!gates_is_ok(err)) return err;
    restyle_from(e, h->at);
    if (redo) {
        e->hist_pos++;
        e->caret = e->anchor = h->at + h->ilen;
    } else {
        e->hist_pos--;
        e->caret = h->caret0;
        e->anchor = h->anchor0;
    }
    e->run_open = false;
    e->pref_x = -1;
    changed(tree, idx);
    keep_caret(tree, idx, e);
    if (user) report(tree, idx, true);
    return GATES_OK;
}

gates_err_t gates_editor_undo(gates_tree_t *tree, gates_node_t editor) {
    struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr ? replay(tree, editor.index, e, false, false) : PROVEN_ERR_INVALID_ARG;
}

gates_err_t gates_editor_redo(gates_tree_t *tree, gates_node_t editor) {
    struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr ? replay(tree, editor.index, e, true, false) : PROVEN_ERR_INVALID_ARG;
}

bool gates_editor_can_undo(const gates_tree_t *tree, gates_node_t editor) {
    const struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr && e->hist_pos > 0;
}

bool gates_editor_can_redo(const gates_tree_t *tree, gates_node_t editor) {
    const struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr && e->hist_pos < e->nhist;
}

bool gates_editor_modified(const gates_tree_t *tree, gates_node_t editor) {
    const struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr && e->clean != (gates_i64)e->hist_pos;
}

void gates_editor_set_unmodified(gates_tree_t *tree, gates_node_t editor) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr) return;
    e->clean = e->hist_pos;
    e->run_open = false; /* the saved text must stay reachable */
}

gates_err_t gates_editor_set_read_only(gates_tree_t *tree, gates_node_t editor, bool read_only) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (e->read_only != read_only) {
        e->read_only = read_only;
        changed(tree, editor.index);
    }
    return GATES_OK;
}

bool gates_editor_read_only(const gates_tree_t *tree, gates_node_t editor) {
    const struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr && e->read_only;
}

void gates_editor_selection(const gates_tree_t *tree, gates_node_t editor, gates_u32 *anchor, gates_u32 *caret) {
    const struct gates_i_editor *e = ed_of(tree, editor);
    if (anchor != nullptr) *anchor = e != nullptr ? e->anchor : 0;
    if (caret != nullptr) *caret = e != nullptr ? e->caret : 0;
}

gates_err_t gates_editor_set_selection(gates_tree_t *tree, gates_node_t editor, gates_u32 anchor, gates_u32 caret) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr) return PROVEN_ERR_INVALID_ARG;
    e->anchor = snap(e, anchor);
    e->caret = snap(e, caret);
    e->pref_x = -1;
    e->run_open = false;
    keep_caret(tree, editor.index, e);
    gates_i_mark_dirty(tree, editor.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_editor_scroll_to(gates_tree_t *tree, gates_node_t editor, gates_u32 offset) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr) return PROVEN_ERR_INVALID_ARG;
    gates_u32 keep = e->caret;
    e->caret = snap(e, offset);
    keep_caret(tree, editor.index, e);
    e->caret = keep;
    gates_i_mark_dirty(tree, editor.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_u32 gates_editor_first_line(const gates_tree_t *tree, gates_node_t editor) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr) return 0;
    ed_geom g;
    geom_now(tree, editor.index, e, &g);
    return g.top;
}

gates_u32 gates_editor_visible_lines(const gates_tree_t *tree, gates_node_t editor) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr) return 0;
    ed_geom g;
    geom_now(tree, editor.index, e, &g);
    return g.visible;
}

/* -- measure and paint ------------------------------------------------------------------------ */

gates_size_t gates_i_editor_measure(const gates_tree_t *tree, const gates_node_slot_t *s,
                                    const gates_text_backend_t *text) {
    const struct gates_i_editor *e = gates_i_state(tree, s->state_index)->editor;
    gates_text_metrics_t m = text->metrics(text->ctx, gates_i_slot_font(tree, s));
    gates_i32 row = m.line_height > 0 ? m.line_height : 16;
    return (gates_size_t){ (gates_i32)e->cols * m.advance + 2 * ED_PAD + GATES_SCROLLBAR_PX + 2,
                           (gates_i32)e->rows * row + 2 * ED_PAD + 2 };
}

#define TRY_DRAW(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

/* One line's style bytes (none without a style table). */
static const gates_u8 *line_styles(const gates_tree_t *tree, struct gates_i_editor *e, gates_u32 line, gates_err_t *err) {
    if (e->nstyles == 0) return nullptr;
    gates_u32 s = gates_text_buffer_line_start(e->buf, line), n = gates_text_buffer_line_end(e->buf, line) - s;
    if (n > e->sscratch_cap) {
        gates_u32 cap = e->sscratch_cap < 256 ? 256 : e->sscratch_cap;
        while (cap < n) cap *= 2;
        proven_result_mem_mut_t r = e->sscratch == nullptr
                                        ? tree->alloc.alloc_fn(tree->alloc.ctx, cap, 1)
                                        : tree->alloc.realloc_fn(tree->alloc.ctx, e->sscratch, e->sscratch_cap, cap, 1);
        if (!proven_is_ok(r.err)) {
            *err = r.err;
            return nullptr;
        }
        e->sscratch = (gates_u8 *)r.value.ptr;
        e->sscratch_cap = cap;
    }
    gates_str_t a, b;
    gates_text_buffer_style_span(e->buf, s, s + n, &a, &b);
    if (a.size > 0) memcpy(e->sscratch, a.ptr, a.size);
    if (b.size > 0) memcpy(e->sscratch + a.size, b.ptr, b.size);
    return e->sscratch;
}

static gates_color_t style_color(const struct gates_i_editor *e, const gates_theme_t *theme, gates_u8 s,
                                 gates_color_t plain) {
    if (s == 0 || s >= e->nstyles) return plain;
    const gates_editor_style_t *st = &e->styles[s];
    return st->use_rgb ? st->rgb : gates_theme_color(theme, st->token);
}

#define ED_MAX_ROWS 512

gates_err_t gates_i_editor_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                 const gates_theme_t *theme, const gates_text_backend_t *text) {
    struct gates_i_editor *e = ed_at(tree, idx);
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    gates_i32 font = gates_i_font(tree, idx);
    gates_text_metrics_t m = text->metrics(text->ctx, font);
    bool focused = tree->focus == idx;
    bool inert = gates_i_widget_inert(tree, st);
    TRY_DRAW(gates_draw_rect(dl, r, gates_theme_color(theme, GATES_COLOR_CONTROL_BG)));
    ed_geom g;
    geom(tree, idx, e, m.line_height, &g);
    lay_t L = { text, font, g.text.w };
    gates_u32 rows = (gates_u32)((g.text.h + g.row_h - 1) / g.row_h);
    if (rows > ED_MAX_ROWS) rows = ED_MAX_ROWS;
    /* Highlighting follows the view: style what is about to show and is stale. */
    if (e->styler != nullptr) {
        gates_u32 last = g.top + rows < g.lines ? g.top + rows : g.lines - 1;
        gates_u32 need = gates_text_buffer_line_start(e->buf, last + 1);
        if (e->styled_end < need) {
            e->styler(e->styler_user, e->buf, e->styled_end, need);
            e->styled_end = need;
        }
    }
    gates_u32 sel_b = e->anchor < e->caret ? e->anchor : e->caret;
    gates_u32 sel_e = e->anchor < e->caret ? e->caret : e->anchor;
    gates_color_t fg = gates_theme_color(theme, inert ? GATES_COLOR_CONTROL_DISABLED_FG : GATES_COLOR_CONTROL_FG);
    gates_color_t sel_fg = gates_theme_color(theme, GATES_COLOR_SELECTION_FG);
    gates_color_t sel_bg = gates_theme_color(theme, GATES_COLOR_SELECTION_BG);
    gates_i32 space = text->glyph_advance != nullptr ? text->glyph_advance(text->ctx, font, ' ') : 8;
    st->caret_valid = false;
    gates_u32 numbers[ED_MAX_ROWS];  /* the line numbers to draw, and on which row */
    gates_u32 number_rows[ED_MAX_ROWS];
    gates_u32 nnumbers = 0;
    TRY_DRAW(gates_draw_clip_push(dl, g.text));
    dpos p = { g.top, g.top_row };
    gates_err_t err = GATES_OK;
    gates_str_t t = line_text(tree, e, p.line, &err);
    TRY_DRAW(err);
    const gates_u8 *sty = line_styles(tree, e, p.line, &err);
    TRY_DRAW(err);
    gates_u32 ls = gates_text_buffer_line_start(e->buf, p.line);
    gates_u32 rs, re;
    (void)row_range(&L, e, t, p.row, &rs, &re);
    for (gates_u32 k = 0; k < rows; k++) {
        gates_i32 y = g.text.y + (gates_i32)k * g.row_h;
        gates_i32 x0 = g.text.x - g.scroll_x;
        bool last_row = re >= t.size;
        gates_str_t u = { .ptr = t.ptr + rs, .size = re - rs };
        gates_u32 rb = ls + rs, rend = ls + re; /* the row's offsets */
        gates_i32 width = x_in(text, font, e, u, (gates_u32)u.size);
        if (!e->wrap && width + 1 > e->max_w) e->max_w = width + 1; /* the scroll extent grows as lines are seen */
        if (p.row == 0 && e->line_numbers) {
            numbers[nnumbers] = p.line + 1;
            number_rows[nnumbers++] = k;
        }
        /* Selection: its part of the row, and the line break when it is selected too. */
        gates_u32 a = sel_b > rb ? sel_b : rb, z = sel_e < rend ? sel_e : rend;
        bool brk = last_row && sel_b <= rend && sel_e > rend;
        if (sel_b < sel_e && (a < z || brk) && sel_b <= rend && sel_e >= rb) {
            gates_i32 sx0 = x_in(text, font, e, u, a > rb ? a - rb : 0);
            gates_i32 sx1 = brk ? width + space : x_in(text, font, e, u, z - rb);
            if (sx1 > sx0) TRY_DRAW(gates_draw_rect(dl, (gates_rect_t){ x0 + sx0, y, sx1 - sx0, g.row_h }, sel_bg));
        }
        /* Text in runs cut at tabs, selection edges, style changes and the
         * composition, which is drawn at the caret, underlined. */
        bool comp_here = e->pre_len > 0 && e->caret >= rb && (e->caret < rend || (last_row && e->caret == rend));
        bool pre_done = false;
        gates_i32 pre_w = 0, pre_cursor_x = 0;
        gates_i32 x = 0, run_x = 0;
        gates_u32 run_start = 0;
        for (gates_u32 i = 0; i <= u.size;) {
            gates_u32 cp = 0;
            gates_u32 next = i < u.size ? i + gates_text_decode(u, i, &cp) : i + 1;
            bool tab = i < u.size && cp == '\t';
            bool cut = i > run_start && i < u.size &&
                       ((sel_b < sel_e && (rb + i == sel_b || rb + i == sel_e)) ||
                        (sty != nullptr && sty[rs + i] != sty[rs + run_start]) || (comp_here && rb + i == e->caret));
            if ((i == u.size || tab || cut) && i > run_start) {
                bool in_sel = rb + run_start >= sel_b && rb + run_start < sel_e;
                gates_color_t c = inert ? fg : in_sel ? sel_fg : sty != nullptr ? style_color(e, theme, sty[rs + run_start], fg) : fg;
                gates_i32 rx = x0 + run_x;
                if (rx < g.text.x + g.text.w && rx + (x - run_x) > g.text.x) {
                    TRY_DRAW(gates_draw_text(dl, (gates_rect_t){ rx, y + (g.row_h - m.line_height) / 2, x - run_x,
                                                                 m.line_height },
                                             (gates_str_t){ .ptr = u.ptr + run_start, .size = i - run_start }, font, c));
                }
                run_start = i;
                run_x = x;
            }
            if (comp_here && !pre_done && rb + i == e->caret) {
                gates_str_t pre = { .ptr = e->pre, .size = e->pre_len };
                pre_w = gates_text_width(text, font, pre);
                pre_cursor_x = x + gates_text_width(text, font, (gates_str_t){ .ptr = e->pre, .size = e->ime_cursor });
                TRY_DRAW(gates_draw_text(dl, (gates_rect_t){ x0 + x, y + (g.row_h - m.line_height) / 2, pre_w, m.line_height },
                                         pre, font, fg));
                TRY_DRAW(gates_draw_rect(dl, (gates_rect_t){ x0 + x, y + g.row_h - 2, pre_w, 1 }, fg));
                x += pre_w;
                run_x = x;
                pre_done = true;
            }
            if (i == u.size) break;
            x += advance(text, font, e, x, cp);
            if (tab) {
                run_start = next;
                run_x = x;
            }
            i = next;
        }
        /* The caret: in this row, or at the line's end on its last row. */
        if (focused && !inert && e->caret >= rb && (e->caret < rend || (last_row && e->caret == rend))) {
            gates_i32 cx = comp_here ? pre_cursor_x : x_in(text, font, e, u, e->caret - rb);
            gates_rect_t caret = { x0 + cx, y, 1, g.row_h };
            if (!e->read_only) TRY_DRAW(gates_draw_rect(dl, caret, fg));
            st->caret_rect = caret;
            st->caret_valid = true;
        }
        /* The next row: in this line, or the next line's first. */
        if (!last_row) {
            p.row++;
            rs = re;
            re = row_end(&L, e, t, rs);
        } else if (p.line + 1 < g.lines) {
            p = (dpos){ p.line + 1, 0 };
            t = line_text(tree, e, p.line, &err);
            TRY_DRAW(err);
            sty = line_styles(tree, e, p.line, &err);
            TRY_DRAW(err);
            ls = gates_text_buffer_line_start(e->buf, p.line);
            rs = 0;
            re = row_end(&L, e, t, 0);
        } else {
            break;
        }
    }
    TRY_DRAW(gates_draw_clip_pop(dl));
    if (!gates_rect_is_empty(g.gutter)) {
        TRY_DRAW(gates_draw_rect(dl, g.gutter, gates_theme_color(theme, GATES_COLOR_PANEL_BG)));
        TRY_DRAW(gates_draw_clip_push(dl, g.gutter));
        gates_color_t nc = gates_theme_color(theme, GATES_COLOR_CONTROL_DISABLED_FG);
        for (gates_u32 i = 0; i < nnumbers; i++) {
            char buf[16];
            int n = snprintf(buf, sizeof buf, "%u", numbers[i]);
            gates_str_t num = { .ptr = (const gates_u8 *)buf, .size = (gates_usize_t)n };
            gates_i32 w = gates_text_width(text, font, num);
            TRY_DRAW(gates_draw_text(dl, (gates_rect_t){ g.gutter.x + g.gutter.w - ED_PAD - w,
                                                         g.text.y + (gates_i32)number_rows[i] * g.row_h +
                                                             (g.row_h - m.line_height) / 2,
                                                         w, m.line_height },
                                     num, font, nc));
        }
        TRY_DRAW(gates_draw_clip_pop(dl));
    }
    if (!gates_rect_is_empty(g.vtrack)) {
        TRY_DRAW(gates_draw_rect(dl, g.vtrack, gates_theme_color(theme, GATES_COLOR_PANEL_BG)));
        TRY_DRAW(gates_draw_rect(dl, g.vthumb, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
    }
    if (!gates_rect_is_empty(g.htrack)) {
        TRY_DRAW(gates_draw_rect(dl, g.htrack, gates_theme_color(theme, GATES_COLOR_PANEL_BG)));
        TRY_DRAW(gates_draw_rect(dl, g.hthumb, gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER)));
    }
    return gates_draw_border(dl, r, focused ? gates_theme_focus_width(theme) : 1,
                             gates_theme_color(theme, focused ? GATES_COLOR_FOCUS_RING : GATES_COLOR_CONTROL_BORDER));
}

/* -- keys --------------------------------------------------------------------------------------- */

/* Moves the caret (extend keeps the anchor); reports it when it moved. */
static void move_to(gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e, gates_u32 to, bool extend,
                    bool keep_pref) {
    gates_u32 old_c = e->caret, old_a = e->anchor;
    e->caret = snap(e, to);
    if (!extend) e->anchor = e->caret;
    if (!keep_pref) e->pref_x = -1;
    e->run_open = false;
    keep_caret(tree, idx, e);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    if ((e->caret != old_c || e->anchor != old_a) && gates_i_wants_events(tree, idx)) {
        gates_err_t err = gates_i_event_reserve(tree, 1, 0);
        if (gates_is_ok(err)) gates_i_event_push(tree, idx, GATES_EVENT_SELECTION_CHANGED, GATES_ORIGIN_USER);
        else tree->input_error = err;
    }
}

static gates_u32 home_of(struct gates_i_editor *e, gates_u32 off) {
    gates_u32 line = gates_text_buffer_line_of(e->buf, off);
    gates_u32 s = gates_text_buffer_line_start(e->buf, line), en = gates_text_buffer_line_end(e->buf, line);
    gates_u32 first = s;
    while (first < en && (byte_at(e, first) == ' ' || byte_at(e, first) == '\t')) first++;
    return off == first ? s : first; /* first non-blank, then the line start */
}

static void copy_out(gates_tree_t *tree, struct gates_i_editor *e) {
    gates_u32 b = e->anchor < e->caret ? e->anchor : e->caret, en = e->anchor < e->caret ? e->caret : e->anchor;
    if (!tree->has_clipboard || b == en) return;
    proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, en - b, 1);
    if (!proven_is_ok(r.err)) {
        tree->input_error = r.err;
        return;
    }
    gates_text_buffer_copy(e->buf, b, en, (gates_u8 *)r.value.ptr, en - b);
    gates_err_t err = tree->clipboard.set_text(tree->clipboard.ctx, (gates_str_t){ .ptr = r.value.ptr, .size = en - b });
    tree->alloc.free_fn(tree->alloc.ctx, r.value.ptr);
    if (!gates_is_ok(err)) tree->input_error = err;
}

static void paste_in(gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e) {
    if (!tree->has_clipboard) return;
    gates_u8 *raw = nullptr;
    gates_usize_t raw_len = 0;
    gates_err_t err = tree->clipboard.get_text(tree->clipboard.ctx, tree->alloc, &raw, &raw_len);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    gates_u8 *text = nullptr;
    gates_u32 len = 0;
    if (raw_len > 0) err = gates_i_paste_normalize(tree->alloc, (gates_str_t){ .ptr = raw, .size = raw_len }, true,
                                                   &text, &len);
    if (raw != nullptr) tree->alloc.free_fn(tree->alloc.ctx, raw);
    if (!gates_is_ok(err)) {
        tree->input_error = err;
        return;
    }
    if (len > 0) {
        gates_u32 b = e->anchor < e->caret ? e->anchor : e->caret, en = e->anchor < e->caret ? e->caret : e->anchor;
        e->run_open = false;
        (void)user_edit(tree, idx, e, b, en, (gates_str_t){ .ptr = text, .size = len }, RUN_OTHER);
    }
    if (text != nullptr) tree->alloc.free_fn(tree->alloc.ctx, text);
}

/* The line ending the caret's line uses ("\r\n" or "\n"). */
static gates_str_t line_break(struct gates_i_editor *e) {
    gates_u32 line = gates_text_buffer_line_of(e->buf, e->caret);
    gates_u32 en = gates_text_buffer_line_end(e->buf, line);
    bool crlf = byte_at(e, en) == '\r' || (line > 0 && byte_at(e, gates_text_buffer_line_start(e->buf, line) - 2) == '\r' &&
                                           gates_text_buffer_line_end(e->buf, line - 1) + 2 ==
                                               gates_text_buffer_line_start(e->buf, line));
    return crlf ? (gates_str_t){ .ptr = (const gates_u8 *)"\r\n", .size = 2 }
                : (gates_str_t){ .ptr = (const gates_u8 *)"\n", .size = 1 };
}

/* Tab / Shift+Tab over the selected lines (or the caret's line): a tab
 * before each non-empty line, or one tab or up to tab-width spaces taken
 * from each. One step in the history; the lines stay selected. */
static void block_indent(gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e, bool indent) {
    gates_u32 b = e->anchor < e->caret ? e->anchor : e->caret, en = e->anchor < e->caret ? e->caret : e->anchor;
    gates_u32 first = gates_text_buffer_line_of(e->buf, b), last = gates_text_buffer_line_of(e->buf, en);
    if (en > b && last > first && en == gates_text_buffer_line_start(e->buf, last)) last--;
    gates_u32 bs = gates_text_buffer_line_start(e->buf, first), be = gates_text_buffer_line_end(e->buf, last);
    gates_u32 n = be - bs, lines = last - first + 1;
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t ri = a.alloc_fn(a.ctx, n + 1, 1);
    if (!proven_is_ok(ri.err)) {
        tree->input_error = ri.err;
        return;
    }
    proven_result_mem_mut_t ro = a.alloc_fn(a.ctx, n + lines + 1, 1);
    if (!proven_is_ok(ro.err)) {
        a.free_fn(a.ctx, ri.value.ptr);
        tree->input_error = ro.err;
        return;
    }
    gates_u8 *in = (gates_u8 *)ri.value.ptr, *out = (gates_u8 *)ro.value.ptr;
    gates_text_buffer_copy(e->buf, bs, be, in, n);
    gates_u32 o = 0;
    for (gates_u32 i = 0; i <= n;) {
        gates_u32 q = i;
        while (q < n && in[q] != '\n') q++;
        gates_u32 from = i;
        bool empty = q == i || (q == i + 1 && in[i] == '\r');
        if (indent && !empty) {
            out[o++] = '\t';
        } else if (!indent) {
            if (from < q && in[from] == '\t') from++;
            else for (gates_u32 k = 0; k < e->tab_width && from < q && in[from] == ' '; k++) from++;
        }
        memcpy(out + o, in + from, q - from);
        o += q - from;
        if (q < n) out[o++] = '\n';
        i = q + 1;
    }
    if (o != n || memcmp(in, out, n) != 0) {
        e->run_open = false;
        gates_u32 caret_col = e->caret - bs;
        bool one_line = first == last && b == en;
        if (gates_is_ok(user_edit(tree, idx, e, bs, be, (gates_str_t){ .ptr = out, .size = o }, RUN_OTHER))) {
            if (one_line) { /* the caret keeps its place in the line */
                gates_i64 c = (gates_i64)caret_col + (gates_i64)o - (gates_i64)n;
                e->caret = e->anchor = bs + (gates_u32)(c < 0 ? 0 : c > (gates_i64)o ? (gates_i64)o : c);
            } else {
                e->anchor = bs;
                e->caret = bs + o;
            }
            keep_caret(tree, idx, e);
        }
    }
    a.free_fn(a.ctx, in);
    a.free_fn(a.ctx, out);
}

bool gates_i_editor_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr || ev->alt) return false;
    ed_geom g;
    geom_now(tree, idx, e, &g);
    gates_u32 b = e->anchor < e->caret ? e->anchor : e->caret, en = e->anchor < e->caret ? e->caret : e->anchor;
    bool sel = b < en;
    gates_u32 line = gates_text_buffer_line_of(e->buf, e->caret);
    switch (ev->key) {
    case GATES_KEY_LEFT:
        move_to(tree, idx, e, sel && !ev->shift ? b : ev->ctrl ? word_left(e, e->caret) : prev_cp(e, e->caret),
                ev->shift, false);
        return true;
    case GATES_KEY_RIGHT:
        move_to(tree, idx, e, sel && !ev->shift ? en : ev->ctrl ? word_right(e, e->caret) : next_cp(e, e->caret),
                ev->shift, false);
        return true;
    case GATES_KEY_UP:
    case GATES_KEY_DOWN:
    case GATES_KEY_PAGE_UP:
    case GATES_KEY_PAGE_DOWN: {
        if (ev->ctrl) return false;
        bool up = ev->key == GATES_KEY_UP || ev->key == GATES_KEY_PAGE_UP;
        gates_i64 step = ev->key == GATES_KEY_UP || ev->key == GATES_KEY_DOWN ? 1 : (gates_i64)g.visible;
        lay_t L = lay_of(tree, idx, &g);
        if (e->pref_x < 0) e->pref_x = caret_x(tree, &L, e, e->caret);
        dpos c = pos_of(tree, &L, e, e->caret);
        gates_i64 moved = 0;
        dpos target = advance_rows(tree, &L, e, c, up ? -step : step, &moved);
        gates_u32 to = moved == 0 ? (up ? 0 : len_of(e)) : at_row_x(tree, &L, e, target, e->pref_x);
        if (step > 1) { /* a page also moves the view by a page */
            dpos top = advance_rows(tree, &L, e, (dpos){ g.top, g.top_row }, up ? -step : step, nullptr);
            e->top = top.line;
            e->top_row = top.row;
        }
        move_to(tree, idx, e, to, ev->shift, moved != 0);
        return true;
    }
    case GATES_KEY_HOME:
        move_to(tree, idx, e, ev->ctrl ? 0 : home_of(e, e->caret), ev->shift, false);
        return true;
    case GATES_KEY_END:
        move_to(tree, idx, e, ev->ctrl ? len_of(e) : gates_text_buffer_line_end(e->buf, line), ev->shift, false);
        return true;
    case GATES_KEY_BACKSPACE:
    case GATES_KEY_DELETE: {
        if (e->read_only) return false;
        bool back = ev->key == GATES_KEY_BACKSPACE;
        run_t run = back ? RUN_DEL_BACK : RUN_DEL_FWD;
        if (!sel) {
            if (back) b = ev->ctrl ? word_left(e, e->caret) : prev_cp(e, e->caret);
            else en = ev->ctrl ? word_right(e, e->caret) : next_cp(e, e->caret);
            if (ev->ctrl) run = RUN_OTHER;
        } else {
            run = RUN_OTHER;
        }
        if (run == RUN_OTHER) e->run_open = false;
        if (b < en) (void)user_edit(tree, idx, e, b, en, (gates_str_t){0}, run);
        return true;
    }
    case GATES_KEY_ENTER: {
        if (e->read_only || ev->ctrl) return false;
        e->run_open = false;
        gates_str_t brk = line_break(e);
        gates_u8 nl[258];
        gates_u32 n = (gates_u32)brk.size;
        memcpy(nl, brk.ptr, n);
        if (e->auto_indent) { /* the line's leading blanks, up to where the new line starts */
            for (gates_u32 i = gates_text_buffer_line_start(e->buf, gates_text_buffer_line_of(e->buf, b));
                 i < b && n < sizeof nl && (byte_at(e, i) == ' ' || byte_at(e, i) == '\t'); i++) {
                nl[n++] = byte_at(e, i);
            }
        }
        (void)user_edit(tree, idx, e, b, en, (gates_str_t){ .ptr = nl, .size = n }, RUN_OTHER);
        return true;
    }
    case GATES_KEY_TAB:
        if (!e->tab_inserts || e->read_only) return false; /* focus moves */
        if (ev->ctrl) return gates_tree_focus_next(tree, ev->shift); /* the way out when Tab types */
        if (ev->shift || (sel && gates_text_buffer_line_of(e->buf, b) != gates_text_buffer_line_of(e->buf, en))) {
            block_indent(tree, idx, e, !ev->shift);
        } else {
            (void)user_edit(tree, idx, e, b, en, (gates_str_t){ .ptr = (const gates_u8 *)"\t", .size = 1 }, RUN_TYPE);
        }
        return true;
    case GATES_KEY_A:
        if (!ev->ctrl) return false;
        e->anchor = 0;
        move_to(tree, idx, e, len_of(e), true, false);
        return true;
    case GATES_KEY_C:
        if (!ev->ctrl) return false;
        copy_out(tree, e);
        return true;
    case GATES_KEY_X:
        if (!ev->ctrl || e->read_only) return false; /* as the text box: a shortcut may have it */
        if (sel) {
            gates_err_t before = tree->input_error;
            tree->input_error = GATES_OK;
            copy_out(tree, e);
            bool copied = gates_is_ok(tree->input_error) && tree->has_clipboard;
            if (gates_is_ok(tree->input_error)) tree->input_error = before;
            if (copied) {
                e->run_open = false;
                (void)user_edit(tree, idx, e, b, en, (gates_str_t){0}, RUN_OTHER);
            }
        }
        return true;
    case GATES_KEY_V:
        if (!ev->ctrl) return false;
        if (!e->read_only) paste_in(tree, idx, e);
        return true;
    case GATES_KEY_Z:
    case GATES_KEY_Y: {
        if (!ev->ctrl) return false;
        if (e->read_only) return true;
        bool redo = ev->key == GATES_KEY_Y || ev->shift;
        gates_err_t err = replay(tree, idx, e, redo, true);
        if (!gates_is_ok(err) && err != PROVEN_ERR_INVALID_STATE) tree->input_error = err;
        return true;
    }
    default:
        return false;
    }
}

/* A typed character (control characters never arrive here). */
bool gates_i_editor_char(gates_tree_t *tree, gates_u32 idx, gates_str_t utf8) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr || e->read_only) return false;
    gates_u32 b = e->anchor < e->caret ? e->anchor : e->caret, en = e->anchor < e->caret ? e->caret : e->anchor;
    if (b < en) e->run_open = false; /* typing over a selection starts a new step */
    return gates_is_ok(user_edit(tree, idx, e, b, en, utf8, RUN_TYPE));
}

/* -- pointer ------------------------------------------------------------------------------------ */

/* The offset under a point in the text area (clamped to the rows there are). */
static gates_u32 offset_at(gates_tree_t *tree, gates_u32 idx, struct gates_i_editor *e, const ed_geom *g, gates_point_t p) {
    gates_i64 row = p.y < g->text.y ? -1 : (p.y - g->text.y) / g->row_h;
    lay_t L = lay_of(tree, idx, g);
    gates_i64 moved = 0;
    dpos d = advance_rows(tree, &L, e, (dpos){ g->top, g->top_row }, row, &moved);
    if (moved < row) return len_of(e); /* below the last row */
    return at_row_x(tree, &L, e, d, p.x - g->text.x + g->scroll_x);
}

bool gates_i_editor_press(gates_tree_t *tree, gates_u32 idx, gates_point_t p, gates_u32 clicks, bool shift) {
    struct gates_i_editor *e = ed_at(tree, idx);
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
    if (e == nullptr || gates_i_widget_inert(tree, st)) return false;
    gates_tree_set_focus(tree, gates_i_handle(tree, idx));
    ed_geom g;
    geom_now(tree, idx, e, &g);
    if (gates_rect_contains(g.vthumb, p)) {
        tree->drag_kind = GATES_DRAG_EDITOR_VTHUMB;
        tree->drag_node = idx;
        tree->drag_start = p;
        e->drag_top = g.top;
        return true;
    }
    if (gates_rect_contains(g.vtrack, p)) {
        lay_t L = lay_of(tree, idx, &g);
        dpos top = advance_rows(tree, &L, e, (dpos){ g.top, g.top_row },
                                p.y < g.vthumb.y ? -(gates_i64)g.visible : (gates_i64)g.visible, nullptr);
        e->top = top.line;
        e->top_row = top.row;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
        return true;
    }
    if (gates_rect_contains(g.hthumb, p)) {
        tree->drag_kind = GATES_DRAG_EDITOR_HTHUMB;
        tree->drag_node = idx;
        tree->drag_start = p;
        e->drag_x = g.scroll_x;
        return true;
    }
    if (gates_rect_contains(g.htrack, p)) {
        gates_i32 sx = p.x < g.hthumb.x ? g.scroll_x - g.text.w : g.scroll_x + g.text.w;
        e->scroll_x = sx < 0 ? 0 : sx > g.max_x ? g.max_x : sx;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
        return true;
    }
    gates_u32 off = offset_at(tree, idx, e, &g, p);
    if (clicks >= 2) {
        /* A word (or the run of blanks or punctuation) under the pointer. */
        gates_u32 len = len_of(e);
        gates_u32 wb = off, we = off;
        if (off < len && cls_at(e, off) != CL_BREAK) {
            cls_t c = cls_at(e, off);
            while (wb > 0 && cls_at(e, prev_cp(e, wb)) == c) wb = prev_cp(e, wb);
            while (we < len && cls_at(e, we) == c) we = next_cp(e, we);
        }
        e->anchor = wb;
        move_to(tree, idx, e, we, true, false);
        return true;
    }
    move_to(tree, idx, e, off, shift, false);
    tree->drag_kind = GATES_DRAG_EDITOR_SELECT;
    tree->drag_node = idx;
    return true;
}

void gates_i_editor_drag(gates_tree_t *tree, gates_point_t p) {
    gates_u32 idx = tree->drag_node;
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr) return;
    ed_geom g;
    geom_now(tree, idx, e, &g);
    if (tree->drag_kind == GATES_DRAG_EDITOR_SELECT) {
        move_to(tree, idx, e, offset_at(tree, idx, e, &g, p), true, false); /* scrolls at the edges */
        return;
    }
    if (tree->drag_kind == GATES_DRAG_EDITOR_VTHUMB) {
        gates_i32 travel = g.vtrack.h - g.vthumb.h;
        if (travel <= 0) return;
        gates_i64 start = g.max_top > 0 ? (gates_i64)travel * e->drag_top / g.max_top : 0;
        gates_i64 pos = start + (p.y - tree->drag_start.y);
        if (pos < 0) pos = 0;
        if (pos > travel) pos = travel;
        e->top = (gates_u32)((gates_i64)g.max_top * pos / travel);
        e->top_row = 0;
    } else {
        gates_i32 travel = g.htrack.w - g.hthumb.w;
        if (travel <= 0) return;
        gates_i64 start = g.max_x > 0 ? (gates_i64)travel * e->drag_x / g.max_x : 0;
        gates_i64 pos = start + (p.x - tree->drag_start.x);
        if (pos < 0) pos = 0;
        if (pos > travel) pos = travel;
        e->scroll_x = (gates_i32)((gates_i64)g.max_x * pos / travel);
    }
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
}

bool gates_i_editor_wheel(gates_tree_t *tree, gates_u32 idx, gates_vec2_t wheel) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr) return false;
    ed_geom g;
    geom_now(tree, idx, e, &g);
    if (wheel.y != 0.0f) {
        lay_t L = lay_of(tree, idx, &g);
        dpos top = advance_rows(tree, &L, e, (dpos){ g.top, g.top_row }, -(gates_i64)(wheel.y * (float)ED_WHEEL_LINES),
                                nullptr);
        e->top = top.line;
        e->top_row = top.row;
    }
    if (wheel.x != 0.0f) {
        gates_i32 sx = g.scroll_x + (gates_i32)(wheel.x * (float)(4 * (tree->advance > 0 ? tree->advance : 8)));
        e->scroll_x = sx < 0 ? 0 : sx > g.max_x ? g.max_x : sx;
    }
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    return true;
}

/* -- highlighting, marks, find (stage 3) -------------------------------------------------------- */

gates_err_t gates_editor_set_styles(gates_tree_t *tree, gates_node_t editor, const gates_editor_style_t *styles,
                                    gates_u32 count) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr || count > 256 || (count > 0 && styles == nullptr)) return PROVEN_ERR_INVALID_ARG;
    if (count > 0) memcpy(e->styles, styles, (gates_usize_t)count * sizeof *styles);
    e->nstyles = count;
    gates_i_mark_dirty(tree, editor.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_editor_set_styler(gates_tree_t *tree, gates_node_t editor, gates_editor_styler_fn fn, void *user) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr) return PROVEN_ERR_INVALID_ARG;
    e->styler = fn;
    e->styler_user = fn != nullptr ? user : nullptr;
    e->styled_end = 0;
    gates_i_mark_dirty(tree, editor.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_editor_set_style(gates_tree_t *tree, gates_node_t editor, gates_u32 begin, gates_u32 end,
                                   gates_u8 style) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr || begin > end || end > len_of(e)) return PROVEN_ERR_INVALID_ARG;
    gates_text_buffer_set_style(e->buf, begin, end, style);
    gates_i_mark_dirty(tree, editor.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_editor_mark_add(gates_tree_t *tree, gates_node_t editor, gates_u32 offset, gates_mark_gravity_t gravity,
                                  gates_mark_id_t *out) {
    struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr ? gates_text_buffer_mark_add(e->buf, offset, gravity, out) : PROVEN_ERR_INVALID_ARG;
}

gates_err_t gates_editor_mark_remove(gates_tree_t *tree, gates_node_t editor, gates_mark_id_t id) {
    struct gates_i_editor *e = ed_of(tree, editor);
    return e != nullptr ? gates_text_buffer_mark_remove(e->buf, id) : PROVEN_ERR_INVALID_ARG;
}

bool gates_editor_find(gates_tree_t *tree, gates_node_t editor, gates_str_t needle, gates_u32 flags, bool wrap_around) {
    struct gates_i_editor *e = ed_of(tree, editor);
    if (e == nullptr || needle.size == 0 || needle.ptr == nullptr) return false;
    bool back = (flags & GATES_FIND_BACKWARD) != 0;
    gates_u32 b = e->anchor < e->caret ? e->anchor : e->caret, en = e->anchor < e->caret ? e->caret : e->anchor;
    gates_u32 at = 0;
    bool found = gates_text_buffer_find(e->buf, back ? b : en, needle, flags, &at);
    if (!found && wrap_around) found = gates_text_buffer_find(e->buf, back ? len_of(e) : 0, needle, flags, &at);
    if (!found) return false;
    e->anchor = at;
    e->caret = at + (gates_u32)needle.size;
    e->pref_x = -1;
    e->run_open = false;
    keep_caret(tree, editor.index, e);
    gates_i_mark_dirty(tree, editor.index, GATES_DIRTY_PAINT);
    return true;
}

/* -- input method and accessibility (stage 4) ------------------------------------------------------ */

bool gates_i_editor_composing(const gates_widget_state_t *st) {
    return st != nullptr && st->editor != nullptr && st->editor->pre_len > 0;
}

gates_str_t gates_i_editor_preedit(const gates_widget_state_t *st) {
    if (st == nullptr || st->editor == nullptr) return (gates_str_t){0};
    return (gates_str_t){ .ptr = st->editor->pre, .size = st->editor->pre_len };
}

static void pre_announce(gates_tree_t *tree, gates_u32 idx) {
    gates_i_event_try_push(tree, idx, GATES_EVENT_PREEDIT_CHANGED, 0);
}

/* The composition shown at the caret (empty ends it). PERMISSION when read-only. */
gates_err_t gates_i_editor_set_preedit(gates_tree_t *tree, gates_u32 idx, gates_str_t text, gates_u32 cursor) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr || e->read_only) return PROVEN_ERR_PERMISSION;
    if (text.size > e->pre_cap) {
        gates_u32 cap = e->pre_cap < 32 ? 32 : e->pre_cap;
        while (cap < text.size) cap *= 2;
        proven_result_mem_mut_t r = e->pre == nullptr ? tree->alloc.alloc_fn(tree->alloc.ctx, cap, 1)
                                                      : tree->alloc.realloc_fn(tree->alloc.ctx, e->pre, e->pre_cap, cap, 1);
        if (!proven_is_ok(r.err)) return r.err; /* the previous composition stays */
        e->pre = (gates_u8 *)r.value.ptr;
        e->pre_cap = cap;
    }
    if (text.size > 0) memcpy(e->pre, text.ptr, text.size);
    e->pre_len = (gates_u32)text.size;
    if (cursor > text.size) cursor = (gates_u32)text.size;
    while (cursor > 0 && cursor < text.size && (text.ptr[cursor] & 0xC0) == 0x80) cursor--;
    e->ime_cursor = cursor;
    e->run_open = false;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    pre_announce(tree, idx);
    return GATES_OK;
}

/* The input method's result replaces the selection as one step; the composition ends. */
gates_err_t gates_i_editor_commit(gates_tree_t *tree, gates_u32 idx, gates_str_t text) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr || e->read_only) return PROVEN_ERR_PERMISSION;
    bool had = e->pre_len > 0;
    if (text.size > 0) {
        if (!utf8_ok(text)) return PROVEN_ERR_INVALID_ARG;
        gates_u32 b = e->anchor < e->caret ? e->anchor : e->caret, en = e->anchor < e->caret ? e->caret : e->anchor;
        e->run_open = false;
        gates_err_t err = user_edit(tree, idx, e, b, en, text, RUN_OTHER);
        if (!gates_is_ok(err) && err != PROVEN_ERR_OUT_OF_BOUNDS) return err; /* text, selection, composition kept */
    }
    e->pre_len = 0;
    e->ime_cursor = 0;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    if (had) pre_announce(tree, idx);
    return GATES_OK;
}

/* A composition cancelled, or dropped when focus leaves (no text changes). */
void gates_i_editor_preedit_cancel(gates_tree_t *tree, gates_u32 idx) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr || e->pre_len == 0) return;
    e->pre_len = 0;
    e->ime_cursor = 0;
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    pre_announce(tree, idx);
}

void gates_i_editor_blur(gates_tree_t *tree, gates_u32 idx) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr) return;
    e->run_open = false;
    gates_i_editor_preedit_cancel(tree, idx);
}

/* One rectangle per shown row of [start, end) (an empty range: a zero-width one at its place). */
gates_u32 gates_i_editor_text_rects(gates_tree_t *tree, gates_u32 idx, gates_u32 start, gates_u32 end, gates_rect_t *out,
                                    gates_u32 cap) {
    struct gates_i_editor *e = ed_at(tree, idx);
    const gates_text_backend_t *be = backend(tree);
    if (e == nullptr || be == nullptr || out == nullptr || cap == 0) return 0;
    gates_u32 len = len_of(e);
    if (end > len) end = len;
    if (start > end) start = end;
    ed_geom g;
    geom_now(tree, idx, e, &g);
    lay_t L = lay_of(tree, idx, &g);
    gates_u32 n = 0;
    dpos p = { g.top, g.top_row };
    for (gates_u32 k = 0; k < g.visible && n < cap; k++) {
        gates_str_t t = line_text(tree, e, p.line, nullptr);
        gates_u32 rs, re;
        (void)row_range(&L, e, t, p.row, &rs, &re);
        gates_u32 ls = gates_text_buffer_line_start(e->buf, p.line), rb = ls + rs, rend = ls + re;
        bool last_row = re >= t.size;
        gates_str_t u = { .ptr = t.ptr + rs, .size = re - rs };
        gates_u32 a = start > rb ? start : rb, z = end < rend ? end : rend;
        bool here = start == end ? (start >= rb && (start < rend || (last_row && start == rend))) : a < z;
        if (here) {
            gates_i32 x0 = g.text.x - g.scroll_x + x_in(be, L.font, e, u, a - rb);
            gates_i32 x1 = g.text.x - g.scroll_x + x_in(be, L.font, e, u, z - rb); /* z == a when empty */
            gates_rect_t r = { x0, g.text.y + (gates_i32)k * g.row_h, x1 - x0, g.row_h };
            gates_rect_t c = gates_rect_intersect(r, g.text);
            if (start == end && r.x >= g.text.x && r.x <= g.text.x + g.text.w) c = (gates_rect_t){ r.x, r.y, 0, r.h };
            if (!gates_rect_is_empty(c) || start == end) out[n++] = c;
        }
        gates_i64 moved = 0;
        p = advance_rows(tree, &L, e, p, 1, &moved);
        if (moved == 0) break;
    }
    return n;
}

gates_u32 gates_i_editor_offset_at_point(gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr) return 0;
    ed_geom g;
    geom_now(tree, idx, e, &g);
    return offset_at(tree, idx, e, &g, p);
}

/* Assistive technology sets the whole text: a person's edit (undoable, reported). */
gates_err_t gates_i_editor_user_set(gates_tree_t *tree, gates_u32 idx, gates_str_t text) {
    struct gates_i_editor *e = ed_at(tree, idx);
    if (e == nullptr || (text.size > 0 && text.ptr == nullptr) || !utf8_ok(text)) return PROVEN_ERR_INVALID_ARG;
    e->run_open = false;
    return user_edit(tree, idx, e, 0, len_of(e), text, RUN_OTHER); /* PERMISSION when read-only */
}
