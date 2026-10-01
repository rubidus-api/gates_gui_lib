/* text encodings at the edge (0.10.0): the UTF forms both ways, byte order
 * marks, malformed input replaced or refused, detection, the code-page seam
 * (a fake CP949 converter), allocation failure. */
#include <gates/encoding.h>
#include <proven/heap.h>
#include "gates_test.h"

#include <stdlib.h>
#include <string.h>

static const gates_allocator_t HEAP = {0};

/* Converts and compares with the expected bytes. */
static bool to8(gates_encoding_t e, const char *in, gates_usize_t n, gates_u32 flags, const char *want) {
    gates_u8 *out = nullptr;
    gates_usize_t on = 0;
    gates_err_t err = gates_encoding_to_utf8(e, in, n, flags, HEAP, &out, &on, nullptr);
    if (!gates_is_ok(err)) return false;
    bool ok = on == strlen(want) && memcmp(out, want, on) == 0 && out[on] == 0;
    proven_heap_allocator().free_fn(nullptr, out);
    return ok;
}

static bool from8(gates_encoding_t e, const char *text, gates_u32 flags, const char *want, gates_usize_t wn) {
    gates_u8 *out = nullptr;
    gates_usize_t on = 0;
    gates_err_t err = gates_encoding_from_utf8(e, (gates_str_t){ .ptr = (const gates_u8 *)text, .size = strlen(text) },
                                               flags, HEAP, &out, &on, nullptr);
    if (!gates_is_ok(err)) return false;
    bool ok = on == wn && memcmp(out, want, wn) == 0 && out[on] == 0 && out[on + 1] == 0;
    proven_heap_allocator().free_fn(nullptr, out);
    return ok;
}

static gates_err_t bad(gates_encoding_t e, const char *in, gates_usize_t n, bool out_dir, gates_usize_t *at) {
    gates_u8 *out = (gates_u8 *)&e;
    gates_usize_t on = 7;
    *at = 999;
    gates_err_t err = out_dir ? gates_encoding_from_utf8(e, (gates_str_t){ .ptr = (const gates_u8 *)in, .size = n },
                                                         GATES_ENCODING_STRICT, HEAP, &out, &on, at)
                              : gates_encoding_to_utf8(e, in, n, GATES_ENCODING_STRICT, HEAP, &out, &on, at);
    GT_ASSERT(gates_is_ok(err) || (out == (gates_u8 *)&e && on == 7)); /* untouched on an error */
    if (gates_is_ok(err)) proven_heap_allocator().free_fn(nullptr, out);
    return err;
}

#define E(k) ((gates_encoding_t){ .kind = GATES_ENCODING_##k })
#define EB(k) ((gates_encoding_t){ .kind = GATES_ENCODING_##k, .bom = true })
#define CP(n) ((gates_encoding_t){ .kind = GATES_ENCODING_CODEPAGE, .codepage = (n) })

/* "A한😀": U+0041, U+D55C, U+1F600 */
#define T8 "A\xED\x95\x9C\xF0\x9F\x98\x80"

