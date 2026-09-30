/* gates_gui_lib - number input: spin box (a text box and arrows in a row) and
 * slider (one node: track, ticks, thumb) over one range model (0.4.0).
 * Platform-free. */
#include <gates/inputs.h>
#include <gates/widget.h>
#include <gates/layout.h>
#include <gates/ui.h>
#include "gates_tree_internal.h"
#include <gates/timer.h>

#include <string.h>

#define ARROWS_W 18
#define SLIDER_LEN 160
#define THUMB_LONG 10              /* along the track */
#define TRACK_THICK 4

static gates_widget_state_t *state_at(const gates_tree_t *tree, gates_u32 idx) {
    return gates_i_state(tree, gates_i_slot(tree, idx)->state_index);
}

static gates_i_range *range_of(const gates_tree_t *tree, gates_u32 idx) {
    const gates_node_slot_t *s = gates_i_slot(tree, idx);
    if (s->kind != GATES_NODE_SPIN && s->kind != GATES_NODE_SLIDER) return nullptr;
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    return st != nullptr ? st->rng : nullptr;
}

static gates_i_range *range_h(const gates_tree_t *tree, gates_node_t node) {
    return tree != nullptr && gates_i_valid(tree, node) ? range_of(tree, node.index) : nullptr;
}

static gates_i64 clamp(const gates_i_range *r, gates_i64 v) {
    return v < r->min ? r->min : v > r->max ? r->max : v;
}

static bool scale_ok(gates_u32 scale) {
    return scale == 0 || scale == 1 || scale == 10 || scale == 100 || scale == 1000;
}

static int decimals(gates_u32 scale) {
    return scale == 10 ? 1 : scale == 100 ? 2 : scale == 1000 ? 3 : 0;
}

static bool desc_ok(const gates_range_t *d) {
    return d != nullptr && d->min <= d->max && d->step >= 0 && d->page >= 0 && scale_ok(d->scale);
}

static void from_desc(gates_i_range *r, const gates_range_t *d) {
    r->min = d->min;
    r->max = d->max;
    r->step = d->step > 0 ? d->step : 1;
    r->page = d->page > 0 ? d->page : 10 * r->step;
    if (r->page < r->step) r->page = r->step;
    r->scale = d->scale;
    r->value = clamp(r, d->value);
}

/* -- text --------------------------------------------------------------------------------- */

gates_usize_t gates_range_format(gates_i64 value, gates_u32 scale, char *buf, gates_usize_t cap) {
    char tmp[32];
    int n = 0;
    bool neg = value < 0;
    gates_u64 u = neg ? (gates_u64)0 - (gates_u64)value : (gates_u64)value;
    int dec = scale_ok(scale) ? decimals(scale) : 0;
    char digits[24];
    int d = 0;
    do { digits[d++] = (char)('0' + u % 10); u /= 10; } while (u != 0);
    while (d <= dec) digits[d++] = '0'; /* at least one digit before the point */
    if (neg) tmp[n++] = '-';
    for (int i = d - 1; i >= 0; i--) {
        tmp[n++] = digits[i];
        if (i == dec && dec > 0) tmp[n++] = '.';
    }
    if (cap > 0) {
        gates_usize_t k = (gates_usize_t)n < cap - 1 ? (gates_usize_t)n : cap - 1;
        memcpy(buf, tmp, k);
        buf[k] = 0;
    }
    return (gates_usize_t)n;
}

