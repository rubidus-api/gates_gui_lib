/* images and icons (0.5.0). */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/image.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include <gates/frame.h>
#include <gates/access.h>
#include <gates/render.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <stdlib.h>
#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 320
#define VH 240

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
}

/* A w x h image of one colour. */
static gates_image_id_t solid(gates_tree_t *t, gates_i32 w, gates_i32 h, gates_u8 r, gates_u8 g, gates_u8 b, gates_u8 a) {
    gates_u8 *px = malloc((size_t)w * (size_t)h * 4);
    for (gates_i32 i = 0; i < w * h; i++) {
        px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a;
    }
    gates_image_id_t id = 0;
    GT_ASSERT_OK(gates_image_add_rgba(t, w, h, px, 0, &id));
    free(px);
    return id;
}

/* -- the store ----------------------------------------------------------------------- */

typedef struct dec_t {
    int calls;
    bool fail;
} dec_t;

/* A test decoder: "IMG w h" becomes a w x h red image; files named "*.bad" fail. */
static gates_err_t test_decode(void *ctx, gates_str_t src, bool is_file, gates_allocator_t alloc, gates_u8 **out,
                               gates_i32 *w, gates_i32 *h) {
    dec_t *d = ctx;
    d->calls++;
    char buf[64] = "";
    memcpy(buf, src.ptr, src.size < 63 ? src.size : 63);
    if (is_file) {
        if (strstr(buf, ".bad") != nullptr) return PROVEN_ERR_INVALID_ARG;
        *w = 3;
        *h = 2;
    } else if (sscanf(buf, "IMG %d %d", w, h) != 2) {
        return PROVEN_ERR_INVALID_ARG;
    }
    proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, (gates_usize_t)(*w * *h * 4), 1);
    if (!proven_is_ok(r.err)) return r.err;
    *out = (gates_u8 *)r.value.ptr;
    for (gates_i32 i = 0; i < *w * *h; i++) {
        (*out)[i * 4] = 255; (*out)[i * 4 + 1] = 0; (*out)[i * 4 + 2] = 0; (*out)[i * 4 + 3] = 255;
    }
    return GATES_OK;
}

