/* gates_gui_lib - software renderer. Platform-free.
 * Reference implementation: correctness and determinism over speed. */
#include <gates/render.h>
#include <gates/text.h>
#include "gates_image_internal.h"

#define GATES_CLIP_STACK_MAX 64

typedef struct soft_ctx_t {
    gates_pixels_t px;
    gates_rect_t clip;                          /* current effective clip */
    gates_rect_t stack[GATES_CLIP_STACK_MAX];   /* saved clips */
    gates_i32 depth;
} soft_ctx_t;

static gates_u32 *row_at(const gates_pixels_t *px, gates_i32 y) {
    return (gates_u32 *)(void *)((gates_u8 *)px->ptr + (gates_usize_t)y * px->stride_bytes);
}

static gates_u32 blend_px(gates_u32 dst, gates_color_t src) {
    if (src.a == 255) {
        return gates_pixel_pack(src);
    }
    if (src.a == 0) {
        return dst;
    }
    gates_color_t d = gates_pixel_unpack(dst);
    gates_u32 a = src.a;
    gates_u32 na = 255u - a;
    gates_color_t out = {
        .r = (gates_u8)((src.r * a + d.r * na + 127u) / 255u),
        .g = (gates_u8)((src.g * a + d.g * na + 127u) / 255u),
        .b = (gates_u8)((src.b * a + d.b * na + 127u) / 255u),
        .a = (gates_u8)((a * 255u + (gates_u32)d.a * na + 127u) / 255u),
    };
    return gates_pixel_pack(out);
}

static void fill_rect(soft_ctx_t *ctx, gates_rect_t rect, gates_color_t color) {
    gates_rect_t r = gates_rect_intersect(rect, ctx->clip);
    if (gates_rect_is_empty(r) || color.a == 0) {
        return;
    }
    for (gates_i32 y = r.y; y < r.y + r.h; y++) {
        gates_u32 *row = row_at(&ctx->px, y);
        if (color.a == 255) {
            gates_u32 packed = gates_pixel_pack(color);
            for (gates_i32 x = r.x; x < r.x + r.w; x++) {
                row[x] = packed;
            }
        } else {
            for (gates_i32 x = r.x; x < r.x + r.w; x++) {
                row[x] = blend_px(row[x], color);
            }
        }
    }
}

/* One source channel at (sx, sy) in 1/256ths, bilinear, edges clamped. */
static gates_color_t sample(const struct gates_image *im, gates_i32 fx, gates_i32 fy) {
    gates_i32 x0 = fx >> 8, y0 = fy >> 8, ax = fx & 255, ay = fy & 255;
    gates_i32 x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) { x0 = 0; ax = 0; }
    if (y0 < 0) { y0 = 0; ay = 0; }
    if (x0 >= im->w - 1) { x0 = im->w - 1; ax = 0; }
    if (y0 >= im->h - 1) { y0 = im->h - 1; ay = 0; }
    if (x1 > im->w - 1) x1 = im->w - 1;
    if (y1 > im->h - 1) y1 = im->h - 1;
    gates_color_t p00 = gates_pixel_unpack(im->px[y0 * im->w + x0]), p10 = gates_pixel_unpack(im->px[y0 * im->w + x1]);
    gates_color_t p01 = gates_pixel_unpack(im->px[y1 * im->w + x0]), p11 = gates_pixel_unpack(im->px[y1 * im->w + x1]);
    gates_i32 w00 = (256 - ax) * (256 - ay), w10 = ax * (256 - ay), w01 = (256 - ax) * ay, w11 = ax * ay;
    /* Weight colours by alpha so transparent neighbours do not darken the edge. */
    gates_i32 a = p00.a * w00 + p10.a * w10 + p01.a * w01 + p11.a * w11;
    gates_color_t out = { .a = (gates_u8)((a + 32768) >> 16) };
    if (a > 0) {
        out.r = (gates_u8)(((gates_i64)p00.r * p00.a * w00 + (gates_i64)p10.r * p10.a * w10 +
                            (gates_i64)p01.r * p01.a * w01 + (gates_i64)p11.r * p11.a * w11 + a / 2) / a);
        out.g = (gates_u8)(((gates_i64)p00.g * p00.a * w00 + (gates_i64)p10.g * p10.a * w10 +
                            (gates_i64)p01.g * p01.a * w01 + (gates_i64)p11.g * p11.a * w11 + a / 2) / a);
        out.b = (gates_u8)(((gates_i64)p00.b * p00.a * w00 + (gates_i64)p10.b * p10.a * w10 +
                            (gates_i64)p01.b * p01.a * w01 + (gates_i64)p11.b * p11.a * w11 + a / 2) / a);
    }
    return out;
}

