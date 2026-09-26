/* gates_gui_lib — draw list implementation (RFC-0001 §20). Platform-free.
 * Failure-atomic growth: on allocator failure the list is unchanged. */
#include <gates/draw.h>
#include <proven/heap.h>

#include <string.h>

#define GATES_DRAW_DEFAULT_CAPACITY 64u

gates_err_t gates_draw_list_init(gates_draw_list_t *dl, gates_allocator_t alloc,
                                 gates_u32 initial_capacity) {
    if (dl == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *dl = (gates_draw_list_t){0};
    dl->alloc = proven_alloc_is_valid(alloc) ? alloc : proven_heap_allocator();
    gates_u32 cap = initial_capacity != 0 ? initial_capacity : GATES_DRAW_DEFAULT_CAPACITY;
    proven_result_mem_mut_t res = dl->alloc.alloc_fn(
        dl->alloc.ctx, (proven_size_t)cap * sizeof(gates_draw_cmd_t),
        alignof(gates_draw_cmd_t));
    if (!proven_is_ok(res.err)) {
        return res.err;
    }
    dl->cmds = (gates_draw_cmd_t *)res.value.ptr;
    dl->cap = cap;
    return GATES_OK;
}

void gates_draw_list_deinit(gates_draw_list_t *dl) {
    if (dl == nullptr || dl->cmds == nullptr) {
        return;
    }
    if (dl->text != nullptr) {
        dl->alloc.free_fn(dl->alloc.ctx, dl->text);
    }
    dl->alloc.free_fn(dl->alloc.ctx, dl->cmds);
    *dl = (gates_draw_list_t){0};
}

void gates_draw_list_reset(gates_draw_list_t *dl) {
    if (dl == nullptr) {
        return;
    }
    dl->len = 0;
    dl->clip_depth = 0;
    dl->text_len = 0;
}

static gates_err_t push_cmd(gates_draw_list_t *dl, gates_draw_cmd_t cmd) {
    if (dl == nullptr || dl->cmds == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (dl->len == dl->cap) {
        gates_u32 new_cap = dl->cap * 2u;
        if (new_cap <= dl->cap) {
            return PROVEN_ERR_OVERFLOW;
        }
        proven_result_mem_mut_t res = dl->alloc.realloc_fn(
            dl->alloc.ctx, dl->cmds,
            (proven_size_t)dl->cap * sizeof(gates_draw_cmd_t),
            (proven_size_t)new_cap * sizeof(gates_draw_cmd_t),
            alignof(gates_draw_cmd_t));
        if (!proven_is_ok(res.err)) {
            return res.err; /* list unchanged */
        }
        dl->cmds = (gates_draw_cmd_t *)res.value.ptr;
        dl->cap = new_cap;
    }
    dl->cmds[dl->len++] = cmd;
    return GATES_OK;
}

gates_err_t gates_draw_rect(gates_draw_list_t *dl, gates_rect_t rect, gates_color_t color) {
    return push_cmd(dl, (gates_draw_cmd_t){
        .kind = GATES_DRAW_RECT, .rect = rect, .color = color });
}

gates_err_t gates_draw_border(gates_draw_list_t *dl, gates_rect_t rect,
                              gates_i32 thickness, gates_color_t color) {
    if (thickness < 1) {
        return PROVEN_ERR_INVALID_ARG;
    }
    return push_cmd(dl, (gates_draw_cmd_t){
        .kind = GATES_DRAW_BORDER, .rect = rect, .color = color,
        .thickness = thickness });
}

gates_err_t gates_draw_line(gates_draw_list_t *dl, gates_point_t p0, gates_point_t p1,
                            gates_color_t color) {
    return push_cmd(dl, (gates_draw_cmd_t){
        .kind = GATES_DRAW_LINE, .p0 = p0, .p1 = p1, .color = color,
        .thickness = 1 });
}

gates_err_t gates_draw_clip_push(gates_draw_list_t *dl, gates_rect_t rect) {
    gates_err_t err = push_cmd(dl, (gates_draw_cmd_t){
        .kind = GATES_DRAW_CLIP_PUSH, .rect = rect });
    if (gates_is_ok(err)) {
        dl->clip_depth++;
    }
    return err;
}

gates_err_t gates_draw_clip_pop(gates_draw_list_t *dl) {
    if (dl == nullptr || dl->clip_depth <= 0) {
        return PROVEN_ERR_INVALID_STATE;
    }
    gates_err_t err = push_cmd(dl, (gates_draw_cmd_t){ .kind = GATES_DRAW_CLIP_POP });
    if (gates_is_ok(err)) {
        dl->clip_depth--;
    }
    return err;
}

/* Failure-atomic text arena reserve. */
static gates_err_t text_reserve(gates_draw_list_t *dl, gates_u32 extra) {
    gates_u32 need = dl->text_len + extra;
    if (need < dl->text_len) {
        return PROVEN_ERR_OVERFLOW;
    }
    if (need <= dl->text_cap) {
        return GATES_OK;
    }
    gates_u32 new_cap = dl->text_cap == 0 ? 256u : dl->text_cap;
    while (new_cap < need) {
        if (new_cap > (UINT32_MAX / 2u)) {
            return PROVEN_ERR_OVERFLOW;
        }
        new_cap *= 2u;
    }
    proven_result_mem_mut_t res;
    if (dl->text == nullptr) {
        res = dl->alloc.alloc_fn(dl->alloc.ctx, new_cap, alignof(gates_u8));
    } else {
        res = dl->alloc.realloc_fn(dl->alloc.ctx, dl->text, dl->text_cap, new_cap,
                                   alignof(gates_u8));
    }
    if (!proven_is_ok(res.err)) {
        return res.err;
    }
    dl->text = (gates_u8 *)res.value.ptr;
    dl->text_cap = new_cap;
    return GATES_OK;
}

gates_err_t gates_draw_text(gates_draw_list_t *dl, gates_rect_t rect, gates_str_t text,
                            gates_i32 font_size, gates_color_t color) {
    if (dl == nullptr || dl->cmds == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (text.size > 0 && text.ptr == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_err_t err = text_reserve(dl, (gates_u32)text.size);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_u32 offset = dl->text_len;
    gates_draw_cmd_t cmd = {
        .kind = GATES_DRAW_TEXT, .rect = rect, .color = color,
        .text_offset = offset, .text_len = (gates_u32)text.size,
        .font_size = font_size,
    };
    err = push_cmd(dl, cmd);
    if (!gates_is_ok(err)) {
        return err; /* arena reserve alone does not change observable state */
    }
    if (text.size > 0) {
        memcpy(dl->text + offset, text.ptr, text.size);
        dl->text_len = offset + (gates_u32)text.size;
    }
    return GATES_OK;
}

gates_str_t gates_draw_cmd_text(const gates_draw_list_t *dl, const gates_draw_cmd_t *cmd) {
    if (dl == nullptr || cmd == nullptr || cmd->kind != GATES_DRAW_TEXT ||
        cmd->text_offset + cmd->text_len > dl->text_len) {
        return (gates_str_t){0};
    }
    return (gates_str_t){ .ptr = dl->text + cmd->text_offset, .size = cmd->text_len };
}

const gates_draw_cmd_t *gates_draw_list_at(const gates_draw_list_t *dl, gates_u32 i) {
    if (dl == nullptr || i >= dl->len) {
        return nullptr;
    }
    return &dl->cmds[i];
}