static void test_store(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_image_id_t a = solid(t, 4, 3, 1, 2, 3, 255), b = solid(t, 1, 1, 0, 0, 0, 0);
    GT_ASSERT(a != 0 && b != 0 && a != b);
    GT_ASSERT(gates_image_size(t, a).w == 4 && gates_image_size(t, a).h == 3);
    GT_ASSERT(gates_tree_image(t, a) != nullptr);
    gates_image_id_t x = 0;
    gates_u8 px[16] = {0};
    GT_ASSERT(gates_image_add_rgba(t, 0, 1, px, 0, &x) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_image_add_rgba(t, 16385, 1, px, 0, &x) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_image_add_rgba(t, 2, 2, nullptr, 0, &x) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_image_add_rgba(t, 2, 2, px, 4, &x) == PROVEN_ERR_INVALID_ARG);  /* stride < w * 4 */
    /* Stride: rows may be further apart than w * 4. */
    gates_u8 wide[2 * 12] = {0};
    wide[12] = 9;                                               /* row 1, pixel 0, red */
    GT_ASSERT_OK(gates_image_add_rgba(t, 2, 2, wide, 12, &x));
    /* Removal; ids are not reused. */
    GT_ASSERT_OK(gates_image_remove(t, a));
    GT_ASSERT(gates_image_remove(t, a) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(gates_tree_image(t, a) == nullptr && gates_image_size(t, a).w == 0);
    gates_image_id_t c = solid(t, 1, 1, 0, 0, 0, 255);
    GT_ASSERT(c != a);
    /* The decoder seam. */
    GT_ASSERT(gates_image_load_memory(t, "IMG 2 2", 7, &x) == PROVEN_ERR_UNSUPPORTED);
    GT_ASSERT(gates_image_load_file(t, GATES_STR("a.png"), &x) == PROVEN_ERR_UNSUPPORTED);
    dec_t d = {0};
    gates_image_decoder_t dec = { .ctx = &d, .decode = test_decode };
    gates_tree_set_image_decoder(t, &dec);
    GT_ASSERT_OK(gates_image_load_memory(t, "IMG 5 7", 7, &x));
    GT_ASSERT(gates_image_size(t, x).w == 5 && gates_image_size(t, x).h == 7);
    GT_ASSERT_OK(gates_image_load_file(t, GATES_STR("icon.png"), &x));
    GT_ASSERT(gates_image_size(t, x).w == 3);
    GT_ASSERT(gates_image_load_file(t, GATES_STR("broken.bad"), &x) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_image_load_memory(t, "junk", 4, &x) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(d.calls == 4);
    gates_tree_set_image_decoder(t, nullptr);
    GT_ASSERT(gates_image_load_memory(t, "IMG 2 2", 7, &x) == PROVEN_ERR_UNSUPPORTED);
    gates_tree_destroy(t);                                      /* frees every image (ASan) */
}

/* -- rendering --------------------------------------------------------------------------- */

static gates_color_t at(const gates_u32 *buf, gates_i32 w, gates_i32 x, gates_i32 y) {
    return gates_pixel_unpack(buf[y * w + x]);
}

static void test_render(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    /* A 2 x 2 image: red, green / blue, half-transparent white. */
    gates_u8 px[16] = { 255, 0, 0, 255,   0, 255, 0, 255,   0, 0, 255, 255,   255, 255, 255, 128 };
    gates_image_id_t id = 0;
    GT_ASSERT_OK(gates_image_add_rgba(t, 2, 2, px, 0, &id));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_u32 buf[8 * 8];
    for (int i = 0; i < 64; i++) buf[i] = gates_pixel_pack(GATES_RGB(0, 0, 0));
    gates_pixels_t target = { .ptr = buf, .w = 8, .h = 8, .stride_bytes = 32 };
    /* 1:1 at 96 dpi: exact pixels, alpha blended over black. */
    GT_ASSERT_OK(gates_draw_image(&dl, (gates_rect_t){ 1, 1, 2, 2 }, gates_tree_image(t, id)));
    GT_ASSERT_OK(gates_render_soft(&dl, target, be));
    GT_ASSERT(at(buf, 8, 1, 1).r == 255 && at(buf, 8, 1, 1).g == 0);
    GT_ASSERT(at(buf, 8, 2, 1).g == 255 && at(buf, 8, 1, 2).b == 255);
    GT_ASSERT(at(buf, 8, 2, 2).r == 128 && at(buf, 8, 2, 2).g == 128);       /* 50 % white on black */
    GT_ASSERT(at(buf, 8, 0, 0).r == 0 && at(buf, 8, 3, 3).r == 0);          /* nothing outside */
    /* Scaled: the rect in device pixels at 192 dpi is 4 x 4; corners keep their colours. */
    for (int i = 0; i < 64; i++) buf[i] = gates_pixel_pack(GATES_RGB(0, 0, 0));
    GT_ASSERT_OK(gates_render_soft_scaled(&dl, target, be, 192));
    GT_ASSERT(at(buf, 8, 2, 2).r == 255 && at(buf, 8, 2, 2).g == 0);        /* top-left of the red */
    GT_ASSERT(at(buf, 8, 5, 2).g == 255 && at(buf, 8, 5, 2).r == 0);        /* top-right green */
    gates_color_t mid = at(buf, 8, 3, 2);                                   /* between red and green */
    GT_ASSERT(mid.r > 0 && mid.g > 0);                                      /* bilinear */
    GT_ASSERT(at(buf, 8, 6, 6).r == 0 && at(buf, 8, 1, 1).r == 0);
    gates_color_t between = at(buf, 8, 2, 4);                               /* between red and blue rows */
    GT_ASSERT(between.r > 0 && between.b > 0);
    /* Clip. */
    for (int i = 0; i < 64; i++) buf[i] = gates_pixel_pack(GATES_RGB(0, 0, 0));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_draw_clip_push(&dl, (gates_rect_t){ 0, 0, 2, 8 }));
    GT_ASSERT_OK(gates_draw_image(&dl, (gates_rect_t){ 1, 1, 2, 2 }, gates_tree_image(t, id)));
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT_OK(gates_render_soft(&dl, target, be));
    GT_ASSERT(at(buf, 8, 1, 1).r == 255 && at(buf, 8, 2, 1).g == 0);
    GT_ASSERT(gates_draw_image(&dl, (gates_rect_t){ 0, 0, 1, 1 }, nullptr) == PROVEN_ERR_INVALID_ARG);
    /* A transparent pixel leaves what is under it, even scaled (its colour does not count). */
    gates_u8 clear[4] = { 0, 0, 0, 0 };
    gates_image_id_t cid = 0;
    GT_ASSERT_OK(gates_image_add_rgba(t, 1, 1, clear, 0, &cid));
    for (int i = 0; i < 64; i++) buf[i] = gates_pixel_pack(GATES_RGB(255, 255, 255));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_draw_image(&dl, (gates_rect_t){ 0, 0, 4, 4 }, gates_tree_image(t, cid)));
    GT_ASSERT_OK(gates_render_soft(&dl, target, be));
    GT_ASSERT(at(buf, 8, 1, 1).r == 255 && at(buf, 8, 1, 1).g == 255);
    /* Rows further apart than w * 4 (stride) are read row by row. */
    gates_u8 wide[2 * 12] = {0};
    for (int k = 0; k < 2; k++) { wide[k * 4 + 3] = 255; wide[12 + k * 4] = 200; wide[12 + k * 4 + 3] = 255; }
    gates_image_id_t sid = 0;
    GT_ASSERT_OK(gates_image_add_rgba(t, 2, 2, wide, 12, &sid));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_draw_image(&dl, (gates_rect_t){ 0, 0, 2, 2 }, gates_tree_image(t, sid)));
    GT_ASSERT_OK(gates_render_soft(&dl, target, be));
    GT_ASSERT(at(buf, 8, 0, 0).r == 0 && at(buf, 8, 0, 1).r == 200 && at(buf, 8, 1, 1).r == 200);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