/* The image scaled into `dst` (device pixels): pixel centres map onto pixel
 * centres, so a 1:1 draw is exact. */
static void draw_image(soft_ctx_t *ctx, gates_rect_t dst, const struct gates_image *im) {
    gates_rect_t r = gates_rect_intersect(dst, ctx->clip);
    if (im == nullptr || im->w <= 0 || im->h <= 0 || gates_rect_is_empty(r) || dst.w <= 0 || dst.h <= 0) {
        return;
    }
    im = gates_i_image_pick(im, dst.w, dst.h); /* 0.10.0: the pixel set for this scale */
    for (gates_i32 y = r.y; y < r.y + r.h; y++) {
        gates_u32 *row = row_at(&ctx->px, y);
        /* source y in 1/256ths: ((y - dst.y) + 0.5) * h / dst.h - 0.5 */
        gates_i32 fy = (gates_i32)(((gates_i64)(2 * (y - dst.y) + 1) * im->h * 256) / (2 * dst.h)) - 128;
        for (gates_i32 x = r.x; x < r.x + r.w; x++) {
            gates_i32 fx = (gates_i32)(((gates_i64)(2 * (x - dst.x) + 1) * im->w * 256) / (2 * dst.w)) - 128;
            row[x] = blend_px(row[x], sample(im, fx, fy));
        }
    }
}

/* Border as four non-overlapping strips - corners are drawn exactly once,
 * which matters for translucent colors. */
static void fill_border(soft_ctx_t *ctx, gates_rect_t r, gates_i32 t, gates_color_t color) {
    if (r.w <= 0 || r.h <= 0 || t < 1) {
        return;
    }
    if (t * 2 >= r.w || t * 2 >= r.h) {
        fill_rect(ctx, r, color); /* border thicker than the rect: solid */
        return;
    }
    fill_rect(ctx, (gates_rect_t){ r.x, r.y, r.w, t }, color);                    /* top */
    fill_rect(ctx, (gates_rect_t){ r.x, r.y + r.h - t, r.w, t }, color);          /* bottom */
    fill_rect(ctx, (gates_rect_t){ r.x, r.y + t, t, r.h - 2 * t }, color);        /* left */
    fill_rect(ctx, (gates_rect_t){ r.x + r.w - t, r.y + t, t, r.h - 2 * t }, color); /* right */
}

static void put_px(soft_ctx_t *ctx, gates_i32 x, gates_i32 y, gates_color_t color) {
    if (!gates_rect_contains(ctx->clip, (gates_point_t){ x, y })) {
        return;
    }
    gates_u32 *row = row_at(&ctx->px, y);
    row[x] = blend_px(row[x], color);
}

/* Bresenham; endpoints inclusive. The error terms are 64-bit (2 * err overflows 32 bits for
 * spans past 2^30). The pixels are those of the plain walk, but the walk starts where the line
 * first reaches the clip and stops where it leaves it: along the major axis step n the minor
 * coordinate is the closed form (2 n minor + major - 1) / (2 major) (checked against the walk),
 * so a line with far-off endpoints costs only the pixels it can show. */
