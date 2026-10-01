/* gates_gui_lib - a built-in converter for code pages 949 (CP949, Unified
 * Hangul Code) and 51949 (EUC-KR), for targets without an OS to ask (0.11.0).
 * The tables were measured on Windows 11 (see the table file's head), so the
 * results are what Windows gives, with these exceptions. Refused here: 51949's
 * lone byte C9 (Windows reads U+0000), U+F8E6-U+F8EB (51949 writes them as
 * single bytes it cannot read back) and U+F8F7 (949 writes a lone FF that does
 * not read back). Kept at the standard: 51949's B4D3 is U+B2D2 as KS X 1001
 * and 949 say (Windows' 51949 reads U+B2D6). A program links this only when it
 * calls gates_codepage_converter_cp949. Platform-free. */
#include <gates/encoding.h>
#include <proven/heap.h>

#include <string.h>

#include "gates_encoding_cp949_table.inc"

#define REPLACEMENT 0xFFFDu

/* 51949 maps a pair as 949 does when both bytes are A1-FE and Windows does not set it apart.
 * B4D3 is the exception kept: Windows' 51949 reads it as U+B2D6, but KS X 1001 (and Windows'
 * own 949) has U+B2D2 there, and EUC-KR files written elsewhere mean that - so here it is
 * U+B2D2, as in 949. B4D3 stays in the measured list; this line overrides it. */
static bool euckr_has(gates_u32 code) {
    if (code == 0xB4D3) return true;
    gates_u32 lo = 0, hi = EUCKR_EXCLUDED_N;
    while (lo < hi) {
        gates_u32 m = lo + (hi - lo) / 2u;
        if (euckr_excluded[m] < code) lo = m + 1u;
        else hi = m;
    }
    return !(lo < EUCKR_EXCLUDED_N && euckr_excluded[lo] == code);
}

/* The character at in[pos]: its code point (U+FFFD and *bad when the bytes are
 * not one) and how many bytes it takes. A broken pair takes its first byte only,
 * so an ASCII byte after it is not lost. */
static gates_usize_t next_char(bool euckr, const gates_u8 *in, gates_usize_t size, gates_usize_t pos, gates_u32 *cp,
                               bool *bad) {
    gates_u8 b = in[pos];
    *bad = false;
    if (b < 0x80) {
        *cp = b;
        return 1;
    }
    if (!euckr && b == 0x80 && cp949_single[0] != 0) {
        *cp = cp949_single[0];
        return 1;
    }
    if (euckr && b <= 0x9F) { /* 51949 passes the C1 bytes through, as Windows measured */
        *cp = b;
        return 1;
    }
    if (pos + 1 < size && b >= 0x81 && b <= 0xFE) {
        gates_u8 t = in[pos + 1];
        if (t >= 0x41 && t <= 0xFE) {
            gates_u32 u = cp949_pairs[b - 0x81][t - 0x41];
            bool ok = u != 0 && (!euckr || (b >= 0xA1 && t >= 0xA1 && euckr_has(((gates_u32)b << 8) | t)));
            if (ok) {
                *cp = u;
                return 2;
            }
        }
    }
    *cp = REPLACEMENT;
    *bad = true;
    return 1;
}

static gates_usize_t utf8_len(gates_u32 cp) {
    return cp < 0x80 ? 1u : cp < 0x800 ? 2u : cp < 0x10000 ? 3u : 4u;
}

static void utf8_put(gates_u32 cp, gates_u8 *o) {
    if (cp < 0x80) {
        o[0] = (gates_u8)cp;
    } else if (cp < 0x800) {
        o[0] = (gates_u8)(0xC0 | (cp >> 6));
        o[1] = (gates_u8)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o[0] = (gates_u8)(0xE0 | (cp >> 12));
        o[1] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (gates_u8)(0x80 | (cp & 0x3F));
    } else {
        o[0] = (gates_u8)(0xF0 | (cp >> 18));
        o[1] = (gates_u8)(0x80 | ((cp >> 12) & 0x3F));
        o[2] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
        o[3] = (gates_u8)(0x80 | (cp & 0x3F));
    }
}

static gates_err_t alloc_out(gates_allocator_t a, gates_usize_t n, gates_u8 **out) {
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, n + 4u, 4);
    if (!proven_is_ok(r.err)) return r.err;
    *out = (gates_u8 *)r.value.ptr;
    return GATES_OK;
}

static bool supported(gates_u32 codepage, bool *euckr) {
    *euckr = codepage == GATES_CODEPAGE_EUC_KR;
    return codepage == GATES_CODEPAGE_CP949 || *euckr;
}

