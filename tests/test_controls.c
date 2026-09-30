/* fields - hidden nodes, separator, progress, radio group, choice with
 * its option list, textbox error state. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/overlay.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 400
#define VH 300

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
}

static bool key_mod(gates_tree_t *t, gates_key_t k, bool alt) {
    gates_key_event_t e = { .key = k, .down = true, .alt = alt };
    return gates_input_key(t, &e);
}

static bool key(gates_tree_t *t, gates_key_t k) {
    return key_mod(t, k, false);
}

static void key_up(gates_tree_t *t, gates_key_t k) {
    gates_key_event_t e = { .key = k, .down = false };
    (void)gates_input_key(t, &e);
}

static void pointer(gates_tree_t *t, gates_pointer_action_t a, gates_point_t p) {
    gates_pointer_event_t e = { .action = a, .button = GATES_BUTTON_LEFT, .pos = p };
    (void)gates_input_pointer(t, &e);
}

static void click_at(gates_tree_t *t, gates_point_t p) {
    pointer(t, GATES_POINTER_DOWN, p);
    pointer(t, GATES_POINTER_UP, p);
}

static gates_point_t center(const gates_tree_t *t, gates_node_t n) {
    gates_rect_t r = gates_node_layout_rect(t, n);
    return (gates_point_t){ r.x + r.w / 2, r.y + r.h / 2 };
}

static bool focused(const gates_tree_t *t, gates_node_t n) {
    return gates_node_eq(gates_tree_focus(t), n);
}

static bool empty(gates_rect_t r) {
    return r.w == 0 && r.h == 0;
}

static bool rect_eq(gates_rect_t a, gates_rect_t b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static bool same_color(gates_color_t a, gates_color_t b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

typedef struct rec_t {
    gates_event_kind_t kind[16];
    gates_event_origin_t origin[16];
    gates_u32 result[16];
    int n;
} rec_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (r->n < 16) {
        r->kind[r->n] = ev->kind;
        r->origin[r->n] = ev->origin;
        r->result[r->n] = ev->result;
        r->n++;
    }
}

static gates_tree_t *new_tree(gates_allocator_t alloc) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &t));
    GT_ASSERT_OK(gates_layout_set(t, gates_tree_root(t), GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_layout_set_gap(t, gates_tree_root(t), 4));
    return t;
}

/* Does the draw list contain a command of this kind with this exact rect? */
static bool drawn_at(const gates_draw_list_t *dl, gates_draw_kind_t kind, gates_rect_t r) {
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == kind && c->rect.x == r.x && c->rect.y == r.y && c->rect.w == r.w &&
            c->rect.h == r.h) {
            return true;
        }
    }
    return false;
}

static bool drawn_color(const gates_draw_list_t *dl, gates_draw_kind_t kind, gates_rect_t r,
                        gates_color_token_t tok) {
    gates_color_t want = gates_theme_color(theme, tok);
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == kind && c->rect.x == r.x && c->rect.y == r.y && c->rect.w == r.w &&
            c->rect.h == r.h && same_color(c->color, want)) {
            return true;
        }
    }
    return false;
}

static void paint(gates_tree_t *t, gates_draw_list_t *dl) {
    GT_ASSERT_OK(gates_draw_list_init(dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, dl, theme, be));
}

/* -- hidden nodes --------------------------------------------------------------- */

