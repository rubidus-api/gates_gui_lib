/* gates_gui_lib - the image a draw command borrows (0.5.0): shared by the
 * core's image store and the software renderer. Never installed. */
#ifndef GATES_IMAGE_INTERNAL_H
#define GATES_IMAGE_INTERNAL_H

#include <gates/types.h>

struct gates_image {
    gates_i32 w;
    gates_i32 h;
    gates_u32 *px;               /* w * h pixels, BGRA8 packed (gates_pixel_pack), straight alpha */
    struct gates_image *variant; /* 0.10.0: other pixel sets of the same picture, by width ascending */
};

/* The pixel set to draw into a w x h device rect: the smallest that covers
 * it, else the largest (scaling down stays sharp, scaling up blurs). */
static inline const struct gates_image *gates_i_image_pick(const struct gates_image *im, gates_i32 w, gates_i32 h) {
    const struct gates_image *best = nullptr, *largest = im;
    for (const struct gates_image *c = im; c != nullptr; c = c->variant) {
        if (c->w >= w && c->h >= h && (best == nullptr || c->w < best->w)) best = c;
        if (c->w > largest->w) largest = c;
    }
    return best != nullptr ? best : largest;
}

#endif /* GATES_IMAGE_INTERNAL_H */