bool gates_range_parse(gates_str_t text, gates_u32 scale, gates_i64 *out) {
    if (!scale_ok(scale) || out == nullptr || (text.size > 0 && text.ptr == nullptr)) return false;
    gates_usize_t i = 0, n = text.size;
    while (i < n && text.ptr[i] == ' ') i++;
    while (n > i && text.ptr[n - 1] == ' ') n--;
    bool neg = false;
    if (i < n && (text.ptr[i] == '-' || text.ptr[i] == '+')) neg = text.ptr[i++] == '-';
    int dec = decimals(scale), frac = -1, digits = 0;
    gates_u64 v = 0;
    for (; i < n; i++) {
        gates_u8 c = text.ptr[i];
        if ((c == '.' || c == ',') && frac < 0) {
            frac = 0;
            continue;
        }
        if (c < '0' || c > '9') return false;
        if (frac >= 0 && ++frac > dec) return false;
        if (v > (UINT64_MAX - 9) / 10) return false;
        v = v * 10 + (gates_u64)(c - '0');
        digits++;
    }
    if (digits == 0) return false;
    for (int k = frac < 0 ? 0 : frac; k < dec; k++) {
        if (v > UINT64_MAX / 10) return false;
        v *= 10;
    }
    if (v > (gates_u64)INT64_MAX + (neg ? 1u : 0u)) return false;
    *out = neg ? (gates_i64)(0 - v) : (gates_i64)v;
    return true;
}

/* -- the value ------------------------------------------------------------------------------ */

static gates_u32 box_of(const gates_tree_t *tree, gates_u32 spin) {
    return gates_i_slot(tree, spin)->first_child;
}

/* Shows the value in the spin box's text, silently (no TEXT_CHANGED). */
static void show(gates_tree_t *tree, gates_u32 spin) {
    const gates_i_range *r = range_of(tree, spin);
    gates_u32 box = box_of(tree, spin);
    if (r == nullptr || box == GATES_NONE) return;
    char buf[32];
    gates_usize_t n = gates_range_format(r->value, r->scale, buf, sizeof buf);
    gates_node_t bh = gates_i_handle(tree, box);
    gates_widget_state_t *bs = state_at(tree, box);
    if (bs != nullptr && bs->edit != nullptr && gates_text_edit_preedit(bs->edit).size > 0) return;
    (void)gates_textbox_set_text(tree, bh, (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = n });
    (void)gates_textbox_set_invalid(tree, bh, false);
}

/* A person sets the value: reserves the report first; nothing when unchanged. */
static gates_err_t user_set(gates_tree_t *tree, gates_u32 idx, gates_i64 v) {
    gates_i_range *r = range_of(tree, idx);
    v = clamp(r, v);
    if (v == r->value) {
        if (gates_i_slot(tree, idx)->kind == GATES_NODE_SPIN) show(tree, idx);
        return GATES_OK;
    }
    if (gates_i_wants_events(tree, idx)) {
        gates_err_t err = gates_i_event_reserve(tree, 1, 0);
        if (!gates_is_ok(err)) return err;
    }
    r->value = v;
    gates_widget_state_t *st = state_at(tree, idx);
    st->revision++;
    if (gates_i_slot(tree, idx)->kind == GATES_NODE_SPIN) show(tree, idx);
    gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    gates_i_access_log(tree, GATES_ACCESS_CHANGED, idx, 0);
    gates_i_event_push(tree, idx, GATES_EVENT_VALUE_CHANGED, GATES_ORIGIN_USER);
    return GATES_OK;
}

static void user_set_or_record(gates_tree_t *tree, gates_u32 idx, gates_i64 v) {
    gates_err_t err = user_set(tree, idx, v);
    if (!gates_is_ok(err)) tree->input_error = err;
}

gates_i64 gates_i_range_value(const gates_tree_t *tree, gates_u32 idx) {
    const gates_i_range *r = range_of(tree, idx);
    return r != nullptr ? r->value : 0;
}

bool gates_i_range_info(const gates_tree_t *tree, gates_u32 idx, gates_i64 *min, gates_i64 *max, gates_i64 *value) {
    const gates_i_range *r = range_of(tree, idx);
    if (r == nullptr) return false;
    *min = r->min;
    *max = r->max;
    *value = r->value;
    return true;
}

void gates_i_range_steps(const gates_tree_t *tree, gates_u32 idx, gates_i64 *step, gates_i64 *page, gates_u32 *scale) {
    const gates_i_range *r = range_of(tree, idx);
    *step = r != nullptr ? r->step : 1;
    *page = r != nullptr ? r->page : 1;
    *scale = r != nullptr && r->scale > 1 ? r->scale : 1u;
}