static void test_hidden(void) {
    gates_tree_t *t = new_tree((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(t);
    gates_node_t a, box, inner, c;
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("A"), nullptr, nullptr, &a));
    GT_ASSERT_OK(gates_panel_create(t, root, &box));
    GT_ASSERT_OK(gates_layout_set(t, box, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_layout_set_padding(t, box, 6)); /* inner's rect is nobody else's */
    GT_ASSERT_OK(gates_button_create(t, box, GATES_STR("inner"), nullptr, nullptr, &inner));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("C"), nullptr, nullptr, &c));
    layout(t);
    gates_rect_t inner_was = gates_node_layout_rect(t, inner);
    gates_rect_t c_was = gates_node_layout_rect(t, c);
    GT_ASSERT(!gates_node_hidden(t, box));

    /* Focus and a press inside the subtree are let go when it hides. */
    gates_tree_set_focus(t, inner);
    pointer(t, GATES_POINTER_DOWN, center(t, inner));
    GT_ASSERT(gates_widget_pressed(t, inner));
    GT_ASSERT_OK(gates_node_set_hidden(t, box, true));
    GT_ASSERT(gates_node_hidden(t, box));
    GT_ASSERT(!gates_node_hidden(t, inner));          /* own flag only */
    GT_ASSERT(!gates_widget_pressed(t, inner));
    GT_ASSERT(focused(t, c));                          /* moved to the next control */
    GT_ASSERT(!gates_widget_focusable(t, inner));
    pointer(t, GATES_POINTER_UP, center(t, inner));
    GT_ASSERT((gates_tree_dirty(t) & GATES_TREE_DIRTY_LAYOUT) != 0);

    /* Even before the next layout (rects still stale) it is not drawn or hit. */
    gates_draw_list_t dl;
    paint(t, &dl);
    GT_ASSERT(!drawn_at(&dl, GATES_DRAW_BORDER, inner_was));
    gates_draw_list_deinit(&dl);
    GT_ASSERT(!gates_node_eq(gates_hit_test(t, center(t, inner)), inner));

    /* Layout: no space, no gap; the subtree's rects are empty. */
    layout(t);
    gates_rect_t ar = gates_node_layout_rect(t, a);
    gates_rect_t cr = gates_node_layout_rect(t, c);
    GT_ASSERT(cr.y == ar.y + ar.h + 4);
    GT_ASSERT(cr.y < c_was.y);
    GT_ASSERT(empty(gates_node_layout_rect(t, box)));
    GT_ASSERT(empty(gates_node_layout_rect(t, inner)));
    GT_ASSERT(gates_node_preferred_size(t, box).h == 0);
    GT_ASSERT(gates_node_preferred_size(t, root).h ==
              gates_node_preferred_size(t, a).h + 4 + gates_node_preferred_size(t, c).h);
    GT_ASSERT(gates_layout_validate(t, root));

    /* Not painted, not hit, not in the Tab order. */
    paint(t, &dl);
    GT_ASSERT(!drawn_at(&dl, GATES_DRAW_BORDER, inner_was));
    gates_draw_list_deinit(&dl);
    GT_ASSERT(!gates_node_eq(gates_hit_test(t, center(t, inner)), inner));
    for (int i = 0; i < 4; i++) {
        GT_ASSERT(gates_tree_focus_next(t, false));
        GT_ASSERT(!focused(t, inner));
    }

    /* Shown again: everything comes back. */
    GT_ASSERT_OK(gates_node_set_hidden(t, box, false));
    layout(t);
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, inner), inner_was));
    GT_ASSERT(rect_eq(gates_node_layout_rect(t, c), c_was));
    GT_ASSERT(gates_widget_focusable(t, inner));

    /* The root cannot be hidden; a hidden scroll child adds no content. */
    GT_ASSERT(gates_node_set_hidden(t, root, true) == PROVEN_ERR_INVALID_ARG);
    gates_node_t sc, s1, s2;
    GT_ASSERT_OK(gates_panel_create(t, root, &sc));
    GT_ASSERT_OK(gates_layout_set(t, sc, GATES_LAYOUT_KIND_SCROLL));
    GT_ASSERT_OK(gates_button_create(t, sc, GATES_STR("s1"), nullptr, nullptr, &s1));
    GT_ASSERT_OK(gates_button_create(t, sc, GATES_STR("s2"), nullptr, nullptr, &s2));
    layout(t);
    gates_i32 both = gates_layout_scroll_content(t, sc).h;
    GT_ASSERT_OK(gates_node_set_hidden(t, s2, true));
    layout(t);
    GT_ASSERT(gates_layout_scroll_content(t, sc).h == gates_node_preferred_size(t, s1).h);
    GT_ASSERT(gates_layout_scroll_content(t, sc).h < both);
    gates_tree_destroy(t);
}

/* -- separator and progress -------------------------------------------------------- */

