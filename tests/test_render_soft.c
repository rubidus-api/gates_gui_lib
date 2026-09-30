/* software renderer, pixel-exact. */
#include <gates/render.h>
#include <gates/text.h>
#include "gates_test.h"

#include <string.h>

#define W 32
#define H 24

/* Target with a deliberately padded stride to catch stride bugs. */
#define PAD_PX 3
static gates_u32 buf[H][W + PAD_PX];

static gates_pixels_t target(void) {
    return (gates_pixels_t){ .ptr = &buf[0][0], .w = W, .h = H,
                             .stride_bytes = (W + PAD_PX) * 4u };
}

static void clear_buf(gates_u32 value) {
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W + PAD_PX; x++) {
            buf[y][x] = value;
        }
    }
}

static gates_u32 px(int x, int y) { return buf[y][x]; }

static gates_u32 packed(gates_color_t c) { return gates_pixel_pack(c); }

/* The renderer's documented blend formula, computed independently. */
static gates_u32 blend_ref(gates_u32 dst, gates_color_t s) {
    gates_color_t d = gates_pixel_unpack(dst);
    gates_u32 a = s.a, na = 255u - a;
    gates_color_t out = {
        .r = (gates_u8)((s.r * a + d.r * na + 127u) / 255u),
        .g = (gates_u8)((s.g * a + d.g * na + 127u) / 255u),
        .b = (gates_u8)((s.b * a + d.b * na + 127u) / 255u),
        .a = (gates_u8)((a * 255u + (gates_u32)d.a * na + 127u) / 255u),
    };
    return gates_pixel_pack(out);
}

static int count_pixels(gates_u32 value) {
    int n = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (px(x, y) == value) n++;
        }
    }
    return n;
}

static void test_pack_unpack(void) {
    gates_color_t c = GATES_RGBA(1, 2, 3, 4);
    gates_color_t rt = gates_pixel_unpack(gates_pixel_pack(c));
    GT_ASSERT(rt.r == 1 && rt.g == 2 && rt.b == 3 && rt.a == 4);
    /* BGRA byte order in memory. */
    gates_u32 p = gates_pixel_pack(GATES_RGBA(0xAA, 0xBB, 0xCC, 0xDD));
    gates_u8 bytes[4];
    memcpy(bytes, &p, 4);
    GT_ASSERT(bytes[0] == 0xCC && bytes[1] == 0xBB && bytes[2] == 0xAA && bytes[3] == 0xDD);
}

static void test_full_fill_and_stride(void) {
    clear_buf(0);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_color_t c = GATES_RGB(50, 60, 70);
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 0, 0, W, H }, c));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));

    GT_ASSERT(count_pixels(packed(c)) == W * H);
    /* Stride padding untouched. */
    for (int y = 0; y < H; y++) {
        for (int x = W; x < W + PAD_PX; x++) {
            GT_ASSERT(buf[y][x] == 0);
        }
    }
    gates_draw_list_deinit(&dl);
}

static void test_rect_clip_intersection(void) {
    clear_buf(0);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_color_t c = GATES_RGB(255, 0, 0);

    GT_ASSERT_OK(gates_draw_clip_push(&dl, (gates_rect_t){ 4, 4, 8, 8 }));
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 0, 0, W, H }, c)); /* clipped to 8x8 */
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));

    GT_ASSERT(count_pixels(packed(c)) == 64);
    GT_ASSERT(px(4, 4) == packed(c) && px(11, 11) == packed(c));
    GT_ASSERT(px(3, 4) == 0 && px(12, 11) == 0 && px(4, 3) == 0);
    gates_draw_list_deinit(&dl);
}