static void test_utf_forms(void) {
    static const char u16le[] = "A\0\x5C\xD5\x3D\xD8\x00\xDE";
    static const char u16be[] = "\0A\xD5\x5C\xD8\x3D\xDE\x00";
    static const char u32le[] = "A\0\0\0\x5C\xD5\0\0\x00\xF6\x01\0";
    static const char u32be[] = "\0\0\0A\0\0\xD5\x5C\0\x01\xF6\x00";
    GT_ASSERT(to8(E(UTF16LE), u16le, 8, 0, T8) && to8(E(UTF16BE), u16be, 8, 0, T8));
    GT_ASSERT(to8(E(UTF32LE), u32le, 12, 0, T8) && to8(E(UTF32BE), u32be, 12, 0, T8));
    GT_ASSERT(to8(E(UTF8), T8, 8, GATES_ENCODING_STRICT, T8));
    GT_ASSERT(from8(E(UTF16LE), T8, 0, u16le, 8) && from8(E(UTF16BE), T8, 0, u16be, 8));
    GT_ASSERT(from8(E(UTF32LE), T8, 0, u32le, 12) && from8(E(UTF32BE), T8, 0, u32be, 12));
    GT_ASSERT(from8(E(UTF8), T8, 0, T8, 8));
    /* Boundaries of each length. */
    GT_ASSERT(to8(E(UTF16LE), "\x7F\0\x80\0\xFF\x07\x00\x08\xFF\xFF", 10, 0, "\x7F\xC2\x80\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF"));
    GT_ASSERT(to8(E(UTF32BE), "\0\x10\xFF\xFF", 4, GATES_ENCODING_STRICT, "\xF4\x8F\xBF\xBF"));
    GT_ASSERT(to8(E(UTF32BE), "\0\x01\0\0", 4, 0, "\xF0\x90\x80\x80"));
    GT_ASSERT(from8(E(UTF16BE), "\xF4\x8F\xBF\xBF", 0, "\xDB\xFF\xDF\xFF", 4));
    /* Byte order marks: written on request, skipped when reading. */
    GT_ASSERT(from8(EB(UTF8), "A", 0, "\xEF\xBB\xBF" "A", 4));
    GT_ASSERT(from8(EB(UTF16LE), "A", 0, "\xFF\xFE" "A\0", 4));
    GT_ASSERT(from8(EB(UTF16BE), "A", 0, "\xFE\xFF\0A", 4));
    GT_ASSERT(from8(EB(UTF32LE), "A", 0, "\xFF\xFE\0\0" "A\0\0\0", 8));
    GT_ASSERT(from8(EB(UTF32BE), "A", 0, "\0\0\xFE\xFF\0\0\0A", 8));
    GT_ASSERT(from8(EB(UTF16LE), "", 0, "\xFF\xFE", 2));
    GT_ASSERT(to8(E(UTF8), "\xEF\xBB\xBF" "A", 4, 0, "A"));
    GT_ASSERT(to8(E(UTF16LE), "\xFF\xFE" "A\0", 4, 0, "A"));
    GT_ASSERT(to8(E(UTF16BE), "\xFE\xFF\0A", 4, 0, "A"));
    GT_ASSERT(to8(E(UTF32LE), "\xFF\xFE\0\0" "A\0\0\0", 8, 0, "A"));
    GT_ASSERT(to8(E(UTF16BE), "\xFF\xFE", 2, 0, "\xEF\xBF\xBE")); /* the other order's mark is text (U+FFFE) */
    GT_ASSERT(to8(E(UTF8), "A\xEF\xBB\xBF", 4, 0, "A\xEF\xBB\xBF")); /* only at the start */
    /* Empty. */
    GT_ASSERT(to8(E(UTF16LE), nullptr, 0, 0, "") && from8(E(UTF32LE), "", 0, "", 0));
}

