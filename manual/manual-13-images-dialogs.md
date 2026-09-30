# Chapter 13 - Images and native dialogs

Headers: `gates/image.h`, `gates/draw.h`, `gates/window.h`.

## Images

A tree keeps images by id. `gates_image_add_rgba(tree, w, h, pixels, stride, &id)` copies
straight-alpha RGBA pixels in; `gates_image_load_file` and `gates_image_load_memory` decode
PNG, JPEG, BMP, GIF, ICO or TIFF through the decoder the platform installs (every Windows
window has one; a bare tree answers UNSUPPORTED). `gates_image_remove` frees an image; nodes
that showed it then show nothing, and ids are never reused.

An image's natural size is its pixel size in logical units, so a 32 x 16 picture covers 32 x 16
units and scales with the window like text: at 150 % it is drawn on 48 x 24 pixels, smoothed
(bilinear). Supply larger pixels when a picture must stay sharp at large scales.

`gates_image_create(tree, parent, id, &node)` shows an image. `gates_image_node_set_size` gives
it another size; the picture then keeps its aspect ratio, centred. An image with an accessible
name (`gates_node_set_access_name`) is an Image to screen readers; without one it is decoration
and not announced.

## Icons

Icons are drawn at 16 x 16 units (`GATES_ICON_SIZE`). `gates_button_set_icon` puts one left of
a button's label; a button with an icon and no text is an icon button, and needs an access
name. `gates_command_set_icon` gives a command an icon: menus draw it in the gutter (a checked
command shows its mark instead) and toolbars left of the label, or alone with
`gates_toolbar_set_icons_only` - the label then lives on in the tooltip and the accessible name.

For custom painting, `gates_tree_image` returns the image for `gates_draw_image(dl, rect,
image)`; the draw list borrows it, so keep it until the list is rendered.

<!-- example: manual/examples/ex_13_images.c -->
```c
/* manual example (host): an image from pixels, an icon on a button and on a command.
 * expect: logo 32x16, drawn 32x16 at 192 dpi as 64x32 pixels; Save button 20 units wider with its icon */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), logo, save;
    /* 32 x 16 pixels, a left-to-right fade from blue to transparent. */
    static gates_u8 px[16][32][4];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
            px[y][x][0] = 0; px[y][x][1] = 90; px[y][x][2] = 200; px[y][x][3] = (gates_u8)(255 - x * 8);
        }
    }
    /* A 16 x 16 icon: a grey square. */
    static gates_u8 ic[16][16][4];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            ic[y][x][0] = ic[y][x][1] = ic[y][x][2] = 90; ic[y][x][3] = 255;
        }
    }
    gates_image_id_t pic = 0, icon = 0;
    if (!gates_is_ok(gates_image_add_rgba(t, 32, 16, &px[0][0][0], 0, &pic)) ||
        !gates_is_ok(gates_image_add_rgba(t, 16, 16, &ic[0][0][0], 0, &icon)) ||
        !gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_image_create(t, root, pic, &logo)) ||
        !gates_is_ok(gates_node_set_access_name(t, logo, GATES_STR("Logo"))) ||   /* named: an Image */
        !gates_is_ok(gates_layout_set_child_align(t, logo, GATES_ALIGN_START_V)) ||
        !gates_is_ok(gates_button_create(t, root, GATES_STR("Save"), nullptr, nullptr, &save)) ||
        !gates_is_ok(gates_layout_set_child_align(t, save, GATES_ALIGN_START_V)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 200, 100 }, gates_text_backend_builtin()))) {
        return 1;
    }
    gates_i32 plain = gates_node_preferred_size(t, save).w;
    if (!gates_is_ok(gates_button_set_icon(t, save, icon)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 200, 100 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* Render at 192 dpi: the logo covers 64 x 32 device pixels. */
    gates_draw_list_t dl;
    static gates_u32 pixels[200 * 2 * 100 * 2];
    gates_pixels_t target = { .ptr = pixels, .w = 400, .h = 200, .stride_bytes = 1600 };
    if (!gates_is_ok(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0)) ||
        !gates_is_ok(gates_paint_tree(t, &dl, gates_theme_light(), gates_text_backend_builtin())) ||
        !gates_is_ok(gates_render_soft_scaled(&dl, target, gates_text_backend_builtin(), 192))) {
        return 1;
    }
    gates_rect_t r = gates_node_layout_rect(t, logo), dev = gates_rect_px(r, 192);
    printf("logo %dx%d, drawn %dx%d at 192 dpi as %dx%d pixels; Save button %d units wider with its icon\n",
           gates_image_size(t, pic).w, gates_image_size(t, pic).h, r.w, r.h, dev.w, dev.h,
           gates_node_preferred_size(t, save).w - plain);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
    return 0;
}
```

## Native dialogs

Choosing a file, a folder or a colour, and asking a yes-or-no question, are best left to the
platform: people know its dialogs, and they come with the platform's keyboard support, screen
reader support and recent places. On Windows:

- `gates_window_open_file`, `gates_window_save_file` and `gates_window_choose_folder` take a
  `gates_file_dialog_t` (a title, filters such as `"Pictures|*.png;*.jpg|All files|*.*"`, a
  starting folder, and for saving a suggested name) and write the chosen path, in UTF-8, to your
  buffer. A save dialog asks before overwriting. `gates_window_open_files` lets the person pick
  several files: each path ends with a NUL byte in the buffer, and `count` says how many.
- `gates_window_choose_color` starts from a colour and answers with the chosen one.
- `gates_window_message` shows a message with OK, OK/Cancel, Yes/No or Yes/No/Cancel and an
  information, warning, error or question icon, and returns the answer (Escape and the close
  box answer Cancel).

These are the platform's modal dialogs: the call returns when the person answers, and the
window's own menus close first. A cancel is not an error - the path comes back empty, or
`chosen` false. Call them from a command or event handler, never while painting.