__extension__ typedef __int128 gates_i128_t; /* 2 n minor reaches 2^66 for a line across all of 32 bits */
static gates_i64 line_minor_after(gates_i64 n, gates_i64 minor, gates_i64 major) {
    return (gates_i64)(((gates_i128_t)2 * n * minor + major - 1) / ((gates_i128_t)2 * major));
}

static void draw_line(soft_ctx_t *ctx, gates_point_t p0, gates_point_t p1, gates_color_t color) {
    gates_rect_t c = ctx->clip;
    if (c.w <= 0 || c.h <= 0) {
        return;
    }
    gates_i64 cx0 = c.x, cx1 = (gates_i64)c.x + c.w - 1, cy0 = c.y, cy1 = (gates_i64)c.y + c.h - 1;
    if ((p0.x < cx0 && p1.x < cx0) || (p0.x > cx1 && p1.x > cx1) || (p0.y < cy0 && p1.y < cy0) ||
        (p0.y > cy1 && p1.y > cy1)) {
        return;
    }
    gates_i64 dx = p1.x > p0.x ? (gates_i64)p1.x - p0.x : (gates_i64)p0.x - p1.x;
    gates_i64 dy = p1.y > p0.y ? (gates_i64)p1.y - p0.y : (gates_i64)p0.y - p1.y;
    gates_i32 sx = p0.x < p1.x ? 1 : -1;
    gates_i32 sy = p0.y < p1.y ? 1 : -1;
    bool xmajor = dx >= dy;
    gates_i64 major = xmajor ? dx : dy;
    gates_i64 minor = xmajor ? dy : dx;
    if (major == 0) { /* one point */
        put_px(ctx, p0.x, p0.y, color);
        return;
    }
    /* The steps [lo, hi] along the major axis whose pixel can lie inside the clip. */
    gates_i64 lo = 0, hi = major;
    {
        /* major coordinate window */
        gates_i64 m0 = xmajor ? p0.x : p0.y;
        gates_i64 ms = xmajor ? sx : sy;
        gates_i64 w0 = xmajor ? cx0 : cy0, w1 = xmajor ? cx1 : cy1;
        gates_i64 a = (w0 - m0) * ms, b = (w1 - m0) * ms; /* steps at which the major coordinate is w0 / w1 */
        gates_i64 first = a < b ? a : b, last = a < b ? b : a;
        if (first > lo) lo = first;
        if (last < hi) hi = last;
        if (lo > hi) {
            return;
        }
        /* minor coordinate window: the steps where the minor coordinate is inside */
        gates_i64 n0 = xmajor ? p0.y : p0.x;
        gates_i64 ns = xmajor ? sy : sx;
        gates_i64 v0 = xmajor ? cy0 : cx0, v1 = xmajor ? cy1 : cx1;
        gates_i64 k_lo = (v0 - n0) * ns, k_hi = (v1 - n0) * ns; /* minor steps to reach the window edges */
        if (k_lo > k_hi) { gates_i64 t = k_lo; k_lo = k_hi; k_hi = t; }
        /* minor steps made after n major steps: line_minor_after(n); find the first n with >= k_lo
         * and the last n with <= k_hi by binary search (the function is non-decreasing). */
        if (k_lo > line_minor_after(hi, minor, major) || k_hi < 0) {
            return;
        }
        if (k_lo > 0) {
            gates_i64 l = lo, h = hi;
            while (l < h) {
                gates_i64 mid = l + (h - l) / 2;
                if (line_minor_after(mid, minor, major) >= k_lo) h = mid; else l = mid + 1;
            }
            if (l > lo) lo = l;
        }
        if (k_hi < line_minor_after(hi, minor, major)) {
            gates_i64 l = lo, h = hi;
            while (l < h) { /* the last n with minor <= k_hi */
                gates_i64 mid = l + (h - l + 1) / 2;
                if (line_minor_after(mid, minor, major) <= k_hi) l = mid; else h = mid - 1;
            }
            if (l < hi) hi = l;
        }
        if (lo > hi) {
            return;
        }
    }
    for (gates_i64 n = lo; n <= hi; n++) {
        gates_i64 m = line_minor_after(n, minor, major);
        gates_i32 x = xmajor ? (gates_i32)(p0.x + sx * n) : (gates_i32)(p0.x + sx * m);
        gates_i32 y = xmajor ? (gates_i32)(p0.y + sy * m) : (gates_i32)(p0.y + sy * n);
        put_px(ctx, x, y, color);
    }
}

