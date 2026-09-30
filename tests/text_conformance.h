/* Backend conformance checks required by RFC-0002 section 8 and RFC-0004.
 *
 * Header-only so the identical assertions can run against the builtin backend
 * on the host (T016) and against the Win32 GDI backend inside a Windows
 * console executable (T017). A backend that passes these may be swapped in
 * without changing any layout. */
#ifndef GATES_TEXT_CONFORMANCE_H
#define GATES_TEXT_CONFORMANCE_H

#include <gates/text.h>
#include "gates_test.h"

#include <string.h>

#define GTC_W 160
#define GTC_H 48

static gates_u32 gtc_buf[GTC_H][GTC_W];

static gates_pixels_t gtc_target(void) {
    return (gates_pixels_t){ .ptr = &gtc_buf[0][0], .w = GTC_W, .h = GTC_H,
                             .stride_bytes = GTC_W * 4u };
}

static void gtc_clear(void) { memset(gtc_buf, 0, sizeof gtc_buf); }

static int gtc_ink_outside(gates_rect_t allowed) {
    int n = 0;
    for (int y = 0; y < GTC_H; y++) {
        for (int x = 0; x < GTC_W; x++) {
            if (gtc_buf[y][x] != 0 &&
                !gates_rect_contains(allowed, (gates_point_t){ x, y })) {
                n++;
            }
        }
    }
    return n;
}

static int gtc_ink_total(void) {
    int n = 0;
    for (int y = 0; y < GTC_H; y++) {
        for (int x = 0; x < GTC_W; x++) {
            if (gtc_buf[y][x] != 0) n++;
        }
    }
    return n;
}

static gates_str_t gtc_str(const char *s) {
    return (gates_str_t){ .ptr = (const proven_byte_t *)s, .size = strlen(s) };
}

