/* text encodings at the edge (0.10.0): the UTF forms both ways, byte order
 * marks, malformed input replaced or refused, detection, the code-page seam
 * (a fake CP949 converter), allocation failure. */
#include <gates/encoding.h>
#include <proven/heap.h>
#include "gates_test.h"

#include <stdio.h>
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

/* -- without allocating: the buffer forms and streams -------------------------------------- */

static void test_buffers(void) {
    gates_u8 out[64];
    gates_usize_t need = 0, at = 0;
    /* A size query, then the conversion; too small writes nothing. */
    GT_ASSERT_OK(gates_encoding_to_utf8_buf(E(UTF16LE), "\xFF\xFE" "A\0\x5C\xD5", 6, 0, nullptr, 0, &need, nullptr));
    GT_ASSERT(need == 4);
    memset(out, 0x55, sizeof out);
    GT_ASSERT(gates_encoding_to_utf8_buf(E(UTF16LE), "\xFF\xFE" "A\0\x5C\xD5", 6, 0, out, 3, &need, nullptr) ==
              PROVEN_ERR_OVERFLOW && need == 4 && out[0] == 0x55);
    GT_ASSERT_OK(gates_encoding_to_utf8_buf(E(UTF16LE), "\xFF\xFE" "A\0\x5C\xD5", 6, 0, out, 4, &need, nullptr));
    GT_ASSERT(memcmp(out, "A\xED\x95\x9C", 4) == 0 && out[4] == 0x55); /* no terminator */
    GT_ASSERT(gates_encoding_to_utf8_buf(E(UTF16LE), "A\0\x00\xDC", 4, GATES_ENCODING_STRICT, out, 64, &need, &at) ==
              PROVEN_ERR_INVALID_ENCODING && at == 2);
    GT_ASSERT(gates_encoding_to_utf8_buf(E(UTF8), "\xEF\xBB\xBF" "a\xFF", 5, GATES_ENCODING_STRICT, out, 64, &need, &at) ==
              PROVEN_ERR_INVALID_ENCODING && at == 4);
    GT_ASSERT_OK(gates_encoding_from_utf8_buf(EB(UTF16BE), GATES_STR("A"), 0, out, 64, &need, nullptr));
    GT_ASSERT(need == 4 && memcmp(out, "\xFE\xFF\0A", 4) == 0);
    GT_ASSERT_OK(gates_encoding_from_utf8_buf(E(UTF32LE), GATES_STR(T8), 0, nullptr, 0, &need, nullptr));
    GT_ASSERT(need == 12);
    GT_ASSERT(gates_encoding_from_utf8_buf(E(UTF16LE), (gates_str_t){ .ptr = (const gates_u8 *)"ab\xFF", .size = 3 },
                                           GATES_ENCODING_STRICT, out, 64, &need, &at) == PROVEN_ERR_INVALID_ENCODING && at == 2);
    GT_ASSERT_OK(gates_encoding_to_utf8_buf(CP(GATES_CODEPAGE_UTF16BE), "\0A", 2, 0, out, 64, &need, nullptr));
    GT_ASSERT(need == 1 && out[0] == 'A');
    /* Code pages and bad arguments. */
    GT_ASSERT(gates_encoding_to_utf8_buf(CP(949), "a", 1, 0, out, 64, &need, nullptr) == PROVEN_ERR_UNSUPPORTED);
    GT_ASSERT(gates_encoding_from_utf8_buf(CP(949), GATES_STR("a"), 0, out, 64, &need, nullptr) == PROVEN_ERR_UNSUPPORTED);
    {   /* also with a converter installed: these forms never allocate */
        fake_t f = {0};
        gates_codepage_converter_t conv = { .ctx = &f, .to_utf8 = f_to, .from_utf8 = f_from, .system_codepage = f_sys };
        gates_encoding_set_codepage_converter(&conv);
        GT_ASSERT(gates_encoding_to_utf8_buf(CP(949), "a", 1, 0, out, 64, &need, nullptr) == PROVEN_ERR_UNSUPPORTED);
        GT_ASSERT(gates_encoding_to_utf8_buf(CP(0), "a", 1, 0, out, 64, &need, nullptr) == PROVEN_ERR_UNSUPPORTED);
        gates_encoding_stream_t st2;
        GT_ASSERT(gates_encoding_stream_init(&st2, CP(949), 0, true) == PROVEN_ERR_UNSUPPORTED);
        GT_ASSERT(f.to_calls == 0 && f.from_calls == 0);
        gates_encoding_set_codepage_converter(nullptr);
    }
    GT_ASSERT(gates_encoding_to_utf8_buf(E(UTF8), "a", 1, 0, out, 64, nullptr, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_to_utf8_buf(E(UTF8), nullptr, 1, 0, out, 64, &need, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_to_utf8_buf(E(UTF8), "a", 1, 0, nullptr, 5, &need, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_from_utf8_buf(E(UTF8), (gates_str_t){ .ptr = nullptr, .size = 1 }, 0, out, 64, &need, nullptr) ==
              PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_from_utf8_buf(E(UTF8), GATES_STR("a"), 0, out, 64, nullptr, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_to_utf8_buf((gates_encoding_t){ .kind = (gates_encoding_kind_t)9 }, "a", 1, 0, out, 64, &need,
                                         nullptr) == PROVEN_ERR_INVALID_ARG);
}

/* A tiny deterministic generator. */
static unsigned long long rng = 88172645463325252ull;
static unsigned rnd(unsigned n) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (unsigned)(rng % n);
}

/* Feeds `in` to a stream in random pieces with random room; compares with the one-shot result. */
static bool stream_matches(gates_encoding_t e, bool to_utf8, bool strict, const gates_u8 *in, gates_usize_t n) {
    gates_u8 *want = nullptr;
    gates_usize_t want_n = 0, want_bad = 0;
    gates_u32 fl = strict ? GATES_ENCODING_STRICT : 0;
    gates_err_t werr = to_utf8 ? gates_encoding_to_utf8(e, in, n, fl, HEAP, &want, &want_n, &want_bad)
                               : gates_encoding_from_utf8(e, (gates_str_t){ .ptr = in, .size = n }, fl, HEAP, &want,
                                                          &want_n, &want_bad);
    gates_encoding_stream_t st;
    GT_ASSERT_OK(gates_encoding_stream_init(&st, e, fl, to_utf8));
    gates_u8 got[2048];
    gates_usize_t got_n = 0, pos = 0;
    gates_err_t gerr = GATES_OK;
    gates_usize_t gbad = 0;
    int guard = 0;
    for (;;) {
        gates_usize_t piece = pos < n ? 1 + rnd((unsigned)(n - pos < 7 ? n - pos : 7)) : 0;
        bool last = pos + piece >= n;
        gates_usize_t room = 1 + rnd(9), used = 0, wrote = 0;
        if (got_n + room > sizeof got) room = sizeof got - got_n;
        gerr = gates_encoding_stream_feed(&st, in + pos, piece, last, got + got_n, room, &used, &wrote, &gbad);
        got_n += wrote;
        pos += used;
        if (!gates_is_ok(gerr)) break;
        if (last && used == piece && st.pending_n == 0 && !st.write_bom) break; /* all taken, nothing carried */
        if (++guard > 100000) return false;
    }
    bool ok;
    if (!gates_is_ok(werr)) {
        ok = gerr == werr && gbad == want_bad;
    } else {
        ok = gates_is_ok(gerr) && got_n == want_n && memcmp(got, want, want_n) == 0;
        proven_heap_allocator().free_fn(nullptr, want);
    }
    return ok;
}

static void test_streams(void) {
    static const gates_encoding_kind_t kinds[] = { GATES_ENCODING_UTF8, GATES_ENCODING_UTF16LE, GATES_ENCODING_UTF16BE,
                                                   GATES_ENCODING_UTF32LE, GATES_ENCODING_UTF32BE };
    static const gates_u32 cps[] = { 'A', 0x7F, 0xE9, 0x7FF, 0x800, 0xD55C, 0xFFFD, 0xFEFF, 0x10000, 0x1F600, 0x10FFFF };
    int mismatches = 0, runs = 0;
    for (int round = 0; round < 600; round++) {
        for (unsigned k = 0; k < 5; k++) {
            gates_encoding_t e = { .kind = kinds[k], .bom = rnd(2) == 0 };
            /* Text in that encoding (sometimes with its mark), then damage some bytes. */
            gates_u8 *src = nullptr;
            gates_usize_t sn = 0;
            char text[96];
            gates_usize_t tn = 0;
            unsigned chars = rnd(12);
            for (unsigned i = 0; i < chars; i++) {
                gates_u32 cp = cps[rnd(11)];
                gates_u8 b[4];
                gates_usize_t l = cp < 0x80 ? (b[0] = (gates_u8)cp, 1)
                                : cp < 0x800 ? (b[0] = (gates_u8)(0xC0 | (cp >> 6)), b[1] = (gates_u8)(0x80 | (cp & 0x3F)), 2)
                                : cp < 0x10000 ? (b[0] = (gates_u8)(0xE0 | (cp >> 12)), b[1] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F)),
                                                  b[2] = (gates_u8)(0x80 | (cp & 0x3F)), 3)
                                : (b[0] = (gates_u8)(0xF0 | (cp >> 18)), b[1] = (gates_u8)(0x80 | ((cp >> 12) & 0x3F)),
                                   b[2] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F)), b[3] = (gates_u8)(0x80 | (cp & 0x3F)), 4);
                memcpy(text + tn, b, l);
                tn += l;
            }
            GT_ASSERT_OK(gates_encoding_from_utf8(e, (gates_str_t){ .ptr = (const gates_u8 *)text, .size = tn }, 0, HEAP,
                                                  &src, &sn, nullptr));
            gates_u8 buf[512];
            memcpy(buf, src, sn);
            proven_heap_allocator().free_fn(nullptr, src);
            unsigned damage = rnd(3);
            for (unsigned d = 0; d < damage && sn > 0; d++) buf[rnd((unsigned)sn)] = (gates_u8)rnd(256);
            if (rnd(4) == 0 && sn > 0) sn -= 1 + rnd((unsigned)(sn < 3 ? sn : 3)); /* cut the end */
            for (int strict = 0; strict < 2; strict++) {
                runs += 2;
                if (!stream_matches(e, true, strict, buf, sn)) mismatches++;
                if (!stream_matches(e, false, strict, (const gates_u8 *)text, tn)) mismatches++;
            }
            /* Broken UTF-8 going out. */
            if (tn > 0) {
                text[rnd((unsigned)tn)] = (char)rnd(256);
                runs++;
                if (!stream_matches(e, false, rnd(2), (const gates_u8 *)text, tn)) mismatches++;
            }
        }
    }
    GT_ASSERT(mismatches == 0 && runs > 10000);
    /* By hand: a mark split over pieces, a character cut and finished, a cut left at the end. */
    gates_encoding_stream_t st;
    gates_u8 out[16];
    gates_usize_t used = 0, wrote = 0, at = 0;
    GT_ASSERT_OK(gates_encoding_stream_init(&st, E(UTF8), 0, true));
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "\xEF\xBB", 2, false, out, 16, &used, &wrote, &at));
    GT_ASSERT(used == 2 && wrote == 0 && st.pending_n == 2);
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "\xBF" "a\xED\x95", 4, false, out, 16, &used, &wrote, &at));
    GT_ASSERT(used == 4 && wrote == 1 && out[0] == 'a' && st.pending_n == 2);
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "\x9C", 1, false, out, 16, &used, &wrote, &at));
    GT_ASSERT(used == 1 && wrote == 3 && memcmp(out, "\xED\x95\x9C", 3) == 0);
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "\xE2\x82", 2, true, out, 16, &used, &wrote, &at));
    GT_ASSERT(wrote == 6 && memcmp(out, "\xEF\xBF\xBD\xEF\xBF\xBD", 6) == 0); /* cut at the very end */
    /* Strict: the offset counts from the start of the stream. */
    GT_ASSERT_OK(gates_encoding_stream_init(&st, E(UTF16LE), GATES_ENCODING_STRICT, true));
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "\xFF\xFE" "A\0", 4, false, out, 16, &used, &wrote, &at));
    GT_ASSERT(gates_encoding_stream_feed(&st, "B\0\x00\xDC", 4, true, out, 16, &used, &wrote, &at) ==
              PROVEN_ERR_INVALID_ENCODING && at == 6 && used == 2 && wrote == 1);
    /* No room: nothing taken; a mark to write waits for room. */
    GT_ASSERT_OK(gates_encoding_stream_init(&st, EB(UTF32BE), 0, false));
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "A", 1, true, out, 3, &used, &wrote, &at));
    GT_ASSERT(used == 0 && wrote == 0);
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "A", 1, true, out, 8, &used, &wrote, &at));
    GT_ASSERT(used == 1 && wrote == 8 && memcmp(out, "\0\0\xFE\xFF\0\0\0A", 8) == 0);
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, nullptr, 0, true, nullptr, 0, &used, &wrote, &at));
    /* Strict, the bad character begun in the previous piece: nothing of this piece is taken. */
    GT_ASSERT_OK(gates_encoding_stream_init(&st, E(UTF8), GATES_ENCODING_STRICT, true));
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "a\xE2", 2, false, out, 16, &used, &wrote, &at));
    GT_ASSERT(used == 2 && wrote == 1);
    GT_ASSERT(gates_encoding_stream_feed(&st, "\x41" "b", 2, true, out, 16, &used, &wrote, &at) ==
              PROVEN_ERR_INVALID_ENCODING && at == 1 && used == 0 && wrote == 0);
    /* Never past cap: a mark, then a character that does not fit after it. */
    GT_ASSERT_OK(gates_encoding_stream_init(&st, EB(UTF32BE), 0, false));
    memset(out, 0x55, sizeof out);
    GT_ASSERT_OK(gates_encoding_stream_feed(&st, "A", 1, true, out, 6, &used, &wrote, &at));
    GT_ASSERT(used == 0 && wrote == 4 && out[4] == 0x55 && out[5] == 0x55 && out[6] == 0x55);
    /* Refusals. */
    GT_ASSERT(gates_encoding_stream_init(&st, CP(949), 0, true) == PROVEN_ERR_UNSUPPORTED);
    GT_ASSERT(gates_encoding_stream_init(nullptr, E(UTF8), 0, true) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_encoding_stream_init(&st, E(UTF8), 0, true));
    GT_ASSERT(gates_encoding_stream_feed(nullptr, "a", 1, true, out, 16, &used, &wrote, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_stream_feed(&st, nullptr, 1, true, out, 16, &used, &wrote, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_stream_feed(&st, "a", 1, true, nullptr, 16, &used, &wrote, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_stream_feed(&st, "a", 1, true, out, 16, nullptr, &wrote, &at) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_encoding_stream_feed(&st, "a", 1, true, out, 16, &used, nullptr, &at) == PROVEN_ERR_INVALID_ARG);
}

/* -- command lines by the Windows rules ------------------------------------------------------ */

static bool split_is(const char *line, int want_n, const char *const *want) {
    int argc = -1;
    char **argv = nullptr;
    GT_ASSERT_OK(gates_args_split((gates_str_t){ .ptr = (const gates_u8 *)line, .size = strlen(line) }, HEAP, &argc, &argv));
    bool ok = argc == want_n && argv[argc] == nullptr;
    for (int i = 0; ok && i < argc; i++) ok = strcmp(argv[i], want[i]) == 0;
    if (!ok) {
        printf("  split [%s] ->", line);
        for (int i = 0; i < argc; i++) printf(" [%s]", argv[i]);
        printf("\n");
    }
    proven_heap_allocator().free_fn(nullptr, argv);
    return ok;
}

static void test_args(void) {
    /* Microsoft's examples ("Parsing C++ command-line arguments"), after a program name. */
    GT_ASSERT(split_is("p \"a b c\" d e", 4, (const char *const[]){ "p", "a b c", "d", "e" }));
    GT_ASSERT(split_is("p \"ab\\\"c\" \"\\\\\" d", 4, (const char *const[]){ "p", "ab\"c", "\\", "d" }));
    GT_ASSERT(split_is("p a\\\\\\b d\"e f\"g h", 4, (const char *const[]){ "p", "a\\\\\\b", "de fg", "h" }));
    GT_ASSERT(split_is("p a\\\\\\\"b c d", 4, (const char *const[]){ "p", "a\\\"b", "c", "d" }));
    GT_ASSERT(split_is("p a\\\\\\\\\"b c\" d e", 4, (const char *const[]){ "p", "a\\\\b c", "d", "e" }));
    GT_ASSERT(split_is("p a\"b\"\" c d", 2, (const char *const[]){ "p", "ab\" c d" }));
    /* The program name: quoted, backslashes literal, no escapes. */
    GT_ASSERT(split_is("\"C:\\Program Files\\x.exe\" -v", 2, (const char *const[]){ "C:\\Program Files\\x.exe", "-v" }));
    GT_ASSERT(split_is("C:\\tools\\a\\\"b c", 1, (const char *const[]){ "C:\\tools\\a\\b c" })); /* the quote opens */
    GT_ASSERT(split_is("  p\t x", 2, (const char *const[]){ "p", "x" }));
    /* Empty arguments, blanks, Korean text. */
    GT_ASSERT(split_is("p \"\" x \"\"", 4, (const char *const[]){ "p", "", "x", "" }));
    GT_ASSERT(split_is("p \"\xED\x95\x9C \xEA\xB8\x80\"", 2, (const char *const[]){ "p", "\xED\x95\x9C \xEA\xB8\x80" }));
    GT_ASSERT(split_is("p a\\", 2, (const char *const[]){ "p", "a\\" }));
    GT_ASSERT(split_is("p \"open", 2, (const char *const[]){ "p", "open" }));
    GT_ASSERT(split_is("", 0, (const char *const[]){ "" }));
    GT_ASSERT(split_is("   \t ", 0, (const char *const[]){ "" }));
    GT_ASSERT(split_is("p", 1, (const char *const[]){ "p" }));
    /* Arguments and allocation. */
    int argc = 0;
    char **argv = nullptr;
    GT_ASSERT(gates_args_split(GATES_STR("p"), HEAP, nullptr, &argv) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_args_split(GATES_STR("p"), HEAP, &argc, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_args_split((gates_str_t){ .ptr = nullptr, .size = 3 }, HEAP, &argc, &argv) == PROVEN_ERR_INVALID_ARG);
    fail_alloc_t fa = { .left = 0 };
    gates_allocator_t a = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc, .free_fn = fa_free };
    argv = (char **)&fa;
    GT_ASSERT(gates_args_split(GATES_STR("p x"), a, &argc, &argv) == PROVEN_ERR_NOMEM && argv == (char **)&fa);
}

int main(void) {
    test_utf_forms();
    test_malformed();
    test_detect();
    test_codepages();
    test_arguments();
    test_buffers();
    test_streams();
    test_args();
    return gt_report("test_encoding");
}
