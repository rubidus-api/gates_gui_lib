/* gates_gui_lib — Win32 GDI text backend (RFC-0002 §5).
 *
 * Real system glyphs (including Hangul and CJK) while still honouring the
 * §3 metrics contract. The trick is that the backend never lets GDI advance
 * the pen: it computes every cell position itself from `advance` and passes
 * an explicit per-character dx array to ExtTextOutW. `measure == cells *
 * advance` therefore holds on ANY font, so layout is identical to the
 * builtin backend and only the glyph shapes differ.
 *
 * Rendering path: GDI draws into a private 32-bit DIB, then the covered
 * pixels are blended into the caller's target so the renderer stays the
 * single owner of the framebuffer. */
#include "gates_win32_internal.h"

#include <string.h>

#define GDI_DEFAULT_HEIGHT 16
#define GDI_SCRATCH_MAX_W 4096

typedef struct gdi_text_t {
    HFONT font;
    gates_i32 requested_height;
    gates_text_metrics_t metrics;

    /* Scratch DIB used to rasterize one run before blending. */
    HDC dc;
    HBITMAP dib;
    HBITMAP old_bitmap;
    gates_u32 *pixels;
    gates_i32 scratch_w;
    gates_i32 scratch_h;
} gdi_text_t;

static gdi_text_t g_gdi;

/* Fixed-pitch faces in preference order; the stock fixed font is the floor. */
static HFONT create_font(gates_i32 height) {
    static const wchar_t *faces[] = { L"Consolas", L"D2Coding", L"Courier New" };
    for (unsigned i = 0; i < sizeof faces / sizeof *faces; i++) {
        HFONT f = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, faces[i]);
        if (f != nullptr) {
            return f;
        }
    }
    return (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
}

static bool ensure_scratch(gates_i32 w, gates_i32 h) {
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (w > GDI_SCRATCH_MAX_W) {
        w = GDI_SCRATCH_MAX_W;
    }
    if (g_gdi.pixels != nullptr && g_gdi.scratch_w >= w && g_gdi.scratch_h >= h) {
        return true;
    }
    gates_i32 want_w = g_gdi.scratch_w > w ? g_gdi.scratch_w : w;
    gates_i32 want_h = g_gdi.scratch_h > h ? g_gdi.scratch_h : h;

    if (g_gdi.dc != nullptr) {
        SelectObject(g_gdi.dc, g_gdi.old_bitmap);
        DeleteObject(g_gdi.dib);
        DeleteDC(g_gdi.dc);
        g_gdi.dc = nullptr;
        g_gdi.dib = nullptr;
        g_gdi.pixels = nullptr;
    }
    BITMAPINFO bmi = { .bmiHeader = {
        .biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = want_w,
        .biHeight = -want_h,     /* top-down */
        .biPlanes = 1,
        .biBitCount = 32,
        .biCompression = BI_RGB,
    }};
    void *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (dib == nullptr || dc == nullptr || bits == nullptr) {
        if (dib != nullptr) DeleteObject(dib);
        if (dc != nullptr) DeleteDC(dc);
        return false;
    }
    g_gdi.dc = dc;
    g_gdi.dib = dib;
    g_gdi.old_bitmap = (HBITMAP)SelectObject(dc, dib);
    g_gdi.pixels = (gates_u32 *)bits;
    g_gdi.scratch_w = want_w;
    g_gdi.scratch_h = want_h;
    SetBkMode(dc, TRANSPARENT);
    return true;
}

static void ensure_font(gates_i32 font_size) {
    gates_i32 height = font_size > 0 ? font_size : GDI_DEFAULT_HEIGHT;
    if (g_gdi.font != nullptr && g_gdi.requested_height == height) {
        return;
    }
    if (g_gdi.font != nullptr) {
        DeleteObject(g_gdi.font);
    }
    g_gdi.font = create_font(height);
    g_gdi.requested_height = height;

    HDC screen = GetDC(nullptr);
    HDC probe = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    HGDIOBJ old = SelectObject(probe, g_gdi.font);
    TEXTMETRICW tm;
    GetTextMetricsW(probe, &tm);
    /* The cell advance comes from a real digit rather than tmAveCharWidth,
     * which some fonts round oddly. */
    INT digit_w = 0;
    if (!GetCharWidth32W(probe, L'0', L'0', &digit_w) || digit_w <= 0) {
        digit_w = tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : height / 2;
    }
    SelectObject(probe, old);
    DeleteDC(probe);

    g_gdi.metrics = (gates_text_metrics_t){
        .advance = digit_w,
        .ascent = tm.tmAscent,
        .descent = tm.tmDescent,
        .line_height = tm.tmHeight + tm.tmExternalLeading,
    };
    if (g_gdi.metrics.line_height < g_gdi.metrics.ascent + g_gdi.metrics.descent) {
        g_gdi.metrics.line_height = g_gdi.metrics.ascent + g_gdi.metrics.descent;
    }
}

