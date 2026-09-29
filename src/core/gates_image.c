/* gates_gui_lib - images: the tree's store, the decoder seam, the image node,
 * icons on buttons, commands and toolbars (plan-0020). Platform-free. */
#include <gates/image.h>
#include <gates/widget.h>
#include <gates/render.h>
#include "gates_tree_internal.h"
#include "../render/gates_image_internal.h"

#include <string.h>

#define IMAGE_MAX_SIDE 16384

/* -- the store ------------------------------------------------------------------------ */

static gates_i_image_slot *slot_of(const gates_tree_t *tree, gates_u32 id) {
    for (gates_u32 i = 0; id != 0 && i < tree->image_count; i++) {
        if (tree->images[i].id == id) return &tree->images[i];
    }
    return nullptr;
}

static void image_free(gates_allocator_t a, struct gates_image *im) {
    if (im == nullptr) return;
    if (im->px != nullptr) a.free_fn(a.ctx, im->px);
    a.free_fn(a.ctx, im);
}

gates_err_t gates_image_add_rgba(gates_tree_t *tree, gates_i32 w, gates_i32 h, const gates_u8 *rgba,
                                 gates_u32 stride, gates_image_id_t *out_id) {
    if (tree == nullptr || out_id == nullptr || rgba == nullptr || w <= 0 || h <= 0 || w > IMAGE_MAX_SIDE ||
        h > IMAGE_MAX_SIDE || (stride != 0 && stride < (gates_u32)w * 4u)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (stride == 0) stride = (gates_u32)w * 4u;
    gates_allocator_t a = tree->alloc;
    gates_i_image_slot *free_slot = nullptr;
    for (gates_u32 i = 0; i < tree->image_count && free_slot == nullptr; i++) {
        if (tree->images[i].id == 0) free_slot = &tree->images[i];
    }
    if (free_slot == nullptr && tree->image_count == tree->image_cap) {
        gates_u32 cap = tree->image_cap == 0 ? 8u : tree->image_cap * 2u;
        proven_result_mem_mut_t r =
            tree->images == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof *tree->images, alignof(gates_i_image_slot))
                : a.realloc_fn(a.ctx, tree->images, tree->image_cap * sizeof *tree->images, cap * sizeof *tree->images,
                               alignof(gates_i_image_slot));
        if (!proven_is_ok(r.err)) return r.err;
        tree->images = (gates_i_image_slot *)r.value.ptr;
        tree->image_cap = cap;
    }
    proven_result_mem_mut_t ri = a.alloc_fn(a.ctx, sizeof(struct gates_image), alignof(struct gates_image));
    if (!proven_is_ok(ri.err)) return ri.err;
    struct gates_image *im = (struct gates_image *)ri.value.ptr;
    proven_result_mem_mut_t rp = a.alloc_fn(a.ctx, (gates_usize_t)w * (gates_usize_t)h * 4u, alignof(gates_u32));
    if (!proven_is_ok(rp.err)) {
        a.free_fn(a.ctx, im);
        return rp.err;
    }
    *im = (struct gates_image){ .w = w, .h = h, .px = (gates_u32 *)rp.value.ptr };
    for (gates_i32 y = 0; y < h; y++) {
        const gates_u8 *src = rgba + (gates_usize_t)y * stride;
        for (gates_i32 x = 0; x < w; x++) {
            im->px[y * w + x] = gates_pixel_pack(GATES_RGBA(src[x * 4], src[x * 4 + 1], src[x * 4 + 2], src[x * 4 + 3]));
        }
    }
    if (free_slot == nullptr) free_slot = &tree->images[tree->image_count++];
    *free_slot = (gates_i_image_slot){ .id = ++tree->next_image_id, .image = im };
    *out_id = free_slot->id;
    return GATES_OK;
}

