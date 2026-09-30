/* gates_gui_lib - text buffer (0.7.0): a gap buffer of bytes with a
 * parallel gap buffer of style bytes, a line-start index in a gap array with
 * a pending shift, and marks. Platform-free. */
#include <gates/text_buffer.h>
#include <proven/heap.h>

#include <string.h>

typedef struct mark_t {
    gates_mark_id_t id;          /* 0 = free slot */
    gates_u32 offset;
    gates_mark_gravity_t gravity;
} mark_t;

struct gates_text_buffer {
    gates_allocator_t alloc;
    /* Bytes and styles share one gap: [gap, gap + gap_len) is unused. */
    gates_u8 *text;
    gates_u8 *style;
    gates_u32 cap, gap, gap_len;
    /* Line starts (line 0 starts at 0), in a gap array. Entries after `step`
     * still owe `delta` (unsigned arithmetic wraps: the sums are exact). */
    gates_u32 *ls;
    gates_u32 ls_cap, ls_gap, ls_gap_len;
    gates_u32 nlines;
    gates_u32 step;
    gates_u32 delta;
    mark_t *marks;
    gates_u32 nmarks, mark_cap;
    gates_mark_id_t next_mark;
};

/* -- allocation ------------------------------------------------------------------ */

static void *grow(gates_allocator_t a, void *p, gates_usize_t old_bytes, gates_usize_t new_bytes, gates_usize_t align,
                  gates_err_t *err) {
    proven_result_mem_mut_t r = p == nullptr ? a.alloc_fn(a.ctx, new_bytes, align)
                                             : a.realloc_fn(a.ctx, p, old_bytes, new_bytes, align);
    *err = r.err;
    return proven_is_ok(r.err) ? r.value.ptr : nullptr;
}

gates_err_t gates_text_buffer_create(gates_allocator_t alloc, gates_text_buffer_t **out) {
    if (out == nullptr) return PROVEN_ERR_INVALID_ARG;
    *out = nullptr;
    if (alloc.alloc_fn == nullptr) alloc = proven_heap_allocator();
    proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, sizeof(gates_text_buffer_t), alignof(gates_text_buffer_t));
    if (!proven_is_ok(r.err)) return r.err;
    gates_text_buffer_t *b = (gates_text_buffer_t *)r.value.ptr;
    memset(b, 0, sizeof *b);
    b->alloc = alloc;
    b->next_mark = 1;
    gates_err_t err = GATES_OK;
    b->ls = grow(alloc, nullptr, 0, 16 * sizeof(gates_u32), alignof(gates_u32), &err);
    if (!gates_is_ok(err)) {
        alloc.free_fn(alloc.ctx, b);
        return err;
    }
    b->ls_cap = 16;
    b->ls[0] = 0;
    b->nlines = 1;
    b->ls_gap = 1;
    b->ls_gap_len = 15;
    *out = b;
    return GATES_OK;
}

void gates_text_buffer_destroy(gates_text_buffer_t *b) {
    if (b == nullptr) return;
    gates_allocator_t a = b->alloc;
    if (b->text != nullptr) a.free_fn(a.ctx, b->text);
    if (b->style != nullptr) a.free_fn(a.ctx, b->style);
    if (b->ls != nullptr) a.free_fn(a.ctx, b->ls);
    if (b->marks != nullptr) a.free_fn(a.ctx, b->marks);
    a.free_fn(a.ctx, b);
}

gates_u32 gates_text_buffer_length(const gates_text_buffer_t *b) {
    return b != nullptr ? b->cap - b->gap_len : 0;
}

/* -- bytes --------------------------------------------------------------------------- */

static gates_u32 phys(const gates_text_buffer_t *b, gates_u32 i) {
    return i < b->gap ? i : i + b->gap_len;
}

static void move_gap(gates_text_buffer_t *b, gates_u32 at) {
    if (at < b->gap) {
        gates_u32 n = b->gap - at;
        memmove(b->text + at + b->gap_len, b->text + at, n);
        memmove(b->style + at + b->gap_len, b->style + at, n);
    } else if (at > b->gap) {
        gates_u32 n = at - b->gap;
        memmove(b->text + b->gap, b->text + b->gap + b->gap_len, n);
        memmove(b->style + b->gap, b->style + b->gap + b->gap_len, n);
    }
    b->gap = at;
}

