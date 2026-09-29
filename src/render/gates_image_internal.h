/* gates_gui_lib - the image a draw command borrows (plan-0020): shared by the
 * core's image store and the software renderer. Never installed. */
#ifndef GATES_IMAGE_INTERNAL_H
#define GATES_IMAGE_INTERNAL_H

#include <gates/types.h>

struct gates_image {
    gates_i32 w;
    gates_i32 h;
    gates_u32 *px;               /* w * h pixels, BGRA8 packed (gates_pixel_pack), straight alpha */
};

#endif /* GATES_IMAGE_INTERNAL_H */