static void test_separator_progress(void) {
    gates_tree_t *t = new_tree((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(t);
    gates_node_t sep, row, vsep, bar;
    GT_ASSERT_OK(gates_separator_create(t, root, &sep));
    GT_ASSERT_OK(gates_panel_create(t, root, &row));
    GT_ASSERT_OK(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    GT_ASSERT_OK(gates_separator_create(t, row, &vsep));
    GT_ASSERT_OK(gates_progress_create(t, root, 250, &bar));
    layout(t);
    GT_ASSERT(gates_node_kind(t, sep) == GATES_NODE_SEPARATOR);
    gates_rect_t sr = gates_node_layout_rect(t, sep);
    GT_ASSERT(sr.w == VW && sr.h > 1);                 /* spans the column */
    gates_size_t vp = gates_node_preferred_size(t, vsep);
    GT_ASSERT(vp.w > 1 && vp.h == 0);                  /* vertical in a row */
    GT_ASSERT(!gates_widget_focusable(t, sep) && !gates_widget_focusable(t, bar));

    GT_ASSERT(gates_progress_value(t, bar) == 250);
    GT_ASSERT_OK(gates_progress_set_value(t, bar, 2000));
    GT_ASSERT(gates_progress_value(t, bar) == 1000);
    GT_ASSERT_OK(gates_progress_set_value(t, bar, -5));
    GT_ASSERT(gates_progress_value(t, bar) == 0);
    GT_ASSERT_OK(gates_progress_set_value(t, bar, 500));
    GT_ASSERT(gates_progress_set_value(t, sep, 1) == PROVEN_ERR_INVALID_ARG);

    gates_draw_list_t dl;
    paint(t, &dl);
    /* A one-pixel line across the column, and a fill of half the track. */
    GT_ASSERT(drawn_color(&dl, GATES_DRAW_RECT, (gates_rect_t){ sr.x, sr.y + sr.h / 2, sr.w, 1 },
                          GATES_COLOR_CONTROL_BORDER));
    gates_rect_t br = gates_node_layout_rect(t, bar);
    GT_ASSERT(br.w > 0 && br.h > 0);
    GT_ASSERT(drawn_color(&dl, GATES_DRAW_RECT,
                          (gates_rect_t){ br.x + 1, br.y + 1, (br.w - 2) / 2, br.h - 2 },
                          GATES_COLOR_SELECTION_BG));
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

/* -- radio group ------------------------------------------------------------------ */

static const gates_option_t sizes[] = {
    { .id = 1, .label = GATES_STR_INIT("Small") },
    { .id = 2, .label = GATES_STR_INIT("Medium"), .disabled = true },
    { .id = 3, .label = GATES_STR_INIT("Large") },
};

/* Centre of option row `i` of a radio group (rows share its height evenly). */
static gates_point_t radio_row(const gates_tree_t *t, gates_node_t r, gates_u32 i) {
    gates_rect_t rr = gates_node_layout_rect(t, r);
    gates_i32 h = rr.h / (gates_i32)gates_options_count(t, r);
    return (gates_point_t){ rr.x + 4, rr.y + (gates_i32)i * h + h / 2 };
}

static void test_radio(void) {
    gates_tree_t *t = new_tree((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(t);
    gates_node_t b1, radio, b2;
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("before"), nullptr, nullptr, &b1));
    GT_ASSERT_OK(gates_radio_create(t, root, sizes, 3, 1, &radio));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("after"), nullptr, nullptr, &b2));
    rec_t rec = {0};
    GT_ASSERT_OK(gates_widget_set_handler(t, radio, record, &rec));
    layout(t);
    GT_ASSERT(gates_node_kind(t, radio) == GATES_NODE_RADIO);
    GT_ASSERT(gates_options_count(t, radio) == 3);
    GT_ASSERT(gates_options_selected(t, radio) == 1);
    GT_ASSERT(gates_node_layout_rect(t, radio).h >= 3 * 12);

    /* One Tab stop. */
    gates_tree_set_focus(t, b1);
    GT_ASSERT(gates_tree_focus_next(t, false) && focused(t, radio));
    GT_ASSERT(gates_tree_focus_next(t, false) && focused(t, b2));
    gates_tree_set_focus(t, radio);

    /* Arrows move and select, skipping the disabled option and wrapping. */
    gates_u32 rev = gates_widget_revision(t, radio);
    GT_ASSERT(key(t, GATES_KEY_DOWN) && gates_options_selected(t, radio) == 3);
    GT_ASSERT(gates_widget_revision(t, radio) == rev + 1);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 1 && rec.kind[0] == GATES_EVENT_VALUE_CHANGED && rec.result[0] == 3 &&
              rec.origin[0] == GATES_ORIGIN_USER);
    GT_ASSERT(key(t, GATES_KEY_RIGHT) && gates_options_selected(t, radio) == 1);
    GT_ASSERT(key(t, GATES_KEY_UP) && gates_options_selected(t, radio) == 3);
    GT_ASSERT(key(t, GATES_KEY_LEFT) && gates_options_selected(t, radio) == 1);
    GT_ASSERT(key(t, GATES_KEY_END) && gates_options_selected(t, radio) == 3);
    GT_ASSERT(key(t, GATES_KEY_HOME) && gates_options_selected(t, radio) == 1);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 2 && rec.result[1] == 1);      /* coalesced to the latest */

    /* Setters are silent; unknown ids are refused, 0 clears. */
    rec.n = 0;
    rev = gates_widget_revision(t, radio);
    GT_ASSERT_OK(gates_options_set_selected(t, radio, 2)); /* disabled: still settable */
    GT_ASSERT(gates_options_selected(t, radio) == 2);
    GT_ASSERT(gates_widget_revision(t, radio) == rev + 1);
    GT_ASSERT(gates_options_set_selected(t, radio, 9) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_options_selected(t, radio) == 2);
    GT_ASSERT_OK(gates_options_set_selected(t, radio, 0));
    GT_ASSERT(gates_options_selected(t, radio) == 0);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 0);

    /* Space with nothing selected picks the first enabled option. */
    GT_ASSERT(key(t, GATES_KEY_SPACE));
    key_up(t, GATES_KEY_SPACE);
    GT_ASSERT(gates_options_selected(t, radio) == 1);

    /* Pointer: a click selects the row and focuses the group. */
    gates_tree_set_focus(t, b1);
    click_at(t, radio_row(t, radio, 2));
    GT_ASSERT(gates_options_selected(t, radio) == 3 && focused(t, radio));
    click_at(t, radio_row(t, radio, 1));               /* disabled row */
    GT_ASSERT(gates_options_selected(t, radio) == 3);
    pointer(t, GATES_POINTER_DOWN, radio_row(t, radio, 2));
    pointer(t, GATES_POINTER_UP, radio_row(t, radio, 0)); /* released on another row */
    GT_ASSERT(gates_options_selected(t, radio) == 3);
    pointer(t, GATES_POINTER_DOWN, radio_row(t, radio, 0));
    pointer(t, GATES_POINTER_UP, radio_row(t, radio, 0));
    GT_ASSERT(gates_options_selected(t, radio) == 1);
    GT_ASSERT_OK(gates_options_set_enabled(t, radio, 2, true));
    click_at(t, radio_row(t, radio, 1));
    GT_ASSERT(gates_options_selected(t, radio) == 2);
    GT_ASSERT(gates_options_set_enabled(t, radio, 7, true) == PROVEN_ERR_INVALID_ARG);

    /* Replacing the options keeps a selection that is still there, else clears it. */
    static const gates_option_t two[] = {
        { .id = 2, .label = GATES_STR_INIT("Two") },
        { .id = 5, .label = GATES_STR_INIT("Five") },
    };
    (void)gates_tree_dispatch_events(t, 0);           /* the clicks above */
    rec.n = 0;
    GT_ASSERT_OK(gates_options_set(t, radio, two, 2));
    GT_ASSERT(gates_options_selected(t, radio) == 2 && gates_options_count(t, radio) == 2);
    GT_ASSERT_OK(gates_options_set(t, radio, sizes, 1));
    GT_ASSERT(gates_options_selected(t, radio) == 0);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 0);                            /* silent */

    /* No enabled option: not focusable, focus moves on. Empty lists are allowed. */
    GT_ASSERT_OK(gates_options_set_enabled(t, radio, 1, false));
    GT_ASSERT(!gates_widget_focusable(t, radio));
    GT_ASSERT(!focused(t, radio));
    GT_ASSERT_OK(gates_options_set(t, radio, nullptr, 0));
    GT_ASSERT(gates_options_count(t, radio) == 0);

    /* Explicit notification carries the selection. */
    GT_ASSERT_OK(gates_options_set(t, radio, sizes, 3));
    GT_ASSERT_OK(gates_options_set_selected(t, radio, 3));
    rec.n = 0;
    GT_ASSERT_OK(gates_widget_notify(t, radio));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(rec.n == 1 && rec.result[0] == 3 && rec.origin[0] == GATES_ORIGIN_PROGRAM);

    /* Bad option lists are refused and leave nothing behind. */
    gates_u32 live = gates_tree_live_count(t);
    static const gates_option_t dup[] = {
        { .id = 4, .label = GATES_STR_INIT("a") },
        { .id = 4, .label = GATES_STR_INIT("b") },
    };
    static const gates_option_t zero[] = { { .id = 0, .label = GATES_STR_INIT("z") } };
    gates_node_t bad = GATES_NODE_NULL;
    GT_ASSERT(gates_radio_create(t, root, dup, 2, 0, &bad) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_radio_create(t, root, zero, 1, 0, &bad) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_radio_create(t, root, sizes, 3, 9, &bad) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_options_set(t, radio, dup, 2) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_options_count(t, radio) == 3 && gates_options_selected(t, radio) == 3);
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT(gates_tree_live_count(t) == live);
    GT_ASSERT(gates_options_set_selected(t, b1, 1) == PROVEN_ERR_INVALID_ARG);
    gates_tree_destroy(t);
}

