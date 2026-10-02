/* gates_gui_lib - Win32 GDI text backend (0.2.0).
 *
 * Two faces: GATES_FONT_UI is the system message font (Segoe UI, Malgun
 * Gothic on Korean Windows - proportional), GATES_FONT_MONO a fixed-pitch face
 * (Consolas, D2Coding, Courier New). A sized font (0.10.0) is the face at a
 * percentage of its height, made on first use; up to GDI_FACES of them are
 * kept (then the last is remade as needed). Advances are measured per code point at
 * the logical (96 dpi) size and cached; a character the face lacks is measured
 * in the fallback face GDI font linking draws it from. The backend never lets
 * GDI advance the pen: every glyph is placed at its logical offset through an
 * explicit dx array, so measure == the sum of the advances holds on any face
 * and drawing matches layout at every scale.
 *
 * Rendering path: GDI draws into a private 32-bit DIB, then the covered
 * pixels are blended into the caller's target so the renderer stays the
 * single owner of the framebuffer. */
#include "gates_win32_internal.h"

#include <string.h>

#define GDI_MONO_HEIGHT 16
#define GDI_SCRATCH_MAX_W 4096
#define GDI_FACES 16

/* Where font linking finds what the UI face lacks (in preference order). */
static const wchar_t *const k_fallbacks[] = { L"Malgun Gothic", L"Yu Gothic UI", L"Microsoft YaHei UI",
                                              L"Microsoft JhengHei UI", L"Segoe UI Symbol" };
#define GDI_FALLBACKS (sizeof k_fallbacks / sizeof k_fallbacks[0])

typedef struct gdi_face_t {
    bool ready;
    gates_font_t key;            /* face and size (gates_font_sized) */
    HFONT fallback[GDI_FALLBACKS]; /* at this face's height, made when first needed */
    LOGFONTW lf;                 /* the face at 96 dpi */
    HFONT font;                  /* for measuring (96 dpi) */
    gates_text_metrics_t metrics;
    gates_i16 *adv;              /* U+0000..U+FFFF, -1 = not measured yet */
    HFONT draw_font;             /* for drawing at draw_dpi */
    gates_u32 draw_dpi;
    gates_i32 draw_top;          /* where its glyph box starts in the scaled line */
} gdi_face_t;

typedef struct gdi_text_t {
    gdi_face_t faces[GDI_FACES];
    HDC probe;                   /* memory DC for measuring */
    /* Scratch DIB used to rasterize one run before blending. */
    HDC dc;
    HBITMAP dib;
    HBITMAP old_bitmap;
    gates_u32 *pixels;
    gates_i32 scratch_w;
    gates_i32 scratch_h;
} gdi_text_t;

static gdi_text_t g_gdi;


/* -- faces --------------------------------------------------------------------------- */

typedef BOOL(WINAPI *spi_for_dpi_fn)(UINT, UINT, PVOID, UINT, UINT);

/* The program's own UI face (gates_app_desc_t.ui_font), or empty. */
static wchar_t g_ui_face[LF_FACESIZE];

static int CALLBACK face_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM found) {
    (void)lf; (void)tm; (void)type;
    *(bool *)found = true;
    return 0;
}

/* Whether a face of that name is installed. */
static bool face_installed(const wchar_t *name) {
    LOGFONTW q = { .lfCharSet = DEFAULT_CHARSET };
    wcsncpy(q.lfFaceName, name, LF_FACESIZE - 1);
    bool found = false;
    HDC dc = GetDC(nullptr);
    if (dc == nullptr) return false;
    EnumFontFamiliesExW(dc, &q, face_found, (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    return found;
}

void gates_win32_text_set_ui_face(const wchar_t *face) {
    g_ui_face[0] = L'\0';
    if (face != nullptr && face[0] != L'\0' && face_installed(face)) {
        wcsncpy(g_ui_face, face, LF_FACESIZE - 1);
        g_ui_face[LF_FACESIZE - 1] = L'\0';
    }
    gates_win32_text_refresh();
}

/* The system message font at 96 dpi. */
static LOGFONTW ui_logfont(void) {
    NONCLIENTMETRICSW ncm = { .cbSize = sizeof ncm };
    spi_for_dpi_fn spi = (spi_for_dpi_fn)(void (*)(void))GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                                         "SystemParametersInfoForDpi");
    bool ok = spi != nullptr && spi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, GATES_DPI_BASE);
    if (!ok) {
        ok = SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        gates_u32 sys = gates_win32_system_dpi();
        if (ok && sys != 0 && sys != GATES_DPI_BASE) {
            ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, GATES_DPI_BASE, (int)sys);
        }
    }
    LOGFONTW lf = ok ? ncm.lfMessageFont : (LOGFONTW){ .lfHeight = -12, .lfFaceName = L"Segoe UI" };
    if (lf.lfHeight == 0) lf.lfHeight = -12;
    if (g_ui_face[0] != L'\0') { /* the program's face at the system's size */
        wcsncpy(lf.lfFaceName, g_ui_face, LF_FACESIZE - 1);
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    }
    lf.lfQuality = CLEARTYPE_QUALITY;
    return lf;
}