/* Room for `need` more bytes in the gap (the gap is kept where it is). */
static gates_err_t reserve_bytes(gates_text_buffer_t *b, gates_u32 need) {
    if (need <= b->gap_len) return GATES_OK;
    gates_u32 len = b->cap - b->gap_len;
    gates_u64 want = (gates_u64)len + need;
    gates_u64 nc = b->cap < 64 ? 64 : b->cap;
    while (nc < want + 64) nc *= 2;
    if (nc > (gates_u64)GATES_TEXT_BUFFER_MAX + 4096) nc = want + 64;
    gates_err_t err = GATES_OK;
    gates_u8 *t = grow(b->alloc, b->text, b->cap, (gates_usize_t)nc, 1, &err);
    if (!gates_is_ok(err)) return err;
    b->text = t;
    gates_u8 *s = grow(b->alloc, b->style, b->cap, (gates_usize_t)nc, 1, &err);
    if (!gates_is_ok(err)) return err; /* text grew: harmless, the gap is still right */
    b->style = s;
    /* Move the tail to the new end. */
    gates_u32 tail = b->cap - b->gap - b->gap_len;
    memmove(b->text + nc - tail, b->text + b->gap + b->gap_len, tail);
    memmove(b->style + nc - tail, b->style + b->gap + b->gap_len, tail);
    b->gap_len += (gates_u32)nc - b->cap;
    b->cap = (gates_u32)nc;
    return GATES_OK;
}

gates_u8 gates_text_buffer_byte(const gates_text_buffer_t *b, gates_u32 offset) {
    return b != nullptr && offset < gates_text_buffer_length(b) ? b->text[phys(b, offset)] : 0;
}

static void spans(const gates_text_buffer_t *b, const gates_u8 *base, gates_u32 begin, gates_u32 end,
                  gates_str_t *first, gates_str_t *second) {
    gates_u32 len = gates_text_buffer_length(b);
    if (end > len) end = len;
    if (begin > end) begin = end;
    gates_str_t none = { .ptr = nullptr, .size = 0 };
    *first = none;
    *second = none;
    if (begin == end) return;
    if (end <= b->gap) {
        *first = (gates_str_t){ .ptr = base + begin, .size = end - begin };
    } else if (begin >= b->gap) {
        *first = (gates_str_t){ .ptr = base + begin + b->gap_len, .size = end - begin };
    } else {
        *first = (gates_str_t){ .ptr = base + begin, .size = b->gap - begin };
        *second = (gates_str_t){ .ptr = base + b->gap + b->gap_len, .size = end - b->gap };
    }
}

void gates_text_buffer_span(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_str_t *first,
                            gates_str_t *second) {
    if (b == nullptr || first == nullptr || second == nullptr) return;
    spans(b, b->text, begin, end, first, second);
}

void gates_text_buffer_style_span(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_str_t *first,
                                  gates_str_t *second) {
    if (b == nullptr || first == nullptr || second == nullptr) return;
    spans(b, b->style, begin, end, first, second);
}

gates_u32 gates_text_buffer_copy(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_u8 *out,
                                 gates_u32 cap) {
    if (b == nullptr || out == nullptr) return 0;
    gates_str_t a, c;
    spans(b, b->text, begin, end, &a, &c);
    gates_u32 n = 0;
    gates_u32 na = a.size < cap ? (gates_u32)a.size : cap;
    if (na > 0) memcpy(out, a.ptr, na);
    n = na;
    gates_u32 nc = c.size < cap - n ? (gates_u32)c.size : cap - n;
    if (nc > 0) memcpy(out + n, c.ptr, nc);
    return n + nc;
}

gates_str_t gates_text_buffer_contiguous(gates_text_buffer_t *b) {
    if (b == nullptr || b->text == nullptr) return (gates_str_t){0};
    move_gap(b, gates_text_buffer_length(b));
    return (gates_str_t){ .ptr = b->text, .size = b->gap };
}