/* -- choice ------------------------------------------------------------------------ */

static const gates_option_t colors[] = {
    { .id = 10, .label = GATES_STR_INIT("Red") },
    { .id = 20, .label = GATES_STR_INIT("Green") },
    { .id = 30, .label = GATES_STR_INIT("Blue") },
};

typedef struct choice_app_t {
    gates_tree_t *t;
    gates_node_t choice, button;
    rec_t rec, brec;
} choice_app_t;

static void make_choice(choice_app_t *a, gates_allocator_t alloc) {
    memset(a, 0, sizeof *a);
    a->t = new_tree(alloc);
    gates_node_t root = gates_tree_root(a->t);
    GT_ASSERT_OK(gates_choice_create(a->t, root, colors, 3, 10, &a->choice));
    GT_ASSERT_OK(gates_button_create(a->t, root, GATES_STR("elsewhere"), nullptr, nullptr,
                                     &a->button));
    /* The button sits below where the open list reaches, so a click on it is outside. */
    GT_ASSERT_OK(gates_layout_set_gap(a->t, root, 120));
    GT_ASSERT_OK(gates_widget_set_handler(a->t, a->choice, record, &a->rec));
    GT_ASSERT_OK(gates_widget_set_handler(a->t, a->button, record, &a->brec));
    layout(a->t);
}

