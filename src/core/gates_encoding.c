/* gates_gui_lib - text encodings at the edge (0.10.0): UTF-8, UTF-16 and
 * UTF-32 in both byte orders here; code pages through the installed
 * converter. Every conversion measures first and writes second, so the
 * result is allocated once, exactly, and nothing is allocated on an error.
 * Platform-free. */
#include <gates/encoding.h>
#include <proven/heap.h>

#include <string.h>

#define REPLACEMENT 0xFFFDu
#define TERMINATOR 4u /* zero bytes after every result (not counted): a C or wide string as is */

static gates_codepage_converter_t g_conv;
static bool g_has_conv;

void gates_encoding_set_codepage_converter(const gates_codepage_converter_t *converter) {
    g_has_conv = converter != nullptr && converter->to_utf8 != nullptr && converter->from_utf8 != nullptr;
    g_conv = g_has_conv ? *converter : (gates_codepage_converter_t){0};
}

bool gates_encoding_has_codepage_converter(void) {
    return g_has_conv;
}

gates_u32 gates_encoding_system_codepage(void) {
    return g_has_conv && g_conv.system_codepage != nullptr ? g_conv.system_codepage(g_conv.ctx) : 0;
}

gates_u32 gates_encoding_console_codepage(void) {
    return g_has_conv && g_conv.console_codepage != nullptr ? g_conv.console_codepage(g_conv.ctx) : 0;
}

/* -- one code point at a time ------------------------------------------------------------- */

/* The code point at in[pos] in `kind`: returns the bytes it takes (>= 1);
 * *bad when they are not text (then *cp = U+FFFD). */
static gates_usize_t next_cp(gates_encoding_kind_t kind, const gates_u8 *in, gates_usize_t size, gates_usize_t pos,
                             gates_u32 *cp, bool *bad) {
    gates_usize_t left = size - pos;
    const gates_u8 *p = in + pos;
    *bad = true;
    *cp = REPLACEMENT;
    switch (kind) {
    case GATES_ENCODING_UTF8: {
        gates_u8 b = p[0];
        if (b < 0x80) {
            *cp = b;
            *bad = false;
            return 1;
        }
        gates_usize_t need;
        gates_u32 v, min;
        if ((b & 0xE0) == 0xC0) { need = 1; v = b & 0x1Fu; min = 0x80; }
        else if ((b & 0xF0) == 0xE0) { need = 2; v = b & 0x0Fu; min = 0x800; }
        else if ((b & 0xF8) == 0xF0) { need = 3; v = b & 0x07u; min = 0x10000; }
        else return 1;
        if (need >= left) return 1;
        for (gates_usize_t i = 1; i <= need; i++) {
            if ((p[i] & 0xC0) != 0x80) return 1;
            v = (v << 6) | (p[i] & 0x3Fu);
        }
        if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 1; /* overlong, too big, surrogate */
        *cp = v;
        *bad = false;
        return need + 1;
    }
    case GATES_ENCODING_UTF16LE:
    case GATES_ENCODING_UTF16BE: {
        bool le = kind == GATES_ENCODING_UTF16LE;
        if (left < 2) return 1;
        gates_u32 u = le ? (gates_u32)(p[0] | (p[1] << 8)) : (gates_u32)((p[0] << 8) | p[1]);
        if (u < 0xD800 || u > 0xDFFF) {
            *cp = u;
            *bad = false;
            return 2;
        }
        if (u > 0xDBFF || left < 4) return 2; /* a lone low surrogate, or a high one at the end */
        gates_u32 w = le ? (gates_u32)(p[2] | (p[3] << 8)) : (gates_u32)((p[2] << 8) | p[3]);
        if (w < 0xDC00 || w > 0xDFFF) return 2;
        *cp = 0x10000 + ((u - 0xD800) << 10) + (w - 0xDC00);
        *bad = false;
        return 4;
    }
    default: { /* UTF-32 */
        bool le = kind == GATES_ENCODING_UTF32LE;
        if (left < 4) return left;
        gates_u32 v = le ? ((gates_u32)p[0] | ((gates_u32)p[1] << 8) | ((gates_u32)p[2] << 16) | ((gates_u32)p[3] << 24))
                         : (((gates_u32)p[0] << 24) | ((gates_u32)p[1] << 16) | ((gates_u32)p[2] << 8) | (gates_u32)p[3]);
        if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 4;
        *cp = v;
        *bad = false;
        return 4;
    }
    }
}