static gates_text_metrics_t gdi_metrics(void *ctx, gates_i32 font_size) {
    (void)ctx;
    ensure_font(font_size);
    return g_gdi.metrics;
}

static gates_size_t gdi_measure(void *ctx, gates_i32 font_size, gates_str_t text) {
    (void)ctx;
    ensure_font(font_size);
    /* By contract, not by asking the font (RFC-0002 §3). */
    return (gates_size_t){ (gates_i32)gates_text_cells(text) * g_gdi.metrics.advance,
                           g_gdi.metrics.line_height };
}

static gates_u32 *target_row(gates_pixels_t *px, gates_i32 y) {
    return (gates_u32 *)(void *)((gates_u8 *)px->ptr + (gates_usize_t)y * px->stride_bytes);
}

/* A font for drawing at another DPI (plan-0013); metrics stay those of the
 * 96-dpi font, so layout never changes with the scale. */
static HFONT g_draw_font;
static gates_i32 g_draw_height;

static HFONT draw_font(gates_i32 font_size, gates_u32 dpi) {
    if (dpi == 0 || dpi == GATES_DPI_BASE) {
        return g_gdi.font;
    }
    gates_i32 height = gates_px(font_size > 0 ? font_size : GDI_DEFAULT_HEIGHT, dpi);
    if (g_draw_font == nullptr || g_draw_height != height) {
        if (g_draw_font != nullptr) DeleteObject(g_draw_font);
        g_draw_font = create_font(height);
        g_draw_height = height;
    }
    return g_draw_font;
}

/* Draws a run at `dpi`: rect and clip are device pixels; cell k of the run
 * starts at gates_px(k * advance) from the run origin, the same edge rounding
 * the renderer uses for every other rect, so glyphs sit on the scaled grid. */