/* The open list is the topmost overlay; its row i centre. */
static gates_point_t list_row(const gates_tree_t *t, gates_node_t choice, gates_u32 i) {
    gates_node_t list = gates_choice_list(t, choice);
    gates_rect_t lr = gates_node_layout_rect(t, list);
    gates_i32 h = (lr.h - 8) / (gates_i32)gates_options_count(t, choice); /* 4 px padding */
    return (gates_point_t){ lr.x + lr.w / 2, lr.y + 4 + (gates_i32)i * h + h / 2 };
}

static void test_choice(void) {
    choice_app_t a;
    make_choice(&a, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    GT_ASSERT(gates_node_kind(t, a.choice) == GATES_NODE_CHOICE);
    GT_ASSERT(gates_options_selected(t, a.choice) == 10);
    gates_rect_t cr = gates_node_layout_rect(t, a.choice);
    GT_ASSERT(cr.w > 0 && cr.h > 0);
    GT_ASSERT(!gates_choice_list_open(t, a.choice));

    /* Enter opens the list under the choice, at least as wide, current row chosen. */
    gates_tree_set_focus(t, a.choice);
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT(gates_choice_list_open(t, a.choice) && gates_tree_overlay_count(t) == 1);
    layout(t);
    gates_rect_t lr = gates_node_layout_rect(t, gates_choice_list(t, a.choice));
    GT_ASSERT(lr.y == cr.y + cr.h && lr.x == cr.x && lr.w >= cr.w);
    GT_ASSERT(key(t, GATES_KEY_DOWN));                 /* Red -> Green */
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT(!gates_choice_list_open(t, a.choice));
    GT_ASSERT(gates_options_selected(t, a.choice) == 20);
    GT_ASSERT(focused(t, a.choice));                   /* focus stayed */
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_VALUE_CHANGED &&
              a.rec.result[0] == 20);
    GT_ASSERT_OK(gates_tree_flush_destroys(t));

    /* Space opens it; Escape closes it with no change. */
    a.rec.n = 0;
    GT_ASSERT(key(t, GATES_KEY_SPACE));
    key_up(t, GATES_KEY_SPACE);
    GT_ASSERT(gates_choice_list_open(t, a.choice));
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    GT_ASSERT(!gates_choice_list_open(t, a.choice));
    GT_ASSERT(gates_options_selected(t, a.choice) == 20);

    /* Choosing the current option again changes nothing. */
    GT_ASSERT(key_mod(t, GATES_KEY_DOWN, true));       /* Alt+Down opens */
    GT_ASSERT(gates_choice_list_open(t, a.choice));
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 0);

    /* A click opens it; a click on a row chooses. */
    gates_tree_set_focus(t, a.button);
    click_at(t, center(t, a.choice));
    GT_ASSERT(gates_choice_list_open(t, a.choice) && focused(t, a.choice));
    layout(t);
    click_at(t, list_row(t, a.choice, 2));
    GT_ASSERT(!gates_choice_list_open(t, a.choice));
    GT_ASSERT(gates_options_selected(t, a.choice) == 30);

    /* An outside click only closes the list. */
    click_at(t, center(t, a.choice));
    layout(t);
    GT_ASSERT(!gates_rect_contains(gates_node_layout_rect(t, gates_choice_list(t, a.choice)),
                                   center(t, a.button)));
    click_at(t, center(t, a.button));
    GT_ASSERT(!gates_choice_list_open(t, a.choice));
    GT_ASSERT(gates_options_selected(t, a.choice) == 30);
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.brec.n == 0);

    /* A disabled option cannot be chosen. */
    GT_ASSERT_OK(gates_options_set_enabled(t, a.choice, 10, false));
    click_at(t, center(t, a.choice));
    layout(t);
    click_at(t, list_row(t, a.choice, 0));
    GT_ASSERT(gates_choice_list_open(t, a.choice));
    GT_ASSERT(gates_options_selected(t, a.choice) == 30);

    /* The list goes when the window loses focus, or the choice is disabled,
     * hidden, given new options, or destroyed. */
    gates_tree_dismiss_menus(t);
    GT_ASSERT(!gates_choice_list_open(t, a.choice));
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT_OK(gates_widget_set_disabled(t, a.choice, true));
    GT_ASSERT(!gates_choice_list_open(t, a.choice) && gates_tree_overlay_count(t) == 0);
    GT_ASSERT(!key(t, GATES_KEY_ENTER) || !gates_choice_list_open(t, a.choice));
    GT_ASSERT_OK(gates_widget_set_disabled(t, a.choice, false));
    gates_tree_set_focus(t, a.choice);
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT_OK(gates_node_set_hidden(t, a.choice, true));
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    GT_ASSERT_OK(gates_node_set_hidden(t, a.choice, false));
    gates_tree_set_focus(t, a.choice);
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT_OK(gates_options_set(t, a.choice, colors, 3));
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT_OK(gates_node_destroy(t, a.choice));
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    gates_tree_destroy(t);

    /* Near the bottom of the window the list opens upwards, clear of the choice. */
    t = new_tree((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(t), low;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_ABSOLUTE));
    GT_ASSERT_OK(gates_choice_create(t, root, colors, 3, 10, &low));
    GT_ASSERT_OK(gates_layout_set_abs_rect(t, low, (gates_rect_t){ 20, VH - 40, 0, 0 }));
    layout(t);
    gates_tree_set_focus(t, low);
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    layout(t);
    gates_rect_t lowr = gates_node_layout_rect(t, low);
    gates_rect_t up = gates_node_layout_rect(t, gates_choice_list(t, low));
    GT_ASSERT(up.y + up.h == lowr.y);
    gates_tree_destroy(t);
}