static gates_err_t cp_to_utf8(void *ctx, gates_u32 codepage, const gates_u8 *in, gates_usize_t size, bool strict,
                              gates_allocator_t alloc, gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at) {
    (void)ctx;
    bool euckr;
    if (!supported(codepage, &euckr)) return PROVEN_ERR_UNSUPPORTED;
    gates_usize_t total = 0;
    for (gates_usize_t pos = 0; pos < size;) {
        gates_u32 cp;
        bool bad;
        gates_usize_t n = next_char(euckr, in, size, pos, &cp, &bad);
        if (bad && strict) {
            if (bad_at != nullptr) *bad_at = pos;
            return PROVEN_ERR_INVALID_ENCODING;
        }
        total += utf8_len(cp);
        pos += n;
    }
    gates_u8 *buf = nullptr;
    gates_err_t err = alloc_out(alloc, total, &buf);
    if (!gates_is_ok(err)) return err;
    gates_usize_t w = 0;
    for (gates_usize_t pos = 0; pos < size;) {
        gates_u32 cp;
        bool bad;
        pos += next_char(euckr, in, size, pos, &cp, &bad);
        utf8_put(cp, buf + w);
        w += utf8_len(cp);
    }
    memset(buf + w, 0, 4);
    *out = buf;
    *out_size = w;
    return GATES_OK;
}

/* The code for a code point, 0 when the code page has none. */
static gates_u32 code_of(bool euckr, gates_u32 cp) {
    if (cp < 0x80) return cp == 0 ? 0x100u : cp; /* 0x100 stands for NUL (0 means none) */
    if (euckr && cp <= 0x9F) return cp; /* the C1 bytes, as in decoding */
    if (cp > 0xFFFF) return 0;
    gates_u32 lo = 0, hi = CP949_REV_N;
    while (lo < hi) {
        gates_u32 m = lo + (hi - lo) / 2u;
        if (cp949_rev_u[m] < cp) lo = m + 1u;
        else hi = m;
    }
    if (lo >= CP949_REV_N || cp949_rev_u[lo] != cp) return 0;
    gates_u32 c = cp949_rev_c[lo];
    if (euckr && (c < 0x100 || (c >> 8) < 0xA1 || (c & 0xFF) < 0xA1 || !euckr_has(c))) return 0;
    return c;
}

/* The next code point of valid UTF-8 (the core passes only valid text). */
static gates_usize_t utf8_next(const gates_u8 *p, gates_usize_t left, gates_u32 *cp) {
    gates_u8 b = p[0];
    gates_usize_t n = b < 0x80 ? 1 : b < 0xE0 ? 2 : b < 0xF0 ? 3 : 4;
    if (n > left) n = left;
    gates_u32 v = n == 1 ? b : n == 2 ? (b & 0x1Fu) : n == 3 ? (b & 0x0Fu) : (b & 0x07u);
    for (gates_usize_t i = 1; i < n; i++) v = (v << 6) | (p[i] & 0x3Fu);
    *cp = v;
    return n;
}

static gates_err_t cp_from_utf8(void *ctx, gates_u32 codepage, gates_str_t text, bool strict, gates_allocator_t alloc,
                                gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at) {
    (void)ctx;
    bool euckr;
    if (!supported(codepage, &euckr)) return PROVEN_ERR_UNSUPPORTED;
    gates_usize_t total = 0;
    for (gates_usize_t pos = 0; pos < text.size;) {
        gates_u32 cp;
        gates_usize_t n = utf8_next(text.ptr + pos, text.size - pos, &cp);
        gates_u32 c = code_of(euckr, cp);
        if (c == 0 && strict) {
            if (bad_at != nullptr) *bad_at = pos;
            return PROVEN_ERR_INVALID_ENCODING;
        }
        total += c > 0xFF && c != 0x100 ? 2u : 1u; /* a code it lacks is one '?' */
        pos += n;
    }
    gates_u8 *buf = nullptr;
    gates_err_t err = alloc_out(alloc, total, &buf);
    if (!gates_is_ok(err)) return err;
    gates_usize_t w = 0;
    for (gates_usize_t pos = 0; pos < text.size;) {
        gates_u32 cp;
        pos += utf8_next(text.ptr + pos, text.size - pos, &cp);
        gates_u32 c = code_of(euckr, cp);
        if (c == 0) {
            buf[w++] = '?';
        } else if (c == 0x100) {
            buf[w++] = 0;
        } else if (c > 0xFF) {
            buf[w++] = (gates_u8)(c >> 8);
            buf[w++] = (gates_u8)(c & 0xFF);
        } else {
            buf[w++] = (gates_u8)c;
        }
    }
    memset(buf + w, 0, 4);
    *out = buf;
    *out_size = w;
    return GATES_OK;
}

static gates_u32 cp_system(void *ctx) {
    (void)ctx;
    return GATES_CODEPAGE_CP949;
}

const gates_codepage_converter_t *gates_codepage_converter_cp949(void) {
    static const gates_codepage_converter_t conv = {
        .ctx = nullptr,
        .to_utf8 = cp_to_utf8,
        .from_utf8 = cp_from_utf8,
        .system_codepage = cp_system,
        .console_codepage = nullptr,
    };
    return &conv;
}
