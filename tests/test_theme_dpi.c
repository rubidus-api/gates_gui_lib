/* themes and DPI. Stage 1: every profile defines every
 * token, text and cue contrast (WCAG ratios), high contrast built from the
 * system's colours, cues that do not rely on colour alone (focus and error
 * widths in paint output). */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/view.h>
#include <gates/render.h>
#include "gates_test.h"

#include <math.h>
#include <string.h>

static const gates_text_backend_t *be;

/* WCAG 2 relative luminance and contrast ratio. */
static double channel(int c) {
    double s = c / 255.0;
    return s <= 0.03928 ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
}
static double luminance(gates_color_t c) {
    return 0.2126 * channel(c.r) + 0.7152 * channel(c.g) + 0.0722 * channel(c.b);
}
static double contrast(gates_color_t a, gates_color_t b) {
    double la = luminance(a), lb = luminance(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

typedef struct pair_t {
    gates_color_token_t fg, bg;
    double min;
    const char *what;
} pair_t;

static const pair_t pairs[] = {
    { GATES_COLOR_WINDOW_FG, GATES_COLOR_WINDOW_BG, 4.5, "window text" },
    { GATES_COLOR_PANEL_FG, GATES_COLOR_PANEL_BG, 4.5, "panel text" },
    { GATES_COLOR_CONTROL_FG, GATES_COLOR_CONTROL_BG, 4.5, "control text" },
    { GATES_COLOR_CONTROL_FG, GATES_COLOR_CONTROL_HOVER_BG, 4.5, "hovered text" },
    { GATES_COLOR_CONTROL_FG, GATES_COLOR_CONTROL_PRESSED_BG, 4.5, "pressed text" },
    { GATES_COLOR_SELECTION_FG, GATES_COLOR_SELECTION_BG, 4.5, "selected text" },
    { GATES_COLOR_PANEL_FG, GATES_COLOR_WINDOW_BG, 4.5, "labels on the window" },
    { GATES_COLOR_CONTROL_DISABLED_FG, GATES_COLOR_CONTROL_BG, 3.0, "disabled text" },
    { GATES_COLOR_CONTROL_DISABLED_FG, GATES_COLOR_PANEL_BG, 3.0, "disabled label" },
    { GATES_COLOR_ERROR, GATES_COLOR_CONTROL_BG, 3.0, "error border" },
    { GATES_COLOR_ERROR, GATES_COLOR_WINDOW_BG, 4.5, "error text" },
    { GATES_COLOR_FOCUS_RING, GATES_COLOR_WINDOW_BG, 3.0, "focus ring" },
    { GATES_COLOR_FOCUS_RING, GATES_COLOR_CONTROL_BG, 3.0, "focus ring on a control" },
    { GATES_COLOR_CONTROL_BORDER, GATES_COLOR_CONTROL_BG, 3.0, "control border" },
    { GATES_COLOR_CONTROL_BORDER, GATES_COLOR_WINDOW_BG, 3.0, "control border on the window" },
};

static int check_profile(const gates_theme_t *t, const char *name) {
    int bad = 0;
    for (int k = 0; k < GATES_COLOR_TOKEN_COUNT; k++) {
        gates_color_t c = t->colors[k];
        bool unset = c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0;
        if (k != GATES_COLOR_OVERLAY_DIM && (unset || c.a != 255)) {
            printf("  %s: token %d undefined or translucent\n", name, k);
            bad++;
        }
    }
    for (size_t i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        double r = contrast(t->colors[pairs[i].fg], t->colors[pairs[i].bg]);
        if (r < pairs[i].min) {
            printf("  %s: %s %.2f < %.1f\n", name, pairs[i].what, r, pairs[i].min);
            bad++;
        }
    }
    return bad;
}

static void test_profiles(void) {
    GT_ASSERT(check_profile(gates_theme_light(), "light") == 0);
    GT_ASSERT(check_profile(gates_theme_dark(), "dark") == 0);
    gates_theme_t hc;
    gates_theme_high_contrast(nullptr, &hc);
    GT_ASSERT(check_profile(&hc, "high contrast black") == 0);
    /* High contrast White, as Windows ships it. */
    gates_system_colors_t white = {
        .window = { 255, 255, 255, 255 }, .window_text = { 0, 0, 0, 255 },
        .highlight = { 55, 0, 110, 255 }, .highlight_text = { 255, 255, 255, 255 },
        .button_face = { 255, 255, 255, 255 }, .button_text = { 0, 0, 0, 255 },
        .gray_text = { 96, 0, 0, 255 }, .hotlight = { 0, 0, 159, 255 },
    };
    gates_theme_high_contrast(&white, &hc);
    GT_ASSERT(check_profile(&hc, "high contrast white") == 0);
    /* The system's colours are used as given. */
    GT_ASSERT(hc.colors[GATES_COLOR_WINDOW_BG].r == 255 && hc.colors[GATES_COLOR_SELECTION_BG].b == 110);
    GT_ASSERT(hc.colors[GATES_COLOR_FOCUS_RING].b == 159 && hc.colors[GATES_COLOR_CONTROL_DISABLED_FG].r == 96);
    GT_ASSERT(hc.colors[GATES_COLOR_OVERLAY_DIM].a == 0);        /* no dimming */
    GT_ASSERT(gates_theme_focus_width(&hc) > gates_theme_focus_width(gates_theme_light()));
    /* Dark really is dark, light really is light. */
    GT_ASSERT(luminance(gates_theme_dark()->colors[GATES_COLOR_WINDOW_BG]) < 0.05);
    GT_ASSERT(luminance(gates_theme_light()->colors[GATES_COLOR_WINDOW_BG]) > 0.8);
    /* A theme that leaves the widths zero gets the defaults. */
    gates_theme_t custom = { .colors = { [GATES_COLOR_WINDOW_BG] = { 1, 2, 3, 255 } } };
    GT_ASSERT(gates_theme_focus_width(&custom) == 2 && gates_theme_error_width(&custom) == 2);
    GT_ASSERT(gates_theme_focus_width(nullptr) == 2);
}

/* -- cues beyond colour: widths show in paint output ---------------------------------- */

static bool border(const gates_draw_list_t *dl, gates_rect_t r, gates_i32 thickness,
                   gates_color_t c) {
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(dl, i);
        if (cmd->kind == GATES_DRAW_BORDER && cmd->rect.x == r.x && cmd->rect.y == r.y &&
            cmd->rect.w == r.w && cmd->rect.h == r.h && cmd->thickness == thickness &&
            cmd->color.r == c.r && cmd->color.g == c.g && cmd->color.b == c.b) {
            return true;
        }
    }
    return false;
}

static void test_cues(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_node_t btn, box, chk;
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("b"), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("x"), 8, &box));
    GT_ASSERT_OK(gates_checkbox_create(t, root, GATES_STR("c"), false, nullptr, nullptr, &chk));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 200 }, be));
    gates_theme_t hc;
    gates_theme_high_contrast(nullptr, &hc);
    const gates_theme_t *themes[] = { gates_theme_light(), gates_theme_dark(), &hc };
    for (int k = 0; k < 3; k++) {
        const gates_theme_t *th = themes[k];
        gates_i32 fw = gates_theme_focus_width(th), ew = gates_theme_error_width(th);
        gates_color_t ring = gates_theme_color(th, GATES_COLOR_FOCUS_RING);
        gates_color_t err = gates_theme_color(th, GATES_COLOR_ERROR);
        gates_node_t targets[] = { btn, box, chk };
        for (int i = 0; i < 3; i++) {
            gates_tree_set_focus(t, targets[i]);
            gates_draw_list_t dl;
            GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
            GT_ASSERT_OK(gates_paint_tree(t, &dl, th, be));
            GT_ASSERT(border(&dl, gates_node_layout_rect(t, targets[i]), fw, ring));
            gates_draw_list_deinit(&dl);
        }
        /* An invalid box: a thicker error border, with or without focus. */
        GT_ASSERT_OK(gates_textbox_set_invalid(t, box, true));
        gates_tree_set_focus(t, btn);
        gates_draw_list_t dl;
        GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
        GT_ASSERT_OK(gates_paint_tree(t, &dl, th, be));
        GT_ASSERT(border(&dl, gates_node_layout_rect(t, box), ew, err));
        GT_ASSERT(ew >= 2);
        gates_draw_list_deinit(&dl);
        GT_ASSERT_OK(gates_textbox_set_invalid(t, box, false));
    }
    gates_tree_destroy(t);
}