/* -- textbox error state --------------------------------------------------------------- */

static void test_invalid_textbox(void) {
    gates_tree_t *t = new_tree((gates_allocator_t){0});
    gates_node_t root = gates_tree_root(t);
    gates_node_t box, btn;
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("x"), 8, &box));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("b"), nullptr, nullptr, &btn));
    layout(t);
    gates_rect_t r = gates_node_layout_rect(t, box);
    GT_ASSERT(!gates_textbox_invalid(t, box));
    GT_ASSERT_OK(gates_textbox_set_invalid(t, box, true));
    GT_ASSERT(gates_textbox_invalid(t, box));
    GT_ASSERT(gates_textbox_set_invalid(t, btn, true) == PROVEN_ERR_INVALID_ARG);

    gates_draw_list_t dl;
    paint(t, &dl);
    GT_ASSERT(drawn_color(&dl, GATES_DRAW_BORDER, r, GATES_COLOR_ERROR));
    gates_draw_list_deinit(&dl);

    /* Focused and invalid: the error border stays and the focus ring shows inside it. */
    gates_tree_set_focus(t, box);
    paint(t, &dl);
    GT_ASSERT(drawn_color(&dl, GATES_DRAW_BORDER, r, GATES_COLOR_ERROR));
    gates_i32 ew = gates_theme_error_width(theme);
    GT_ASSERT(drawn_color(&dl, GATES_DRAW_BORDER,
                          (gates_rect_t){ r.x + ew, r.y + ew, r.w - 2 * ew, r.h - 2 * ew },
                          GATES_COLOR_FOCUS_RING));
    gates_draw_list_deinit(&dl);
    gates_str_t txt = gates_textbox_text(t, box);
    GT_ASSERT(txt.size == 1 && txt.ptr[0] == 'x'); /* text untouched */

    GT_ASSERT_OK(gates_textbox_set_invalid(t, box, false));
    gates_tree_set_focus(t, GATES_NODE_NULL);
    paint(t, &dl);
    GT_ASSERT(drawn_color(&dl, GATES_DRAW_BORDER, r, GATES_COLOR_CONTROL_BORDER));
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