/* Writes cp in `kind` at out (when not null); returns its length. */
static gates_usize_t put_cp(gates_encoding_kind_t kind, gates_u32 cp, gates_u8 *out) {
    gates_u8 b[4];
    gates_usize_t n;
    switch (kind) {
    case GATES_ENCODING_UTF8:
        if (cp < 0x80) { b[0] = (gates_u8)cp; n = 1; }
        else if (cp < 0x800) { b[0] = (gates_u8)(0xC0 | (cp >> 6)); b[1] = (gates_u8)(0x80 | (cp & 0x3F)); n = 2; }
        else if (cp < 0x10000) {
            b[0] = (gates_u8)(0xE0 | (cp >> 12)); b[1] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
            b[2] = (gates_u8)(0x80 | (cp & 0x3F)); n = 3;
        } else {
            b[0] = (gates_u8)(0xF0 | (cp >> 18)); b[1] = (gates_u8)(0x80 | ((cp >> 12) & 0x3F));
            b[2] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (gates_u8)(0x80 | (cp & 0x3F)); n = 4;
        }
        break;
    case GATES_ENCODING_UTF16LE:
    case GATES_ENCODING_UTF16BE: {
        gates_u32 u[2];
        gates_usize_t units = 1;
        if (cp < 0x10000) u[0] = cp;
        else { u[0] = 0xD800 + ((cp - 0x10000) >> 10); u[1] = 0xDC00 + ((cp - 0x10000) & 0x3FF); units = 2; }
        for (gates_usize_t i = 0; i < units; i++) {
            gates_u8 hi = (gates_u8)(u[i] >> 8), lo = (gates_u8)(u[i] & 0xFF);
            b[2 * i] = kind == GATES_ENCODING_UTF16LE ? lo : hi;
            b[2 * i + 1] = kind == GATES_ENCODING_UTF16LE ? hi : lo;
        }
        n = 2 * units;
        break;
    }
    default:
        for (int i = 0; i < 4; i++) {
            gates_u8 v = (gates_u8)(cp >> (8 * i));
            b[kind == GATES_ENCODING_UTF32LE ? i : 3 - i] = v;
        }
        n = 4;
        break;
    }
    if (out != nullptr) memcpy(out, b, n);
    return n;
}

/* The byte order mark is U+FEFF in the encoding itself (EF BB BF in UTF-8). */
static gates_usize_t bom_of(gates_encoding_kind_t kind, gates_u8 *out) {
    return put_cp(kind, 0xFEFF, out);
}

/* Converts between two UTF forms: measure, allocate, write. */
static gates_err_t transcode(gates_encoding_kind_t from, gates_encoding_kind_t to, const gates_u8 *in,
                             gates_usize_t size, bool bom, bool strict, gates_allocator_t alloc, gates_u8 **out,
                             gates_usize_t *out_size, gates_usize_t *bad_at) {
    gates_usize_t total = bom ? bom_of(to, nullptr) : 0;
    for (gates_usize_t pos = 0; pos < size;) {
        gates_u32 cp;
        bool bad;
        gates_usize_t used = next_cp(from, in, size, pos, &cp, &bad);
        if (bad && strict) {
            if (bad_at != nullptr) *bad_at = pos;
            return PROVEN_ERR_INVALID_ENCODING;
        }
        gates_usize_t n = put_cp(to, cp, nullptr);
        if (total > (gates_usize_t)-1 - n - TERMINATOR) return PROVEN_ERR_OVERFLOW;
        total += n;
        pos += used;
    }
    gates_allocator_t a = proven_alloc_is_valid(alloc) ? alloc : proven_heap_allocator();
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, total + TERMINATOR, 4);
    if (!proven_is_ok(r.err)) return r.err;
    gates_u8 *buf = (gates_u8 *)r.value.ptr;
    gates_usize_t w = bom ? bom_of(to, buf) : 0;
    for (gates_usize_t pos = 0; pos < size;) {
        gates_u32 cp;
        bool bad;
        pos += next_cp(from, in, size, pos, &cp, &bad);
        w += put_cp(to, cp, buf + w);
    }
    memset(buf + w, 0, TERMINATOR);
    *out = buf;
    *out_size = w;
    return GATES_OK;
}

