/* gates_gui_lib - clipboard boundary and the single-line paste policy.
 * Platform-free: providers do the OS work. */
#include <gates/clipboard.h>
#include <gates/text.h>
#include "gates_tree_internal.h"

void gates_tree_set_clipboard(gates_tree_t *tree, const gates_clipboard_t *clipboard) {
    if (tree == nullptr) {
        return;
    }
    if (clipboard == nullptr || clipboard->get_text == nullptr || clipboard->set_text == nullptr) {
        tree->has_clipboard = false;
        tree->clipboard = (gates_clipboard_t){0};
        return;
    }
    tree->clipboard = *clipboard;
    tree->has_clipboard = true;
}

static gates_u32 put_utf8(gates_u8 *out, gates_u32 cp) {
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

gates_err_t gates_i_paste_normalize(gates_allocator_t alloc, gates_str_t in, bool keep_lines, gates_u8 **out,
                                    gates_u32 *out_len) {
    *out = nullptr;
    *out_len = 0;
    if (in.size == 0) {
        return GATES_OK;
    }
    if (in.size > (UINT32_MAX - 4u) / 3u) {
        return PROVEN_ERR_OVERFLOW;
    }
    /* Worst case: every byte is invalid and becomes a 3-byte U+FFFD. */
    proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, in.size * 3u + 4u, 1);
    if (!proven_is_ok(r.err)) {
        return r.err;
    }
    gates_u8 *dst = (gates_u8 *)r.value.ptr;
    gates_u32 n = 0;
    for (gates_u32 at = 0; at < in.size;) {
        gates_u32 cp = 0;
        gates_u32 step = gates_text_decode(in, at, &cp);
        if (cp == '\r' && at + 1 < in.size && in.ptr[at + 1] == '\n') {
            step = 2; /* CR LF is one line break */
        }
        at += step;
        if (keep_lines && (cp == '\r' || cp == '\n')) {
            dst[n++] = '\n'; /* an editor keeps line breaks (0.7.0) */
        } else if (keep_lines && cp == '\t') {
            dst[n++] = '\t';
        } else if (cp == '\r' || cp == '\n' || cp == '\t') {
            dst[n++] = ' ';
        } else if (cp < 0x20 || cp == 0x7F) {
            continue; /* other control characters are not text */
        } else {
            n += put_utf8(dst + n, cp);
        }
    }
    *out = dst;
    *out_len = n;
    return GATES_OK;
}