/* A fixed-pitch face that exists: the first of the preferred ones. */
static LOGFONTW mono_logfont(void) {
    static const wchar_t *const faces[] = { L"Consolas", L"D2Coding", L"Courier New" };
    LOGFONTW lf = { .lfHeight = GDI_MONO_HEIGHT, .lfWeight = FW_NORMAL, .lfCharSet = DEFAULT_CHARSET,
                    .lfQuality = CLEARTYPE_QUALITY, .lfPitchAndFamily = FIXED_PITCH | FF_MODERN };
    for (unsigned i = 0; i < sizeof faces / sizeof faces[0]; i++) {
        wcsncpy(lf.lfFaceName, faces[i], LF_FACESIZE - 1);
        HFONT f = CreateFontIndirectW(&lf);
        if (f != nullptr) {
            DeleteObject(f);
            return lf;
        }
    }
    return lf;
}

static bool ensure_probe(void) {
    if (g_gdi.probe != nullptr) return true;
    HDC screen = GetDC(nullptr);
    g_gdi.probe = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    return g_gdi.probe != nullptr;
}

static void face_release(gdi_face_t *f) {
    if (f->font != nullptr) DeleteObject(f->font); /* a stock font ignores it */
    if (f->draw_font != nullptr) DeleteObject(f->draw_font);
    for (unsigned i = 0; i < GDI_FALLBACKS; i++) {
        if (f->fallback[i] != nullptr) DeleteObject(f->fallback[i]);
    }
    if (f->adv != nullptr) HeapFree(GetProcessHeap(), 0, f->adv);
    *f = (gdi_face_t){0};
}

void gates_win32_text_refresh(void) {
    for (unsigned i = 0; i < GDI_FACES; i++) {
        if (g_gdi.faces[i].ready) face_release(&g_gdi.faces[i]);
    }
}