gates_err_t gates_image_remove(gates_tree_t *tree, gates_image_id_t id) {
    gates_i_image_slot *s = tree != nullptr ? slot_of(tree, id) : nullptr;
    if (s == nullptr) return PROVEN_ERR_NOT_FOUND;
    image_free(tree->alloc, s->image);
    *s = (gates_i_image_slot){0};
    gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT); /* nodes that showed it */
    return GATES_OK;
}

gates_size_t gates_image_size(const gates_tree_t *tree, gates_image_id_t id) {
    const gates_i_image_slot *s = tree != nullptr ? slot_of(tree, id) : nullptr;
    return s != nullptr ? (gates_size_t){ s->image->w, s->image->h } : (gates_size_t){ 0, 0 };
}

const gates_image_t *gates_tree_image(const gates_tree_t *tree, gates_image_id_t id) {
    const gates_i_image_slot *s = tree != nullptr ? slot_of(tree, id) : nullptr;
    return s != nullptr ? s->image : nullptr;
}

void gates_i_images_free(gates_tree_t *tree) {
    for (gates_u32 i = 0; i < tree->image_count; i++) {
        if (tree->images[i].id != 0) image_free(tree->alloc, tree->images[i].image);
    }
    if (tree->images != nullptr) tree->alloc.free_fn(tree->alloc.ctx, tree->images);
    tree->images = nullptr;
    tree->image_count = tree->image_cap = 0;
}

void gates_tree_set_image_decoder(gates_tree_t *tree, const gates_image_decoder_t *decoder) {
    if (tree == nullptr) return;
    tree->has_decoder = decoder != nullptr && decoder->decode != nullptr;
    tree->decoder = tree->has_decoder ? *decoder : (gates_image_decoder_t){0};
}

static gates_err_t load(gates_tree_t *tree, gates_str_t src, bool is_file, gates_image_id_t *out_id) {
    if (tree == nullptr || out_id == nullptr || (src.size > 0 && src.ptr == nullptr)) return PROVEN_ERR_INVALID_ARG;
    if (!tree->has_decoder) return PROVEN_ERR_UNSUPPORTED;
    gates_u8 *px = nullptr;
    gates_i32 w = 0, h = 0;
    gates_err_t err = tree->decoder.decode(tree->decoder.ctx, src, is_file, tree->alloc, &px, &w, &h);
    if (gates_is_ok(err)) {
        err = gates_image_add_rgba(tree, w, h, px, 0, out_id);
    }
    if (px != nullptr) tree->alloc.free_fn(tree->alloc.ctx, px);
    return err;
}

gates_err_t gates_image_load_file(gates_tree_t *tree, gates_str_t path, gates_image_id_t *out_id) {
    return load(tree, path, true, out_id);
}

gates_err_t gates_image_load_memory(gates_tree_t *tree, const void *bytes, gates_usize_t size, gates_image_id_t *out_id) {
    return load(tree, (gates_str_t){ .ptr = (const gates_u8 *)bytes, .size = size }, false, out_id);
}

/* -- drawing ------------------------------------------------------------------------------- */

gates_err_t gates_i_draw_image_fit(const gates_tree_t *tree, gates_draw_list_t *dl, gates_u32 id, gates_rect_t r) {
    const struct gates_image *im = gates_tree_image(tree, id);
    if (im == nullptr || r.w <= 0 || r.h <= 0) return GATES_OK;
    /* The largest rect of the image's aspect inside r, centred. */
    gates_i32 w = r.w, h = (gates_i32)((gates_i64)r.w * im->h / im->w);
    if (h > r.h) {
        h = r.h;
        w = (gates_i32)((gates_i64)r.h * im->w / im->h);
    }
    return gates_draw_image(dl, (gates_rect_t){ r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h }, im);
}

gates_size_t gates_i_image_measure(const gates_tree_t *tree, const gates_node_slot_t *s) {
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    if (st == nullptr) return (gates_size_t){ 0, 0 };
    if (st->image_size.w > 0 && st->image_size.h > 0) return st->image_size;
    return gates_image_size(tree, st->image);
}

