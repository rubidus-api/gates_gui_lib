/* gates_gui_lib — tree -> draw list paint walk (RFC-0001 §20, Phase 2).
 * Widgets emit commands only; rendering happens in the renderer (§21).
 * All colors come from theme tokens (§30: no hard-coded RGB here). */
#include <gates/ui.h>
#include <gates/widget.h>
#include "gates_tree_internal.h"

typedef struct paint_ctx_t {
    gates_tree_t *tree;
    gates_draw_list_t *dl;
    const gates_theme_t *theme;
    const gates_text_backend_t *text;
    gates_err_t err;
} paint_ctx_t;

static void emit(paint_ctx_t *ctx, gates_err_t err) {
    if (gates_is_ok(ctx->err) && !gates_is_ok(err)) {
        ctx->err = err;
    }
}

static gates_str_t node_text(const gates_tree_t *tree, const gates_node_slot_t *s) {
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    if (st == nullptr) {
        return (gates_str_t){0};
    }
    return gates_i_widget_label(tree, st); /* a bound button shows its command */
}

static gates_rect_t centered_text_rect(const paint_ctx_t *ctx, gates_rect_t box,
                                       gates_str_t text, gates_i32 font_size) {
    gates_size_t ts = ctx->text->measure(ctx->text->ctx, font_size, text);
    return (gates_rect_t){
        box.x + (box.w - ts.w) / 2,
        box.y + (box.h - ts.h) / 2,
        ts.w, ts.h,
    };
}

/* A node's text at its rect's top-left: markup (mnemonics, plan-0018) or plain. */
static void node_text_draw(paint_ctx_t *ctx, gates_u32 idx, gates_rect_t r, gates_str_t text,
                           gates_i32 font, gates_color_t color) {
    if (gates_i_mn_markup(ctx->tree, idx)) {
        emit(ctx, gates_i_mn_draw(ctx->dl, ctx->text, r, text, font, color, gates_i_cues(ctx->tree)));
    } else {
        emit(ctx, gates_draw_text(ctx->dl, r, text, font, color));
    }
}

