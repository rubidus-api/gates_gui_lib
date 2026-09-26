/* gates_gui_lib — 2D geometry primitives (RFC-0001 §11, §15, §20).
 * Pixel geometry is integer (gates_i32); float appears only in vectors that
 * carry sub-pixel data (pointer deltas). Platform-free. */
#ifndef GATES_GEOMETRY_H
#define GATES_GEOMETRY_H

#include <gates/types.h>

typedef struct gates_point_t {
    gates_i32 x;
    gates_i32 y;
} gates_point_t;

/* 2D size (RFC-0001 §11.1). Byte sizes are gates_usize_t in gates/types.h. */
typedef struct gates_size_t {
    gates_i32 w;
    gates_i32 h;
} gates_size_t;

typedef struct gates_rect_t {
    gates_i32 x;
    gates_i32 y;
    gates_i32 w;
    gates_i32 h;
} gates_rect_t;

typedef struct gates_vec2_t {
    float x;
    float y;
} gates_vec2_t;

static inline bool gates_rect_is_empty(gates_rect_t r) {
    return r.w <= 0 || r.h <= 0;
}

static inline bool gates_rect_contains(gates_rect_t r, gates_point_t p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.w && p.y < r.y + r.h;
}

/* Intersection; empty results are normalized to w=h=0. */
static inline gates_rect_t gates_rect_intersect(gates_rect_t a, gates_rect_t b) {
    gates_i32 x0 = a.x > b.x ? a.x : b.x;
    gates_i32 y0 = a.y > b.y ? a.y : b.y;
    gates_i32 x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    gates_i32 y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    if (x1 <= x0 || y1 <= y0) {
        return (gates_rect_t){ 0, 0, 0, 0 };
    }
    return (gates_rect_t){ x0, y0, x1 - x0, y1 - y0 };
}

/* -- logical units (plan-0013) ---------------------------------------------------
 *
 * Every coordinate and size the API takes or returns is a logical unit,
 * 1/96 inch. A window scales once at its boundary: drawing goes to device
 * pixels with these helpers and pointer positions come back through them.
 * At 96 dpi (100%) both are the identity. `dpi` 0 means 96. */
#define GATES_DPI_BASE 96

/* round-half-up(v * dpi / 96), in integers for any sign. Scale edges, not
 * lengths: a rect's pixel width is px(x + w) - px(x), so neighbours tile. */
static inline gates_i32 gates_px(gates_i32 v, gates_u32 dpi) {
    gates_i64 d = dpi != 0 ? (gates_i64)dpi : GATES_DPI_BASE;
    gates_i64 n = (gates_i64)v * d * 2 + GATES_DPI_BASE;
    gates_i64 q = n / (2 * GATES_DPI_BASE);
    if (n % (2 * GATES_DPI_BASE) != 0 && n < 0) q -= 1; /* floor */
    return (gates_i32)q;
}

/* The logical unit whose scaled span contains the device pixel: the v with
 * gates_px(v) <= px < gates_px(v + 1), i.e. floor((96 * (2px + 1) - 1) / (2 dpi)).
 * The exact inverse of the edge rounding above (a plain px * 96 / dpi would
 * put some pixels into the neighbouring unit at 125% and 175%). */
static inline gates_i32 gates_logical(gates_i32 px, gates_u32 dpi) {
    gates_i64 d = 2 * (dpi != 0 ? (gates_i64)dpi : GATES_DPI_BASE);
    gates_i64 n = (gates_i64)GATES_DPI_BASE * (2 * (gates_i64)px + 1) - 1;
    gates_i64 q = n / d;
    if (n % d != 0 && n < 0) q -= 1; /* floor */
    return (gates_i32)q;
}

static inline gates_rect_t gates_rect_px(gates_rect_t r, gates_u32 dpi) {
    gates_i32 x0 = gates_px(r.x, dpi), y0 = gates_px(r.y, dpi);
    return (gates_rect_t){ x0, y0, gates_px(r.x + r.w, dpi) - x0, gates_px(r.y + r.h, dpi) - y0 };
}

/* A line or border thickness: scaled, but never thinner than one pixel. */
static inline gates_i32 gates_thickness_px(gates_i32 t, gates_u32 dpi) {
    if (t <= 0) return 0;
    gates_i32 p = gates_px(t, dpi);
    return p < 1 ? 1 : p;
}

#endif /* GATES_GEOMETRY_H */
