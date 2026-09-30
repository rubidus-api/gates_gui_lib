/* the text buffer (0.7.0) - a gap buffer with a lazily
 * shifted line index, style bytes, marks and find, checked against a plain
 * model after every edit of long random runs, plus the edges: edits at 0 and
 * at the end, ranges over several lines, "\r\n" handling, gravity, finding
 * across the gap, and allocation failure leaving the buffer unchanged. */
#include <gates/text_buffer.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <stdlib.h>
#include <string.h>

/* -- the plain model ---------------------------------------------------------------- */

#define MODEL_CAP 20000

typedef struct model_t {
    gates_u8 text[MODEL_CAP];
    gates_u8 style[MODEL_CAP];
    gates_u32 len;
    struct { gates_mark_id_t id; gates_u32 off; gates_mark_gravity_t g; bool live; } marks[16];
    int nmarks;
} model_t;

static void m_replace(model_t *m, gates_u32 b, gates_u32 e, const gates_u8 *t, gates_u32 k) {
    memmove(m->text + b + k, m->text + e, m->len - e);
    memmove(m->style + b + k, m->style + e, m->len - e);
    memcpy(m->text + b, t, k);
    memset(m->style + b, 0, k);
    m->len = m->len - (e - b) + k;
    for (int i = 0; i < m->nmarks; i++) {
        if (!m->marks[i].live || m->marks[i].off < b) continue;
        if (m->marks[i].off > e) m->marks[i].off = m->marks[i].off - (e - b) + k;
        else m->marks[i].off = b + (m->marks[i].g == GATES_MARK_RIGHT ? k : 0);
    }
}

static gates_u32 m_lines(const model_t *m) {
    gates_u32 n = 1;
    for (gates_u32 i = 0; i < m->len; i++) n += m->text[i] == '\n';
    return n;
}

static gates_u32 m_line_start(const model_t *m, gates_u32 line) {
    if (line == 0) return 0;
    gates_u32 seen = 0;
    for (gates_u32 i = 0; i < m->len; i++) {
        if (m->text[i] == '\n' && ++seen == line) return i + 1;
    }
    return m->len;
}



/* Everything the buffer says agrees with the model. */
static void agree(const gates_text_buffer_t *b, const model_t *m) {
    GT_ASSERT(gates_text_buffer_length(b) == m->len);
    static gates_u8 copy[MODEL_CAP];
    GT_ASSERT(gates_text_buffer_copy(b, 0, m->len, copy, MODEL_CAP) == m->len);
    GT_ASSERT(memcmp(copy, m->text, m->len) == 0);
    gates_str_t s1, s2;
    gates_text_buffer_style_span(b, 0, m->len, &s1, &s2);
    GT_ASSERT(s1.size + s2.size == m->len);
    GT_ASSERT(s1.size == 0 || memcmp(s1.ptr, m->style, s1.size) == 0);
    GT_ASSERT(s2.size == 0 || memcmp(s2.ptr, m->style + s1.size, s2.size) == 0);
    gates_u32 lines = gates_text_buffer_line_count(b);
    GT_ASSERT(lines == m_lines(m));
    GT_ASSERT(gates_text_buffer_line_start(b, 0) == 0);
    gates_u32 prev = 0;
    for (gates_u32 l = 0; l < lines; l++) {
        gates_u32 s = gates_text_buffer_line_start(b, l);
        GT_ASSERT(s == m_line_start(m, l));
        GT_ASSERT(l == 0 || s > prev);
        GT_ASSERT(s <= m->len);
        GT_ASSERT(gates_text_buffer_line_of(b, s) == l);
        if (s > 0) GT_ASSERT(gates_text_buffer_line_of(b, s - 1) == l - 1);
        gates_u32 e = gates_text_buffer_line_end(b, l);
        GT_ASSERT(e >= s && e <= m->len);
        if (l + 1 < lines) {
            gates_u32 next = gates_text_buffer_line_start(b, l + 1);
            GT_ASSERT(m->text[next - 1] == '\n');
            GT_ASSERT(e == next - 1 - (next - 1 > s && m->text[next - 2] == '\r' ? 1u : 0u));
        } else {
            GT_ASSERT(e == m->len);
        }
        prev = s;
    }
    GT_ASSERT(gates_text_buffer_line_of(b, m->len) == lines - 1);
    GT_ASSERT(gates_text_buffer_line_of(b, m->len + 10) == lines - 1);
    GT_ASSERT(gates_text_buffer_line_start(b, lines) == m->len);
    for (int i = 0; i < m->nmarks; i++) {
        gates_u32 off = 0;
        GT_ASSERT(gates_text_buffer_mark_offset(b, m->marks[i].id, &off) == m->marks[i].live);
        if (m->marks[i].live) GT_ASSERT(off == m->marks[i].off);
    }
}

