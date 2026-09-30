/* T007: builtin text backend + metrics contract (docs/tests/cases/T007-text-builtin.md). */
#include <gates/text.h>
#include "gates_test.h"

#include <string.h>

#define W 96
#define H 40
static gates_u32 buf[H][W];

static gates_pixels_t target(void) {
    return (gates_pixels_t){ .ptr = &buf[0][0], .w = W, .h = H, .stride_bytes = W * 4u };
}

static void clear_buf(void) { memset(buf, 0, sizeof buf); }

static int lit_pixels(void) {
    int n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (buf[y][x] != 0) n++;
    return n;
}

static int lit_in(gates_rect_t r) {
    int n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (buf[y][x] != 0 &&
                gates_rect_contains(r, (gates_point_t){ x, y })) n++;
    return n;
}

static const gates_text_backend_t *be;

static void test_metrics_contract(void) {
    gates_text_metrics_t m = be->metrics(be->ctx, 0);
    GT_ASSERT(m.advance > 0);
    GT_ASSERT(m.ascent > 0 && m.descent >= 0);
    GT_ASSERT(m.line_height >= m.ascent + m.descent);
    /* Builtin is size-invariant (one embedded size). */
    gates_text_metrics_t m2 = be->metrics(be->ctx, 99);
    GT_ASSERT(m.advance == m2.advance && m.line_height == m2.line_height);
}

static void test_cells_and_measure(void) {
    gates_text_metrics_t m = be->metrics(be->ctx, 0);

    GT_ASSERT(gates_text_cells(GATES_STR("")) == 0);
    GT_ASSERT(gates_text_cells(GATES_STR("abc")) == 3);
    GT_ASSERT(gates_text_cells(GATES_STR("한")) == 2);        /* Hangul: wide */
    GT_ASSERT(gates_text_cells(GATES_STR("한글")) == 4);
    GT_ASSERT(gates_text_cells(GATES_STR("a한b")) == 4);
    GT_ASSERT(gates_text_cells(GATES_STR("漢字")) == 4);       /* CJK: wide */

    /* section 3 guarantee: measure == cells * advance, height == line_height. */
    const char *samples[] = { "x", "hello", "a한b", "한글 label", "" };
    for (unsigned i = 0; i < sizeof samples / sizeof *samples; i++) {
        gates_str_t s = { .ptr = (const proven_byte_t *)samples[i],
                          .size = strlen(samples[i]) };
        gates_size_t sz = be->measure(be->ctx, 0, s);
        GT_ASSERT(sz.w == (gates_i32)gates_text_cells(s) * m.advance);
        GT_ASSERT(sz.h == m.line_height);
    }

    /* Malformed UTF-8: consumes bytes, never crashes, counts >= 1 cell/byte-error. */
    static const gates_u8 bad[] = { 'a', 0xFF, 0xC3, 'b' };
    gates_str_t bs = { .ptr = bad, .size = sizeof bad };
    GT_ASSERT(gates_text_cells(bs) >= 3);
}

static void test_draw_ascii_deterministic(void) {
    gates_text_metrics_t m = be->metrics(be->ctx, 0);
    gates_rect_t full = { 0, 0, W, H };
    gates_str_t s = GATES_STR("Ag");

    clear_buf();
    be->draw(be->ctx, target(), (gates_rect_t){ 2, 2, 90, 20 }, full, 0, s,
             GATES_RGB(255, 255, 255));
    int first = lit_pixels();
    GT_ASSERT(first > 20); /* something legible was drawn */
    /* All ink stays inside the assigned area (2 cells wide, one line tall). */
    GT_ASSERT(lit_in((gates_rect_t){ 2, 2, 2 * m.advance, m.line_height }) == first);

    /* Determinism: identical input -> identical pixels. */
    gates_u32 snap[H][W];
    memcpy(snap, buf, sizeof buf);
    clear_buf();
    be->draw(be->ctx, target(), (gates_rect_t){ 2, 2, 90, 20 }, full, 0, s,
             GATES_RGB(255, 255, 255));
    GT_ASSERT(memcmp(snap, buf, sizeof buf) == 0);
}

static void test_draw_clip(void) {
    gates_rect_t full = { 0, 0, W, H };
    clear_buf();
    /* Clip to the left half of the first glyph. */
    gates_rect_t clip = { 0, 0, 4, H };
    be->draw(be->ctx, target(), (gates_rect_t){ 0, 0, 90, 20 }, clip, 0,
             GATES_STR("MM"), GATES_RGB(255, 255, 255));
    GT_ASSERT(lit_pixels() > 0);
    GT_ASSERT(lit_in((gates_rect_t){ 0, 0, 4, H }) == lit_pixels());

    /* Zero-area clip: no ink at all. */
    clear_buf();
    be->draw(be->ctx, target(), (gates_rect_t){ 0, 0, 90, 20 },
             (gates_rect_t){ 0, 0, 0, 0 }, 0, GATES_STR("MM"), GATES_RGB(255, 255, 255));
    GT_ASSERT(lit_pixels() == 0);
    (void)full;
}

static void test_replacement_box_for_non_ascii(void) {
    gates_text_metrics_t m = be->metrics(be->ctx, 0);
    clear_buf();
    be->draw(be->ctx, target(), (gates_rect_t){ 0, 0, 90, 20 },
             (gates_rect_t){ 0, 0, W, H }, 0, GATES_STR("한"), GATES_RGB(255, 255, 255));
    int ink = lit_pixels();
    GT_ASSERT(ink > 0); /* box outline drawn */
    /* Ink confined to the 2-cell area the glyph occupies. */
    GT_ASSERT(lit_in((gates_rect_t){ 0, 0, 2 * m.advance, m.line_height }) == ink);
}

static void test_space_is_blank(void) {
    clear_buf();
    be->draw(be->ctx, target(), (gates_rect_t){ 0, 0, 90, 20 },
             (gates_rect_t){ 0, 0, W, H }, 0, GATES_STR("   "), GATES_RGB(255, 255, 255));
    GT_ASSERT(lit_pixels() == 0);
}

int main(void) {
    be = gates_text_backend_builtin();
    test_metrics_contract();
    test_cells_and_measure();
    test_draw_ascii_deterministic();
    test_draw_clip();
    test_replacement_box_for_non_ascii();
    test_space_is_blank();
    return gt_report("test_text_builtin");
}