/* -- code pages -------------------------------------------------------------------------- */

/* A UTF code page as its kind; false for a real code page. */
static bool utf_codepage(gates_u32 cp, gates_encoding_kind_t *kind) {
    switch (cp) {
    case GATES_CODEPAGE_UTF8: *kind = GATES_ENCODING_UTF8; return true;
    case GATES_CODEPAGE_UTF16LE: *kind = GATES_ENCODING_UTF16LE; return true;
    case GATES_CODEPAGE_UTF16BE: *kind = GATES_ENCODING_UTF16BE; return true;
    case GATES_CODEPAGE_UTF32LE: *kind = GATES_ENCODING_UTF32LE; return true;
    case GATES_CODEPAGE_UTF32BE: *kind = GATES_ENCODING_UTF32BE; return true;
    default: return false;
    }
}

/* Resolves `enc`: a UTF kind (true) or a code page for the converter (false,
 * *cp set). UNSUPPORTED when a code page has no converter. */
static gates_err_t resolve(gates_encoding_t enc, gates_encoding_kind_t *kind, gates_u32 *cp, bool *native) {
    if (enc.kind > GATES_ENCODING_CODEPAGE) return PROVEN_ERR_INVALID_ARG;
    *native = true;
    *kind = enc.kind;
    if (enc.kind != GATES_ENCODING_CODEPAGE) return GATES_OK;
    gates_u32 page = enc.codepage != GATES_CODEPAGE_SYSTEM ? enc.codepage : gates_encoding_system_codepage();
    if (utf_codepage(page, kind)) return GATES_OK;
    if (!g_has_conv || page == 0) return PROVEN_ERR_UNSUPPORTED;
    *native = false;
    *cp = page;
    return GATES_OK;
}

static bool starts_with_bom(gates_encoding_kind_t kind, const gates_u8 *in, gates_usize_t size, gates_usize_t *len) {
    gates_u8 b[4];
    gates_usize_t n = bom_of(kind, b);
    if (size < n || memcmp(in, b, n) != 0) return false;
    *len = n;
    return true;
}

gates_err_t gates_encoding_to_utf8(gates_encoding_t enc, const void *in, gates_usize_t size, gates_u32 flags,
                                   gates_allocator_t alloc, gates_u8 **out, gates_usize_t *out_size,
                                   gates_usize_t *bad_at) {
    if (out == nullptr || out_size == nullptr || (in == nullptr && size > 0)) return PROVEN_ERR_INVALID_ARG;
    gates_encoding_kind_t kind;
    gates_u32 cp = 0;
    bool native;
    gates_err_t err = resolve(enc, &kind, &cp, &native);
    if (!gates_is_ok(err)) return err;
    bool strict = (flags & GATES_ENCODING_STRICT) != 0;
    const gates_u8 *p = (const gates_u8 *)in;
    if (!native) {
        return g_conv.to_utf8(g_conv.ctx, cp, p, size, strict, proven_alloc_is_valid(alloc) ? alloc : proven_heap_allocator(),
                              out, out_size, bad_at);
    }
    gates_usize_t skip = 0;
    if (size > 0 && starts_with_bom(kind, p, size, &skip)) {
        err = transcode(kind, GATES_ENCODING_UTF8, p + skip, size - skip, false, strict, alloc, out, out_size, bad_at);
        if (err == PROVEN_ERR_INVALID_ENCODING && bad_at != nullptr) *bad_at += skip;
        return err;
    }
    return transcode(kind, GATES_ENCODING_UTF8, p, size, false, strict, alloc, out, out_size, bad_at);
}