static void paint_node(paint_ctx_t *ctx, gates_u32 idx) {
    gates_tree_t *tree = ctx->tree;
    gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->hidden) {
        s->dirty &= ~GATES_DIRTY_PAINT;
        return; /* plan-0010: nothing of a hidden subtree is drawn */
    }
    gates_rect_t r = s->layout_rect;
    gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    bool hovered = tree->hover == idx;
    bool pressed = tree->pressed == idx;
    bool disabled = st != nullptr && gates_i_widget_inert(tree, st);
    bool has_focus = tree->focus == idx;
    gates_i32 fsz = gates_i_font(tree, idx); /* RFC-0004: the node's effective font */

    switch (s->kind) {
    case GATES_NODE_PANEL:
        emit(ctx, gates_draw_rect(ctx->dl, r,
                                  gates_theme_color(ctx->theme, GATES_COLOR_PANEL_BG)));
        break;
    case GATES_NODE_LABEL: {
        gates_str_t text = node_text(tree, s);
        if (text.size > 0) {
            gates_color_token_t tok = st != nullptr && st->invalid ? GATES_COLOR_ERROR
                                      : disabled                   ? GATES_COLOR_CONTROL_DISABLED_FG
                                                                   : GATES_COLOR_PANEL_FG;
            node_text_draw(ctx, idx, r, text, fsz, gates_theme_color(ctx->theme, tok));
        }
        break;
    }
    case GATES_NODE_BUTTON: {
        gates_color_token_t bg = disabled ? GATES_COLOR_CONTROL_BG
                                 : pressed ? GATES_COLOR_CONTROL_PRESSED_BG
                                 : hovered ? GATES_COLOR_CONTROL_HOVER_BG
                                           : GATES_COLOR_CONTROL_BG;
        emit(ctx, gates_draw_rect(ctx->dl, r, gates_theme_color(ctx->theme, bg)));
        emit(ctx, gates_draw_border(ctx->dl, r,
                                    has_focus ? gates_theme_focus_width(ctx->theme)
                                              : GATES_BUTTON_BORDER,
                                    gates_theme_color(ctx->theme,
                                                      has_focus ? GATES_COLOR_FOCUS_RING
                                                                : GATES_COLOR_CONTROL_BORDER)));
        gates_str_t text = node_text(tree, s);
        if (text.size > 0) {
            gates_color_token_t fg = disabled ? GATES_COLOR_CONTROL_DISABLED_FG
                                              : GATES_COLOR_CONTROL_FG;
            gates_rect_t tr = centered_text_rect(ctx, r, text, fsz);
            gates_i32 w = gates_i_text_w(tree, idx, ctx->text, fsz, text);
            tr.x = r.x + (r.w - w) / 2;
            tr.w = w;
            node_text_draw(ctx, idx, tr, text, fsz, gates_theme_color(ctx->theme, fg));
        }
        break;
    }
    case GATES_NODE_CHECKBOX: {
        gates_rect_t box = { r.x, r.y + (r.h - GATES_CHECK_BOX) / 2,
                             GATES_CHECK_BOX, GATES_CHECK_BOX };
        emit(ctx, gates_draw_rect(ctx->dl, box,
                                  gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_BG)));
        emit(ctx, gates_draw_border(ctx->dl, box, 1,
                                    gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_BORDER)));
        if (st != nullptr && st->checked) {
            gates_rect_t mark = { box.x + 3, box.y + 3, box.w - 6, box.h - 6 };
            emit(ctx, gates_draw_rect(ctx->dl, mark,
                                      gates_theme_color(ctx->theme, GATES_COLOR_SELECTION_BG)));
        }
        gates_str_t text = node_text(tree, s);
        if (text.size > 0) {
            gates_size_t ts = ctx->text->measure(ctx->text->ctx, fsz, text);
            gates_rect_t tr = { r.x + GATES_CHECK_BOX + GATES_CHECK_GAP,
                                r.y + (r.h - ts.h) / 2, gates_i_text_w(tree, idx, ctx->text, fsz, text),
                                ts.h };
            gates_color_token_t fg = disabled ? GATES_COLOR_CONTROL_DISABLED_FG
                                              : GATES_COLOR_PANEL_FG;
            node_text_draw(ctx, idx, tr, text, fsz, gates_theme_color(ctx->theme, fg));
        }
        if (has_focus) {
            emit(ctx, gates_draw_border(ctx->dl, r, gates_theme_focus_width(ctx->theme),
                                        gates_theme_color(ctx->theme, GATES_COLOR_FOCUS_RING)));
        }
        break;
    }
    case GATES_NODE_DIALOG: /* same background as its content panel: one surface */
        emit(ctx, gates_draw_rect(ctx->dl, r,
                                  gates_theme_color(ctx->theme, GATES_COLOR_PANEL_BG)));
        emit(ctx, gates_draw_border(ctx->dl, r, 1,
                                    gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_BORDER)));
        break; /* children (title, content) follow */
    case GATES_NODE_MENU:
        emit(ctx, gates_i_menu_paint(tree, idx, ctx->dl, ctx->theme, ctx->text));
        break;
    case GATES_NODE_MENUBAR:
        emit(ctx, gates_i_menubar_paint(tree, idx, ctx->dl, ctx->theme, ctx->text));
        break;
    case GATES_NODE_TOOLBAR:
        emit(ctx, gates_i_toolbar_paint(tree, idx, ctx->dl, ctx->theme, ctx->text));
        break;
    case GATES_NODE_GROUP:
        emit(ctx, gates_i_group_paint(tree, idx, ctx->dl, ctx->theme));
        break; /* the title and the content follow */
    case GATES_NODE_GROUPHEAD:
        emit(ctx, gates_i_group_head_paint(tree, idx, ctx->dl, ctx->theme, ctx->text));
        break;
    case GATES_NODE_SPINARROWS:
        emit(ctx, gates_i_spin_arrows_paint(tree, idx, ctx->dl, ctx->theme));
        break;
    case GATES_NODE_SLIDER:
        emit(ctx, gates_i_slider_paint(tree, idx, ctx->dl, ctx->theme));
        break;
    case GATES_NODE_TABSTRIP:
        emit(ctx, gates_i_tabstrip_paint(tree, idx, ctx->dl, ctx->theme, ctx->text));
        break;
    case GATES_NODE_STATUSBAR:
        emit(ctx, gates_i_statusbar_paint(tree, idx, ctx->dl, ctx->theme));
        break; /* its segments follow */
    case GATES_NODE_RADIO:
    case GATES_NODE_CHOICE:
        emit(ctx, gates_i_options_paint(tree, idx, ctx->dl, ctx->theme, ctx->text, pressed,
                                        hovered));
        break;
    case GATES_NODE_VIEW:
        emit(ctx, gates_i_view_paint(tree, idx, ctx->dl, ctx->theme, ctx->text));
        break;
    case GATES_NODE_SEPARATOR: {
        bool in_row = s->parent != GATES_NONE &&
                      gates_i_slot(tree, s->parent)->layout_kind == GATES_LAYOUT_ROW;
        gates_rect_t line = in_row ? (gates_rect_t){ r.x + r.w / 2, r.y, 1, r.h }
                                   : (gates_rect_t){ r.x, r.y + r.h / 2, r.w, 1 };
        emit(ctx, gates_draw_rect(ctx->dl, line,
                                  gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_BORDER)));
        break;
    }
    case GATES_NODE_PROGRESS: {
        emit(ctx, gates_draw_rect(ctx->dl, r,
                                  gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_BG)));
        emit(ctx, gates_draw_border(ctx->dl, r, 1,
                                    gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_BORDER)));
        gates_rect_t in = { r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
        gates_i32 v = st != nullptr ? st->value : 0;
        gates_i32 w = in.w > 0 ? (gates_i32)(((gates_i64)in.w * v) / 1000) : 0;
        if (w > 0 && in.h > 0) {
            emit(ctx, gates_draw_rect(ctx->dl, (gates_rect_t){ in.x, in.y, w, in.h },
                                      gates_theme_color(ctx->theme, GATES_COLOR_SELECTION_BG)));
        }
        break;
    }
    case GATES_NODE_TEXTBOX: {
        bool focused = tree->focus == idx;
        bool invalid = st != nullptr && st->invalid;
        emit(ctx, gates_draw_rect(ctx->dl, r,
                                  gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_BG)));
        /* Not by colour alone: an error border is thicker, focus has its own width. */
        gates_i32 ew = gates_theme_error_width(ctx->theme);
        gates_i32 fw = gates_theme_focus_width(ctx->theme);
        emit(ctx, gates_draw_border(ctx->dl, r,
                                    invalid ? ew : focused ? fw : GATES_TEXTBOX_BORDER,
                                    gates_theme_color(ctx->theme,
                                                      invalid   ? GATES_COLOR_ERROR
                                                      : focused ? GATES_COLOR_FOCUS_RING
                                                                : GATES_COLOR_CONTROL_BORDER)));
        if (invalid && focused) {
            /* Both must show: the error outside, the focus ring just inside it. */
            emit(ctx, gates_draw_border(ctx->dl,
                                        (gates_rect_t){ r.x + ew, r.y + ew, r.w - 2 * ew,
                                                        r.h - 2 * ew },
                                        1, gates_theme_color(ctx->theme, GATES_COLOR_FOCUS_RING)));
        }
        if (st == nullptr || st->edit == nullptr) {
            break;
        }
        gates_rect_t inner = gates_i_textbox_inner(tree, idx);
        gates_text_metrics_t m = ctx->text->metrics(ctx->text->ctx, fsz);
        gates_str_t txt = gates_text_edit_text(st->edit);
        /* RFC-0004: every x is a sum of the font's advances (gates_i_box_x). */
        gates_i32 caret_x = gates_i_box_x(ctx->text, fsz, st, gates_text_edit_caret(st->edit));
        gates_i32 text_w = gates_i_box_x(ctx->text, fsz, st, (gates_u32)txt.size);

        /* IME preedit (plan-0006): displayed at preedit_at, pushing the rest of
         * the committed text right; while composing the caret is the IME's
         * cursor inside it. */
        gates_str_t pre = gates_text_edit_preedit(st->edit);
        gates_u32 pre_at = st->edit->preedit_at;
        gates_i32 pre_x0 = 0, pre_w = 0;
        if (pre.size > 0) {
            pre_x0 = gates_i_box_x(ctx->text, fsz, st, pre_at);
            pre_w = gates_text_width(ctx->text, fsz, pre);
            gates_u32 cur = st->ime_cursor <= pre.size ? st->ime_cursor : (gates_u32)pre.size;
            caret_x = pre_x0 + gates_text_width(ctx->text, fsz, (gates_str_t){ .ptr = pre.ptr, .size = cur });
            text_w += pre_w;
        }

        /* Keep the caret in view, and no empty space after the text while
         * scrolled: this cache is the only paint-time mutation. */
        gates_i32 room = inner.w > 1 ? inner.w - 1 : 1; /* the caret takes one unit */
        if (caret_x < st->view_x) {
            st->view_x = caret_x;
        } else if (caret_x > st->view_x + room) {
            st->view_x = caret_x - room;
        }
        if (st->view_x > 0 && text_w - st->view_x < room) st->view_x = text_w - room;
        if (st->view_x < 0) st->view_x = 0;
        gates_i32 origin_x = inner.x - st->view_x;

        gates_err_t pushed = gates_draw_clip_push(ctx->dl, inner);
        emit(ctx, pushed);
        if (gates_text_edit_has_selection(st->edit)) {
            gates_i32 b = gates_i_box_x(ctx->text, fsz, st, gates_text_edit_sel_begin(st->edit));
            gates_i32 e = gates_i_box_x(ctx->text, fsz, st, gates_text_edit_sel_end(st->edit));
            emit(ctx, gates_draw_rect(ctx->dl, (gates_rect_t){ origin_x + b, inner.y, e - b, m.line_height },
                                      gates_theme_color(ctx->theme, GATES_COLOR_SELECTION_BG)));
        }
        gates_color_t fg = gates_theme_color(ctx->theme, disabled
                                                             ? GATES_COLOR_CONTROL_DISABLED_FG
                                                             : GATES_COLOR_CONTROL_FG);
        if (pre.size == 0 && st->password) {
            /* One '*' per codepoint; the text itself is never drawn. */
            static const char stars[] = "****************************************************************";
            gates_u32 count = 0;
            for (gates_u32 at = 0; at < txt.size; at += gates_text_decode(txt, at, nullptr)) count++;
            gates_i32 star = ctx->text->glyph_advance != nullptr ? ctx->text->glyph_advance(ctx->text->ctx, fsz, '*') : 0;
            gates_i32 x = origin_x;
            while (count > 0) {
                gates_u32 run = count < 64u ? count : 64u;
                emit(ctx, gates_draw_text(ctx->dl, (gates_rect_t){ x, inner.y, (gates_i32)run * star, m.line_height },
                                          (gates_str_t){ .ptr = (const gates_u8 *)stars, .size = run }, fsz, fg));
                x += (gates_i32)run * star;
                count -= run;
            }
        } else if (pre.size == 0) {
            if (txt.size > 0) {
                emit(ctx, gates_draw_text(ctx->dl, (gates_rect_t){ origin_x, inner.y, text_w, m.line_height },
                                          txt, fsz, fg));
            }
        } else {
            gates_str_t head = { .ptr = txt.ptr, .size = pre_at };
            gates_str_t tail = { .ptr = txt.ptr + pre_at, .size = txt.size - pre_at };
            gates_i32 pre_x = origin_x + pre_x0;
            if (head.size > 0) {
                emit(ctx, gates_draw_text(ctx->dl, (gates_rect_t){ origin_x, inner.y, pre_x0, m.line_height },
                                          head, fsz, fg));
            }
            emit(ctx, gates_draw_text(ctx->dl, (gates_rect_t){ pre_x, inner.y, pre_w, m.line_height }, pre, fsz, fg));
            /* Composition underline: one pixel on the last row of the line. */
            emit(ctx, gates_draw_rect(ctx->dl, (gates_rect_t){ pre_x, inner.y + m.line_height - 1, pre_w, 1 }, fg));
            if (tail.size > 0) {
                emit(ctx, gates_draw_text(ctx->dl,
                                          (gates_rect_t){ pre_x + pre_w, inner.y, gates_text_width(ctx->text, fsz, tail),
                                                          m.line_height },
                                          tail, fsz, fg));
            }
        }
        st->caret_valid = false;
        if (focused && !disabled && !st->read_only) { /* read-only: no caret */
            gates_rect_t caret = { origin_x + caret_x, inner.y, 1, m.line_height };
            emit(ctx, gates_draw_rect(ctx->dl, caret,
                                      gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_FG)));
            st->caret_rect = caret; /* for the IME window position */
            st->caret_valid = true;
        }
        if (gates_is_ok(pushed)) {
            emit(ctx, gates_draw_clip_pop(ctx->dl));
        }
        break;
    }
    case GATES_NODE_CUSTOM:
    default:
        break; /* app-drawn via the window overlay callback */
    }

    /* Children: stack paints only the active page (§10); scroll clips its
     * content to the viewport and draws a scrollbar; split draws its handle. */
    if (s->layout_kind == GATES_LAYOUT_STACK) {
        gates_u32 i = 0;
        for (gates_u32 c = s->first_child; c != GATES_NONE;
             c = gates_i_slot(tree, c)->next_sibling, i++) {
            if (i == s->active_child) {
                paint_node(ctx, c);
                break;
            }
        }
    } else if (s->layout_kind == GATES_LAYOUT_SCROLL) {
        gates_err_t pushed = gates_draw_clip_push(ctx->dl,
                                                  gates_i_scroll_viewport(tree, idx));
        emit(ctx, pushed);
        for (gates_u32 c = s->first_child; c != GATES_NONE;
             c = gates_i_slot(tree, c)->next_sibling) {
            paint_node(ctx, c);
        }
        if (gates_is_ok(pushed)) {
            emit(ctx, gates_draw_clip_pop(ctx->dl));
        }
        if (gates_i_scrollable(tree, idx)) {
            emit(ctx, gates_draw_rect(ctx->dl, gates_i_scroll_track(tree, idx),
                                      gates_theme_color(ctx->theme, GATES_COLOR_PANEL_BG)));
            emit(ctx, gates_draw_rect(ctx->dl, gates_i_scroll_thumb(tree, idx),
                                      gates_theme_color(ctx->theme,
                                                        GATES_COLOR_CONTROL_BORDER)));
        }
    } else {
        for (gates_u32 c = s->first_child; c != GATES_NONE;
             c = gates_i_slot(tree, c)->next_sibling) {
            paint_node(ctx, c);
        }
        if (s->layout_kind == GATES_LAYOUT_SPLIT && s->child_count >= 1) {
            gates_rect_t handle = gates_i_split_handle(tree, idx);
            emit(ctx, gates_draw_rect(ctx->dl, handle,
                                      gates_theme_color(ctx->theme, GATES_COLOR_CONTROL_BG)));
            emit(ctx, gates_draw_border(ctx->dl, handle, 1,
                                        gates_theme_color(ctx->theme,
                                                          GATES_COLOR_CONTROL_BORDER)));
        }
    }
    s->dirty &= ~GATES_DIRTY_PAINT;
}