/* Bounding box of all ink; empty when nothing was drawn. */
static gates_rect_t gtc_ink_bounds(void) {
    gates_i32 x0 = GTC_W, y0 = GTC_H, x1 = -1, y1 = -1;
    for (int y = 0; y < GTC_H; y++) {
        for (int x = 0; x < GTC_W; x++) {
            if (gtc_buf[y][x] == 0) {
                continue;
            }
            if (x < x0) x0 = x;
            if (y < y0) y0 = y;
            if (x > x1) x1 = x;
            if (y > y1) y1 = y;
        }
    }
    if (x1 < x0 || y1 < y0) {
        return (gates_rect_t){ 0, 0, 0, 0 };
    }
    return (gates_rect_t){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
}

/* Runs the whole RFC-0002 section 8 checklist against `be`. */
static gates_i32 gtc_width(const gates_text_backend_t *be, gates_font_t f, const char *s) {
    return gates_text_width(be, f, gtc_str(s));
}

/* Every check, for one font (RFC-0004: advances, not cells). */
static inline void gates_text_conformance_font(const gates_text_backend_t *be, gates_font_t f) {
    /* 1. Metrics coherence. */
    gates_text_metrics_t m = be->metrics(be->ctx, f);
    GT_ASSERT(m.advance > 0);
    GT_ASSERT(m.ascent > 0);
    GT_ASSERT(m.descent >= 0);
    GT_ASSERT(m.line_height >= m.ascent + m.descent);

    /* 2. Empty string measures to zero width, one line tall. */
    gates_size_t empty = be->measure(be->ctx, f, (gates_str_t){0});
    GT_ASSERT(empty.w == 0 && empty.h == m.line_height);

    /* 3. measure == the sum of the code points' advances for every sample,
     *    every advance is positive for visible characters, and the shared
     *    cell rule still counts the same cells on every backend. */
    static const struct { const char *text; gates_u32 cells; } samples[] = {
        { "a", 1 }, { "hello", 5 }, { "  ", 2 },
        { "\xed\x95\x9c", 2 }, { "\xed\x95\x9c\xea\xb8\x80", 4 }, { "a\xed\x95\x9c" "b", 4 },
        { "\xe6\xbc\xa2\xe5\xad\x97", 4 }, { "abc\xed\x95\x9c\xea\xb8\x80xyz", 10 },
    };
    for (unsigned i = 0; i < sizeof samples / sizeof *samples; i++) {
        gates_str_t s = gtc_str(samples[i].text);
        GT_ASSERT(gates_text_cells(s) == samples[i].cells);
        gates_size_t sz = be->measure(be->ctx, f, s);
        GT_ASSERT(sz.w == gates_text_width(be, f, s));
        GT_ASSERT(sz.w > 0);
        GT_ASSERT(sz.h == m.line_height);
    }
    GT_ASSERT(be->glyph_advance(be->ctx, f, 'M') > 0 && be->glyph_advance(be->ctx, f, 0xD55C) > 0);

    /* 4. Ink stays inside the assigned rect. */
    gates_rect_t rect = { 10, 8, gtc_width(be, f, "Ag\xed\x95\x9c\xea\xb8\x80"), m.line_height };
    gates_rect_t full = { 0, 0, GTC_W, GTC_H };
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, f, gtc_str("Ag\xed\x95\x9c\xea\xb8\x80"),
             GATES_RGB(255, 255, 255));
    GT_ASSERT(gtc_ink_total() > 0);
    GT_ASSERT(gtc_ink_outside(rect) == 0);

    /* 4b. Ink follows the rect. Moving the assigned rect must move the ink by
     *     exactly the same delta, and a single glyph must land inside its own
     *     advance - this catches a backend that draws at a fixed origin. */
    gates_i32 adv_m = be->glyph_advance(be->ctx, f, 'M');
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, f, gtc_str("M"), GATES_RGB(255, 255, 255));
    gates_rect_t ink_a = gtc_ink_bounds();
    GT_ASSERT(!gates_rect_is_empty(ink_a));
    GT_ASSERT(ink_a.x >= rect.x && ink_a.x + ink_a.w <= rect.x + adv_m);

    gates_rect_t moved = { rect.x + 23, rect.y + 5, rect.w, rect.h };
    gtc_clear();
    be->draw(be->ctx, gtc_target(), moved, full, f, gtc_str("M"), GATES_RGB(255, 255, 255));
    gates_rect_t ink_b = gtc_ink_bounds();
    GT_ASSERT(!gates_rect_is_empty(ink_b));
    GT_ASSERT(ink_b.x - ink_a.x == 23);
    GT_ASSERT(ink_b.y - ink_a.y == 5);
    GT_ASSERT(ink_b.w == ink_a.w && ink_b.h == ink_a.h);

    /* 4c. Placement: the second glyph of a run sits exactly the first one's
     *     advance to the right, and a wide glyph pushes the next one by its own. */
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, f, gtc_str(" M"), GATES_RGB(255, 255, 255));
    gates_rect_t ink_second = gtc_ink_bounds();
    GT_ASSERT(!gates_rect_is_empty(ink_second));
    GT_ASSERT(ink_second.x - ink_a.x == be->glyph_advance(be->ctx, f, ' '));

    gates_i32 adv_wide = be->glyph_advance(be->ctx, f, 0xD55C);
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, f, gtc_str("\xed\x95\x9cM"), GATES_RGB(255, 255, 255));
    gates_rect_t ink_after_wide = gtc_ink_bounds();
    GT_ASSERT(!gates_rect_is_empty(ink_after_wide));
    /* The 'M' lives right after the wide glyph's advance. */
    GT_ASSERT(ink_after_wide.x + ink_after_wide.w > rect.x + adv_wide);
    GT_ASSERT(ink_after_wide.x + ink_after_wide.w <= rect.x + adv_wide + adv_m);

    /* 5. Clip discipline: a narrow clip keeps ink inside it, an empty clip
     *    produces none at all. */
    gtc_clear();
    gates_rect_t narrow = { rect.x, rect.y, adv_m, m.line_height };
    be->draw(be->ctx, gtc_target(), rect, narrow, f, gtc_str("MMMM"), GATES_RGB(255, 255, 255));
    GT_ASSERT(gtc_ink_outside(narrow) == 0);

    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, (gates_rect_t){ 0, 0, 0, 0 }, f, gtc_str("MMMM"),
             GATES_RGB(255, 255, 255));
    GT_ASSERT(gtc_ink_total() == 0);
}

static inline void gates_text_conformance(const gates_text_backend_t *be) {
    GT_ASSERT(be != nullptr && be->metrics != nullptr && be->measure != nullptr &&
              be->draw != nullptr && be->glyph_advance != nullptr);
    if (be == nullptr || be->glyph_advance == nullptr) {
        return;
    }
    gates_text_conformance_font(be, GATES_FONT_UI);
    gates_text_conformance_font(be, GATES_FONT_MONO);
    gates_text_metrics_t m = be->metrics(be->ctx, 0);
    gates_rect_t rect = { 10, 8, 8 * m.advance, m.line_height };
    gates_rect_t full = { 0, 0, GTC_W, GTC_H };

    /* 6. Determinism: identical calls produce identical pixels. */
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, 0, gtc_str("determinism"),
             GATES_RGB(255, 255, 255));
    static gates_u32 snapshot[GTC_H][GTC_W];
    memcpy(snapshot, gtc_buf, sizeof gtc_buf);
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, 0, gtc_str("determinism"),
             GATES_RGB(255, 255, 255));
    GT_ASSERT(memcmp(snapshot, gtc_buf, sizeof gtc_buf) == 0);

    /* 7. A fully transparent color draws nothing. */
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, 0, gtc_str("invisible"),
             GATES_RGBA(255, 255, 255, 0));
    GT_ASSERT(gtc_ink_total() == 0);
}

#endif /* GATES_TEXT_CONFORMANCE_H */