/* -- logical units scaled at the window boundary ------------------------------- */

static const gates_u32 dpis[] = { 96, 120, 144, 168, 192 };   /* 100..200% */

static void test_scale_helpers(void) {
    GT_ASSERT(gates_px(10, 0) == 10 && gates_logical(10, 0) == 10);
    for (size_t k = 0; k < sizeof dpis / sizeof dpis[0]; k++) {
        gates_u32 d = dpis[k];
        bool ok = true;
        for (gates_i32 v = -300; v <= 300; v++) {
            gates_i32 p = gates_px(v, d);
            /* round-half-up of v*d/96, exact in integers */
            gates_i64 twice = (gates_i64)v * d * 2;           /* 2 * v * d */
            gates_i64 lo = (gates_i64)p * 192 - 96, hi = (gates_i64)p * 192 + 96;
            if (!(twice >= lo && twice < hi)) ok = false;
            if (gates_px(v + 1, d) < p) ok = false;           /* monotonic */
            /* Every device pixel of a logical unit maps back to that unit. */
            for (gates_i32 x = gates_px(v, d); x < gates_px(v + 1, d); x++) {
                if (gates_logical(x, d) != v) ok = false;
            }
        }
        GT_ASSERT(ok);
        /* Neighbours tile: no gap, no overlap, at every scale. */
        gates_i32 x = 0;
        bool tiles = true;
        for (int i = 0; i < 50; i++) {
            gates_i32 w = 3 + (i * 7) % 11;
            gates_rect_t a = gates_rect_px((gates_rect_t){ x, 0, w, 5 }, d);
            gates_rect_t b = gates_rect_px((gates_rect_t){ x + w, 0, 4, 5 }, d);
            if (a.x + a.w != b.x || a.w <= 0) tiles = false;
            x += w;
        }
        GT_ASSERT(tiles);
        /* A point inside a scaled rect maps back inside the logical rect. */
        gates_rect_t lr = { 13, 7, 9, 5 };
        gates_rect_t pr = gates_rect_px(lr, d);
        bool inside = true;
        for (gates_i32 py = pr.y; py < pr.y + pr.h; py++) {
            for (gates_i32 px = pr.x; px < pr.x + pr.w; px++) {
                gates_point_t lp = { gates_logical(px, d), gates_logical(py, d) };
                if (!gates_rect_contains(lr, lp)) inside = false;
            }
        }
        GT_ASSERT(inside);
        GT_ASSERT(gates_thickness_px(1, d) >= 1 && gates_thickness_px(0, d) == 0);
    }
    GT_ASSERT(gates_px(1, 120) == 1 && gates_px(1, 144) == 2 && gates_px(2, 144) == 3);
    GT_ASSERT(gates_px(1, 40) == 0 && gates_thickness_px(1, 40) == 1); /* never thinner than a pixel */
    GT_ASSERT(gates_logical(-1, 144) == -1 && gates_px(-1, 144) == -1);
}