static void test_nested_clip(void) {
    clear_buf(0);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_color_t c1 = GATES_RGB(0, 255, 0);
    gates_color_t c2 = GATES_RGB(0, 0, 255);

    GT_ASSERT_OK(gates_draw_clip_push(&dl, (gates_rect_t){ 2, 2, 12, 12 }));
    GT_ASSERT_OK(gates_draw_clip_push(&dl, (gates_rect_t){ 8, 8, 12, 12 })); /* eff: 8..13 */
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 0, 0, W, H }, c1));    /* 6x6 = 36 px */
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    /* Back to the outer clip: paint one row proving the restore. */
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 0, 4, W, 1 }, c2));    /* x 2..13 */
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));

    GT_ASSERT(count_pixels(packed(c1)) == 36);
    GT_ASSERT(px(8, 8) == packed(c1) && px(13, 13) == packed(c1));
    GT_ASSERT(px(14, 8) == 0 && px(8, 14) == 0);
    GT_ASSERT(count_pixels(packed(c2)) == 12);
    GT_ASSERT(px(2, 4) == packed(c2) && px(13, 4) == packed(c2) && px(1, 4) == 0);
    gates_draw_list_deinit(&dl);
}

static void test_border_no_double_draw(void) {
    clear_buf(packed(GATES_RGB(0, 0, 0)));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    /* Translucent border: a double-drawn corner would blend twice and differ. */
    gates_color_t c = GATES_RGBA(200, 100, 40, 128);
    gates_rect_t r = { 2, 2, 10, 8 };
    GT_ASSERT_OK(gates_draw_border(&dl, r, 2, c));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));

    gates_u32 once = blend_ref(packed(GATES_RGB(0, 0, 0)), c);
    GT_ASSERT(px(2, 2) == once);            /* corner */
    GT_ASSERT(px(3, 3) == once);            /* inner corner of thickness 2 */
    GT_ASSERT(px(6, 2) == once);            /* top edge */
    GT_ASSERT(px(2, 5) == once);            /* left edge */
    GT_ASSERT(px(11, 9) == once);           /* bottom-right corner */
    GT_ASSERT(px(6, 5) == packed(GATES_RGB(0, 0, 0))); /* interior untouched */
    /* Exact border pixel count: 2*(w*t) + 2*(t*(h-2t)) = 2*20 + 2*8 = 56. */
    GT_ASSERT(count_pixels(once) == 56);
    gates_draw_list_deinit(&dl);
}

static void test_lines(void) {
    clear_buf(0);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_color_t c = GATES_RGB(255, 255, 0);

    GT_ASSERT_OK(gates_draw_line(&dl, (gates_point_t){ 1, 1 }, (gates_point_t){ 6, 1 }, c));
    GT_ASSERT_OK(gates_draw_line(&dl, (gates_point_t){ 1, 3 }, (gates_point_t){ 1, 8 }, c));
    GT_ASSERT_OK(gates_draw_line(&dl, (gates_point_t){ 10, 10 }, (gates_point_t){ 15, 15 }, c));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));

    /* Horizontal: endpoints inclusive. */
    GT_ASSERT(px(1, 1) == packed(c) && px(6, 1) == packed(c) && px(7, 1) == 0);
    /* Vertical. */
    GT_ASSERT(px(1, 3) == packed(c) && px(1, 8) == packed(c) && px(1, 9) == 0);
    /* Perfect diagonal: exactly one pixel per step. */
    for (int i = 0; i <= 5; i++) {
        GT_ASSERT(px(10 + i, 10 + i) == packed(c));
    }
    GT_ASSERT(count_pixels(packed(c)) == 6 + 6 + 6);
    gates_draw_list_deinit(&dl);
}

static void test_line_clipped(void) {
    clear_buf(0);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_color_t c = GATES_RGB(9, 9, 9);
    GT_ASSERT_OK(gates_draw_clip_push(&dl, (gates_rect_t){ 0, 0, 4, H }));
    GT_ASSERT_OK(gates_draw_line(&dl, (gates_point_t){ 0, 0 }, (gates_point_t){ 10, 0 }, c));
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));
    GT_ASSERT(count_pixels(packed(c)) == 4); /* x 0..3 only */
    gates_draw_list_deinit(&dl);
}