static void test_malformed(void) {
    gates_usize_t at = 0;
    /* UTF-8: overlong, surrogate, past U+10FFFF, stray continuation, cut off, bad leads. */
    GT_ASSERT(to8(E(UTF8), "a\xC0\xAF" "b", 4, 0, "a\xEF\xBF\xBD\xEF\xBF\xBD" "b"));
    GT_ASSERT(bad(E(UTF8), "a\xC0\xAF", 3, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 1);
    GT_ASSERT(bad(E(UTF8), "ab\xE0\x80\x80", 5, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2);
    GT_ASSERT(bad(E(UTF8), "\xED\xA0\x80", 3, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 0);
    GT_ASSERT(bad(E(UTF8), "x\xF4\x90\x80\x80", 5, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 1);
    GT_ASSERT(bad(E(UTF8), "x\xF0\x80\x80\x80", 5, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 1);
    GT_ASSERT(bad(E(UTF8), "xy\x80", 3, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2);
    GT_ASSERT(bad(E(UTF8), "x\xE2\x82", 3, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 1);
    GT_ASSERT(bad(E(UTF8), "\xF5\x80\x80\x80", 4, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 0);
    GT_ASSERT(bad(E(UTF8), "\xC1\xBF", 2, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 0);
    GT_ASSERT(bad(E(UTF8), "\xE2\x28\xA1", 3, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 0);
    GT_ASSERT(bad(E(UTF8), "\xEF\xBB\xBF\xFF", 4, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 3); /* after the mark */
    GT_ASSERT(bad(E(UTF8), "\xF0\x9F\x98\x80\xDF\xBF\xEF\xBF\xBF", 9, false, &at) == GATES_OK);
    GT_ASSERT(to8(E(UTF8), "\xE2\x82", 2, 0, "\xEF\xBF\xBD\xEF\xBF\xBD"));
    /* Cut off at the very end of the input: nothing past it is read (ASan). */
    gates_u8 *cut = malloc(2);
    memcpy(cut, "\xE2\x82", 2);
    GT_ASSERT(bad(E(UTF8), (const char *)cut, 2, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 0);
    GT_ASSERT(!gates_utf8_valid((gates_str_t){ .ptr = cut, .size = 2 }, nullptr));
    free(cut);
    /* UTF-16: lone surrogates, an odd last byte. */
    GT_ASSERT(to8(E(UTF16LE), "\x00\xDC" "A\0", 4, 0, "\xEF\xBF\xBD" "A"));
    GT_ASSERT(to8(E(UTF16LE), "\x00\xD8" "A\0", 4, 0, "\xEF\xBF\xBD" "A"));
    GT_ASSERT(to8(E(UTF16LE), "A\0\x00\xD8", 4, 0, "A\xEF\xBF\xBD"));
    GT_ASSERT(to8(E(UTF16LE), "A\0B", 3, 0, "A\xEF\xBF\xBD"));
    GT_ASSERT(to8(E(UTF16LE), "\x00\xDC\x00\xDC", 4, 0, "\xEF\xBF\xBD\xEF\xBF\xBD")); /* two low halves */
    GT_ASSERT(bad(E(UTF16BE), "\0A\xDC\x00", 4, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2);
    GT_ASSERT(bad(E(UTF16BE), "\0A\xD8\x00\0B", 6, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2);
    GT_ASSERT(bad(E(UTF16LE), "A\0B", 3, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2);
    GT_ASSERT(bad(E(UTF16LE), "\xFF\xFE\x00\xDC", 4, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2);
    /* UTF-32: surrogates, past U+10FFFF, a cut-off value. */
    GT_ASSERT(to8(E(UTF32LE), "\x00\xD8\0\0" "A\0\0\0", 8, 0, "\xEF\xBF\xBD" "A"));
    GT_ASSERT(to8(E(UTF32BE), "\0\x11\0\0" "\0\0\0A", 8, 0, "\xEF\xBF\xBD" "A"));
    GT_ASSERT(to8(E(UTF32BE), "\0\0\0A\0\0", 6, 0, "A\xEF\xBF\xBD"));
    GT_ASSERT(bad(E(UTF32BE), "\0\0\0A\0\x11\0\0", 8, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 4);
    GT_ASSERT(bad(E(UTF32LE), "A\0\0", 3, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 0);
    /* Going out, malformed UTF-8 is the same: replaced, or refused at its offset. */
    GT_ASSERT(from8(E(UTF16LE), "a\xFF" "b", 0, "a\0\xFD\xFF" "b\0", 6));
    GT_ASSERT(bad(E(UTF16LE), "ab\xFF", 3, true, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2);
    /* Validation alone. */
    gates_usize_t v = 7;
    GT_ASSERT(gates_utf8_valid(GATES_STR(T8), &v) && v == 7);
    GT_ASSERT(!gates_utf8_valid((gates_str_t){ .ptr = (const gates_u8 *)"ok\xC3", .size = 3 }, &v) && v == 2);
    GT_ASSERT(!gates_utf8_valid((gates_str_t){ .ptr = (const gates_u8 *)"\x80", .size = 1 }, nullptr));
    GT_ASSERT(gates_utf8_valid((gates_str_t){0}, nullptr));
}

static void test_detect(void) {
    gates_encoding_t e;
    GT_ASSERT(gates_encoding_detect("\xEF\xBB\xBF" "a", 4, &e) == 3 && e.kind == GATES_ENCODING_UTF8);
    GT_ASSERT(gates_encoding_detect("\xFF\xFE" "a\0", 4, &e) == 2 && e.kind == GATES_ENCODING_UTF16LE);
    GT_ASSERT(gates_encoding_detect("\xFE\xFF\0a", 4, &e) == 2 && e.kind == GATES_ENCODING_UTF16BE);
    GT_ASSERT(gates_encoding_detect("\xFF\xFE\0\0" "a\0\0\0", 8, &e) == 4 && e.kind == GATES_ENCODING_UTF32LE);
    GT_ASSERT(gates_encoding_detect("\0\0\xFE\xFF", 4, &e) == 4 && e.kind == GATES_ENCODING_UTF32BE);
    GT_ASSERT(gates_encoding_detect("plain", 5, &e) == 0 && e.kind == GATES_ENCODING_UTF8);
    GT_ASSERT(gates_encoding_detect(T8, 8, &e) == 0 && e.kind == GATES_ENCODING_UTF8 && e.codepage == 0);
    GT_ASSERT(gates_encoding_detect("h\0i\0!\0", 6, &e) == 0 && e.kind == GATES_ENCODING_UTF16LE);
    GT_ASSERT(gates_encoding_detect("\0h\0i", 4, &e) == 0 && e.kind == GATES_ENCODING_UTF16BE);
    GT_ASSERT(gates_encoding_detect("\xC7\xD1\xB1\xDB", 4, &e) == 0 && e.kind == GATES_ENCODING_CODEPAGE &&
              e.codepage == GATES_CODEPAGE_SYSTEM); /* "한글" in CP949 is not UTF-8 */
    GT_ASSERT(gates_encoding_detect("h\0i", 3, &e) == 0 && e.kind == GATES_ENCODING_UTF8); /* odd length: not UTF-16 */
    GT_ASSERT(gates_encoding_detect("h\0\0i", 4, &e) == 0 && e.kind == GATES_ENCODING_UTF8); /* zeros on both sides */
    GT_ASSERT(gates_encoding_detect("ab\0c", 4, &e) == 0 && e.kind == GATES_ENCODING_UTF8); /* too few zeros */
    GT_ASSERT(gates_encoding_detect("a\0b\0\0\0", 6, &e) == 0 && e.kind == GATES_ENCODING_UTF8); /* zeros on both sides */
    GT_ASSERT(gates_encoding_detect("\0a\0b\0\0", 6, &e) == 0 && e.kind == GATES_ENCODING_UTF8);
    GT_ASSERT(gates_encoding_detect(nullptr, 9, &e) == 0 && e.kind == GATES_ENCODING_UTF8);
    GT_ASSERT(gates_encoding_detect("", 0, nullptr) == 0);
}

/* -- the code-page seam: a fake CP949 that knows ASCII, 한 (C7 D1) and 글 (B1 DB) --------- */

typedef struct fake_t { int to_calls, from_calls; gates_u32 last_cp; bool last_strict; } fake_t;

static gates_err_t emit(gates_allocator_t a, const gates_u8 *src, gates_usize_t n, gates_u8 **out, gates_usize_t *on) {
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, n + 4, 4);
    if (!proven_is_ok(r.err)) return r.err;
    memcpy(r.value.ptr, src, n);
    memset((gates_u8 *)r.value.ptr + n, 0, 4);
    *out = r.value.ptr;
    *on = n;
    return GATES_OK;
}

static gates_err_t f_to(void *ctx, gates_u32 cp, const gates_u8 *in, gates_usize_t n, bool strict, gates_allocator_t a,
                        gates_u8 **out, gates_usize_t *on, gates_usize_t *bad_at) {
    fake_t *f = ctx;
    f->to_calls++;
    f->last_cp = cp;
    f->last_strict = strict;
    if (cp != 949) return PROVEN_ERR_UNSUPPORTED;
    gates_u8 buf[256];
    gates_usize_t w = 0;
    for (gates_usize_t i = 0; i < n && w + 3 < sizeof buf;) {
        if (in[i] < 0x80) { buf[w++] = in[i++]; continue; }
        if (i + 1 < n && in[i] == 0xC7 && in[i + 1] == 0xD1) { memcpy(buf + w, "\xED\x95\x9C", 3); w += 3; i += 2; continue; }
        if (i + 1 < n && in[i] == 0xB1 && in[i + 1] == 0xDB) { memcpy(buf + w, "\xEA\xB8\x80", 3); w += 3; i += 2; continue; }
        if (strict) { if (bad_at != nullptr) *bad_at = i; return PROVEN_ERR_INVALID_ENCODING; }
        memcpy(buf + w, "\xEF\xBF\xBD", 3); w += 3; i++;
    }
    return emit(a, buf, w, out, on);
}

static gates_err_t f_from(void *ctx, gates_u32 cp, gates_str_t t, bool strict, gates_allocator_t a, gates_u8 **out,
                          gates_usize_t *on, gates_usize_t *bad_at) {
    fake_t *f = ctx;
    f->from_calls++;
    f->last_cp = cp;
    f->last_strict = strict;
    gates_u8 buf[256];
    gates_usize_t w = 0;
    for (gates_usize_t i = 0; i < t.size && w + 2 < sizeof buf;) {
        if (t.ptr[i] < 0x80) { buf[w++] = t.ptr[i++]; continue; }
        gates_usize_t len = t.ptr[i] >= 0xF0 ? 4 : t.ptr[i] >= 0xE0 ? 3 : 2;
        if (len == 3 && memcmp(t.ptr + i, "\xED\x95\x9C", 3) == 0) { buf[w++] = 0xC7; buf[w++] = 0xD1; }
        else if (len == 3 && memcmp(t.ptr + i, "\xEA\xB8\x80", 3) == 0) { buf[w++] = 0xB1; buf[w++] = 0xDB; }
        else if (strict) { if (bad_at != nullptr) *bad_at = i; return PROVEN_ERR_INVALID_ENCODING; }
        else buf[w++] = '?';
        i += len;
    }
    return emit(a, buf, w, out, on);
}

static gates_u32 f_sys(void *ctx) { (void)ctx; return 949; }
static gates_u32 f_con(void *ctx) { (void)ctx; return 437; }

static void test_codepages(void) {
    gates_usize_t at = 0;
    /* No converter: real code pages are UNSUPPORTED; the UTF ones need none. */
    gates_encoding_set_codepage_converter(nullptr);
    GT_ASSERT(!gates_encoding_has_codepage_converter());
    GT_ASSERT(gates_encoding_system_codepage() == 0 && gates_encoding_console_codepage() == 0);
    GT_ASSERT(bad(CP(949), "a", 1, false, &at) == PROVEN_ERR_UNSUPPORTED);
    GT_ASSERT(bad(CP(0), "a", 1, true, &at) == PROVEN_ERR_UNSUPPORTED);
    GT_ASSERT(to8(CP(GATES_CODEPAGE_UTF16LE), "A\0", 2, 0, "A") && to8(CP(GATES_CODEPAGE_UTF16BE), "\0A", 2, 0, "A"));
    GT_ASSERT(to8(CP(GATES_CODEPAGE_UTF32LE), "A\0\0\0", 4, 0, "A") && to8(CP(GATES_CODEPAGE_UTF32BE), "\0\0\0A", 4, 0, "A"));
    GT_ASSERT(to8(CP(GATES_CODEPAGE_UTF8), "\xEF\xBB\xBF" "A", 4, 0, "A"));
    GT_ASSERT(from8(CP(GATES_CODEPAGE_UTF16BE), "A", 0, "\0A", 2));
    /* With one: system and console answers, code page 0 is the system's. */
    fake_t f = {0};
    gates_codepage_converter_t conv = { .ctx = &f, .to_utf8 = f_to, .from_utf8 = f_from, .system_codepage = f_sys,
                                        .console_codepage = f_con };
    gates_encoding_set_codepage_converter(&conv);
    GT_ASSERT(gates_encoding_has_codepage_converter());
    GT_ASSERT(gates_encoding_system_codepage() == 949 && gates_encoding_console_codepage() == 437);
    GT_ASSERT(to8(CP(949), "ok \xC7\xD1\xB1\xDB", 7, 0, "ok \xED\x95\x9C\xEA\xB8\x80"));
    GT_ASSERT(f.to_calls == 1 && f.last_cp == 949 && !f.last_strict);
    GT_ASSERT(to8(CP(GATES_CODEPAGE_SYSTEM), "\xC7\xD1", 2, GATES_ENCODING_STRICT, "\xED\x95\x9C") && f.last_strict);
    GT_ASSERT(from8(CP(949), "\xED\x95\x9C\xEA\xB8\x80!", 0, "\xC7\xD1\xB1\xDB!", 5) && f.from_calls == 1);
    GT_ASSERT(from8(CP(949), "\xE2\x82\xAC", 0, "?", 1));               /* not in the code page */
    GT_ASSERT(bad(CP(949), "a\xE2\x82\xAC", 4, true, &at) == PROVEN_ERR_INVALID_ENCODING && at == 1);
    GT_ASSERT(bad(CP(949), "a\xFF", 2, false, &at) == PROVEN_ERR_INVALID_ENCODING && at == 1);
    GT_ASSERT(bad(CP(51949), "a", 1, false, &at) == PROVEN_ERR_UNSUPPORTED && f.last_cp == 51949);
    /* Malformed UTF-8 never reaches the converter: refused (strict) or replaced first. */
    int before = f.from_calls;
    GT_ASSERT(bad(CP(949), "ab\xFF", 3, true, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2 && f.from_calls == before);
    GT_ASSERT(from8(CP(949), "a\xFF" "b", 0, "a?b", 3) && f.from_calls == before + 1);
    /* The UTF code pages still need no converter call; BOMs are not written for code pages. */
    before = f.to_calls;
    GT_ASSERT(to8(CP(GATES_CODEPAGE_UTF16LE), "A\0", 2, 0, "A") && f.to_calls == before);
    GT_ASSERT(from8(((gates_encoding_t){ .kind = GATES_ENCODING_CODEPAGE, .codepage = 949, .bom = true }), "A", 0, "A", 1));
    /* A converter without both directions is no converter. */
    gates_encoding_set_codepage_converter(&(gates_codepage_converter_t){ .to_utf8 = f_to });
    GT_ASSERT(!gates_encoding_has_codepage_converter());
    gates_encoding_set_codepage_converter(&(gates_codepage_converter_t){ .to_utf8 = f_to, .from_utf8 = f_from });
    GT_ASSERT(gates_encoding_has_codepage_converter() && gates_encoding_system_codepage() == 0);
    GT_ASSERT(bad(CP(0), "a", 1, false, &at) == PROVEN_ERR_UNSUPPORTED); /* no system code page to name */
    GT_ASSERT(gates_encoding_console_codepage() == 0);
    gates_encoding_set_codepage_converter(nullptr);
}

/* -- arguments and allocation failure --------------------------------------------------------- */

typedef struct fail_alloc_t { int left; } fail_alloc_t;
static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->left-- <= 0) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return proven_heap_allocator().alloc_fn(nullptr, size, align);
}
static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns, proven_size_t al) {
    (void)ctx; (void)p; (void)os; (void)ns; (void)al;
    return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
}
static void fa_free(void *ctx, void *p) { (void)ctx; proven_heap_allocator().free_fn(nullptr, p); }

static void test_arguments(void) {
    gates_u8 *out = nullptr;
    gates_usize_t on = 0, at = 0;
    GT_ASSERT(gates_encoding_to_utf8(E(UTF8), "a", 1, 0, HEAP, nullptr, &on, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_to_utf8(E(UTF8), "a", 1, 0, HEAP, &out, nullptr, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_to_utf8(E(UTF8), nullptr, 1, 0, HEAP, &out, &on, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_to_utf8((gates_encoding_t){ .kind = (gates_encoding_kind_t)9 }, "a", 1, 0, HEAP, &out, &on,
                                     &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_from_utf8(E(UTF8), (gates_str_t){ .ptr = nullptr, .size = 2 }, 0, HEAP, &out, &on, &at) ==
              PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_from_utf8(E(UTF8), GATES_STR("a"), 0, HEAP, nullptr, &on, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_from_utf8(E(UTF8), GATES_STR("a"), 0, HEAP, &out, nullptr, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_from_utf8((gates_encoding_t){ .kind = (gates_encoding_kind_t)9 }, GATES_STR("a"), 0, HEAP,
                                       &out, &on, &at) == PROVEN_ERR_INVALID_ARG);
    /* bad_at may be null. */
    GT_ASSERT(gates_encoding_to_utf8(E(UTF8), "\xFF", 1, GATES_ENCODING_STRICT, HEAP, &out, &on, nullptr) ==
              PROVEN_ERR_INVALID_ENCODING);
    /* An allocator of the caller's; out of memory leaves nothing. */
    fail_alloc_t fa = { .left = 1 };
    gates_allocator_t a = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
    GT_ASSERT_OK(gates_encoding_to_utf8(E(UTF16LE), "A\0", 2, 0, a, &out, &on, nullptr));
    GT_ASSERT(on == 1 && out[0] == 'A');
    fa_free(nullptr, out);
    out = nullptr;
    GT_ASSERT(gates_encoding_to_utf8(E(UTF16LE), "A\0", 2, 0, a, &out, &on, nullptr) == PROVEN_ERR_NOMEM && out == nullptr);
    GT_ASSERT(gates_encoding_from_utf8(E(UTF32BE), GATES_STR("A"), 0, a, &out, &on, nullptr) == PROVEN_ERR_NOMEM);
    /* The replacing path with a converter allocates twice: the first may fail too. */
    fake_t f = {0};
    gates_codepage_converter_t conv = { .ctx = &f, .to_utf8 = f_to, .from_utf8 = f_from };
    gates_encoding_set_codepage_converter(&conv);
    fa.left = 0;
    GT_ASSERT(gates_encoding_from_utf8(CP(949), GATES_STR("a\xFF"), 0, a, &out, &on, nullptr) == PROVEN_ERR_NOMEM &&
              f.from_calls == 0);
    fa.left = 1;
    GT_ASSERT(gates_encoding_from_utf8(CP(949), GATES_STR("a\xFF"), 0, a, &out, &on, nullptr) == PROVEN_ERR_NOMEM &&
              f.from_calls == 1);
    fa.left = 5;
    GT_ASSERT_OK(gates_encoding_from_utf8(CP(949), GATES_STR("a\xFF"), 0, a, &out, &on, nullptr));
    GT_ASSERT(on == 2 && memcmp(out, "a?", 2) == 0 && fa.left == 3); /* a scratch copy and the result */
    fa_free(nullptr, out);
    gates_encoding_set_codepage_converter(nullptr);
}

int main(void) {
    test_utf_forms();
    test_malformed();
    test_detect();
    test_codepages();
    test_arguments();
    return gt_report("test_encoding");
}