/* -- line index ------------------------------------------------------------------------ */

static gates_u32 ls_phys(const gates_text_buffer_t *b, gates_u32 i) {
    return i < b->ls_gap ? i : i + b->ls_gap_len;
}

static gates_u32 ls_get(const gates_text_buffer_t *b, gates_u32 i) {
    gates_u32 v = b->ls[ls_phys(b, i)];
    return i > b->step ? v + b->delta : v;
}

/* Entries (step, to] take their pending shift: step moves up to `to`. */
static void apply_step(gates_text_buffer_t *b, gates_u32 to) {
    for (gates_u32 i = b->step + 1; i <= to && i < b->nlines; i++) b->ls[ls_phys(b, i)] += b->delta;
    b->step = to;
}

/* Entries (to, step] give their shift back: step moves down to `to`. */
static void back_step(gates_text_buffer_t *b, gates_u32 to) {
    for (gates_u32 i = to + 1; i <= b->step && i < b->nlines; i++) b->ls[ls_phys(b, i)] -= b->delta;
    b->step = to;
}

/* Puts the step at `line` so that exactly the entries after it owe `delta`. */
static void step_to(gates_text_buffer_t *b, gates_u32 line) {
    if (b->delta == 0) {
        b->step = line;
    } else if (line >= b->step) {
        apply_step(b, line);
    } else {
        back_step(b, line);
    }
}

static void ls_move_gap(gates_text_buffer_t *b, gates_u32 at) {
    if (at < b->ls_gap) {
        memmove(b->ls + at + b->ls_gap_len, b->ls + at, (gates_usize_t)(b->ls_gap - at) * sizeof(gates_u32));
    } else if (at > b->ls_gap) {
        memmove(b->ls + b->ls_gap, b->ls + b->ls_gap + b->ls_gap_len, (gates_usize_t)(at - b->ls_gap) * sizeof(gates_u32));
    }
    b->ls_gap = at;
}

static gates_err_t ls_reserve(gates_text_buffer_t *b, gates_u32 need) {
    if (need <= b->ls_gap_len) return GATES_OK;
    gates_u32 nc = b->ls_cap * 2;
    while (nc - b->nlines < need + 16) nc *= 2;
    gates_err_t err = GATES_OK;
    gates_u32 *p = grow(b->alloc, b->ls, (gates_usize_t)b->ls_cap * sizeof(gates_u32), (gates_usize_t)nc * sizeof(gates_u32),
                        alignof(gates_u32), &err);
    if (!gates_is_ok(err)) return err;
    b->ls = p;
    gates_u32 tail = b->ls_cap - b->ls_gap - b->ls_gap_len;
    memmove(b->ls + nc - tail, b->ls + b->ls_gap + b->ls_gap_len, (gates_usize_t)tail * sizeof(gates_u32));
    b->ls_gap_len += nc - b->ls_cap;
    b->ls_cap = nc;
    return GATES_OK;
}

gates_u32 gates_text_buffer_line_count(const gates_text_buffer_t *b) {
    return b != nullptr ? b->nlines : 0;
}

gates_u32 gates_text_buffer_line_start(const gates_text_buffer_t *b, gates_u32 line) {
    if (b == nullptr) return 0;
    return line < b->nlines ? ls_get(b, line) : gates_text_buffer_length(b);
}

gates_u32 gates_text_buffer_line_end(const gates_text_buffer_t *b, gates_u32 line) {
    if (b == nullptr) return 0;
    if (line + 1 >= b->nlines) return gates_text_buffer_length(b);
    gates_u32 e = ls_get(b, line + 1) - 1; /* the "\n" */
    if (e > ls_get(b, line) && gates_text_buffer_byte(b, e - 1) == '\r') e--;
    return e;
}

gates_u32 gates_text_buffer_line_of(const gates_text_buffer_t *b, gates_u32 offset) {
    if (b == nullptr) return 0;
    gates_u32 lo = 0, hi = b->nlines - 1;
    while (lo < hi) {
        gates_u32 mid = lo + (hi - lo + 1) / 2;
        if (ls_get(b, mid) <= offset) lo = mid; else hi = mid - 1;
    }
    return lo;
}