gates_err_t gates_i_range_user_set(gates_tree_t *tree, gates_u32 idx, gates_i64 v) {
    return range_of(tree, idx) != nullptr ? user_set(tree, idx, v) : PROVEN_ERR_INVALID_ARG;
}

/* -- creation --------------------------------------------------------------------------------- */

static gates_err_t make_node(gates_tree_t *tree, gates_node_t parent, gates_node_kind_t kind, gates_node_t *out) {
    gates_node_desc_t nd = { .kind = kind };
    gates_err_t err = gates_node_create(tree, parent, &nd, out);
    if (!gates_is_ok(err)) return err;
    gates_u32 state = GATES_NONE;
    err = gates_i_state_acquire(tree, &state);
    if (!gates_is_ok(err)) {
        gates_i_discard_detached(tree, *out);
        return err;
    }
    gates_i_slot(tree, out->index)->state_index = state;
    return GATES_OK;
}

static gates_err_t new_range(gates_tree_t *tree, const gates_range_t *d, gates_i_range **out) {
    proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, sizeof(gates_i_range), alignof(gates_i_range));
    if (!proven_is_ok(r.err)) return r.err;
    *out = (gates_i_range *)r.value.ptr;
    **out = (gates_i_range){0};
    from_desc(*out, d);
    return GATES_OK;
}

void gates_i_range_free(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->rng != nullptr) tree->alloc.free_fn(tree->alloc.ctx, st->rng);
    st->rng = nullptr;
}

/* Builds detached, then attaches: a failure leaves nothing behind. */
static gates_err_t attach(gates_tree_t *tree, gates_node_t parent, gates_node_t node) {
    if (gates_node_eq(parent, GATES_NODE_NULL)) return GATES_OK;
    gates_err_t err = gates_node_append(tree, parent, node);
    if (!gates_is_ok(err)) gates_i_discard_detached(tree, node);
    return err;
}