gates_err_t gates_encoding_from_utf8(gates_encoding_t enc, gates_str_t text, gates_u32 flags, gates_allocator_t alloc,
                                     gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at) {
    if (out == nullptr || out_size == nullptr || (text.ptr == nullptr && text.size > 0)) return PROVEN_ERR_INVALID_ARG;
    gates_encoding_kind_t kind;
    gates_u32 cp = 0;
    bool native;
    gates_err_t err = resolve(enc, &kind, &cp, &native);
    if (!gates_is_ok(err)) return err;
    bool strict = (flags & GATES_ENCODING_STRICT) != 0;
    if (native) {
        return transcode(GATES_ENCODING_UTF8, kind, text.ptr, text.size, enc.bom, strict, alloc, out, out_size, bad_at);
    }
    gates_allocator_t a = proven_alloc_is_valid(alloc) ? alloc : proven_heap_allocator();
    gates_usize_t bad = 0;
    if (gates_utf8_valid(text, &bad)) return g_conv.from_utf8(g_conv.ctx, cp, text, strict, a, out, out_size, bad_at);
    if (strict) {
        if (bad_at != nullptr) *bad_at = bad;
        return PROVEN_ERR_INVALID_ENCODING;
    }
    /* Malformed UTF-8 becomes U+FFFD first, then goes to the converter. */
    gates_u8 *clean = nullptr;
    gates_usize_t clean_n = 0;
    err = transcode(GATES_ENCODING_UTF8, GATES_ENCODING_UTF8, text.ptr, text.size, false, false, a, &clean, &clean_n,
                    nullptr);
    if (!gates_is_ok(err)) return err;
    err = g_conv.from_utf8(g_conv.ctx, cp, (gates_str_t){ .ptr = clean, .size = clean_n }, false, a, out, out_size,
                           bad_at);
    a.free_fn(a.ctx, clean);
    return err;
}

bool gates_utf8_valid(gates_str_t text, gates_usize_t *bad_at) {
    const gates_u8 *p = text.ptr;
    for (gates_usize_t pos = 0; pos < text.size;) {
        gates_u32 cp;
        bool bad;
        gates_usize_t used = next_cp(GATES_ENCODING_UTF8, p, text.size, pos, &cp, &bad);
        if (bad) {
            if (bad_at != nullptr) *bad_at = pos;
            return false;
        }
        pos += used;
    }
    return true;
}

gates_usize_t gates_encoding_detect(const void *in, gates_usize_t size, gates_encoding_t *out) {
    gates_encoding_t e = { .kind = GATES_ENCODING_CODEPAGE, .codepage = GATES_CODEPAGE_SYSTEM };
    const gates_u8 *p = (const gates_u8 *)in;
    gates_usize_t len = 0;
    if (p == nullptr) size = 0;
    /* UTF-32 first: its little-endian mark begins with UTF-16's. */
    static const gates_encoding_kind_t order[] = { GATES_ENCODING_UTF32LE, GATES_ENCODING_UTF32BE, GATES_ENCODING_UTF8,
                                                   GATES_ENCODING_UTF16LE, GATES_ENCODING_UTF16BE };
    for (unsigned i = 0; i < sizeof order / sizeof order[0]; i++) {
        if (starts_with_bom(order[i], p, size, &len)) {
            e.kind = order[i];
            if (out != nullptr) *out = e;
            return len;
        }
    }
    /* Zeros in at least three of four even (or odd) bytes and none in the
     * others: UTF-16 (tried before UTF-8, which allows NUL). */
    gates_usize_t pairs = size / 2, z_even = 0, z_odd = 0;
    for (gates_usize_t i = 0; i + 1 < size; i += 2) {
        z_even += p[i] == 0;
        z_odd += p[i + 1] == 0;
    }
    if (size % 2 == 0 && pairs > 0 && z_even == 0 && z_odd * 4 >= pairs * 3) {
        e.kind = GATES_ENCODING_UTF16LE;
    } else if (size % 2 == 0 && pairs > 0 && z_odd == 0 && z_even * 4 >= pairs * 3) {
        e.kind = GATES_ENCODING_UTF16BE;
    } else if (gates_utf8_valid((gates_str_t){ .ptr = p, .size = size }, nullptr)) {
        e.kind = GATES_ENCODING_UTF8;
    }
    if (out != nullptr) *out = e;
    return 0;
}

/* -- into the caller's buffer (0.10.0): the UTF forms, no allocation ------------------------ */

/* Converts between UTF forms into out: *needed always receives the size; with
 * out null (cap 0) it is a size query; a too small cap writes nothing. */