static gdi_face_t *face(gates_font_t font) {
    gates_font_t kind = gates_font_face(font) == GATES_FONT_MONO ? GATES_FONT_MONO : GATES_FONT_UI;
    gates_u32 pct = gates_font_percent(font);
    if (pct < GATES_FONT_SIZE_MIN) pct = GATES_FONT_SIZE_MIN;
    if (pct > GATES_FONT_SIZE_MAX) pct = GATES_FONT_SIZE_MAX;
    gates_font_t key = gates_font_sized(kind, pct);
    gdi_face_t *f = nullptr;
    for (unsigned i = 0; i < GDI_FACES && f == nullptr; i++) {
        if (!g_gdi.faces[i].ready || g_gdi.faces[i].key == key) f = &g_gdi.faces[i];
    }
    if (f == nullptr) { /* all kept: remake the last one */
        f = &g_gdi.faces[GDI_FACES - 1];
        face_release(f);
    }
    if (f->ready || !ensure_probe()) return f;
    f->key = key;
    f->lf = kind == GATES_FONT_MONO ? mono_logfont() : ui_logfont();
    if (pct != 100u) {
        LONG h = MulDiv(f->lf.lfHeight, (int)pct, 100);
        f->lf.lfHeight = h != 0 ? h : (f->lf.lfHeight < 0 ? -1 : 1);
    }
    f->font = CreateFontIndirectW(&f->lf);
    if (f->font == nullptr) f->font = (HFONT)GetStockObject(kind == GATES_FONT_MONO ? SYSTEM_FIXED_FONT : DEFAULT_GUI_FONT);
    HGDIOBJ old = SelectObject(g_gdi.probe, f->font);
    TEXTMETRICW tm;
    GetTextMetricsW(g_gdi.probe, &tm);
    INT digit_w = 0;
    if (!GetCharWidth32W(g_gdi.probe, L'0', L'0', &digit_w) || digit_w <= 0) digit_w = tm.tmAveCharWidth;
    SelectObject(g_gdi.probe, old);
    f->metrics = (gates_text_metrics_t){
        /* The average width is a sizing hint: a digit for the fixed face, the
         * face's own average for the proportional one. */
        .advance = kind == GATES_FONT_MONO ? digit_w : (tm.tmAveCharWidth > 0 ? tm.tmAveCharWidth : digit_w),
        .ascent = tm.tmAscent,
        .descent = tm.tmDescent,
        .line_height = tm.tmHeight + tm.tmExternalLeading,
    };
    if (f->metrics.advance <= 0) f->metrics.advance = 7;
    if (f->metrics.line_height < f->metrics.ascent + f->metrics.descent) {
        f->metrics.line_height = f->metrics.ascent + f->metrics.descent;
    }
    f->adv = (gates_i16 *)HeapAlloc(GetProcessHeap(), 0, 0x10000 * sizeof(gates_i16));
    if (f->adv != nullptr) memset(f->adv, 0xFF, 0x10000 * sizeof(gates_i16)); /* all -1 */
    f->ready = true;
    return f;
}

/* Whether `font` (selected in the probe) has a glyph for the UTF-16 unit. */
static bool has_glyph(wchar_t wc) {
    WORD idx = 0;
    return GetGlyphIndicesW(g_gdi.probe, &wc, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR && idx != 0xFFFF;
}

/* The fallback face (at the face's height) that has the character, or null. */
static HFONT fallback_for(gdi_face_t *f, wchar_t wc) {
    for (unsigned i = 0; i < GDI_FALLBACKS; i++) {
        if (f->fallback[i] == nullptr) {
            LOGFONTW lf = f->lf;
            wcsncpy(lf.lfFaceName, k_fallbacks[i], LF_FACESIZE - 1);
            lf.lfFaceName[LF_FACESIZE - 1] = L'\0';
            lf.lfPitchAndFamily = DEFAULT_PITCH;
            f->fallback[i] = CreateFontIndirectW(&lf);
            if (f->fallback[i] == nullptr) continue;
        }
        HGDIOBJ old = SelectObject(g_gdi.probe, f->fallback[i]);
        bool has = has_glyph(wc);
        SelectObject(g_gdi.probe, old);
        if (has) return f->fallback[i];
    }
    return nullptr;
}

/* Measures UTF-16 units in the face (the fallback's width when the face lacks a
 * single BMP character). */
static gates_i32 measure_units(gdi_face_t *f, const wchar_t *w, int n) {
    HGDIOBJ old = SelectObject(g_gdi.probe, f->font);
    HFONT use = nullptr;
    if (n == 1 && !has_glyph(w[0])) use = fallback_for(f, w[0]);
    if (use != nullptr) SelectObject(g_gdi.probe, use);
    SIZE sz = { 0, 0 };
    if (!GetTextExtentPoint32W(g_gdi.probe, w, n, &sz)) sz.cx = f->metrics.advance;
    SelectObject(g_gdi.probe, old);
    return sz.cx > 0 ? sz.cx : 0;
}

static gates_i32 advance_of(gdi_face_t *f, gates_u32 cp) {
    if (cp < 0x20 || cp == 0x7F) return 0; /* controls take no space */
    if (cp <= 0xFFFF) {
        if (f->adv != nullptr && f->adv[cp] >= 0) return f->adv[cp];
        wchar_t wc = (wchar_t)cp;
        gates_i32 a = measure_units(f, &wc, 1);
        if (f->adv != nullptr) f->adv[cp] = (gates_i16)(a < 0x7FFF ? a : 0x7FFF);
        return a;
    }
    wchar_t pair[2] = { (wchar_t)(0xD800 + ((cp - 0x10000) >> 10)), (wchar_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)) };
    return measure_units(f, pair, 2);
}