/* -- randomized runs ------------------------------------------------------------------ */

static gates_u32 seed = 12345;
static gates_u32 rnd(gates_u32 n) {
    seed = seed * 1103515245u + 12345u;
    return n == 0 ? 0 : (seed >> 8) % n;
}

static const char alphabet[] = "ab\ncd\r\nxy\n\n\rZ ";

static void random_run(gates_u32 steps, gates_u32 max_insert, bool local) {
    gates_text_buffer_t *b = nullptr;
    GT_ASSERT_OK(gates_text_buffer_create((gates_allocator_t){0}, &b));
    static model_t m;
    memset(&m, 0, sizeof m);
    int failed = gt_fail;
    gates_u32 cursor = 0;
    for (gates_u32 step = 0; step < steps && gt_fail == failed; step++) {
        gates_u32 op = rnd(10);
        if (op < 6) {
            /* replace a range (often near the last edit, sometimes anywhere) */
            gates_u32 at = local && rnd(4) != 0 ? (cursor + rnd(9) > 4 ? cursor + rnd(9) - 4 : 0) : rnd(m.len + 1);
            if (at > m.len) at = m.len;
            gates_u32 del = rnd(4) == 0 ? rnd(m.len - at + 1) : rnd(3);
            if (at + del > m.len) del = m.len - at;
            gates_u8 ins[64];
            gates_u32 k = rnd(max_insert + 1);
            if (m.len - del + k >= MODEL_CAP) k = 0;
            for (gates_u32 i = 0; i < k; i++) ins[i] = (gates_u8)alphabet[rnd(sizeof alphabet - 1)];
            GT_ASSERT_OK(gates_text_buffer_replace(b, at, at + del, (gates_str_t){ .ptr = ins, .size = k }));
            m_replace(&m, at, at + del, ins, k);
            cursor = at + k;
        } else if (op == 6 && m.len > 0) {
            gates_u32 s = rnd(m.len), e = s + rnd(m.len - s + 1);
            gates_u8 st = (gates_u8)(1 + rnd(250));
            gates_text_buffer_set_style(b, s, e, st);
            memset(m.style + s, st, e - s);
        } else if (op == 7 && m.nmarks < 16) {
            gates_u32 off = rnd(m.len + 1);
            gates_mark_gravity_t g = rnd(2) ? GATES_MARK_RIGHT : GATES_MARK_LEFT;
            gates_mark_id_t id = 0;
            GT_ASSERT_OK(gates_text_buffer_mark_add(b, off, g, &id));
            m.marks[m.nmarks].id = id;
            m.marks[m.nmarks].off = off;
            m.marks[m.nmarks].g = g;
            m.marks[m.nmarks].live = true;
            m.nmarks++;
        } else if (op == 8 && m.nmarks > 0) {
            int i = (int)rnd((gates_u32)m.nmarks);
            if (m.marks[i].live && rnd(2)) {
                GT_ASSERT_OK(gates_text_buffer_mark_remove(b, m.marks[i].id));
                m.marks[i].live = false;
            } else if (m.marks[i].live) {
                gates_u32 off = rnd(m.len + 1);
                GT_ASSERT_OK(gates_text_buffer_mark_set(b, m.marks[i].id, off));
                m.marks[i].off = off;
            }
        } else if (op == 9 && m.len > 0) {
            /* find a piece of the text, forward and backward, case-insensitively too */
            gates_u32 s = rnd(m.len), n = 1 + rnd(m.len - s < 4 ? m.len - s : 4);
            gates_u8 needle[8];
            memcpy(needle, m.text + s, n);
            gates_u32 from = rnd(m.len + 1), got = 0;
            gates_u32 want = UINT32_MAX;
            for (gates_u32 i = from; i + n <= m.len && want == UINT32_MAX; i++) {
                if (memcmp(m.text + i, needle, n) == 0) want = i;
            }
            bool f = gates_text_buffer_find(b, from, (gates_str_t){ needle, n }, 0, &got);
            GT_ASSERT(f == (want != UINT32_MAX) && (!f || got == want));
            want = UINT32_MAX;
            for (gates_u32 i = from >= n ? from - n + 1 : 0; i-- > 0 && want == UINT32_MAX;) {
                if (i + n <= m.len && memcmp(m.text + i, needle, n) == 0) want = i;
            }
            f = gates_text_buffer_find(b, from, (gates_str_t){ needle, n }, GATES_FIND_BACKWARD, &got);
            GT_ASSERT(f == (want != UINT32_MAX) && (!f || got == want));
        }
        agree(b, &m);
    }
    /* One span after asking for it; the lines still agree. */
    gates_str_t all = gates_text_buffer_contiguous(b);
    GT_ASSERT(all.size == m.len && (m.len == 0 || memcmp(all.ptr, m.text, m.len) == 0));
    agree(b, &m);
    gates_text_buffer_destroy(b);
}