static gates_err_t transcode_into(gates_encoding_kind_t from, gates_encoding_kind_t to, const gates_u8 *in,
                                  gates_usize_t size, bool bom, bool strict, gates_u8 *out, gates_usize_t cap,
                                  gates_usize_t *needed, gates_usize_t *bad_at) {
    gates_usize_t total = bom ? bom_of(to, nullptr) : 0;
    for (gates_usize_t pos = 0; pos < size;) {
        gates_u32 cp;
        bool bad;
        gates_usize_t used = next_cp(from, in, size, pos, &cp, &bad);
        if (bad && strict) {
            if (bad_at != nullptr) *bad_at = pos;
            return PROVEN_ERR_INVALID_ENCODING;
        }
        gates_usize_t n = put_cp(to, cp, nullptr);
        if (total > (gates_usize_t)-1 - n) return PROVEN_ERR_OVERFLOW;
        total += n;
        pos += used;
    }
    *needed = total;
    if (out == nullptr && cap == 0) return GATES_OK;
    if (out == nullptr || cap < total) return out == nullptr ? PROVEN_ERR_INVALID_ARG : PROVEN_ERR_OVERFLOW;
    gates_usize_t w = bom ? bom_of(to, out) : 0;
    for (gates_usize_t pos = 0; pos < size;) {
        gates_u32 cp;
        bool bad;
        pos += next_cp(from, in, size, pos, &cp, &bad);
        w += put_cp(to, cp, out + w);
    }
    return GATES_OK;
}

/* The UTF kind an encoding stands for; UNSUPPORTED for a real code page. */
static gates_err_t utf_kind(gates_encoding_t enc, gates_encoding_kind_t *kind) {
    bool native;
    gates_u32 cp = 0;
    gates_err_t err = resolve(enc, kind, &cp, &native);
    if (gates_is_ok(err) && !native) return PROVEN_ERR_UNSUPPORTED;
    return err;
}

gates_err_t gates_encoding_to_utf8_buf(gates_encoding_t enc, const void *in, gates_usize_t size, gates_u32 flags,
                                       gates_u8 *out, gates_usize_t cap, gates_usize_t *needed, gates_usize_t *bad_at) {
    if (needed == nullptr || (in == nullptr && size > 0)) return PROVEN_ERR_INVALID_ARG;
    gates_encoding_kind_t kind;
    gates_err_t err = utf_kind(enc, &kind);
    if (!gates_is_ok(err)) return err;
    const gates_u8 *p = (const gates_u8 *)in;
    gates_usize_t skip = 0;
    if (size > 0 && starts_with_bom(kind, p, size, &skip)) {
        p += skip;
        size -= skip;
    }
    err = transcode_into(kind, GATES_ENCODING_UTF8, p, size, false, (flags & GATES_ENCODING_STRICT) != 0, out, cap,
                         needed, bad_at);
    if (err == PROVEN_ERR_INVALID_ENCODING && bad_at != nullptr) *bad_at += skip;
    return err;
}

gates_err_t gates_encoding_from_utf8_buf(gates_encoding_t enc, gates_str_t text, gates_u32 flags, gates_u8 *out,
                                         gates_usize_t cap, gates_usize_t *needed, gates_usize_t *bad_at) {
    if (needed == nullptr || (text.ptr == nullptr && text.size > 0)) return PROVEN_ERR_INVALID_ARG;
    gates_encoding_kind_t kind;
    gates_err_t err = utf_kind(enc, &kind);
    if (!gates_is_ok(err)) return err;
    return transcode_into(GATES_ENCODING_UTF8, kind, text.ptr, text.size, enc.bom, (flags & GATES_ENCODING_STRICT) != 0,
                          out, cap, needed, bad_at);
}

/* -- in pieces (0.10.0): a stream that carries a character cut at a piece's end ------------- */

/* Whether the bytes at p (avail of them) hold a whole character of `kind` -
 * or enough to know it is broken; false means "wait for more". */