/* -- the contract ---------------------------------------------------------------------- */

static gates_text_metrics_t gdi_metrics(void *ctx, gates_font_t font) {
    (void)ctx;
    return face(font)->metrics;
}

static gates_i32 gdi_advance(void *ctx, gates_font_t font, gates_u32 cp) {
    (void)ctx;
    return advance_of(face(font), cp);
}

static gates_size_t gdi_measure(void *ctx, gates_font_t font, gates_str_t text) {
    (void)ctx;
    gdi_face_t *f = face(font);
    gates_i32 w = 0;
    for (gates_u32 i = 0; i < text.size;) {
        gates_u32 cp;
        i += gates_text_decode(text, i, &cp);
        w += advance_of(f, cp);
    }
    return (gates_size_t){ w, f->metrics.line_height };
}

/* -- drawing --------------------------------------------------------------------------- */

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

static gates_u32 *target_row(gates_pixels_t *px, gates_i32 y) {
    return (gates_u32 *)(void *)((gates_u8 *)px->ptr + (gates_usize_t)y * px->stride_bytes);
}

/* The face for drawing at `dpi`; advances stay the 96-dpi ones,
 * so layout never changes with the scale. A scaled face can come out taller
 * than the scaled line (hinting rounds up): only then is it made smaller until
 * it fits; its baseline goes where the logical baseline scales to, kept inside
 * the line, so descenders are never cut. */
static HFONT draw_font(gdi_face_t *f, gates_u32 dpi, gates_i32 *top) {
    *top = 0;
    if (dpi == 0 || dpi == GATES_DPI_BASE) return f->font;
    if (f->draw_font == nullptr || f->draw_dpi != dpi) {
        if (f->draw_font != nullptr) DeleteObject(f->draw_font);
        f->draw_font = nullptr;
        f->draw_top = 0;
        gates_i32 line = gates_px(f->metrics.line_height, dpi);
        gates_i32 base = gates_px(f->metrics.ascent, dpi);
        LOGFONTW lf = f->lf;
        gates_i32 h = lf.lfHeight < 0 ? -gates_px(-lf.lfHeight, dpi) : gates_px(lf.lfHeight, dpi);
        for (int tries = 0; tries < 6; tries++) {
            lf.lfHeight = h;
            HFONT cand = CreateFontIndirectW(&lf);
            if (cand == nullptr) break;
            TEXTMETRICW tm;
            HGDIOBJ old = SelectObject(g_gdi.probe, cand);
            GetTextMetricsW(g_gdi.probe, &tm);
            SelectObject(g_gdi.probe, old);
            if (tm.tmHeight <= line || tries == 5) {
                /* the baseline where the logical one scales to, kept inside the line */
                gates_i32 top_at = base - tm.tmAscent;
                if (top_at > line - tm.tmHeight) top_at = line - tm.tmHeight;
                if (top_at < 0) top_at = 0;
                f->draw_font = cand;
                f->draw_top = top_at;
                break;
            }
            DeleteObject(cand);
            h += h < 0 ? 1 : -1; /* one pixel smaller */
            if (h == 0) break;
        }
        f->draw_dpi = dpi;
    }
    *top = f->draw_top;
    return f->draw_font != nullptr ? f->draw_font : f->font;
}

/* Draws a run at `dpi`: rect and clip are device pixels; code point k starts
 * at gates_px(its logical offset) from the run origin, the same edge rounding
 * the renderer uses for every other rect, so glyphs sit where layout put them. */