/* -- edges -------------------------------------------------------------------------------- */

static bool text_is(const gates_text_buffer_t *b, const char *z) {
    gates_u8 buf[256];
    gates_u32 n = gates_text_buffer_copy(b, 0, gates_text_buffer_length(b), buf, sizeof buf);
    return n == strlen(z) && memcmp(buf, z, n) == 0;
}

static void test_edges(void) {
    gates_text_buffer_t *b = nullptr;
    GT_ASSERT(gates_text_buffer_create((gates_allocator_t){0}, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_text_buffer_create((gates_allocator_t){0}, &b));
    /* Empty: one line, no bytes. */
    GT_ASSERT(gates_text_buffer_length(b) == 0 && gates_text_buffer_line_count(b) == 1);
    GT_ASSERT(gates_text_buffer_line_start(b, 0) == 0 && gates_text_buffer_line_end(b, 0) == 0);
    GT_ASSERT(gates_text_buffer_byte(b, 0) == 0 && gates_text_buffer_style(b, 0) == 0);
    gates_str_t c = gates_text_buffer_contiguous(b);
    GT_ASSERT(c.size == 0);
    /* Bad ranges change nothing. */
    GT_ASSERT(gates_text_buffer_replace(b, 1, 1, GATES_STR("x")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_text_buffer_replace(b, 0, 0, (gates_str_t){ .ptr = nullptr, .size = 2 }) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_text_buffer_replace(nullptr, 0, 0, GATES_STR("x")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_text_buffer_set_text(b, GATES_STR("one\r\ntwo\nthree")));
    GT_ASSERT(gates_text_buffer_replace(b, 5, 3, GATES_STR("x")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(text_is(b, "one\r\ntwo\nthree"));
    /* "\r\n" belongs to the line end; a lone "\r" is text. */
    GT_ASSERT(gates_text_buffer_line_count(b) == 3);
    GT_ASSERT(gates_text_buffer_line_end(b, 0) == 3 && gates_text_buffer_line_start(b, 1) == 5);
    GT_ASSERT_OK(gates_text_buffer_replace(b, 4, 4, GATES_STR("\n"))); /* between \r and \n */
    GT_ASSERT(text_is(b, "one\r\n\ntwo\nthree") && gates_text_buffer_line_count(b) == 4);
    GT_ASSERT(gates_text_buffer_line_end(b, 0) == 3 && gates_text_buffer_line_end(b, 1) == 5);
    GT_ASSERT_OK(gates_text_buffer_set_text(b, GATES_STR("a\rb")));
    GT_ASSERT(gates_text_buffer_line_count(b) == 1 && gates_text_buffer_line_end(b, 0) == 3);
    /* A deletion over several lines takes their starts with it. */
    GT_ASSERT_OK(gates_text_buffer_set_text(b, GATES_STR("1\n2\n3\n4\n5")));
    GT_ASSERT_OK(gates_text_buffer_replace(b, 1, 7, GATES_STR("")));
    GT_ASSERT(text_is(b, "1\n5") && gates_text_buffer_line_count(b) == 2);
    /* Edits at 0 and at the end; a text ending in "\n" has an empty last line. */
    GT_ASSERT_OK(gates_text_buffer_replace(b, 0, 0, GATES_STR("\n")));
    GT_ASSERT_OK(gates_text_buffer_replace(b, gates_text_buffer_length(b), gates_text_buffer_length(b), GATES_STR("\n")));
    GT_ASSERT(text_is(b, "\n1\n5\n") && gates_text_buffer_line_count(b) == 4);
    GT_ASSERT(gates_text_buffer_line_start(b, 3) == 5 && gates_text_buffer_line_end(b, 3) == 5);
    /* Spans cross the gap as two pieces. */
    GT_ASSERT_OK(gates_text_buffer_set_text(b, GATES_STR("abcdef")));
    GT_ASSERT_OK(gates_text_buffer_replace(b, 3, 3, GATES_STR("")));
    GT_ASSERT_OK(gates_text_buffer_replace(b, 3, 3, GATES_STR("X")));
    gates_str_t s1, s2;
    gates_text_buffer_span(b, 1, 6, &s1, &s2);
    GT_ASSERT(s1.size + s2.size == 5 && s2.size > 0);
    gates_u8 joined[8];
    memcpy(joined, s1.ptr, s1.size);
    memcpy(joined + s1.size, s2.ptr, s2.size);
    GT_ASSERT(memcmp(joined, "bcXde", 5) == 0);
    gates_text_buffer_span(b, 5, 2, &s1, &s2);
    GT_ASSERT(s1.size == 0 && s2.size == 0);
    gates_u8 small[3];
    GT_ASSERT(gates_text_buffer_copy(b, 0, 7, small, 3) == 3 && memcmp(small, "abc", 3) == 0);
    /* Find: across the gap, backward, ignoring ASCII case. */
    gates_u32 at = 0;
    GT_ASSERT(gates_text_buffer_find(b, 0, GATES_STR("cXd"), 0, &at) && at == 2);
    GT_ASSERT(!gates_text_buffer_find(b, 3, GATES_STR("cXd"), 0, &at));
    GT_ASSERT(gates_text_buffer_find(b, 7, GATES_STR("xd"), GATES_FIND_BACKWARD | GATES_FIND_IGNORE_CASE, &at) && at == 3);
    GT_ASSERT(!gates_text_buffer_find(b, 4, GATES_STR("xd"), GATES_FIND_BACKWARD | GATES_FIND_IGNORE_CASE, &at));
    GT_ASSERT(!gates_text_buffer_find(b, 0, GATES_STR("XD"), 0, &at));
    GT_ASSERT(!gates_text_buffer_find(b, 0, GATES_STR(""), 0, &at));
    GT_ASSERT(!gates_text_buffer_find(b, 0, GATES_STR("abcXdefg"), 0, &at));
    /* Marks: gravity at an insertion, a deletion that contains one, unknown ids. */
    gates_mark_id_t l = 0, r = 0, inside = 0;
    GT_ASSERT_OK(gates_text_buffer_mark_add(b, 3, GATES_MARK_LEFT, &l));
    GT_ASSERT_OK(gates_text_buffer_mark_add(b, 3, GATES_MARK_RIGHT, &r));
    GT_ASSERT_OK(gates_text_buffer_mark_add(b, 5, GATES_MARK_LEFT, &inside));
    GT_ASSERT(gates_text_buffer_mark_add(b, 99, GATES_MARK_LEFT, &l) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_text_buffer_mark_add(b, 0, (gates_mark_gravity_t)7, &l) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_text_buffer_replace(b, 3, 3, GATES_STR("++")));
    gates_u32 o = 0;
    GT_ASSERT(gates_text_buffer_mark_offset(b, l, &o) && o == 3);
    GT_ASSERT(gates_text_buffer_mark_offset(b, r, &o) && o == 5);
    GT_ASSERT_OK(gates_text_buffer_replace(b, 6, 8, GATES_STR(""))); /* contains `inside` (now at 7) */
    GT_ASSERT(gates_text_buffer_mark_offset(b, inside, &o) && o == 6);
    GT_ASSERT_OK(gates_text_buffer_mark_remove(b, inside));
    GT_ASSERT(gates_text_buffer_mark_remove(b, inside) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(!gates_text_buffer_mark_offset(b, inside, &o));
    GT_ASSERT(gates_text_buffer_mark_set(b, inside, 0) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(gates_text_buffer_mark_set(b, l, 999) == PROVEN_ERR_INVALID_ARG);
    gates_mark_id_t again = 0;
    GT_ASSERT_OK(gates_text_buffer_mark_add(b, 0, GATES_MARK_LEFT, &again));
    GT_ASSERT(again != inside && again > r); /* ids are never reused */
    /* Styles: set, read, inserted text is plain, clamped past the end. */
    GT_ASSERT_OK(gates_text_buffer_set_text(b, GATES_STR("styled")));
    gates_text_buffer_set_style(b, 1, 99, 7);
    GT_ASSERT(gates_text_buffer_style(b, 0) == 0 && gates_text_buffer_style(b, 5) == 7);
    GT_ASSERT_OK(gates_text_buffer_replace(b, 3, 3, GATES_STR("!")));
    GT_ASSERT(gates_text_buffer_style(b, 3) == 0 && gates_text_buffer_style(b, 4) == 7);
    gates_text_buffer_destroy(b);
    gates_text_buffer_destroy(nullptr);
    /* Nulls. */
    GT_ASSERT(gates_text_buffer_length(nullptr) == 0 && gates_text_buffer_line_count(nullptr) == 0);
    GT_ASSERT(gates_text_buffer_copy(nullptr, 0, 1, small, 1) == 0);
}

/* A long text: many lines, edits near the end and far from each other. */
static void test_large(void) {
    gates_text_buffer_t *b = nullptr;
    GT_ASSERT_OK(gates_text_buffer_create((gates_allocator_t){0}, &b));
    for (int i = 0; i < 20000; i++) {
        GT_ASSERT_OK(gates_text_buffer_replace(b, gates_text_buffer_length(b), gates_text_buffer_length(b), GATES_STR("line of text\n")));
    }
    GT_ASSERT(gates_text_buffer_line_count(b) == 20001);
    GT_ASSERT(gates_text_buffer_line_start(b, 12345) == 12345u * 13u);
    GT_ASSERT_OK(gates_text_buffer_replace(b, 13, 13, GATES_STR("\n\n")));
    GT_ASSERT(gates_text_buffer_line_count(b) == 20003 && gates_text_buffer_line_start(b, 12347) == 12345u * 13u + 2u);
    GT_ASSERT(gates_text_buffer_line_of(b, 12345u * 13u + 2u) == 12347);
    GT_ASSERT_OK(gates_text_buffer_replace(b, gates_text_buffer_length(b) - 13, gates_text_buffer_length(b), GATES_STR("")));
    GT_ASSERT(gates_text_buffer_line_count(b) == 20002);
    GT_ASSERT_OK(gates_text_buffer_replace(b, 0, 13, GATES_STR("")));
    GT_ASSERT(gates_text_buffer_line_start(b, 1) == 1 && gates_text_buffer_line_start(b, 2) == 2);
    GT_ASSERT(gates_text_buffer_line_start(b, 20000) == gates_text_buffer_length(b));
    gates_text_buffer_destroy(b);
}

/* -- allocation failure: nothing changes ---------------------------------------------------- */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    int left;
} fail_alloc_t;
static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (f->left > 0) f->left--;
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}
static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left == 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    if (f->left > 0) f->left--;
    return f->inner.realloc_fn(f->inner.ctx, p, os, ns, align);
}
static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_failures(void) {
    for (int k = 0; k < 12; k++) {
        fail_alloc_t f = { .inner = proven_heap_allocator(), .left = k };
        gates_allocator_t al = { .ctx = &f, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
        gates_text_buffer_t *b = nullptr;
        gates_err_t err = gates_text_buffer_create(al, &b);
        if (!gates_is_ok(err)) {
            GT_ASSERT(err == PROVEN_ERR_NOMEM && b == nullptr);
            continue;
        }
        static model_t m;
        memset(&m, 0, sizeof m);
        gates_mark_id_t id = 0;
        if (gates_is_ok(gates_text_buffer_mark_add(b, 0, GATES_MARK_RIGHT, &id))) {
            m.marks[0].id = id;
            m.marks[0].g = GATES_MARK_RIGHT;
            m.marks[0].live = true;
            m.nmarks = 1;
        }
        for (int i = 0; i < 60; i++) {
            const char *piece = i % 3 == 0 ? "many\nlines\nhere\n" : "text ";
            gates_u32 at = (gates_u32)(i * 7) % (m.len + 1);
            gates_err_t e = gates_text_buffer_replace(b, at, at, (gates_str_t){ (const gates_u8 *)piece, strlen(piece) });
            GT_ASSERT(gates_is_ok(e) || e == PROVEN_ERR_NOMEM);
            if (gates_is_ok(e)) m_replace(&m, at, at, (const gates_u8 *)piece, (gates_u32)strlen(piece));
            agree(b, &m); /* a failure changed nothing */
        }
        gates_text_buffer_destroy(b);
    }
}

int main(void) {

    test_edges();
    test_large();
    random_run(4000, 6, true);
    random_run(3000, 40, false);
    random_run(2000, 1, true);
    test_failures();
    return gt_report("test_text_buffer");
}