static bool char_ready(gates_encoding_kind_t kind, const gates_u8 *p, gates_usize_t avail) {
    if (avail == 0) return false;
    switch (kind) {
    case GATES_ENCODING_UTF8: {
        gates_u8 b = p[0];
        gates_usize_t need = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1;
        return avail >= need; /* a broken one is told when its bytes are in */
    }
    case GATES_ENCODING_UTF16LE:
    case GATES_ENCODING_UTF16BE: {
        if (avail < 2) return false;
        gates_u32 u = kind == GATES_ENCODING_UTF16LE ? (gates_u32)(p[0] | (p[1] << 8)) : (gates_u32)((p[0] << 8) | p[1]);
        return !(u >= 0xD800 && u <= 0xDBFF) || avail >= 4;
    }
    default:
        return avail >= 4;
    }
}

gates_err_t gates_encoding_stream_init(gates_encoding_stream_t *st, gates_encoding_t enc, gates_u32 flags,
                                       bool to_utf8) {
    if (st == nullptr) return PROVEN_ERR_INVALID_ARG;
    gates_encoding_kind_t kind;
    gates_err_t err = utf_kind(enc, &kind);
    if (!gates_is_ok(err)) return err;
    *st = (gates_encoding_stream_t){ .from = to_utf8 ? kind : GATES_ENCODING_UTF8,
                                     .to = to_utf8 ? GATES_ENCODING_UTF8 : kind,
                                     .strict = (flags & GATES_ENCODING_STRICT) != 0,
                                     .skip_bom = to_utf8,
                                     .write_bom = !to_utf8 && enc.bom };
    return GATES_OK;
}