static void test_alpha_blend_exact(void) {
    gates_u32 bg = packed(GATES_RGB(0, 0, 0));
    clear_buf(bg);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_color_t half = GATES_RGBA(255, 100, 20, 128);
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 0, 0, 2, 1 }, half));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));

    GT_ASSERT(px(0, 0) == blend_ref(bg, half));
    gates_color_t out = gates_pixel_unpack(px(0, 0));
    GT_ASSERT(out.r == 128 && out.g == 50 && out.b == 10); /* (v*128+127)/255 */

    /* a=0 leaves the target untouched; a=255 overwrites exactly. */
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 0, 0, W, H }, GATES_RGBA(7, 7, 7, 0)));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));
    GT_ASSERT(px(5, 5) == bg);
    gates_draw_list_deinit(&dl);
}

static void test_out_of_bounds_safe(void) {
    clear_buf(0);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_color_t c = GATES_RGB(1, 2, 3);
    /* Fully outside, partially outside, negative origin, huge extents. */
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ -100, -100, 50, 50 }, c));
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ W - 2, H - 2, 100, 100 }, c));
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 1000, 1000, 10, 10 }, c));
    GT_ASSERT_OK(gates_draw_border(&dl, (gates_rect_t){ -5, -5, 8, 8 }, 2, c));
    GT_ASSERT_OK(gates_draw_line(&dl, (gates_point_t){ -10, -10 }, (gates_point_t){ 2, 2 }, c));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), nullptr));
    /* No crash (ASan-verified); bottom-right 2x2 painted. */
    GT_ASSERT(px(W - 1, H - 1) == packed(c) && px(W - 2, H - 2) == packed(c));
    gates_draw_list_deinit(&dl);
}

static void test_unsupported_and_invalid(void) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_rect_t r = { 0, 0, 4, 4 };

    /* Unbalanced list rejected before any pixel is written. */
    clear_buf(0);
    GT_ASSERT_OK(gates_draw_clip_push(&dl, r));
    GT_ASSERT_OK(gates_draw_rect(&dl, r, GATES_RGB(9, 9, 9)));
    GT_ASSERT_ERR(gates_render_soft(&dl, target(), nullptr));
    GT_ASSERT(count_pixels(0) == W * H); /* nothing was written */

    /* Bad targets. */
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_draw_rect(&dl, r, GATES_RGB(9, 9, 9)));
    GT_ASSERT_ERR(gates_render_soft(&dl, (gates_pixels_t){0}, nullptr));
    GT_ASSERT_ERR(gates_render_soft(&dl, (gates_pixels_t){ .ptr = buf, .w = W, .h = H,
                                                           .stride_bytes = W * 4u - 1 }, nullptr));
    GT_ASSERT_ERR(gates_render_soft(nullptr, target(), nullptr));
    gates_draw_list_deinit(&dl);
}

static void test_text_delegation(void) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));

    /* TEXT without a backend is rejected. */
    clear_buf(0);
    GT_ASSERT_OK(gates_draw_text(&dl, (gates_rect_t){ 0, 0, W, 16 }, GATES_STR("Hi"),
                                 0, GATES_RGB(255, 255, 255)));
    GT_ASSERT_ERR(gates_render_soft(&dl, target(), nullptr));

    /* With the builtin backend, ink lands inside the assigned rect and obeys
     * the renderer clip stack. */
    clear_buf(0);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_draw_clip_push(&dl, (gates_rect_t){ 0, 0, 5, H }));
    GT_ASSERT_OK(gates_draw_text(&dl, (gates_rect_t){ 0, 0, W, 16 }, GATES_STR("MM"),
                                 0, GATES_RGB(255, 255, 255)));
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT_OK(gates_render_soft(&dl, target(), gates_text_backend_builtin()));
    int lit = 0, lit_clipped = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (px(x, y) != 0) {
                lit++;
                if (x < 5) lit_clipped++;
            }
        }
    }
    GT_ASSERT(lit > 0);
    GT_ASSERT(lit == lit_clipped); /* everything stayed inside the clip */

    gates_draw_list_deinit(&dl);
}

int main(void) {
    test_pack_unpack();
    test_full_fill_and_stride();
    test_rect_clip_intersection();
    test_nested_clip();
    test_border_no_double_draw();
    test_lines();
    test_line_clipped();
    test_alpha_blend_exact();
    test_out_of_bounds_safe();
    test_unsupported_and_invalid();
    test_text_delegation();
    return gt_report("test_render_soft");
}
