/* gates_gui_lib - UTF-8 text edit core (RFC-0001 section 16). Platform-free.
 * Every mutation keeps the buffer valid UTF-8 and the caret/anchor on
 * codepoint boundaries; growth is failure-atomic. */
#include <gates/text_edit.h>
#include <proven/heap.h>

#include <string.h>

static gates_str_t view(const gates_text_edit_t *ed) {
    return (gates_str_t){ .ptr = ed->buf, .size = ed->len };
}

static gates_u32 min_u32(gates_u32 a, gates_u32 b) { return a < b ? a : b; }
static gates_u32 max_u32(gates_u32 a, gates_u32 b) { return a > b ? a : b; }

/* Failure-atomic: on error the buffer is untouched. */
static gates_err_t reserve(gates_u8 **buf, gates_u32 *cap, gates_allocator_t alloc,
                           gates_u32 need) {
    if (need <= *cap) {
        return GATES_OK;
    }
    gates_u32 new_cap = *cap == 0 ? 32u : *cap;
    while (new_cap < need) {
        if (new_cap > (UINT32_MAX / 2u)) {
            return PROVEN_ERR_OVERFLOW;
        }
        new_cap *= 2u;
    }
    proven_result_mem_mut_t res;
    if (*buf == nullptr) {
        res = alloc.alloc_fn(alloc.ctx, new_cap, alignof(gates_u8));
    } else {
        res = alloc.realloc_fn(alloc.ctx, *buf, *cap, new_cap, alignof(gates_u8));
    }
    if (!proven_is_ok(res.err)) {
        return res.err;
    }
    *buf = (gates_u8 *)res.value.ptr;
    *cap = new_cap;
    return GATES_OK;
}