gates_err_t gates_encoding_stream_feed(gates_encoding_stream_t *st, const void *in, gates_usize_t size, bool last,
                                       gates_u8 *out, gates_usize_t cap, gates_usize_t *used,
                                       gates_usize_t *written, gates_usize_t *bad_at) {
    if (st == nullptr || used == nullptr || written == nullptr || (in == nullptr && size > 0) ||
        (out == nullptr && cap > 0)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    const gates_u8 *p = (const gates_u8 *)in;
    gates_usize_t pos = 0, w = 0;
    *used = 0;
    *written = 0;
    if (st->write_bom) {
        gates_usize_t n = bom_of(st->to, nullptr);
        if (cap < n) return GATES_OK; /* no room yet: nothing taken */
        w += bom_of(st->to, out);
        st->write_bom = false;
    }
    for (;;) {
        /* The next character: from the carried bytes topped up from the input, or the input itself. */
        gates_u8 window[4];
        const gates_u8 *c;
        gates_usize_t avail, from_pending = st->pending_n;
        if (st->pending_n > 0) {
            while (st->pending_n < 4 && pos < size) {
                st->pending[st->pending_n++] = p[pos++];
            }
            memcpy(window, st->pending, st->pending_n);
            c = window;
            avail = st->pending_n;
        } else {
            c = p + pos;
            avail = size - pos;
        }
        if (avail == 0) break;
        if (st->skip_bom) { /* the mark of the input encoding, once, at the very start */
            gates_u8 b[4];
            gates_usize_t n = bom_of(st->from, b);
            gates_usize_t k = avail < n ? avail : n;
            if (memcmp(c, b, k) != 0) {
                st->skip_bom = false;
            } else if (avail < n && !last) {
                if (from_pending == 0) { /* too few bytes to tell: carry them */
                    memcpy(st->pending, c, avail);
                    st->pending_n = (gates_u32)avail;
                    pos = size;
                }
                break;
            } else {
                st->skip_bom = false;
                if (avail >= n) {
                    st->offset += n;
                    if (from_pending > 0) {
                        memmove(st->pending, st->pending + n, st->pending_n - n);
                        st->pending_n -= (gates_u32)n;
                    } else {
                        pos += n;
                    }
                    continue;
                }
            }
        }
        if (!char_ready(st->from, c, avail) && !last) {
            if (from_pending == 0) { /* a character cut at the end: carry it */
                memcpy(st->pending, c, avail);
                st->pending_n = (gates_u32)avail;
                pos = size;
            }
            break;
        }
        gates_u32 cp;
        bool bad;
        gates_usize_t n = next_cp(st->from, c, avail, 0, &cp, &bad);
        if (bad && st->strict) {
            if (bad_at != nullptr) *bad_at = st->offset;
            if (from_pending > 0) pos -= st->pending_n - from_pending; /* give back what was topped up */
            *used = pos;
            *written = w;
            return PROVEN_ERR_INVALID_ENCODING;
        }
        gates_usize_t len = put_cp(st->to, cp, nullptr);
        if (cap - w < len) { /* no room: stop before this character */
            if (from_pending > 0) {
                pos -= st->pending_n - from_pending;
                st->pending_n = (gates_u32)from_pending;
            }
            break;
        }
        w += put_cp(st->to, cp, out + w);
        st->offset += n;
        if (from_pending > 0) {
            memmove(st->pending, st->pending + n, st->pending_n - n);
            st->pending_n -= (gates_u32)n;
        } else {
            pos += n;
        }
    }
    *used = pos;
    *written = w;
    return GATES_OK;
}

/* -- a command line, by the Windows rules (0.10.0) -------------------------------------- */

/* One pass over the command line: counts (out null) or fills the arguments.
 * The first argument (the program) ends at a space unless quoted and knows no
 * escapes; after it: blanks separate, 2n backslashes before a quote give n and
 * toggle quoting, 2n+1 give n and a literal quote, other backslashes are
 * literal, and "" inside quotes is one literal quote. */
static void split_args(const gates_u8 *c, gates_usize_t n, gates_usize_t *argc, gates_usize_t *bytes, char **argv,
                       char *strings) {
    gates_usize_t i = 0, count = 0, total = 0;
    char *w = strings;
    while (i < n && (c[i] == ' ' || c[i] == '\t')) i++;
    if (i < n) { /* the program name */
        bool quoted = false;
        if (argv != nullptr) argv[count] = w;
        while (i < n && (quoted || (c[i] != ' ' && c[i] != '\t'))) {
            if (c[i] == '"') quoted = !quoted;
            else {
                if (w != nullptr) *w++ = (char)c[i];
                total++;
            }
            i++;
        }
        if (w != nullptr) *w++ = 0;
        total++;
        count++;
    }
    for (;;) {
        while (i < n && (c[i] == ' ' || c[i] == '\t')) i++;
        if (i >= n) break;
        bool quoted = false;
        if (argv != nullptr) argv[count] = w;
        while (i < n && (quoted || (c[i] != ' ' && c[i] != '\t'))) {
            if (c[i] == '\\') {
                gates_usize_t bs = 0;
                while (i < n && c[i] == '\\') { bs++; i++; }
                if (i < n && c[i] == '"') {
                    for (gates_usize_t k = 0; k < bs / 2; k++) { if (w != nullptr) *w++ = '\\'; total++; }
                    if (bs % 2 == 1) { if (w != nullptr) *w++ = '"'; total++; i++; }
                } else {
                    for (gates_usize_t k = 0; k < bs; k++) { if (w != nullptr) *w++ = '\\'; total++; }
                }
            } else if (c[i] == '"') {
                if (quoted && i + 1 < n && c[i + 1] == '"') { /* "" inside quotes */
                    if (w != nullptr) *w++ = '"';
                    total++;
                    i += 2;
                } else {
                    quoted = !quoted;
                    i++;
                }
            } else {
                if (w != nullptr) *w++ = (char)c[i];
                total++;
                i++;
            }
        }
        if (w != nullptr) *w++ = 0;
        total++;
        count++;
    }
    *argc = count;
    *bytes = total;
}

gates_err_t gates_args_split(gates_str_t command_line, gates_allocator_t alloc, int *argc, char ***argv) {
    if (argc == nullptr || argv == nullptr || (command_line.ptr == nullptr && command_line.size > 0)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_usize_t count = 0, bytes = 0;
    split_args(command_line.ptr, command_line.size, &count, &bytes, nullptr, nullptr);
    if (count > (gates_usize_t)INT32_MAX) return PROVEN_ERR_OVERFLOW;
    gates_usize_t head = (count + 1) * sizeof(char *);
    gates_allocator_t a = proven_alloc_is_valid(alloc) ? alloc : proven_heap_allocator();
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, head + bytes, alignof(char *));
    if (!proven_is_ok(r.err)) return r.err;
    char **v = (char **)r.value.ptr;
    split_args(command_line.ptr, command_line.size, &count, &bytes, v, (char *)r.value.ptr + head);
    v[count] = nullptr;
    *argc = (int)count;
    *argv = v;
    return GATES_OK;
}