/* -- the image node and icons ------------------------------------------------------------ */

typedef struct calls_t {
    int n;
} calls_t;
static void on_cmd(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    (void)id;
    ((calls_t *)user)->n++;
}

static void test_node_and_icons(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), img, fixed, btn, only;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_image_id_t pic = solid(t, 40, 20, 10, 20, 30, 255), ic = solid(t, 16, 16, 0, 0, 0, 255);
    GT_ASSERT_OK(gates_image_create(t, root, pic, &img));
    GT_ASSERT_OK(gates_layout_set_child_align(t, img, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_image_create(t, root, pic, &fixed));
    GT_ASSERT_OK(gates_layout_set_child_align(t, fixed, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_image_node_set_size(t, fixed, (gates_size_t){ 20, 20 }));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("Save"), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_layout_set_child_align(t, btn, GATES_ALIGN_START_V));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR(""), nullptr, nullptr, &only));
    GT_ASSERT_OK(gates_layout_set_child_align(t, only, GATES_ALIGN_START_V));
    gates_i32 plain_w = 0;
    layout(t);
    plain_w = gates_node_preferred_size(t, btn).w;
    GT_ASSERT_OK(gates_button_set_icon(t, btn, ic));
    GT_ASSERT_OK(gates_button_set_icon(t, only, ic));
    GT_ASSERT_OK(gates_node_set_access_name(t, only, GATES_STR("Settings")));
    layout(t);
    /* Sizes: natural, fixed with the aspect kept, icon plus label. */
    GT_ASSERT(gates_node_preferred_size(t, img).w == 40 && gates_node_preferred_size(t, img).h == 20);
    GT_ASSERT(gates_node_preferred_size(t, fixed).w == 20 && gates_node_preferred_size(t, fixed).h == 20);
    GT_ASSERT(gates_node_preferred_size(t, btn).w == plain_w + GATES_ICON_SIZE + 4);
    GT_ASSERT(gates_node_preferred_size(t, only).w >= 24 && gates_node_preferred_size(t, only).h >= 24);
    GT_ASSERT(gates_image_node_image(t, img) == pic);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    int images = 0;
    gates_rect_t fixed_r = gates_node_layout_rect(t, fixed), drawn_fixed = {0};
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind != GATES_DRAW_IMAGE) continue;
        images++;
        if (c->rect.y >= fixed_r.y && c->rect.y < fixed_r.y + fixed_r.h) drawn_fixed = c->rect;
    }
    GT_ASSERT(images == 4);                                       /* two pictures, two icons */
    GT_ASSERT(drawn_fixed.w == 20 && drawn_fixed.h == 10 && drawn_fixed.y == fixed_r.y + 5); /* 2:1 in 20 x 20 */
    /* A tall picture in a square: full height, centred across. */
    gates_image_id_t tall = solid(t, 20, 40, 5, 5, 5, 255);
    GT_ASSERT_OK(gates_image_node_set(t, fixed, tall));
    layout(t);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    fixed_r = gates_node_layout_rect(t, fixed);
    gates_rect_t tall_r = {0};
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind == GATES_DRAW_IMAGE && c->rect.y == fixed_r.y && c->rect.h == 20) tall_r = c->rect;
    }
    GT_ASSERT(tall_r.w == 10 && tall_r.x == fixed_r.x + 5);
    GT_ASSERT_OK(gates_image_node_set(t, fixed, pic));
    /* Removed: the node shows nothing, keeps no size of its own. */
    GT_ASSERT_OK(gates_image_remove(t, pic));
    layout(t);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    images = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) images += gates_draw_list_at(&dl, i)->kind == GATES_DRAW_IMAGE;
    GT_ASSERT(images == 2 && gates_node_preferred_size(t, img).w == 0);
    GT_ASSERT(gates_image_node_set(t, btn, pic) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_button_set_icon(t, img, ic) == PROVEN_ERR_INVALID_ARG);
    /* Accessibility: a named image is an Image, an unnamed one is decoration. */
    gates_image_id_t logo = solid(t, 8, 8, 1, 1, 1, 255);
    gates_node_t named;
    GT_ASSERT_OK(gates_image_create(t, root, logo, &named));
    GT_ASSERT_OK(gates_node_set_access_name(t, named, GATES_STR("Company logo")));
    GT_ASSERT_OK(gates_image_node_set(t, img, logo));
    layout(t);
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(t, named, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_IMAGE);
    GT_ASSERT_OK(gates_access_info(t, img, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_NONE);
    GT_ASSERT_OK(gates_access_info(t, only, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_BUTTON && info.name.size == 8);
    gates_access_issue_t issues[8];
    GT_ASSERT(gates_access_audit(t, theme, issues, 8) == 0);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static void test_command_icons(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), bar;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    calls_t calls = {0};
    gates_command_desc_t cut = { .id = 1, .label = GATES_STR("Cu&t"), .enabled = true, .invoke = on_cmd, .user = &calls };
    gates_command_desc_t bold = { .id = 2, .label = GATES_STR("&Bold"), .enabled = true, .checked = true,
                                  .invoke = on_cmd, .user = &calls };
    GT_ASSERT_OK(gates_command_register(t, root, &cut));
    GT_ASSERT_OK(gates_command_register(t, root, &bold));
    gates_image_id_t ic = solid(t, 16, 16, 9, 9, 9, 255);
    GT_ASSERT_OK(gates_command_set_icon(t, root, 1, ic));
    GT_ASSERT_OK(gates_command_set_icon(t, root, 2, ic));
    GT_ASSERT(gates_command_icon(t, root, 1) == ic);
    GT_ASSERT(gates_command_set_icon(t, root, 9, ic) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT_OK(gates_toolbar_create(t, root, root, &bar));
    GT_ASSERT_OK(gates_toolbar_add(t, bar, 1));
    GT_ASSERT_OK(gates_toolbar_add(t, bar, 2));
    layout(t);
    gates_i32 with_label = gates_node_preferred_size(t, bar).w;
    /* Toolbar: icon and label; icons only is narrower and keeps the label for the tooltip and name. */
    GT_ASSERT_OK(gates_toolbar_set_icons_only(t, bar, true));
    layout(t);
    GT_ASSERT(gates_node_preferred_size(t, bar).w < with_label);
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(t, bar, 1, &info));
    GT_ASSERT(info.name.size == 3 && info.description.size > 0);    /* "Cut", "Cut" tooltip */
    GT_ASSERT(gates_toolbar_set_icons_only(t, root, true) == PROVEN_ERR_INVALID_ARG);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    int images = 0;
    char texts[128] = "";
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        images += c->kind == GATES_DRAW_IMAGE;
        if (c->kind == GATES_DRAW_TEXT) {
            gates_str_t s = gates_draw_cmd_text(&dl, c);
            strncat(texts, (const char *)s.ptr, s.size < 20 ? s.size : 20);
        }
    }
    GT_ASSERT(images == 2 && strstr(texts, "Cut") == nullptr);    /* icons only: no labels */
    /* Menu: the icon in the gutter, the check mark kept for a checked command. */
    static const gates_command_id_t ids[] = { 1, 2 };
    gates_node_t menu;
    GT_ASSERT_OK(gates_menu_open(t, (gates_point_t){ 10, 60 }, root, ids, 2, &menu));
    layout(t);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    gates_rect_t mr = gates_node_layout_rect(t, menu);
    int in_menu = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(&dl, i);
        if (c->kind == GATES_DRAW_IMAGE && gates_rect_contains(mr, (gates_point_t){ c->rect.x, c->rect.y })) in_menu++;
    }
    GT_ASSERT(in_menu == 1);                                       /* Cut; Bold shows its check */
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