gates_err_t gates_spin_create(gates_tree_t *tree, gates_node_t parent, const gates_range_t *range,
                              gates_node_t *out_spin) {
    if (tree == nullptr || out_spin == nullptr || !desc_ok(range) ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_spin = GATES_NODE_NULL;
    gates_i_range *rng = nullptr;
    gates_err_t err = new_range(tree, range, &rng);
    if (!gates_is_ok(err)) return err;
    gates_node_t spin = GATES_NODE_NULL, box, arrows;
    err = make_node(tree, GATES_NODE_NULL, GATES_NODE_SPIN, &spin);
    if (gates_is_ok(err)) {
        state_at(tree, spin.index)->rng = rng;
        rng = nullptr;
        err = gates_layout_set(tree, spin, GATES_LAYOUT_KIND_ROW);
    }
    if (gates_is_ok(err)) err = gates_textbox_create(tree, spin, GATES_STR(""), 8, &box);
    if (gates_is_ok(err)) err = gates_layout_set_child_grow(tree, box, 1);
    if (gates_is_ok(err)) err = make_node(tree, spin, GATES_NODE_SPINARROWS, &arrows);
    if (gates_is_ok(err)) {
        show(tree, spin.index);
        err = attach(tree, parent, spin);
        if (!gates_is_ok(err)) return err;
    }
    if (!gates_is_ok(err)) {
        if (rng != nullptr) tree->alloc.free_fn(tree->alloc.ctx, rng);
        if (gates_i_valid(tree, spin)) gates_i_discard_detached(tree, spin);
        return err;
    }
    *out_spin = spin;
    return GATES_OK;
}

gates_err_t gates_slider_create(gates_tree_t *tree, gates_node_t parent, const gates_range_t *range, bool vertical,
                                gates_node_t *out_slider) {
    if (tree == nullptr || out_slider == nullptr || !desc_ok(range) ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_slider = GATES_NODE_NULL;
    gates_i_range *rng = nullptr;
    gates_err_t err = new_range(tree, range, &rng);
    if (!gates_is_ok(err)) return err;
    rng->vertical = vertical;
    gates_node_t s = GATES_NODE_NULL;
    err = make_node(tree, GATES_NODE_NULL, GATES_NODE_SLIDER, &s);
    if (!gates_is_ok(err)) {
        tree->alloc.free_fn(tree->alloc.ctx, rng);
        return err;
    }
    state_at(tree, s.index)->rng = rng;
    err = attach(tree, parent, s);
    if (!gates_is_ok(err)) return err;
    gates_i_mark_dirty(tree, s.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    *out_slider = s;
    return GATES_OK;
}

/* -- the program ------------------------------------------------------------------------------ */

gates_err_t gates_range_set_value(gates_tree_t *tree, gates_node_t node, gates_i64 value) {
    gates_i_range *r = range_h(tree, node);
    if (r == nullptr) return PROVEN_ERR_INVALID_ARG;
    value = clamp(r, value);
    if (value != r->value) {
        r->value = value;
        state_at(tree, node.index)->revision++;
        gates_i_mark_dirty(tree, node.index, GATES_DIRTY_PAINT);
        gates_i_access_log(tree, GATES_ACCESS_CHANGED, node.index, 0);
    }
    if (gates_i_slot(tree, node.index)->kind == GATES_NODE_SPIN) show(tree, node.index);
    return GATES_OK;
}

gates_i64 gates_range_value(const gates_tree_t *tree, gates_node_t node) {
    const gates_i_range *r = range_h(tree, node);
    return r != nullptr ? r->value : 0;
}

gates_err_t gates_range_set(gates_tree_t *tree, gates_node_t node, const gates_range_t *range) {
    gates_i_range *r = range_h(tree, node);
    if (r == nullptr || !desc_ok(range)) return PROVEN_ERR_INVALID_ARG;
    gates_range_t d = *range;
    d.value = r->value; /* kept, then clamped */
    bool vertical = r->vertical;
    gates_u32 ticks = r->ticks;
    from_desc(r, &d);
    r->vertical = vertical;
    r->ticks = ticks;
    return gates_range_set_value(tree, node, r->value);
}

gates_range_t gates_range_get(const gates_tree_t *tree, gates_node_t node) {
    const gates_i_range *r = range_h(tree, node);
    if (r == nullptr) return (gates_range_t){0};
    return (gates_range_t){ .min = r->min, .max = r->max, .step = r->step, .page = r->page, .value = r->value,
                            .scale = r->scale };
}

gates_err_t gates_slider_set_ticks(gates_tree_t *tree, gates_node_t slider, gates_u32 steps) {
    gates_i_range *r = range_h(tree, slider);
    if (r == nullptr || gates_i_slot(tree, slider.index)->kind != GATES_NODE_SLIDER) return PROVEN_ERR_INVALID_ARG;
    r->ticks = steps;
    gates_i_mark_dirty(tree, slider.index, GATES_DIRTY_PAINT);
    return GATES_OK;
}

gates_node_t gates_spin_box(const gates_tree_t *tree, gates_node_t spin) {
    if (tree == nullptr || !gates_i_valid(tree, spin) || gates_i_slot(tree, spin.index)->kind != GATES_NODE_SPIN) {
        return GATES_NODE_NULL;
    }
    return gates_i_handle(tree, box_of(tree, spin.index));
}

/* -- spin box behaviour ---------------------------------------------------------------------- */

gates_u32 gates_i_spin_of_box(const gates_tree_t *tree, gates_u32 box) {
    if (box == GATES_NONE) return GATES_NONE;
    gates_u32 p = gates_i_slot(tree, box)->parent;
    return p != GATES_NONE && gates_i_slot(tree, p)->kind == GATES_NODE_SPIN && box_of(tree, p) == box ? p : GATES_NONE;
}

/* The typed text: a number in range, or none. */
static bool typed(const gates_tree_t *tree, gates_u32 spin, gates_i64 *v) {
    const gates_i_range *r = range_of(tree, spin);
    return gates_range_parse(gates_textbox_text(tree, gates_i_handle(tree, box_of(tree, spin))), r->scale, v) &&
           *v >= r->min && *v <= r->max;
}

void gates_i_spin_typed(gates_tree_t *tree, gates_u32 spin) {
    gates_i64 v;
    (void)gates_textbox_set_invalid(tree, gates_i_handle(tree, box_of(tree, spin)), !typed(tree, spin, &v));
}

/* Enter, leaving, or a step key: a valid typed number becomes the value;
 * anything else goes back to the value. */
void gates_i_spin_commit(gates_tree_t *tree, gates_u32 spin) {
    gates_i64 v;
    if (typed(tree, spin, &v)) {
        user_set_or_record(tree, spin, v);
    }
    show(tree, spin);
}

bool gates_i_spin_key(gates_tree_t *tree, gates_u32 spin, const gates_key_event_t *ev) {
    if (ev->ctrl || ev->alt) return false;
    const gates_i_range *r = range_of(tree, spin);
    gates_i64 d;
    switch (ev->key) {
    case GATES_KEY_UP: d = r->step; break;
    case GATES_KEY_DOWN: d = -r->step; break;
    case GATES_KEY_PAGE_UP: d = r->page; break;
    case GATES_KEY_PAGE_DOWN: d = -r->page; break;
    case GATES_KEY_ENTER:
        gates_i_spin_commit(tree, spin);
        return true;
    default:
        return false;
    }
    gates_i_spin_commit(tree, spin);
    gates_i64 v = r->value;
    gates_i64 to = (d > 0 && v > INT64_MAX - d) ? INT64_MAX : (d < 0 && v < INT64_MIN - d) ? INT64_MIN : v + d;
    user_set_or_record(tree, spin, to);
    return true;
}

static void spin_step(gates_tree_t *tree, gates_u32 spin, bool up) {
    gates_key_event_t ev = { .key = up ? GATES_KEY_UP : GATES_KEY_DOWN, .down = true };
    (void)gates_i_spin_key(tree, spin, &ev);
}

/* Held down, an arrow repeats after a pause, while the pointer stays on it (0.8.0). */
#define SPIN_REPEAT_DELAY_MS 400u
#define SPIN_REPEAT_MS 50u

static bool on_half(const gates_tree_t *tree, gates_u32 arrows, gates_point_t p, bool up) {
    gates_rect_t r = gates_i_slot(tree, arrows)->layout_rect;
    return gates_rect_contains(r, p) && (p.y < r.y + r.h / 2) == up;
}

static void spin_tick(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)id;
    (void)user;
    tree->spin_timer = 0;
    if (tree->drag_kind != GATES_DRAG_SPIN || tree->drag_node != node.index) return;
    bool up = tree->drag_start_value > 0;
    gates_u32 spin = gates_i_slot(tree, node.index)->parent;
    if (on_half(tree, node.index, tree->drag_start, up)) spin_step(tree, spin, up);
    gates_timer_id_t t = 0;
    if (gates_is_ok(gates_timer_start(tree, node, SPIN_REPEAT_MS, false, spin_tick, nullptr, &t))) tree->spin_timer = t;
}

/* A press on an arrow steps once and, when the tree has a clock, repeats while held. */
bool gates_i_spin_arrows_press(gates_tree_t *tree, gates_u32 arrows, gates_point_t p) {
    gates_u32 spin = gates_i_slot(tree, arrows)->parent;
    const gates_widget_state_t *st = spin != GATES_NONE ? state_at(tree, spin) : nullptr;
    if (st == nullptr || st->disabled) return true;
    gates_rect_t r = gates_i_slot(tree, arrows)->layout_rect;
    bool up = p.y < r.y + r.h / 2;
    gates_tree_set_focus(tree, gates_i_handle(tree, box_of(tree, spin)));
    spin_step(tree, spin, up);
    gates_timer_id_t t = 0;
    if (gates_is_ok(gates_timer_start(tree, gates_i_handle(tree, arrows), SPIN_REPEAT_DELAY_MS, false, spin_tick,
                                      nullptr, &t))) {
        tree->spin_timer = t;
        tree->drag_kind = GATES_DRAG_SPIN;
        tree->drag_node = arrows;
        tree->drag_start = p;
        tree->drag_start_value = up ? 1 : -1;
    }
    return true;
}

void gates_i_spin_arrows_release(gates_tree_t *tree) {
    if (tree->spin_timer != 0) (void)gates_timer_cancel(tree, tree->spin_timer);
    tree->spin_timer = 0;
}

/* The wheel over a focused spin box or slider steps it (0.8.0); unfocused, it
 * scrolls what holds it, so scrolling a page never changes a value by accident. */
bool gates_i_range_wheel(gates_tree_t *tree, gates_u32 idx, gates_vec2_t wheel) {
    gates_u32 ctl = GATES_NONE;
    gates_node_kind_t k = (gates_node_kind_t)gates_i_slot(tree, idx)->kind;
    if (k == GATES_NODE_SLIDER || k == GATES_NODE_SPIN) {
        ctl = idx;
    } else if (k == GATES_NODE_SPINARROWS) {
        ctl = gates_i_slot(tree, idx)->parent;
    } else if (k == GATES_NODE_TEXTBOX) {
        ctl = gates_i_spin_of_box(tree, idx);
    }
    if (ctl == GATES_NONE || wheel.y == 0.0f) return false;
    bool slider = gates_i_slot(tree, ctl)->kind == GATES_NODE_SLIDER;
    const gates_widget_state_t *st = state_at(tree, ctl);
    if (st == nullptr || st->disabled || tree->focus != (slider ? ctl : box_of(tree, ctl))) return false;
    gates_i32 n = (gates_i32)(wheel.y < 0 ? -wheel.y + 0.5f : wheel.y + 0.5f);
    if (n < 1) n = 1;
    if (n > 100) n = 100;
    for (gates_i32 i = 0; i < n; i++) {
        if (slider) {
            gates_key_event_t ev = { .key = wheel.y > 0 ? GATES_KEY_UP : GATES_KEY_DOWN, .down = true };
            (void)gates_i_slider_key(tree, ctl, &ev);
        } else {
            spin_step(tree, ctl, wheel.y > 0);
        }
    }
    return true;
}

gates_err_t gates_i_spin_arrows_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                      const gates_theme_t *theme) {
    gates_rect_t r = gates_i_slot(tree, idx)->layout_rect;
    gates_u32 spin = gates_i_slot(tree, idx)->parent;
    const gates_widget_state_t *st = spin != GATES_NONE ? state_at(tree, spin) : nullptr;
    bool off = st == nullptr || st->disabled;
    gates_color_t border = gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER);
    gates_color_t fg = gates_theme_color(theme, off ? GATES_COLOR_CONTROL_DISABLED_FG : GATES_COLOR_CONTROL_FG);
    gates_err_t err = gates_draw_rect(dl, r, gates_theme_color(theme, GATES_COLOR_CONTROL_BG));
    if (gates_is_ok(err)) err = gates_draw_border(dl, r, 1, border);
    if (gates_is_ok(err)) err = gates_draw_rect(dl, (gates_rect_t){ r.x, r.y + r.h / 2, r.w, 1 }, border);
    /* Two small triangles drawn as stacked bars: up in the top half, down below. */
    gates_i32 cx = r.x + r.w / 2;
    for (int half = 0; half < 2 && gates_is_ok(err); half++) {
        gates_i32 top = half == 0 ? r.y : r.y + r.h / 2;
        gates_i32 mid = top + r.h / 4;
        for (gates_i32 k = 0; k < 3 && gates_is_ok(err); k++) {
            gates_i32 y = half == 0 ? mid - 1 + k : mid + 1 - k;
            err = gates_draw_rect(dl, (gates_rect_t){ cx - k, y, 2 * k + 1, 1 }, fg);
        }
    }
    return err;
}