/* -- editing ---------------------------------------------------------------------------- */

static void move_marks(gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_u32 inserted) {
    for (gates_u32 i = 0; i < b->nmarks; i++) {
        mark_t *m = &b->marks[i];
        if (m->id == 0 || m->offset < begin) continue;
        if (m->offset > end) {
            m->offset = m->offset - (end - begin) + inserted;
        } else {
            m->offset = begin + (m->gravity == GATES_MARK_RIGHT ? inserted : 0);
        }
    }
}

gates_err_t gates_text_buffer_replace(gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_str_t text) {
    gates_u32 len = gates_text_buffer_length(b);
    if (b == nullptr || begin > end || end > len || (text.size > 0 && text.ptr == nullptr)) return PROVEN_ERR_INVALID_ARG;
    if ((gates_u64)len - (end - begin) + text.size > GATES_TEXT_BUFFER_MAX) return PROVEN_ERR_OUT_OF_BOUNDS;
    gates_u32 k = (gates_u32)text.size;
    gates_u32 newlines = 0;
    for (gates_u32 i = 0; i < k; i++) newlines += text.ptr[i] == '\n' ? 1u : 0u;
    /* Everything that can fail comes first. */
    gates_err_t err = reserve_bytes(b, k > (end - begin) ? k - (end - begin) : 0);
    if (gates_is_ok(err)) err = ls_reserve(b, newlines);
    if (!gates_is_ok(err)) return err;

    /* Lines: starts in (begin, end] go (their "\n" is deleted), later ones shift. */
    gates_u32 line = gates_text_buffer_line_of(b, begin);
    step_to(b, line);
    gates_u32 gone = 0;
    while (line + 1 + gone < b->nlines && ls_get(b, line + 1 + gone) <= end) gone++;
    if (gone > 0) {
        ls_move_gap(b, line + 1);
        b->ls_gap_len += gone;
        b->nlines -= gone;
    }
    b->delta += k - (end - begin);

    /* Bytes: delete [begin, end), insert at begin. */
    move_gap(b, end);
    b->gap -= end - begin;
    b->gap_len += end - begin;
    if (k > 0) {
        memcpy(b->text + b->gap, text.ptr, k);
        memset(b->style + b->gap, 0, k);
        b->gap += k;
        b->gap_len -= k;
    }

    /* New starts after the inserted "\n"s (absolute values, stored minus the pending shift). */
    if (newlines > 0) {
        ls_move_gap(b, line + 1);
        for (gates_u32 i = 0; i < k; i++) {
            if (text.ptr[i] != '\n') continue;
            b->ls[b->ls_gap++] = begin + i + 1 - b->delta; /* after the step: it owes delta */
            b->ls_gap_len--;
            b->nlines++;
        }
    }
    move_marks(b, begin, end, k);
    return GATES_OK;
}

gates_err_t gates_text_buffer_set_text(gates_text_buffer_t *b, gates_str_t text) {
    return gates_text_buffer_replace(b, 0, gates_text_buffer_length(b), text);
}

/* -- styles ------------------------------------------------------------------------------ */

gates_u8 gates_text_buffer_style(const gates_text_buffer_t *b, gates_u32 offset) {
    return b != nullptr && offset < gates_text_buffer_length(b) ? b->style[phys(b, offset)] : 0;
}

void gates_text_buffer_set_style(gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_u8 style) {
    if (b == nullptr) return;
    gates_u32 len = gates_text_buffer_length(b);
    if (end > len) end = len;
    for (gates_u32 i = begin; i < end; i++) b->style[phys(b, i)] = style;
}

/* -- marks ------------------------------------------------------------------------------- */

static mark_t *find_mark(const gates_text_buffer_t *b, gates_mark_id_t id) {
    for (gates_u32 i = 0; b != nullptr && id != 0 && i < b->nmarks; i++) {
        if (b->marks[i].id == id) return &b->marks[i];
    }
    return nullptr;
}

