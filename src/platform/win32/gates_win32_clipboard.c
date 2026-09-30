/* gates_gui_lib - Win32 clipboard provider: CF_UNICODETEXT with
 * checked UTF-16 <-> UTF-8 conversion. A lone surrogate from another
 * application becomes U+FFFD; the core never receives invalid UTF-8. When
 * another process holds the clipboard the call fails with BUSY (no retry). */
#include "gates_win32_internal.h"

#include <string.h>

static gates_u32 utf8_put(gates_u8 *out, gates_u32 cp) {
    if (cp < 0x80) {
        out[0] = (gates_u8)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (gates_u8)(0xC0 | (cp >> 6));
        out[1] = (gates_u8)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (gates_u8)(0xE0 | (cp >> 12));
        out[1] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (gates_u8)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (gates_u8)(0xF0 | (cp >> 18));
    out[1] = (gates_u8)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (gates_u8)(0x80 | (cp & 0x3F));
    return 4;
}

static gates_err_t clip_get(void *ctx, gates_allocator_t alloc, gates_u8 **out,
                            gates_usize_t *out_len) {
    gates_window_t *win = ctx;
    *out = nullptr;
    *out_len = 0;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        return GATES_OK; /* nothing textual to paste */
    }
    if (!OpenClipboard(win->hwnd)) {
        return PROVEN_ERR_BUSY;
    }
    gates_err_t err = GATES_OK;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const wchar_t *w = h != nullptr ? (const wchar_t *)GlobalLock(h) : nullptr;
    if (w != nullptr) {
        /* Bounded by the block size: never trust a missing terminator. */
        gates_usize_t max_units = (gates_usize_t)(GlobalSize(h) / sizeof(wchar_t));
        gates_usize_t units = 0;
        while (units < max_units && w[units] != 0) {
            units++;
        }
        if (units > 0) {
            proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, units * 3u, 1);
            if (!proven_is_ok(r.err)) {
                err = r.err;
            } else {
                gates_u8 *u = (gates_u8 *)r.value.ptr;
                gates_usize_t n = 0;
                for (gates_usize_t i = 0; i < units; i++) {
                    gates_u32 cp = w[i];
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units && w[i + 1] >= 0xDC00 &&
                        w[i + 1] <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + ((gates_u32)w[i + 1] - 0xDC00);
                        i++;
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    n += utf8_put(u + n, cp);
                }
                *out = u;
                *out_len = n;
            }
        }
        GlobalUnlock(h);
    }
    CloseClipboard();
    return err;
}

static gates_err_t clip_set(void *ctx, gates_str_t text) {
    gates_window_t *win = ctx;
    /* UTF-8 -> UTF-16: at most one unit per byte, plus the terminator. */
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size + 1u) * sizeof(wchar_t));
    if (mem == nullptr) {
        return PROVEN_ERR_NOMEM;
    }
    wchar_t *w = (wchar_t *)GlobalLock(mem);
    if (w == nullptr) {
        GlobalFree(mem);
        return PROVEN_ERR_NOMEM;
    }
    gates_usize_t n = 0;
    for (gates_u32 at = 0; at < text.size;) {
        gates_u32 cp = 0;
        at += gates_text_decode(text, at, &cp);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            w[n++] = (wchar_t)(0xD800 + (cp >> 10));
            w[n++] = (wchar_t)(0xDC00 + (cp & 0x3FF));
        } else {
            w[n++] = (wchar_t)cp;
        }
    }
    w[n] = 0;
    GlobalUnlock(mem);
    if (!OpenClipboard(win->hwnd)) {
        GlobalFree(mem);
        return PROVEN_ERR_BUSY;
    }
    EmptyClipboard();
    gates_err_t err = GATES_OK;
    if (SetClipboardData(CF_UNICODETEXT, mem) == nullptr) {
        GlobalFree(mem); /* ownership passes only on success */
        err = PROVEN_ERR_IO;
    }
    CloseClipboard();
    return err;
}

void gates_win32_install_clipboard(gates_window_t *win) {
    gates_clipboard_t cb = { .ctx = win, .get_text = clip_get, .set_text = clip_set };
    gates_tree_set_clipboard(win->tree, &cb);
}