gates_size_t gates_i_spin_arrows_measure(void) {
    return (gates_size_t){ ARROWS_W, GATES_ACCESS_MIN_TARGET };
}

/* -- slider behaviour ------------------------------------------------------------------------- */

gates_size_t gates_i_slider_measure(const gates_tree_t *tree, const gates_node_slot_t *s) {
    const gates_widget_state_t *st = gates_i_state(tree, s->state_index);
    bool v = st != nullptr && st->rng != nullptr && st->rng->vertical;
    return v ? (gates_size_t){ GATES_ACCESS_MIN_TARGET, SLIDER_LEN } : (gates_size_t){ SLIDER_LEN, GATES_ACCESS_MIN_TARGET };
}

/* The track's travel: where the thumb's centre can go, along the axis. */
static void travel(const gates_tree_t *tree, gates_u32 idx, gates_i32 *lo, gates_i32 *hi) {
    const gates_i_range *r = range_of(tree, idx);
    gates_rect_t b = gates_i_slot(tree, idx)->layout_rect;
    gates_i32 start = r->vertical ? b.y : b.x, len = r->vertical ? b.h : b.w;
    *lo = start + THUMB_LONG / 2 + 1;
    *hi = start + len - THUMB_LONG / 2 - 1;
    if (*hi < *lo) *hi = *lo;
}

