/* gates_gui_lib - clipboard boundary (plan-0008, RFC-0003 section 6.1).
 *
 * The core never talks to an operating system clipboard. A platform (the
 * Win32 window) or a test installs a provider on the tree; textboxes use it
 * for copy, cut and paste. Text crosses the boundary as UTF-8 only; the
 * provider converts and validates (the Win32 provider turns a lone UTF-16
 * surrogate into U+FFFD). The core re-validates what it receives. */
#ifndef GATES_CLIPBOARD_H
#define GATES_CLIPBOARD_H

#include <gates/tree.h>

typedef struct gates_clipboard_t {
    void *ctx;
    /* Reads the clipboard's text into memory from `alloc` (the caller frees it
     * with that allocator). An empty or non-text clipboard yields OK with
     * *out = null and *out_len = 0. May fail (e.g. BUSY while another process
     * holds the clipboard). */
    gates_err_t (*get_text)(void *ctx, gates_allocator_t alloc, gates_u8 **out,
                            gates_usize_t *out_len);
    /* Replaces the clipboard's content with this UTF-8 text. */
    gates_err_t (*set_text)(void *ctx, gates_str_t text);
} gates_clipboard_t;

/* Copies the provider (its ctx is borrowed); null removes it, after which
 * copy, cut and paste do nothing. */
void gates_tree_set_clipboard(gates_tree_t *tree, const gates_clipboard_t *clipboard);

#endif /* GATES_CLIPBOARD_H */