gates_err_t gates_render_soft(const gates_draw_list_t *dl, gates_pixels_t target,
                              const gates_text_backend_t *text_backend) {
    return gates_render_soft_scaled(dl, target, text_backend, GATES_DPI_BASE);
}

gates_err_t gates_render_soft_scaled(const gates_draw_list_t *dl, gates_pixels_t target,
                                     const gates_text_backend_t *text_backend, gates_u32 dpi) {
    if (dl == nullptr || target.ptr == nullptr || target.w <= 0 || target.h <= 0) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (target.stride_bytes < (gates_u32)target.w * 4u) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (!gates_draw_list_balanced(dl)) {
        return PROVEN_ERR_INVALID_STATE;
    }

    soft_ctx_t ctx = {
        .px = target,
        .clip = { 0, 0, target.w, target.h },
        .depth = 0,
    };

    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *cmd = gates_draw_list_at(dl, i);
        switch (cmd->kind) {
        case GATES_DRAW_RECT:
            fill_rect(&ctx, gates_rect_px(cmd->rect, dpi), cmd->color);
            break;
        case GATES_DRAW_BORDER:
            fill_border(&ctx, gates_rect_px(cmd->rect, dpi), gates_thickness_px(cmd->thickness, dpi),
                        cmd->color);
            break;
        case GATES_DRAW_LINE:
            draw_line(&ctx, (gates_point_t){ gates_px(cmd->p0.x, dpi), gates_px(cmd->p0.y, dpi) },
                      (gates_point_t){ gates_px(cmd->p1.x, dpi), gates_px(cmd->p1.y, dpi) },
                      cmd->color);
            break;
        case GATES_DRAW_CLIP_PUSH:
            if (ctx.depth >= GATES_CLIP_STACK_MAX) {
                return PROVEN_ERR_OUT_OF_BOUNDS;
            }
            ctx.stack[ctx.depth++] = ctx.clip;
            ctx.clip = gates_rect_intersect(ctx.clip, gates_rect_px(cmd->rect, dpi));
            break;
        case GATES_DRAW_CLIP_POP:
            /* Balance was pre-validated; depth 0 here would be a list bug. */
            if (ctx.depth <= 0) {
                return PROVEN_ERR_INVALID_STATE;
            }
            ctx.clip = ctx.stack[--ctx.depth];
            break;
        case GATES_DRAW_TEXT:
            if (text_backend == nullptr || text_backend->draw == nullptr) {
                return PROVEN_ERR_UNSUPPORTED;
            }
            if (text_backend->draw_scaled != nullptr && dpi != 0 && dpi != GATES_DPI_BASE) {
                text_backend->draw_scaled(text_backend->ctx, target, gates_rect_px(cmd->rect, dpi),
                                          ctx.clip, cmd->font, gates_draw_cmd_text(dl, cmd),
                                          cmd->color, dpi);
            } else {
                text_backend->draw(text_backend->ctx, target, gates_rect_px(cmd->rect, dpi), ctx.clip,
                                   cmd->font, gates_draw_cmd_text(dl, cmd), cmd->color);
            }
            break;
        case GATES_DRAW_IMAGE:
            draw_image(&ctx, gates_rect_px(cmd->rect, dpi), cmd->image);
            break;
        default:
            return PROVEN_ERR_INVALID_ARG;
        }
    }
    return GATES_OK;
}