/* Position of a value along the axis (vertical sliders grow upward). */
static gates_i32 pos_of(const gates_tree_t *tree, gates_u32 idx, gates_i64 v) {
    const gates_i_range *r = range_of(tree, idx);
    gates_i32 lo, hi;
    travel(tree, idx, &lo, &hi);
    gates_i64 span = r->max - r->min;
    gates_i32 off = span > 0 ? (gates_i32)((double)(v - r->min) * (double)(hi - lo) / (double)span + 0.5) : 0;
    return r->vertical ? hi - off : lo + off;
}

/* The value at a position, rounded to a whole step from min. */
static gates_i64 value_at(const gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    const gates_i_range *r = range_of(tree, idx);
    gates_i32 lo, hi;
    travel(tree, idx, &lo, &hi);
    gates_i32 at = r->vertical ? lo + (hi - p.y) : p.x;
    if (at < lo) at = lo;
    if (at > hi) at = hi;
    gates_i64 span = r->max - r->min;
    if (hi == lo || span == 0) return r->min;
    double raw = (double)(at - lo) * (double)span / (double)(hi - lo); /* plenty for a screen */
    double steps = raw / (double)r->step + 0.5;
    gates_i64 k = steps >= (double)INT64_MAX ? INT64_MAX : (gates_i64)steps;
    return k > (r->max - r->min) / r->step ? r->max : clamp(r, r->min + k * r->step);
}

