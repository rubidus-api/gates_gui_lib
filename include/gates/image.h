/* gates_gui_lib - images and icons (0.5.0).
 *
 * A tree owns images by id (0 = none): RGBA8 pixels copied in, or decoded from
 * a file or memory by the decoder the platform installs (the Win32 window
 * installs Windows Imaging Component: PNG, JPEG, BMP, GIF, ICO, TIFF). An
 * image's natural size is its pixel size in logical units, so it scales with
 * the window like everything else (bilinear). Removing an image leaves the
 * nodes that showed it empty; ids are never reused within a tree.
 *
 * An image node shows one image, at its natural size or a size the program
 * sets (then the picture keeps its aspect ratio, centred). With an accessible
 * name (gates_node_set_access_name) it is an Image to assistive technology;
 * without one it is decoration and left out. Icons are drawn at 16 x 16 units:
 * on buttons (left of the label; a button with an icon and no text needs an
 * access name), on commands (menus draw it in the gutter, toolbars left of the
 * label or alone with gates_toolbar_set_icons_only).
 *
 * Variants (0.10.0). An image may hold more pixel sets of the same picture -
 * a 16-unit icon drawn at 16, 24 and 32 pixels, say. Its natural size stays
 * the first set's; whenever it is drawn, the renderer takes the smallest set
 * that covers the device pixels it fills, else the largest, so icons stay
 * crisp at 150 % and 200 % and any image drawn larger than its natural size
 * gains too. Platform-free. */
#ifndef GATES_IMAGE_H
#define GATES_IMAGE_H

#include <gates/tree.h>
#include <gates/command.h>

typedef gates_u32 gates_image_id_t;
typedef struct gates_image gates_image_t;   /* opaque: for gates_draw_image */

#define GATES_ICON_SIZE 16

/* Copies w x h straight-alpha RGBA8 pixels (rows `stride` bytes apart, 0 =
 * w * 4). INVALID_ARG for a size of 0 or over 16384 on a side. */
[[nodiscard]] gates_err_t gates_image_add_rgba(gates_tree_t *tree, gates_i32 w, gates_i32 h,
                                               const gates_u8 *rgba, gates_u32 stride,
                                               gates_image_id_t *out_id);
/* NOT_FOUND for an unknown id. */
[[nodiscard]] gates_err_t gates_image_remove(gates_tree_t *tree, gates_image_id_t id);
/* The natural size ({0, 0} for an unknown id). */
gates_size_t gates_image_size(const gates_tree_t *tree, gates_image_id_t id);
/* For custom painting with gates_draw_image (valid until the image is removed). */
const gates_image_t *gates_tree_image(const gates_tree_t *tree, gates_image_id_t id);

/* The decoder seam: turns a file (a UTF-8 path) or bytes into RGBA8 pixels
 * allocated from `alloc` (the tree frees them after copying). */
typedef struct gates_image_decoder_t {
    void *ctx;
    gates_err_t (*decode)(void *ctx, gates_str_t path_or_bytes, bool is_file, gates_allocator_t alloc,
                          gates_u8 **out_rgba, gates_i32 *out_w, gates_i32 *out_h);
} gates_image_decoder_t;
/* Copied; null removes it. */
void gates_tree_set_image_decoder(gates_tree_t *tree, const gates_image_decoder_t *decoder);
/* UNSUPPORTED without a decoder; the decoder's error when it cannot decode. */
[[nodiscard]] gates_err_t gates_image_load_file(gates_tree_t *tree, gates_str_t path, gates_image_id_t *out_id);
[[nodiscard]] gates_err_t gates_image_load_memory(gates_tree_t *tree, const void *bytes, gates_usize_t size,
                                                  gates_image_id_t *out_id);

/* Adds a pixel set to image `id` (0.10.0): the same picture at another size,
 * copied like gates_image_add_rgba. Its h must be within one pixel of
 * w * (natural h) / (natural w), and its width not the natural one
 * (INVALID_ARG otherwise); a set as wide as one added before replaces it.
 * NOT_FOUND for an unknown id. The load forms decode like the ones above. */
[[nodiscard]] gates_err_t gates_image_add_variant_rgba(gates_tree_t *tree, gates_image_id_t id, gates_i32 w,
                                                       gates_i32 h, const gates_u8 *rgba, gates_u32 stride);
[[nodiscard]] gates_err_t gates_image_load_variant_file(gates_tree_t *tree, gates_image_id_t id, gates_str_t path);
[[nodiscard]] gates_err_t gates_image_load_variant_memory(gates_tree_t *tree, gates_image_id_t id,
                                                          const void *bytes, gates_usize_t size);
/* Pixel sets, the natural one first then the others by width (0 for an
 * unknown id), and the size of one ({0, 0} past the end). */
gates_u32 gates_image_variant_count(const gates_tree_t *tree, gates_image_id_t id);
gates_size_t gates_image_variant_size(const gates_tree_t *tree, gates_image_id_t id, gates_u32 index);
/* The pixel size of the set drawn into `device` pixels ({0, 0} for an unknown
 * id): what a renderer of its own should use too. */
gates_size_t gates_image_pick_size(const gates_tree_t *tree, gates_image_id_t id, gates_size_t device);

/* -- the image node and icons ------------------------------------------------------ */

/* An image node showing `image` (0 = nothing yet); gates_image_node_set changes it. */
[[nodiscard]] gates_err_t gates_image_create(gates_tree_t *tree, gates_node_t parent, gates_image_id_t image,
                                             gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_image_node_set(gates_tree_t *tree, gates_node_t node, gates_image_id_t image);
/* {0, 0} = the natural size. */
[[nodiscard]] gates_err_t gates_image_node_set_size(gates_tree_t *tree, gates_node_t node, gates_size_t size);
gates_image_id_t gates_image_node_image(const gates_tree_t *tree, gates_node_t node);

/* 0 removes it. */
[[nodiscard]] gates_err_t gates_button_set_icon(gates_tree_t *tree, gates_node_t button, gates_image_id_t icon);
/* Menus and toolbars show the command's icon. */
[[nodiscard]] gates_err_t gates_command_set_icon(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id,
                                                 gates_image_id_t icon);
gates_image_id_t gates_command_icon(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
/* Toolbar buttons with an icon show only it (the label goes to the tooltip). */
[[nodiscard]] gates_err_t gates_toolbar_set_icons_only(gates_tree_t *tree, gates_node_t bar, bool icons_only);

#endif /* GATES_IMAGE_H */