static void draw_run(gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                     gates_i32 font_size, gates_str_t text, gates_color_t color, gates_u32 dpi) {
    if (target.ptr == nullptr || text.size == 0 || color.a == 0) {
        return;
    }
    ensure_font(font_size);
    HFONT font = draw_font(font_size, dpi);

    gates_rect_t bounds = gates_rect_intersect(
        gates_rect_intersect(rect, clip), (gates_rect_t){ 0, 0, target.w, target.h });
    if (gates_rect_is_empty(bounds)) {
        return;
    }

    gates_i32 cells = (gates_i32)gates_text_cells(text);
    gates_i32 adv = g_gdi.metrics.advance;
    gates_i32 run_w = gates_px(cells * adv, dpi);
    gates_i32 run_h = gates_px(g_gdi.metrics.line_height, dpi);
    if (!ensure_scratch(run_w, run_h)) {
        return;
    }
    if (run_w > g_gdi.scratch_w) {
        run_w = g_gdi.scratch_w;
    }

    /* UTF-8 -> UTF-16 with a per-character dx array holding each cell width,
     * so GDI places every glyph exactly where the layout expects it. */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char *)text.ptr, (int)text.size,
                                   nullptr, 0);
    if (wlen <= 0) {
        return;
    }
    wchar_t *wbuf = (wchar_t *)HeapAlloc(GetProcessHeap(), 0,
                                         (SIZE_T)wlen * sizeof(wchar_t));
    INT *dx = (INT *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)wlen * sizeof(INT));
    if (wbuf == nullptr || dx == nullptr) {
        if (wbuf != nullptr) HeapFree(GetProcessHeap(), 0, wbuf);
        if (dx != nullptr) HeapFree(GetProcessHeap(), 0, dx);
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, (const char *)text.ptr, (int)text.size, wbuf, wlen);

    gates_i32 cell = 0; /* cells placed so far */
    for (int i = 0; i < wlen; i++) {
        gates_u32 cp = wbuf[i];
        bool pair = cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wlen;
        if (pair) {
            gates_u32 low = wbuf[i + 1];
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
        gates_i32 w = (gates_i32)gates_text_cell_width(cp);
        dx[i] = (INT)(gates_px((cell + w) * adv, dpi) - gates_px(cell * adv, dpi));
        cell += w;
        if (pair) {
            dx[i + 1] = 0; /* surrogate pair: the lead carries the whole advance */
            i++;
        }
    }

    /* Rasterize white-on-black, then blend the coverage with the wanted color. */
    for (gates_i32 y = 0; y < run_h; y++) {
        memset(g_gdi.pixels + (gates_usize_t)y * g_gdi.scratch_w, 0,
               (gates_usize_t)run_w * 4u);
    }
    HGDIOBJ old_font = SelectObject(g_gdi.dc, font);
    SetTextColor(g_gdi.dc, RGB(255, 255, 255));
    /* ETO_CLIPPED keeps a glyph that is wider than its cell inside the run
     * box, so the grid stays honest even with a font whose Hangul glyphs
     * overhang. */
    RECT run_rect = { 0, 0, (LONG)run_w, (LONG)run_h };
    ExtTextOutW(g_gdi.dc, 0, 0, ETO_CLIPPED, &run_rect, wbuf, (UINT)wlen, dx);
    SelectObject(g_gdi.dc, old_font);
    GdiFlush();

    /* Blit in TARGET coordinates: `paint` is the part of the run box that
     * survives the clip, and the scratch is indexed relative to the run
     * origin. Keeping both indices explicit is what stops a source offset
     * from being used as a destination offset (the 2026-07-26 defect). */
    gates_rect_t paint = gates_rect_intersect(
        (gates_rect_t){ rect.x, rect.y, run_w, run_h }, bounds);
    if (gates_rect_is_empty(paint)) {
        HeapFree(GetProcessHeap(), 0, wbuf);
        HeapFree(GetProcessHeap(), 0, dx);
        return;
    }

    for (gates_i32 ty = paint.y; ty < paint.y + paint.h; ty++) {
        const gates_u32 *src = g_gdi.pixels +
                               (gates_usize_t)(ty - rect.y) * g_gdi.scratch_w;
        gates_u32 *dst = target_row(&target, ty);
        for (gates_i32 tx = paint.x; tx < paint.x + paint.w; tx++) {
            gates_u32 s = src[tx - rect.x] & 0x00FFFFFFu;
            if (s == 0) {
                continue; /* no coverage */
            }
            /* Coverage = brightest channel of the antialiased glyph mask. */
            gates_u32 r = (s >> 16) & 0xFF, g = (s >> 8) & 0xFF, b = s & 0xFF;
            gates_u32 cov = r > g ? r : g;
            if (b > cov) cov = b;
            gates_u32 a = (cov * color.a + 127u) / 255u;
            gates_color_t d = gates_pixel_unpack(dst[tx]);
            gates_u32 na = 255u - a;
            gates_color_t out = {
                .r = (gates_u8)((color.r * a + d.r * na + 127u) / 255u),
                .g = (gates_u8)((color.g * a + d.g * na + 127u) / 255u),
                .b = (gates_u8)((color.b * a + d.b * na + 127u) / 255u),
                .a = (gates_u8)((a * 255u + (gates_u32)d.a * na + 127u) / 255u),
            };
            dst[tx] = gates_pixel_pack(out);
        }
    }

    HeapFree(GetProcessHeap(), 0, wbuf);
    HeapFree(GetProcessHeap(), 0, dx);
}

static void gdi_draw(void *ctx, gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                     gates_i32 font_size, gates_str_t text, gates_color_t color) {
    (void)ctx;
    draw_run(target, rect, clip, font_size, text, color, GATES_DPI_BASE);
}

static void gdi_draw_scaled(void *ctx, gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                            gates_i32 font_size, gates_str_t text, gates_color_t color,
                            gates_u32 dpi) {
    (void)ctx;
    draw_run(target, rect, clip, font_size, text, color, dpi);
}

const gates_text_backend_t *gates_text_backend_win32_gdi(void) {
    static const gates_text_backend_t backend = {
        .ctx = nullptr,
        .metrics = gdi_metrics,
        .measure = gdi_measure,
        .draw = gdi_draw,
        .draw_scaled = gdi_draw_scaled,
    };
    return &backend;
}