static gates_rect_t thumb(const gates_tree_t *tree, gates_u32 idx) {
    const gates_i_range *r = range_of(tree, idx);
    gates_rect_t b = gates_i_slot(tree, idx)->layout_rect;
    gates_i32 c = pos_of(tree, idx, r->value);
    return r->vertical ? (gates_rect_t){ b.x + 2, c - THUMB_LONG / 2, b.w - 4, THUMB_LONG }
                       : (gates_rect_t){ c - THUMB_LONG / 2, b.y + 2, THUMB_LONG, b.h - 4 };
}

gates_err_t gates_i_slider_paint(const gates_tree_t *tree, gates_u32 idx, gates_draw_list_t *dl,
                                 const gates_theme_t *theme) {
    const gates_i_range *r = range_of(tree, idx);
    const gates_widget_state_t *st = state_at(tree, idx);
    gates_rect_t b = gates_i_slot(tree, idx)->layout_rect;
    gates_i32 lo, hi;
    travel(tree, idx, &lo, &hi);
    bool off = st->disabled;
    gates_color_t border = gates_theme_color(theme, GATES_COLOR_CONTROL_BORDER);
    gates_rect_t track = r->vertical ? (gates_rect_t){ b.x + (b.w - TRACK_THICK) / 2, lo, TRACK_THICK, hi - lo + 1 }
                                     : (gates_rect_t){ lo, b.y + (b.h - TRACK_THICK) / 2, hi - lo + 1, TRACK_THICK };
    gates_err_t err = gates_draw_rect(dl, track, border);
    /* Ticks every `ticks` steps, beside the track. */
    if (gates_is_ok(err) && r->ticks > 0 && r->step > 0) {
        gates_i64 every = r->step * (gates_i64)r->ticks;
        for (gates_i64 v = r->min; v <= r->max && gates_is_ok(err); v += every) {
            gates_i32 p = pos_of(tree, idx, v);
            err = r->vertical ? gates_draw_rect(dl, (gates_rect_t){ b.x + 1, p, 3, 1 }, border)
                              : gates_draw_rect(dl, (gates_rect_t){ p, b.y + b.h - 4, 1, 3 }, border);
            if (v > INT64_MAX - every) break;
        }
    }
    gates_rect_t t = thumb(tree, idx);
    if (gates_is_ok(err)) {
        gates_color_token_t fill = off ? GATES_COLOR_CONTROL_BG
                                 : (tree->pressed == idx ? GATES_COLOR_CONTROL_PRESSED_BG : GATES_COLOR_SELECTION_BG);
        err = gates_draw_rect(dl, t, gates_theme_color(theme, fill));
    }
    if (gates_is_ok(err)) err = gates_draw_border(dl, t, 1, border);
    if (gates_is_ok(err) && tree->focus == idx) {
        err = gates_draw_border(dl, b, gates_theme_focus_width(theme), gates_theme_color(theme, GATES_COLOR_FOCUS_RING));
    }
    return err;
}