/* -- the image node ---------------------------------------------------------------------- */

gates_err_t gates_image_create(gates_tree_t *tree, gates_node_t parent, gates_image_id_t image, gates_node_t *out_node) {
    if (tree == nullptr || out_node == nullptr ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_desc_t nd = { .kind = GATES_NODE_IMAGE };
    gates_node_t n;
    gates_err_t err = gates_node_create(tree, parent, &nd, &n);
    if (!gates_is_ok(err)) return err;
    gates_u32 state = GATES_NONE;
    err = gates_i_state_acquire(tree, &state);
    if (!gates_is_ok(err)) {
        (void)gates_node_destroy(tree, n);
        (void)gates_tree_flush_destroys(tree);
        return err;
    }
    gates_i_slot(tree, n.index)->state_index = state;
    gates_i_state(tree, state)->image = image;
    gates_i_mark_dirty(tree, n.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    *out_node = n;
    return GATES_OK;
}

static gates_widget_state_t *image_node(gates_tree_t *tree, gates_node_t node) {
    if (tree == nullptr || !gates_i_valid(tree, node) || gates_i_slot(tree, node.index)->kind != GATES_NODE_IMAGE) {
        return nullptr;
    }
    return gates_i_state(tree, gates_i_slot(tree, node.index)->state_index);
}

gates_err_t gates_image_node_set(gates_tree_t *tree, gates_node_t node, gates_image_id_t image) {
    gates_widget_state_t *st = image_node(tree, node);
    if (st == nullptr) return PROVEN_ERR_INVALID_ARG;
    st->image = image;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_image_node_set_size(gates_tree_t *tree, gates_node_t node, gates_size_t size) {
    gates_widget_state_t *st = image_node(tree, node);
    if (st == nullptr || size.w < 0 || size.h < 0) return PROVEN_ERR_INVALID_ARG;
    st->image_size = size;
    gates_i_mark_dirty(tree, node.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_image_id_t gates_image_node_image(const gates_tree_t *tree, gates_node_t node) {
    const gates_widget_state_t *st = image_node((gates_tree_t *)tree, node);
    return st != nullptr ? st->image : 0;
}

/* -- icons ---------------------------------------------------------------------------------- */

gates_err_t gates_button_set_icon(gates_tree_t *tree, gates_node_t button, gates_image_id_t icon) {
    if (tree == nullptr || !gates_i_valid(tree, button) || gates_i_slot(tree, button.index)->kind != GATES_NODE_BUTTON) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_state(tree, gates_i_slot(tree, button.index)->state_index)->icon = icon;
    gates_i_mark_dirty(tree, button.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_err_t gates_command_set_icon(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id, gates_image_id_t icon) {
    if (tree == nullptr || !gates_i_valid(tree, scope)) return PROVEN_ERR_INVALID_ARG;
    gates_i_command_t *c = gates_i_command_find(tree, scope.index, scope.generation, id);
    if (c == nullptr) return PROVEN_ERR_NOT_FOUND;
    c->icon = icon;
    gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_image_id_t gates_command_icon(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id) {
    if (tree == nullptr || !gates_i_valid(tree, scope)) return 0;
    const gates_i_command_t *c = gates_i_command_find(tree, scope.index, scope.generation, id);
    return c != nullptr ? c->icon : 0;
}

gates_err_t gates_toolbar_set_icons_only(gates_tree_t *tree, gates_node_t bar, bool icons_only) {
    if (tree == nullptr || !gates_i_valid(tree, bar) || gates_i_slot(tree, bar.index)->kind != GATES_NODE_TOOLBAR) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_i_state(tree, gates_i_slot(tree, bar.index)->state_index)->tbar->icons_only = icons_only;
    gates_i_mark_dirty(tree, bar.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    return GATES_OK;
}