/* -- pixel sets for other scales (0.10.0) ------------------------------------------------ */

static gates_u8 *fill(gates_i32 w, gates_i32 h, gates_u8 r, gates_u8 g, gates_u8 b) {
    gates_u8 *px = malloc((size_t)w * (size_t)h * 4);
    for (gates_i32 i = 0; i < w * h; i++) {
        px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
    }
    return px;
}

static gates_err_t add_variant(gates_tree_t *t, gates_image_id_t id, gates_i32 w, gates_i32 h, gates_u8 r, gates_u8 g,
                               gates_u8 b) {
    gates_u8 *px = fill(w, h, r, g, b);
    gates_err_t err = gates_image_add_variant_rgba(t, id, w, h, px, 0);
    free(px);
    return err;
}

/* The colour at device pixel (x, y) of an image drawn at `r` (logical) and dpi. */
static gates_color_t drawn(gates_tree_t *t, gates_image_id_t id, gates_rect_t r, gates_u32 dpi, gates_i32 x, gates_i32 y) {
    static gates_u32 buf[64 * 64];
    for (int i = 0; i < 64 * 64; i++) buf[i] = gates_pixel_pack(GATES_RGB(0, 0, 0));
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_draw_image(&dl, r, gates_tree_image(t, id)));
    GT_ASSERT_OK(gates_render_soft_scaled(&dl, (gates_pixels_t){ .ptr = buf, .w = 64, .h = 64, .stride_bytes = 256 },
                                          be, dpi));
    gates_draw_list_deinit(&dl);
    return at(buf, 64, x, y);
}