bool gates_i_slider_key(gates_tree_t *tree, gates_u32 idx, const gates_key_event_t *ev) {
    if (ev->ctrl || ev->alt) return false;
    const gates_i_range *r = range_of(tree, idx);
    gates_i64 v = r->value, to;
    switch (ev->key) {
    case GATES_KEY_RIGHT: case GATES_KEY_UP: to = v > r->max - r->step ? r->max : v + r->step; break;
    case GATES_KEY_LEFT: case GATES_KEY_DOWN: to = v < r->min + r->step ? r->min : v - r->step; break;
    case GATES_KEY_PAGE_UP: to = v > r->max - r->page ? r->max : v + r->page; break;
    case GATES_KEY_PAGE_DOWN: to = v < r->min + r->page ? r->min : v - r->page; break;
    case GATES_KEY_HOME: to = r->min; break;
    case GATES_KEY_END: to = r->max; break;
    default: return false;
    }
    user_set_or_record(tree, idx, to);
    return true;
}

/* A press: on the thumb it starts a drag; elsewhere on the track it pages. */
bool gates_i_slider_press(gates_tree_t *tree, gates_u32 idx, gates_point_t p) {
    const gates_i_range *r = range_of(tree, idx);
    const gates_widget_state_t *st = state_at(tree, idx);
    if (st->disabled) return false;
    gates_tree_set_focus(tree, gates_i_handle(tree, idx));
    gates_rect_t t = thumb(tree, idx);
    if (gates_rect_contains(t, p)) {
        tree->drag_kind = GATES_DRAG_SLIDER;
        tree->drag_node = idx;
        tree->drag_start = p;
        tree->pressed = idx;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
        return true;
    }
    gates_i32 c = pos_of(tree, idx, r->value);
    bool more = r->vertical ? p.y < c : p.x > c;
    gates_i64 v = r->value;
    user_set_or_record(tree, idx, more ? (v > r->max - r->page ? r->max : v + r->page)
                                       : (v < r->min + r->page ? r->min : v - r->page));
    return true;
}

void gates_i_slider_drag(gates_tree_t *tree, gates_point_t p, bool end) {
    gates_u32 idx = tree->drag_node;
    user_set_or_record(tree, idx, value_at(tree, idx, p));
    if (end) {
        tree->pressed = GATES_NONE;
        gates_i_mark_dirty(tree, idx, GATES_DIRTY_PAINT);
    }
}