static void draw_run(gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                     gates_font_t font, gates_str_t text, gates_color_t color, gates_u32 dpi) {
    if (target.ptr == nullptr || text.size == 0 || color.a == 0) {
        return;
    }
    gdi_face_t *f = face(font);
    gates_i32 top = 0;
    HFONT hfont = draw_font(f, dpi, &top);

    gates_rect_t bounds = gates_rect_intersect(
        gates_rect_intersect(rect, clip), (gates_rect_t){ 0, 0, target.w, target.h });
    if (gates_rect_is_empty(bounds)) {
        return;
    }

    /* UTF-8 -> UTF-16 with a per-unit dx array from the logical advances. */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, (const char *)text.ptr, (int)text.size, nullptr, 0);
    if (wlen <= 0) {
        return;
    }
    wchar_t *wbuf = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)wlen * sizeof(wchar_t));
    INT *dx = (INT *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)wlen * sizeof(INT));
    if (wbuf == nullptr || dx == nullptr) {
        if (wbuf != nullptr) HeapFree(GetProcessHeap(), 0, wbuf);
        if (dx != nullptr) HeapFree(GetProcessHeap(), 0, dx);
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, (const char *)text.ptr, (int)text.size, wbuf, wlen);

    gates_i32 pos = 0; /* logical offset of the next glyph */
    for (int i = 0; i < wlen; i++) {
        gates_u32 cp = wbuf[i];
        bool pair = cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wlen;
        if (pair) {
            gates_u32 low = wbuf[i + 1];
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
        }
        gates_i32 a = advance_of(f, cp);
        dx[i] = (INT)(gates_px(pos + a, dpi) - gates_px(pos, dpi));
        pos += a;
        if (pair) {
            dx[i + 1] = 0; /* surrogate pair: the lead carries the whole advance */
            i++;
        }
    }
    gates_i32 run_w = gates_px(pos, dpi);
    gates_i32 run_h = gates_px(f->metrics.line_height, dpi);
    if (run_w <= 0 || !ensure_scratch(run_w, run_h)) {
        HeapFree(GetProcessHeap(), 0, wbuf);
        HeapFree(GetProcessHeap(), 0, dx);
        return;
    }
    if (run_w > g_gdi.scratch_w) {
        run_w = g_gdi.scratch_w;
    }

    /* Rasterize white-on-black, then blend the coverage with the wanted color. */
    for (gates_i32 y = 0; y < run_h; y++) {
        memset(g_gdi.pixels + (gates_usize_t)y * g_gdi.scratch_w, 0, (gates_usize_t)run_w * 4u);
    }
    HGDIOBJ old_font = SelectObject(g_gdi.dc, hfont);
    SetTextColor(g_gdi.dc, RGB(255, 255, 255));
    /* ETO_CLIPPED keeps a glyph that overhangs its advance inside the run box. */
    RECT run_rect = { 0, 0, (LONG)run_w, (LONG)run_h };
    ExtTextOutW(g_gdi.dc, 0, top, ETO_CLIPPED, &run_rect, wbuf, (UINT)wlen, dx);
    SelectObject(g_gdi.dc, old_font);
    GdiFlush();

    /* Blit in TARGET coordinates: `paint` is the part of the run box that
     * survives the clip, and the scratch is indexed relative to the run
     * origin. Keeping both indices explicit is what stops a source offset
     * from being used as a destination offset (the 2026-07-26 defect). */
    gates_rect_t paint = gates_rect_intersect((gates_rect_t){ rect.x, rect.y, run_w, run_h }, bounds);
    for (gates_i32 ty = paint.y; ty < paint.y + paint.h; ty++) {
        const gates_u32 *src = g_gdi.pixels + (gates_usize_t)(ty - rect.y) * g_gdi.scratch_w;
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
                     gates_font_t font, gates_str_t text, gates_color_t color) {
    (void)ctx;
    draw_run(target, rect, clip, font, text, color, GATES_DPI_BASE);
}

static void gdi_draw_scaled(void *ctx, gates_pixels_t target, gates_rect_t rect, gates_rect_t clip,
                            gates_font_t font, gates_str_t text, gates_color_t color, gates_u32 dpi) {
    (void)ctx;
    draw_run(target, rect, clip, font, text, color, dpi);
}

const gates_text_backend_t *gates_text_backend_win32_gdi(void) {
    static const gates_text_backend_t backend = {
        .ctx = nullptr,
        .metrics = gdi_metrics,
        .measure = gdi_measure,
        .draw = gdi_draw,
        .draw_scaled = gdi_draw_scaled,
        .glyph_advance = gdi_advance,
    };
    return &backend;
}