/* -- allocation failure ------------------------------------------------------------------- */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    bool fail;
} fail_alloc_t;

static proven_result_mem_mut_t fa_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}

static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, proven_size_t os, proven_size_t ns,
                                          proven_size_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.realloc_fn(f->inner.ctx, p, os, ns, align);
}

static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_allocation_failure(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc,
                                .free_fn = fa_free };
    choice_app_t a;
    make_choice(&a, alloc);
    gates_tree_t *t = a.t;
    gates_u32 live = gates_tree_live_count(t);

    fa.fail = true;
    gates_node_t r = GATES_NODE_NULL;
    GT_ASSERT(gates_radio_create(t, gates_tree_root(t), sizes, 3, 1, &r) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_options_set(t, a.choice, sizes, 3) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_options_count(t, a.choice) == 3 && gates_options_selected(t, a.choice) == 10);
    /* The list cannot open: nothing opens, the error is reported. */
    gates_tree_set_focus(t, a.choice);
    (void)key(t, GATES_KEY_ENTER);
    GT_ASSERT(!gates_choice_list_open(t, a.choice) && gates_tree_overlay_count(t) == 0);
    GT_ASSERT(gates_input_take_error(t) == PROVEN_ERR_NOMEM);
    fa.fail = false;
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT(gates_tree_live_count(t) == live);

    /* The change cannot be announced: it does not happen. */
    GT_ASSERT(key(t, GATES_KEY_ENTER) && gates_choice_list_open(t, a.choice));
    GT_ASSERT(key(t, GATES_KEY_DOWN));
    fa.fail = true;
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    fa.fail = false;
    GT_ASSERT(gates_options_selected(t, a.choice) == 10);
    GT_ASSERT(gates_input_take_error(t) == PROVEN_ERR_NOMEM);
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_hidden();
    test_separator_progress();
    test_radio();
    test_choice();
    test_invalid_textbox();
    test_allocation_failure();
    return gt_report("test_controls");
}