gates_err_t gates_text_buffer_mark_add(gates_text_buffer_t *b, gates_u32 offset, gates_mark_gravity_t gravity,
                                       gates_mark_id_t *out) {
    if (b == nullptr || out == nullptr || offset > gates_text_buffer_length(b) ||
        (gravity != GATES_MARK_LEFT && gravity != GATES_MARK_RIGHT)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    mark_t *slot = nullptr;
    for (gates_u32 i = 0; i < b->nmarks && slot == nullptr; i++) {
        if (b->marks[i].id == 0) slot = &b->marks[i];
    }
    if (slot == nullptr) {
        if (b->nmarks == b->mark_cap) {
            gates_u32 nc = b->mark_cap == 0 ? 8 : b->mark_cap * 2;
            gates_err_t err = GATES_OK;
            mark_t *m = grow(b->alloc, b->marks, (gates_usize_t)b->mark_cap * sizeof(mark_t), (gates_usize_t)nc * sizeof(mark_t),
                             alignof(mark_t), &err);
            if (!gates_is_ok(err)) return err;
            b->marks = m;
            b->mark_cap = nc;
        }
        slot = &b->marks[b->nmarks++];
    }
    *slot = (mark_t){ .id = b->next_mark++, .offset = offset, .gravity = gravity };
    *out = slot->id;
    return GATES_OK;
}

gates_err_t gates_text_buffer_mark_remove(gates_text_buffer_t *b, gates_mark_id_t id) {
    mark_t *m = find_mark(b, id);
    if (m == nullptr) return PROVEN_ERR_NOT_FOUND;
    m->id = 0;
    return GATES_OK;
}

gates_err_t gates_text_buffer_mark_set(gates_text_buffer_t *b, gates_mark_id_t id, gates_u32 offset) {
    mark_t *m = find_mark(b, id);
    if (m == nullptr) return PROVEN_ERR_NOT_FOUND;
    if (offset > gates_text_buffer_length(b)) return PROVEN_ERR_INVALID_ARG;
    m->offset = offset;
    return GATES_OK;
}

bool gates_text_buffer_mark_offset(const gates_text_buffer_t *b, gates_mark_id_t id, gates_u32 *offset) {
    const mark_t *m = find_mark(b, id);
    if (m == nullptr || offset == nullptr) return false;
    *offset = m->offset;
    return true;
}

/* -- find --------------------------------------------------------------------------------- */

static gates_u8 fold(gates_u8 c, bool ignore_case) {
    return ignore_case && c >= 'A' && c <= 'Z' ? (gates_u8)(c + 32) : c;
}

static bool match_at(const gates_text_buffer_t *b, gates_u32 at, gates_str_t needle, bool ic) {
    for (gates_usize_t i = 0; i < needle.size; i++) {
        if (fold(b->text[phys(b, at + (gates_u32)i)], ic) != fold(needle.ptr[i], ic)) return false;
    }
    return true;
}

bool gates_text_buffer_find(const gates_text_buffer_t *b, gates_u32 from, gates_str_t needle, gates_u32 flags,
                            gates_u32 *match_begin) {
    if (b == nullptr || match_begin == nullptr || needle.size == 0 || needle.ptr == nullptr) return false;
    gates_u32 len = gates_text_buffer_length(b);
    if (needle.size > len) return false;
    gates_u32 last = len - (gates_u32)needle.size; /* the last possible start */
    bool ic = (flags & GATES_FIND_IGNORE_CASE) != 0;
    if ((flags & GATES_FIND_BACKWARD) != 0) {
        if (from < needle.size) return false;
        gates_u32 start = from - (gates_u32)needle.size < last ? from - (gates_u32)needle.size : last;
        for (gates_u32 i = start + 1; i-- > 0;) {
            if (match_at(b, i, needle, ic)) { *match_begin = i; return true; }
        }
        return false;
    }
    for (gates_u32 i = from; i <= last; i++) {
        if (match_at(b, i, needle, ic)) { *match_begin = i; return true; }
    }
    return false;
}