gates_err_t gates_text_edit_init(gates_text_edit_t *ed, gates_allocator_t alloc,
                                 gates_str_t initial) {
    if (ed == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *ed = (gates_text_edit_t){0};
    ed->alloc = proven_alloc_is_valid(alloc) ? alloc : proven_heap_allocator();
    if (initial.size > 0) {
        gates_err_t err = gates_text_edit_set_text(ed, initial);
        if (!gates_is_ok(err)) {
            gates_text_edit_deinit(ed);
            return err;
        }
    }
    return GATES_OK;
}

void gates_text_edit_deinit(gates_text_edit_t *ed) {
    if (ed == nullptr) {
        return;
    }
    if (ed->buf != nullptr) {
        ed->alloc.free_fn(ed->alloc.ctx, ed->buf);
    }
    if (ed->preedit != nullptr) {
        ed->alloc.free_fn(ed->alloc.ctx, ed->preedit);
    }
    *ed = (gates_text_edit_t){0};
}

gates_str_t gates_text_edit_text(const gates_text_edit_t *ed) {
    return ed == nullptr ? (gates_str_t){0} : view(ed);
}

gates_u32 gates_text_edit_caret(const gates_text_edit_t *ed) {
    return ed == nullptr ? 0 : ed->caret;
}

bool gates_text_edit_has_selection(const gates_text_edit_t *ed) {
    return ed != nullptr && ed->caret != ed->anchor;
}

gates_u32 gates_text_edit_sel_begin(const gates_text_edit_t *ed) {
    return ed == nullptr ? 0 : min_u32(ed->caret, ed->anchor);
}

gates_u32 gates_text_edit_sel_end(const gates_text_edit_t *ed) {
    return ed == nullptr ? 0 : max_u32(ed->caret, ed->anchor);
}

gates_err_t gates_text_edit_set_text(gates_text_edit_t *ed, gates_str_t text) {
    if (ed == nullptr || (text.size > 0 && text.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_err_t err = reserve(&ed->buf, &ed->cap, ed->alloc, (gates_u32)text.size);
    if (!gates_is_ok(err)) {
        return err;
    }
    if (text.size > 0) {
        memcpy(ed->buf, text.ptr, text.size);
    }
    ed->len = (gates_u32)text.size;
    ed->caret = ed->len;
    ed->anchor = ed->len;
    return GATES_OK;
}

/* Removes [begin,end) without touching the caret (callers fix it up). */
static void erase_range(gates_text_edit_t *ed, gates_u32 begin, gates_u32 end) {
    if (end > ed->len) end = ed->len;
    if (begin >= end) return;
    memmove(ed->buf + begin, ed->buf + end, ed->len - end);
    ed->len -= (end - begin);
}

static gates_err_t delete_selection(gates_text_edit_t *ed) {
    if (!gates_text_edit_has_selection(ed)) {
        return GATES_OK;
    }
    gates_u32 b = gates_text_edit_sel_begin(ed);
    gates_u32 e = gates_text_edit_sel_end(ed);
    erase_range(ed, b, e);
    ed->caret = b;
    ed->anchor = b;
    return GATES_OK;
}

gates_err_t gates_text_edit_reserve(gates_text_edit_t *ed, gates_u32 total_bytes) {
    if (ed == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    return reserve(&ed->buf, &ed->cap, ed->alloc, total_bytes);
}

gates_err_t gates_text_edit_insert(gates_text_edit_t *ed, gates_str_t text) {
    if (ed == nullptr || (text.size > 0 && text.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    /* Reserve before mutating so a failed insert leaves the text intact. */
    gates_u32 sel = gates_text_edit_sel_end(ed) - gates_text_edit_sel_begin(ed);
    gates_u32 need = ed->len - sel + (gates_u32)text.size;
    gates_err_t err = reserve(&ed->buf, &ed->cap, ed->alloc, need);
    if (!gates_is_ok(err)) {
        return err;
    }
    (void)delete_selection(ed);
    if (text.size > 0) {
        memmove(ed->buf + ed->caret + text.size, ed->buf + ed->caret, ed->len - ed->caret);
        memcpy(ed->buf + ed->caret, text.ptr, text.size);
        ed->len += (gates_u32)text.size;
        ed->caret += (gates_u32)text.size;
    }
    ed->anchor = ed->caret;
    return GATES_OK;
}

gates_err_t gates_text_edit_backspace(gates_text_edit_t *ed) {
    if (ed == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_text_edit_has_selection(ed)) {
        return delete_selection(ed);
    }
    if (ed->caret == 0) {
        return GATES_OK;
    }
    gates_u32 prev = gates_text_prev_offset(view(ed), ed->caret);
    erase_range(ed, prev, ed->caret);
    ed->caret = prev;
    ed->anchor = prev;
    return GATES_OK;
}

gates_err_t gates_text_edit_delete(gates_text_edit_t *ed) {
    if (ed == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (gates_text_edit_has_selection(ed)) {
        return delete_selection(ed);
    }
    if (ed->caret >= ed->len) {
        return GATES_OK;
    }
    gates_u32 next = gates_text_next_offset(view(ed), ed->caret);
    erase_range(ed, ed->caret, next);
    ed->anchor = ed->caret;
    return GATES_OK;
}

void gates_text_edit_move(gates_text_edit_t *ed, gates_caret_move_t how, bool extend) {
    if (ed == nullptr) {
        return;
    }
    gates_u32 target = ed->caret;
    switch (how) {
    case GATES_CARET_LEFT:
        /* An unextended move with a selection collapses to its near edge. */
        target = (!extend && gates_text_edit_has_selection(ed))
                     ? gates_text_edit_sel_begin(ed)
                     : gates_text_prev_offset(view(ed), ed->caret);
        break;
    case GATES_CARET_RIGHT:
        target = (!extend && gates_text_edit_has_selection(ed))
                     ? gates_text_edit_sel_end(ed)
                     : gates_text_next_offset(view(ed), ed->caret);
        break;
    case GATES_CARET_HOME:
        target = 0;
        break;
    case GATES_CARET_END:
    default:
        target = ed->len;
        break;
    }
    ed->caret = target;
    if (!extend) {
        ed->anchor = target;
    }
}

void gates_text_edit_select_all(gates_text_edit_t *ed) {
    if (ed == nullptr) {
        return;
    }
    ed->anchor = 0;
    ed->caret = ed->len;
}

void gates_text_edit_set_caret(gates_text_edit_t *ed, gates_u32 byte_offset, bool extend) {
    if (ed == nullptr) {
        return;
    }
    if (byte_offset > ed->len) {
        byte_offset = ed->len;
    }
    /* Snap onto a codepoint boundary. */
    if (byte_offset > 0 && byte_offset < ed->len &&
        (ed->buf[byte_offset] & 0xC0) == 0x80) {
        byte_offset = gates_text_prev_offset(view(ed), byte_offset);
    }
    ed->caret = byte_offset;
    if (!extend) {
        ed->anchor = byte_offset;
    }
}

gates_err_t gates_text_edit_set_preedit(gates_text_edit_t *ed, gates_str_t text) {
    if (ed == nullptr || (text.size > 0 && text.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_err_t err = reserve(&ed->preedit, &ed->preedit_cap, ed->alloc, (gates_u32)text.size);
    if (!gates_is_ok(err)) {
        return err;
    }
    if (text.size > 0) {
        memcpy(ed->preedit, text.ptr, text.size);
    }
    ed->preedit_len = (gates_u32)text.size;
    /* Shown after the selection, which the commit will replace. */
    ed->preedit_at = gates_text_edit_sel_end(ed);
    return GATES_OK;
}

void gates_text_edit_clear_preedit(gates_text_edit_t *ed) {
    if (ed != nullptr) {
        ed->preedit_len = 0;
        ed->preedit_at = 0;
    }
}

gates_str_t gates_text_edit_preedit(const gates_text_edit_t *ed) {
    if (ed == nullptr || ed->preedit_len == 0) {
        return (gates_str_t){0};
    }
    return (gates_str_t){ .ptr = ed->preedit, .size = ed->preedit_len };
}

gates_u32 gates_text_edit_cells_before(const gates_text_edit_t *ed, gates_u32 byte_offset) {
    if (ed == nullptr) {
        return 0;
    }
    if (byte_offset > ed->len) {
        byte_offset = ed->len;
    }
    return gates_text_cells((gates_str_t){ .ptr = ed->buf, .size = byte_offset });
}

gates_u32 gates_text_edit_offset_at_cell(const gates_text_edit_t *ed, gates_u32 cell) {
    if (ed == nullptr) {
        return 0;
    }
    gates_str_t s = view(ed);
    gates_u32 i = 0, cells = 0;
    while (i < s.size) {
        gates_u32 cp;
        gates_u32 next = i + gates_text_decode(s, i, &cp);
        gates_u32 w = gates_text_cell_width(cp);
        if (cells + w > cell) {
            /* Past the middle of a wide cell counts as the following boundary. */
            return (cell - cells) >= (w + 1) / 2 ? next : i;
        }
        cells += w;
        i = next;
    }
    return s.size;
}