static gates_color_t px_at(const gates_u32 *buf, gates_i32 w, gates_i32 x, gates_i32 y) {
    return gates_pixel_unpack(buf[y * w + x]);
}

static void test_scaled_render(void) {
    enum { W = 90, H = 60 };
    static gates_u32 a[W * H], b[W * H];
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_color_t red = { 255, 0, 0, 255 }, blue = { 0, 0, 255, 255 }, green = { 0, 200, 0, 255 };
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 10, 4, 10, 6 }, red));
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 20, 4, 10, 6 }, blue)); /* its neighbour */
    GT_ASSERT_OK(gates_draw_border(&dl, (gates_rect_t){ 2, 20, 20, 10 }, 1, green));
    GT_ASSERT_OK(gates_draw_clip_push(&dl, (gates_rect_t){ 40, 20, 5, 5 }));
    GT_ASSERT_OK(gates_draw_rect(&dl, (gates_rect_t){ 30, 10, 30, 30 }, red));
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT_OK(gates_draw_text(&dl, (gates_rect_t){ 2, 32, 24, 16 }, GATES_STR("ab"), 0, blue));
    gates_pixels_t ta = { a, W, H, W * 4 }, tb = { b, W, H, W * 4 };
    /* At 96 dpi the scaled renderer is the plain one. */
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    GT_ASSERT_OK(gates_render_soft(&dl, ta, be));
    GT_ASSERT_OK(gates_render_soft_scaled(&dl, tb, be, 96));
    GT_ASSERT(memcmp(a, b, sizeof a) == 0);
    /* At 150%: the red rect 10..20 covers pixels 15..29, blue starts at 30. */
    memset(a, 0, sizeof a);
    GT_ASSERT_OK(gates_render_soft_scaled(&dl, ta, be, 144));
    GT_ASSERT(px_at(a, W, 14, 8).r == 0 && px_at(a, W, 15, 8).r == 255);
    GT_ASSERT(px_at(a, W, 29, 8).r == 255 && px_at(a, W, 30, 8).b == 255 && px_at(a, W, 30, 8).r == 0);
    GT_ASSERT(px_at(a, W, 44, 8).b == 255 && px_at(a, W, 45, 8).b == 0);
    GT_ASSERT(px_at(a, W, 20, 5).a == 0 && px_at(a, W, 20, 6).r == 255); /* y 4 -> 6 */
    /* A 1-unit border is 2 pixels at 150% (1.5 rounds up); the clip scales too. */
    GT_ASSERT(px_at(a, W, 3, 35).g == 200 && px_at(a, W, 4, 35).g == 200 && px_at(a, W, 5, 35).g == 0);
    GT_ASSERT(px_at(a, W, 60, 30).r == 255 && px_at(a, W, 67, 37).r == 255);
    GT_ASSERT(px_at(a, W, 59, 30).r == 0 && px_at(a, W, 68, 37).r == 0);
    /* At 125%, 1 unit stays 1 pixel thick (never 0). */
    memset(a, 0, sizeof a);
    GT_ASSERT_OK(gates_render_soft_scaled(&dl, ta, be, 120));
    GT_ASSERT(px_at(a, W, 3, 30).g == 200 && px_at(a, W, 4, 30).g == 0);
    gates_draw_list_deinit(&dl);
}

int main(void) {
    be = gates_text_backend_builtin();
    test_profiles();
    test_cues();
    test_scale_helpers();
    test_scaled_render();
    return gt_report("test_theme_dpi");
}
