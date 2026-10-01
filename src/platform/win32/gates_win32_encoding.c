/* gates_gui_lib - the Win32 code-page converter (0.10.0): bytes of a Windows
 * code page to UTF-8 and back through UTF-16 (MultiByteToWideChar,
 * WideCharToMultiByte); the UTF-16 side is gates' own conversion. Strict
 * mode finds the first bad offset only on the error path. */
#include "gates_win32_internal.h"
#include <gates/encoding.h>
#include <proven/heap.h>

#include <limits.h>

/* The first input offset whose character the code page refuses: the
 * shortest valid run (1 to 4 bytes) at each step. */
static gates_usize_t first_bad_bytes(UINT cp, const gates_u8 *in, gates_usize_t size) {
    gates_usize_t pos = 0;
    while (pos < size) {
        int ok = 0;
        for (int len = 1; len <= 4 && pos + (gates_usize_t)len <= size && ok == 0; len++) {
            if (MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, (LPCCH)(in + pos), len, nullptr, 0) > 0) ok = len;
        }
        if (ok == 0) return pos;
        pos += (gates_usize_t)ok;
    }
    return size;
}

static gates_err_t cp_to_utf8(void *ctx, gates_u32 codepage, const gates_u8 *in, gates_usize_t size, bool strict,
                              gates_allocator_t alloc, gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at) {
    (void)ctx;
    gates_encoding_t wide = { .kind = GATES_ENCODING_UTF16LE };
    if (size == 0) return gates_encoding_to_utf8(wide, nullptr, 0, 0, alloc, out, out_size, nullptr);
    if (size > INT_MAX) return PROVEN_ERR_OVERFLOW;
    UINT cp = (UINT)codepage;
    DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
    int n = MultiByteToWideChar(cp, flags, (LPCCH)in, (int)size, nullptr, 0);
    if (n <= 0) {
        DWORD e = GetLastError();
        if (e == ERROR_NO_UNICODE_TRANSLATION) {
            if (bad_at != nullptr) *bad_at = first_bad_bytes(cp, in, size);
            return PROVEN_ERR_INVALID_ENCODING;
        }
        return e == ERROR_INVALID_PARAMETER || e == ERROR_INVALID_FLAGS ? PROVEN_ERR_UNSUPPORTED : PROVEN_ERR_IO;
    }
    wchar_t *w = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(wchar_t));
    if (w == nullptr) return PROVEN_ERR_NOMEM;
    MultiByteToWideChar(cp, flags, (LPCCH)in, (int)size, w, n);
    gates_err_t err = gates_encoding_to_utf8(wide, w, (gates_usize_t)n * 2u, 0, alloc, out, out_size, nullptr);
    HeapFree(GetProcessHeap(), 0, w);
    return err;
}

/* WideCharToMultiByte with the flags a code page accepts (some take none). */
static int wide_to_cp(UINT cp, const wchar_t *w, int wn, char *out, int cap, BOOL *used) {
    int n = WideCharToMultiByte(cp, WC_NO_BEST_FIT_CHARS, w, wn, out, cap, nullptr, used);
    if (n <= 0 && GetLastError() == ERROR_INVALID_FLAGS) n = WideCharToMultiByte(cp, 0, w, wn, out, cap, nullptr, used);
    return n;
}

static gates_err_t cp_from_utf8(void *ctx, gates_u32 codepage, gates_str_t text, bool strict, gates_allocator_t alloc,
                                gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at) {
    (void)ctx;
    gates_allocator_t heap = proven_heap_allocator();
    gates_u8 *wbuf = nullptr;
    gates_usize_t wbytes = 0;
    gates_err_t err = gates_encoding_from_utf8((gates_encoding_t){ .kind = GATES_ENCODING_UTF16LE }, text, 0, heap,
                                               &wbuf, &wbytes, nullptr);
    if (!gates_is_ok(err)) return err;
    gates_allocator_t a = proven_alloc_is_valid(alloc) ? alloc : heap;
    if (wbytes / 2u > INT_MAX) {
        heap.free_fn(heap.ctx, wbuf);
        return PROVEN_ERR_OVERFLOW;
    }
    UINT cp = (UINT)codepage;
    wchar_t *w = (wchar_t *)(void *)wbuf;
    int wn = (int)(wbytes / 2u);
    if (!strict) {
        /* Windows replaces each half of a pair it cannot map: make such a
         * character one '?' (pairs only; the rest is one unit already). */
        int k = 0;
        for (int i = 0; i < wn;) {
            bool pair = w[i] >= 0xD800 && w[i] <= 0xDBFF && i + 1 < wn;
            BOOL u = FALSE;
            if (pair) wide_to_cp(cp, w + i, 2, nullptr, 0, &u);
            if (pair && u) {
                w[k++] = L'?';
                i += 2;
            } else {
                w[k++] = w[i++];
                if (pair) w[k++] = w[i++];
            }
        }
        wn = k;
    }
    BOOL used = FALSE;
    int n = wn == 0 ? 0 : wide_to_cp(cp, w, wn, nullptr, 0, strict ? &used : nullptr);
    if (wn > 0 && n <= 0) {
        DWORD e = GetLastError();
        heap.free_fn(heap.ctx, wbuf);
        return e == ERROR_INVALID_PARAMETER || e == ERROR_INVALID_FLAGS ? PROVEN_ERR_UNSUPPORTED : PROVEN_ERR_IO;
    }
    if (strict && used) {
        /* The first character the code page cannot hold, as a UTF-8 offset. */
        gates_usize_t at = 0;
        int i = 0;
        while (i < wn) {
            int units = (w[i] >= 0xD800 && w[i] <= 0xDBFF && i + 1 < wn) ? 2 : 1;
            BOOL u = FALSE;
            wide_to_cp(cp, w + i, units, nullptr, 0, &u);
            if (u) break;
            gates_u32 c = units == 2 ? 0x10000u + (((gates_u32)w[i] - 0xD800u) << 10) + ((gates_u32)w[i + 1] - 0xDC00u)
                                     : (gates_u32)w[i];
            at += c < 0x80 ? 1u : c < 0x800 ? 2u : c < 0x10000 ? 3u : 4u;
            i += units;
        }
        heap.free_fn(heap.ctx, wbuf);
        if (bad_at != nullptr) *bad_at = at;
        return PROVEN_ERR_INVALID_ENCODING;
    }
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, (gates_usize_t)n + 4u, 4);
    if (!proven_is_ok(r.err)) {
        heap.free_fn(heap.ctx, wbuf);
        return r.err;
    }
    gates_u8 *buf = (gates_u8 *)r.value.ptr;
    if (n > 0) wide_to_cp(cp, w, wn, (char *)buf, n, nullptr);
    buf[n] = buf[n + 1] = buf[n + 2] = buf[n + 3] = 0;
    heap.free_fn(heap.ctx, wbuf);
    *out = buf;
    *out_size = (gates_usize_t)n;
    return GATES_OK;
}

static gates_u32 cp_system(void *ctx) {
    (void)ctx;
    return (gates_u32)GetACP();
}

static gates_u32 cp_console(void *ctx) {
    (void)ctx;
    return (gates_u32)GetConsoleOutputCP();
}

const gates_codepage_converter_t *gates_codepage_converter_win32(void) {
    static const gates_codepage_converter_t conv = {
        .ctx = nullptr,
        .to_utf8 = cp_to_utf8,
        .from_utf8 = cp_from_utf8,
        .system_codepage = cp_system,
        .console_codepage = cp_console,
    };
    return &conv;
}