static void test_variants(void) {
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_image_id_t icon = solid(t, 16, 16, 255, 0, 0, 255); /* red at 16 */
    GT_ASSERT(gates_image_variant_count(t, icon) == 1);
    GT_ASSERT(gates_image_variant_size(t, icon, 0).w == 16 && gates_image_variant_size(t, icon, 1).w == 0);
    /* Refused: another picture's shape, the natural width, bad pixels, unknown ids. */
    GT_ASSERT(add_variant(t, icon, 32, 30, 0, 0, 255) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(add_variant(t, icon, 32, 34, 0, 0, 255) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(add_variant(t, icon, 16, 16, 0, 0, 255) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(add_variant(t, 999, 32, 32, 0, 0, 255) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(add_variant(t, 0, 32, 32, 0, 0, 255) == PROVEN_ERR_NOT_FOUND);
    gates_u8 one[4] = {0};
    GT_ASSERT(gates_image_add_variant_rgba(t, icon, 0, 0, one, 0) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_image_add_variant_rgba(t, icon, 1, 1, nullptr, 0) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_image_add_variant_rgba(nullptr, icon, 1, 1, one, 0) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_image_variant_count(t, icon) == 1);
    /* Blue at 32, green at 24; kept by width; the natural size stays 16. */
    GT_ASSERT_OK(add_variant(t, icon, 32, 32, 0, 0, 255));
    GT_ASSERT_OK(add_variant(t, icon, 24, 24, 0, 255, 0));
    GT_ASSERT(gates_image_variant_count(t, icon) == 3);
    GT_ASSERT(gates_image_variant_size(t, icon, 0).w == 16);
    GT_ASSERT(gates_image_variant_size(t, icon, 1).w == 24 && gates_image_variant_size(t, icon, 2).h == 32);
    GT_ASSERT(gates_image_size(t, icon).w == 16);
    /* Picked: the smallest that covers, else the largest. */
    GT_ASSERT(gates_image_pick_size(t, icon, (gates_size_t){ 16, 16 }).w == 16);
    GT_ASSERT(gates_image_pick_size(t, icon, (gates_size_t){ 10, 10 }).w == 16);
    GT_ASSERT(gates_image_pick_size(t, icon, (gates_size_t){ 20, 20 }).w == 24);
    GT_ASSERT(gates_image_pick_size(t, icon, (gates_size_t){ 24, 24 }).w == 24);
    GT_ASSERT(gates_image_pick_size(t, icon, (gates_size_t){ 24, 25 }).w == 32);
    GT_ASSERT(gates_image_pick_size(t, icon, (gates_size_t){ 32, 32 }).w == 32);
    GT_ASSERT(gates_image_pick_size(t, icon, (gates_size_t){ 40, 40 }).w == 32);
    GT_ASSERT(gates_image_pick_size(t, 999, (gates_size_t){ 40, 40 }).w == 0);
    /* The renderer draws that set: red at 100 %, green at 150 %, blue at 200 %. */
    gates_rect_t r = { 1, 1, 16, 16 };
    gates_color_t c = drawn(t, icon, r, 96, 5, 5);
    GT_ASSERT(c.r == 255 && c.g == 0 && c.b == 0);
    c = drawn(t, icon, r, 144, 8, 8);
    GT_ASSERT(c.r == 0 && c.g == 255 && c.b == 0);
    c = drawn(t, icon, r, 192, 10, 10);
    GT_ASSERT(c.r == 0 && c.g == 0 && c.b == 255);
    c = drawn(t, icon, r, 288, 10, 10); /* 300 %: past the largest, the largest */
    GT_ASSERT(c.b == 255);
    c = drawn(t, icon, (gates_rect_t){ 0, 0, 8, 8 }, 96, 3, 3); /* smaller than natural: the natural */
    GT_ASSERT(c.r == 255);
    /* The same width again replaces that set; a smaller one goes first after the natural. */
    GT_ASSERT_OK(add_variant(t, icon, 24, 24, 255, 255, 0));
    GT_ASSERT(gates_image_variant_count(t, icon) == 3);
    c = drawn(t, icon, r, 144, 8, 8);
    GT_ASSERT(c.r == 255 && c.g == 255 && c.b == 0);
    GT_ASSERT_OK(add_variant(t, icon, 8, 8, 255, 255, 255));
    GT_ASSERT(gates_image_variant_count(t, icon) == 4);
    GT_ASSERT(gates_image_variant_size(t, icon, 0).w == 16 && gates_image_variant_size(t, icon, 1).w == 8);
    GT_ASSERT(gates_image_variant_size(t, icon, 2).w == 24 && gates_image_variant_size(t, icon, 3).w == 32);
    GT_ASSERT(gates_image_pick_size(t, icon, (gates_size_t){ 6, 6 }).w == 8);
    GT_ASSERT_OK(add_variant(t, icon, 48, 48, 1, 1, 1));
    GT_ASSERT(gates_image_variant_size(t, icon, 4).w == 48);
    /* A wide picture: h within one pixel of the shape. */
    gates_image_id_t wide = solid(t, 100, 75, 9, 9, 9, 255);
    GT_ASSERT_OK(add_variant(t, wide, 133, 100, 0, 0, 255));
    GT_ASSERT_OK(add_variant(t, wide, 150, 113, 0, 0, 255)); /* 112.5 */
    GT_ASSERT(add_variant(t, wide, 200, 152, 0, 0, 255) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(add_variant(t, wide, 200, 148, 0, 0, 255) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(add_variant(t, wide, 200, 151, 0, 0, 255));                    /* 150 + 1 */
    GT_ASSERT(add_variant(t, wide, 133, 101, 0, 0, 255) == PROVEN_ERR_INVALID_ARG); /* 99.75 + 1.25 */
    /* A new set repaints (the picture may look different at this scale). */
    gates_tree_clear_dirty(t, GATES_TREE_DIRTY_PAINT);
    GT_ASSERT_OK(add_variant(t, wide, 400, 300, 0, 0, 255));
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_PAINT) != 0);
    /* Decoded sets. */
    GT_ASSERT(gates_image_load_variant_memory(t, icon, "IMG 64 64", 9) == PROVEN_ERR_UNSUPPORTED);
    dec_t d = {0};
    gates_tree_set_image_decoder(t, &(gates_image_decoder_t){ .ctx = &d, .decode = test_decode });
    GT_ASSERT_OK(gates_image_load_variant_memory(t, icon, "IMG 64 64", 9));
    GT_ASSERT(gates_image_variant_size(t, icon, 5).w == 64);
    GT_ASSERT(gates_image_load_variant_memory(t, icon, "IMG 64 60", 9) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_image_load_variant_memory(t, 999, "IMG 64 64", 9) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(gates_image_load_variant_memory(t, 0, "IMG 64 64", 9) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(gates_image_load_variant_file(t, 0, GATES_STR("icon.png")) == PROVEN_ERR_NOT_FOUND);
    GT_ASSERT(gates_image_load_memory(t, "IMG 2 2", 7, nullptr) == PROVEN_ERR_INVALID_ARG);
    gates_image_id_t tiny = solid(t, 3, 2, 0, 0, 0, 255);
    GT_ASSERT(gates_image_load_variant_file(t, tiny, GATES_STR("same.png")) == PROVEN_ERR_INVALID_ARG); /* 3 x 2 again */
    GT_ASSERT(gates_image_load_variant_file(t, tiny, GATES_STR("x.bad")) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(d.calls == 4); /* an unknown id is refused before decoding */
    GT_ASSERT(gates_image_variant_count(t, 999) == 0);
    /* A button's icon at 200 %: the 32-pixel set. */
    gates_node_t btn;
    GT_ASSERT_OK(gates_button_create(t, gates_tree_root(t), GATES_STR(""), nullptr, nullptr, &btn));
    GT_ASSERT_OK(gates_button_set_icon(t, btn, icon));
    layout(t);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    static gates_u32 big[2 * VW * 2 * VH];
    gates_pixels_t target = { .ptr = big, .w = 2 * VW, .h = 2 * VH, .stride_bytes = 2 * VW * 4 };
    GT_ASSERT_OK(gates_render_soft_scaled(&dl, target, be, 192));
    bool blue = false;
    for (gates_u32 i = 0; i < gates_draw_list_len(&dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(&dl, i);
        if (cmd->kind != GATES_DRAW_IMAGE) continue;
        gates_rect_t d2 = gates_rect_px(cmd->rect, 192);
        gates_color_t m = at(big, 2 * VW, d2.x + d2.w / 2, d2.y + d2.h / 2);
        blue = d2.w == 32 && m.b == 255 && m.r == 0;
    }
    GT_ASSERT(blue);
    gates_draw_list_deinit(&dl);
    /* Removing the image frees every set (ASan). */
    GT_ASSERT_OK(gates_image_remove(t, wide));
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_store();
    test_render();
    test_node_and_icons();
    test_command_icons();
    test_variants();
    return gt_report("test_images");
}