gates_err_t gates_paint_tree(gates_tree_t *tree, gates_draw_list_t *dl,
                             const gates_theme_t *theme, const gates_text_backend_t *text) {
    if (tree == nullptr || dl == nullptr || theme == nullptr || text == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    paint_ctx_t ctx = { .tree = tree, .dl = dl, .theme = theme, .text = text,
                        .err = GATES_OK };
    /* Root background: window token, then the root goes through the ordinary
     * node path so every layout kind (stack/scroll/split) behaves identically
     * at the root as anywhere else. */
    emit(&ctx, gates_draw_rect(dl, gates_i_slot(tree, tree->root)->layout_rect,
                               gates_theme_color(theme, GATES_COLOR_WINDOW_BG)));
    paint_node(&ctx, tree->root);
    /* Overlays on top, in order; a dialog dims everything below it. */
    gates_rect_t view = gates_i_slot(tree, tree->root)->layout_rect;
    for (gates_u32 i = 0; i < tree->overlay_count; i++) {
        if (tree->overlays[i].kind == GATES_NODE_DIALOG) {
            emit(&ctx, gates_draw_rect(dl, view,
                                       gates_theme_color(theme, GATES_COLOR_OVERLAY_DIM)));
        }
        paint_node(&ctx, tree->overlays[i].index);
    }
    emit(&ctx, gates_i_tip_paint(tree, dl, theme, text)); /* above everything (plan-0018) */
    if (gates_is_ok(ctx.err)) {
        tree->dirty_bits &= ~(gates_u32)GATES_TREE_DIRTY_PAINT;
    }
    return ctx.err;
}
