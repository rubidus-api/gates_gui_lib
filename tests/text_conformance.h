/* Backend conformance checks required by RFC-0002 §8.
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

/* Runs the whole RFC-0002 §8 checklist against `be`. */
static inline void gates_text_conformance(const gates_text_backend_t *be) {
    GT_ASSERT(be != nullptr && be->metrics != nullptr && be->measure != nullptr &&
              be->draw != nullptr);
    if (be == nullptr) {
        return;
    }

    /* 1. Metrics coherence. */
    gates_text_metrics_t m = be->metrics(be->ctx, 0);
    GT_ASSERT(m.advance > 0);
    GT_ASSERT(m.ascent > 0);
    GT_ASSERT(m.descent >= 0);
    GT_ASSERT(m.line_height >= m.ascent + m.descent);

    /* 2. Empty string measures to zero width, one line tall. */
    gates_size_t empty = be->measure(be->ctx, 0, (gates_str_t){0});
    GT_ASSERT(empty.w == 0 && empty.h == m.line_height);

    /* 3. measure == cells * advance for every sample, and the cell counts
     *    themselves match the shared rule (so backends cannot disagree). */
    static const struct { const char *text; gates_u32 cells; } samples[] = {
        { "a", 1 }, { "hello", 5 }, { "  ", 2 },
        { "한", 2 }, { "한글", 4 }, { "a한b", 4 },
        { "漢字", 4 }, { "abc한글xyz", 10 },
    };
    for (unsigned i = 0; i < sizeof samples / sizeof *samples; i++) {
        gates_str_t s = gtc_str(samples[i].text);
        GT_ASSERT(gates_text_cells(s) == samples[i].cells);
        gates_size_t sz = be->measure(be->ctx, 0, s);
        GT_ASSERT(sz.w == (gates_i32)samples[i].cells * m.advance);
        GT_ASSERT(sz.h == m.line_height);
    }

    /* 4. Ink stays inside the assigned rect. */
    gates_rect_t rect = { 10, 8, 8 * m.advance, m.line_height };
    gates_rect_t full = { 0, 0, GTC_W, GTC_H };
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, 0, gtc_str("Ag한글"),
             GATES_RGB(255, 255, 255));
    GT_ASSERT(gtc_ink_total() > 0);
    GT_ASSERT(gtc_ink_outside(rect) == 0);

    /* 4b. Ink follows the rect. Moving the assigned rect must move the ink by
     *     exactly the same delta, and a single narrow glyph must land in the
     *     first cell — this is what catches a backend that draws at a fixed
     *     origin instead of the one it was given. */
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, 0, gtc_str("M"), GATES_RGB(255, 255, 255));
    gates_rect_t ink_a = gtc_ink_bounds();
    GT_ASSERT(!gates_rect_is_empty(ink_a));
    GT_ASSERT(ink_a.x >= rect.x && ink_a.x + ink_a.w <= rect.x + m.advance);

    gates_rect_t moved = { rect.x + 23, rect.y + 5, rect.w, rect.h };
    gtc_clear();
    be->draw(be->ctx, gtc_target(), moved, full, 0, gtc_str("M"), GATES_RGB(255, 255, 255));
    gates_rect_t ink_b = gtc_ink_bounds();
    GT_ASSERT(!gates_rect_is_empty(ink_b));
    GT_ASSERT(ink_b.x - ink_a.x == 23);
    GT_ASSERT(ink_b.y - ink_a.y == 5);
    GT_ASSERT(ink_b.w == ink_a.w && ink_b.h == ink_a.h);

    /* 4c. Cell placement: the second glyph of a run sits exactly one advance
     *     right of the first, and a wide glyph pushes the next one by two. */
    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, 0, gtc_str(" M"), GATES_RGB(255, 255, 255));
    gates_rect_t ink_second = gtc_ink_bounds();
    GT_ASSERT(!gates_rect_is_empty(ink_second));
    GT_ASSERT(ink_second.x - ink_a.x == m.advance);

    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, full, 0, gtc_str("한M"), GATES_RGB(255, 255, 255));
    gates_rect_t ink_after_wide = gtc_ink_bounds();
    GT_ASSERT(!gates_rect_is_empty(ink_after_wide));
    /* The run now starts with the wide glyph, so its right edge must reach
     * into the third cell where the 'M' lives. */
    GT_ASSERT(ink_after_wide.x + ink_after_wide.w > rect.x + 2 * m.advance);
    GT_ASSERT(ink_after_wide.x + ink_after_wide.w <= rect.x + 3 * m.advance);

    /* 5. Clip discipline: a narrow clip keeps ink inside it, an empty clip
     *    produces none at all. */
    gtc_clear();
    gates_rect_t narrow = { rect.x, rect.y, m.advance, m.line_height };
    be->draw(be->ctx, gtc_target(), rect, narrow, 0, gtc_str("MMMM"),
             GATES_RGB(255, 255, 255));
    GT_ASSERT(gtc_ink_outside(narrow) == 0);

    gtc_clear();
    be->draw(be->ctx, gtc_target(), rect, (gates_rect_t){ 0, 0, 0, 0 }, 0,
             gtc_str("MMMM"), GATES_RGB(255, 255, 255));
    GT_ASSERT(gtc_ink_total() == 0);

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
