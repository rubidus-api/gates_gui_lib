/* T047: the application frame (docs/tests/cases/T047-frame.md, plan-0018) -
 * mnemonics, keymap (A4), menu bar. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include <gates/frame.h>
#include <gates/access.h>
#include "gates_test.h"
#include <proven/heap.h>

#include <string.h>

static const gates_text_backend_t *be;
static const gates_theme_t *theme;
#define VW 480
#define VH 320

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, be));
}

static bool keyx(gates_tree_t *t, gates_key_t k, bool ctrl, bool shift, gates_u8 letter) {
    gates_key_event_t e = { .key = k, .down = true, .ctrl = ctrl, .shift = shift,
                            .letter = letter };
    return gates_input_key(t, &e);
}
static bool key(gates_tree_t *t, gates_key_t k) { return keyx(t, k, false, false, 0); }
static bool letter(gates_tree_t *t, gates_u8 l) { return keyx(t, GATES_KEY_NONE, false, false, l); }

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

typedef struct rec_t {
    gates_event_kind_t kind[32];
    gates_node_t node[32];
    int n;
    int cmd[64];
} rec_t;

static void record(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    rec_t *r = user;
    if (r->n < 32) {
        r->kind[r->n] = ev->kind;
        r->node[r->n] = ev->source;
        r->n++;
    }
}
static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    rec_t *r = user;
    if (id < 64) r->cmd[id]++;
}
static gates_command_desc_t cmd(gates_command_id_t id, const char *label, gates_shortcut_t k,
                                rec_t *r) {
    return (gates_command_desc_t){ .id = id,
                                   .label = { .ptr = (const gates_u8 *)label, .size = strlen(label) },
                                   .shortcut = k, .enabled = true, .invoke = on_command, .user = r };
}
static void dispatch(gates_tree_t *t) { (void)gates_tree_dispatch_events(t, 0); }
static bool seq(gates_str_t a, gates_str_t b) {
    return a.size == b.size && (a.size == 0 || memcmp(a.ptr, b.ptr, a.size) == 0);
}

/* The concatenated TEXT commands of a draw list, and whether an underline
 * (a 1-high RECT) was emitted inside rect `in`. */
static gates_usize_t draw_texts(const gates_draw_list_t *dl, char *out, gates_usize_t cap) {
    gates_usize_t n = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind != GATES_DRAW_TEXT) continue;
        gates_str_t s = gates_draw_cmd_text(dl, c);
        for (gates_usize_t k = 0; k < s.size && n + 1 < cap; k++) out[n++] = (char)s.ptr[k];
    }
    out[n] = 0;
    return n;
}
static int underlines(const gates_draw_list_t *dl, gates_rect_t in, gates_rect_t *first) {
    int n = 0;
    for (gates_u32 i = 0; i < gates_draw_list_len(dl); i++) {
        const gates_draw_cmd_t *c = gates_draw_list_at(dl, i);
        if (c->kind == GATES_DRAW_RECT && c->rect.h == 1 && c->rect.w > 0 &&
            c->rect.x >= in.x && c->rect.x + c->rect.w <= in.x + in.w && c->rect.y >= in.y &&
            c->rect.y < in.y + in.h) {
            if (n == 0 && first != nullptr) *first = c->rect;
            n++;
        }
    }
    return n;
}

/* -- mnemonics --------------------------------------------------------------------- */

static void test_mnemonic_parse(void) {
    GT_ASSERT(gates_mnemonic_of(GATES_STR("&File")) == 'F');
    GT_ASSERT(gates_mnemonic_of(GATES_STR("e&xit")) == 'X');
    GT_ASSERT(gates_mnemonic_of(GATES_STR("Page &2")) == '2');
    GT_ASSERT(gates_mnemonic_of(GATES_STR("Save & close")) == 0);
    GT_ASSERT(gates_mnemonic_of(GATES_STR("a&&b")) == 0);
    GT_ASSERT(gates_mnemonic_of(GATES_STR("&&&x")) == 'X');
    GT_ASSERT(gates_mnemonic_of(GATES_STR("&")) == 0);
    GT_ASSERT(gates_mnemonic_of(GATES_STR("x&")) == 0);
    GT_ASSERT(gates_mnemonic_of(GATES_STR("&\xed\x95\x9c")) == 0); /* not ASCII */
    GT_ASSERT(gates_mnemonic_of(GATES_STR("&a&b")) == 'A');       /* the first counts */
    GT_ASSERT(gates_mnemonic_of((gates_str_t){0}) == 0);
}

typedef struct mapp_t {
    gates_tree_t *t;
    gates_node_t ok, check, name_label, name_box, plain_label, dup1, dup2, off;
    rec_t rec;
    int clicks;
} mapp_t;

static void on_click(gates_tree_t *tree, gates_node_t node, void *user) {
    (void)tree;
    (void)node;
    ((mapp_t *)user)->clicks++;
}

static void make_mapp(mapp_t *a) {
    memset(a, 0, sizeof *a);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &a->t));
    gates_tree_t *t = a->t;
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("&Name"), &a->name_label));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR(""), 10, &a->name_box));
    GT_ASSERT_OK(gates_label_set_target(t, a->name_label, a->name_box));
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("&Plain"), &a->plain_label));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("&OK"), on_click, a, &a->ok));
    GT_ASSERT_OK(gates_widget_set_handler(t, a->ok, record, &a->rec));
    GT_ASSERT_OK(gates_checkbox_create(t, root, GATES_STR("&Wrap"), false, nullptr, nullptr, &a->check));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("&Delete"), nullptr, nullptr, &a->dup1));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("&Duplicate"), nullptr, nullptr, &a->dup2));
    GT_ASSERT_OK(gates_widget_set_handler(t, a->dup1, record, &a->rec));
    GT_ASSERT_OK(gates_widget_set_handler(t, a->dup2, record, &a->rec));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("&Gone"), nullptr, nullptr, &a->off));
    GT_ASSERT_OK(gates_widget_set_disabled(t, a->off, true));
    layout(t);
}

static void test_mnemonic_geometry_and_paint(void) {
    mapp_t a;
    make_mapp(&a);
    gates_tree_t *t = a.t;
    gates_i32 adv = be->metrics(be->ctx, GATES_FONT_UI).advance;
    /* Markup takes no room: "&OK" is as wide as "OK"; a label without a target
     * shows its '&'. */
    gates_node_t plain_ok;
    GT_ASSERT_OK(gates_button_create(t, gates_tree_root(t), GATES_STR("OK"), nullptr, nullptr, &plain_ok));
    layout(t);
    GT_ASSERT(gates_node_preferred_size(t, a.ok).w == gates_node_preferred_size(t, plain_ok).w);
    GT_ASSERT(gates_node_preferred_size(t, a.name_label).w == 4 * adv);
    GT_ASSERT(gates_node_preferred_size(t, a.plain_label).w == 6 * adv);
    GT_ASSERT(seq(gates_widget_text(t, a.ok), GATES_STR("&OK"))); /* as set */

    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    char buf[256];
    GT_ASSERT(!gates_tree_cues_visible(t));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    draw_texts(&dl, buf, sizeof buf);
    GT_ASSERT(strstr(buf, "OK") != nullptr && strstr(buf, "&OK") == nullptr);
    GT_ASSERT(strstr(buf, "&Plain") != nullptr);          /* no target: plain text */
    GT_ASSERT(strstr(buf, "&Name") == nullptr && strstr(buf, "Name") != nullptr);
    GT_ASSERT(underlines(&dl, gates_node_layout_rect(t, a.ok), nullptr) == 0);

    /* Alt shows the cues: the mnemonic letter is underlined, under its glyph. */
    gates_input_show_cues(t);
    GT_ASSERT(gates_tree_cues_visible(t));
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    gates_rect_t ul = {0};
    gates_rect_t okr = gates_node_layout_rect(t, a.ok);
    GT_ASSERT(underlines(&dl, okr, &ul) == 1);
    gates_i32 text_x = okr.x + (okr.w - 2 * adv) / 2;
    GT_ASSERT(ul.x == text_x && ul.w == adv);
    GT_ASSERT(underlines(&dl, gates_node_layout_rect(t, a.plain_label), nullptr) == 0);
    GT_ASSERT(underlines(&dl, gates_node_layout_rect(t, a.name_label), nullptr) == 1);
    /* Only the first "&x" is underlined; "&-" is shown as it is. */
    gates_node_t two, dash;
    GT_ASSERT_OK(gates_button_create(t, gates_tree_root(t), GATES_STR("&Save &As"), nullptr, nullptr, &two));
    GT_ASSERT_OK(gates_button_create(t, gates_tree_root(t), GATES_STR("a&-b"), nullptr, nullptr, &dash));
    layout(t);
    gates_draw_list_reset(&dl);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    GT_ASSERT(underlines(&dl, gates_node_layout_rect(t, two), nullptr) == 1);
    draw_texts(&dl, buf, sizeof buf);
    GT_ASSERT(strstr(buf, "Save As") != nullptr && strstr(buf, "a&-b") != nullptr);
    GT_ASSERT(gates_node_preferred_size(t, dash).w - gates_node_preferred_size(t, plain_ok).w == 2 * adv);
    /* A pointer press hides them again; "always" keeps them. */
    pointer(&*t, GATES_POINTER_DOWN, (gates_point_t){ VW - 2, VH - 2 });
    pointer(&*t, GATES_POINTER_UP, (gates_point_t){ VW - 2, VH - 2 });
    GT_ASSERT(!gates_tree_cues_visible(t));
    gates_tree_set_cues_always(t, true);
    click_at(t, (gates_point_t){ VW - 2, VH - 2 });
    GT_ASSERT(gates_tree_cues_visible(t));
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
}

static gates_text_metrics_t tall_metrics(void *ctx, gates_i32 font) {
    gates_text_metrics_t m = be->metrics(ctx, font);
    m.ascent = m.line_height;   /* no descent: one below the baseline would leave the line */
    m.descent = 0;
    return m;
}

static void test_underline_inside_line(void) {
    gates_text_backend_t tall = *be;
    tall.metrics = tall_metrics;
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_tree_t *t;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t b;
    GT_ASSERT_OK(gates_button_create(t, gates_tree_root(t), GATES_STR("&Go"), nullptr, nullptr, &b));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ VW, VH }, &tall));
    gates_input_show_cues(t);
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, &tall));
    gates_rect_t ul = {0};
    gates_rect_t br = gates_node_layout_rect(t, b);
    GT_ASSERT(underlines(&dl, br, &ul) == 1);
    gates_i32 lh = tall.metrics(tall.ctx, GATES_FONT_UI).line_height;
    gates_i32 text_y = br.y + (br.h - lh) / 2;
    GT_ASSERT(ul.y == text_y + lh - 1);
    gates_tree_destroy(t);
    gates_draw_list_deinit(&dl);
}

static void test_mnemonic_activation(void) {
    mapp_t a;
    make_mapp(&a);
    gates_tree_t *t = a.t;
    /* A button is pressed (event, legacy callback), case does not matter. */
    GT_ASSERT(gates_input_mnemonic(t, 'o'));
    GT_ASSERT(a.clicks == 1);
    dispatch(t);
    GT_ASSERT(a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_ACTIVATED);
    GT_ASSERT(gates_tree_cues_visible(t));                 /* Alt use shows cues */
    /* A check box toggles and takes the focus. */
    GT_ASSERT(gates_input_mnemonic(t, 'W'));
    GT_ASSERT(gates_checkbox_checked(t, a.check));
    GT_ASSERT(focused(t, a.check));
    /* A label focuses its target; a label without one is no mnemonic. */
    GT_ASSERT(gates_input_mnemonic(t, 'n'));
    GT_ASSERT(focused(t, a.name_box));
    GT_ASSERT(!gates_input_mnemonic(t, 'p'));
    /* Two controls share D: each press moves the focus, nothing is activated. */
    a.rec.n = 0;
    GT_ASSERT(gates_input_mnemonic(t, 'd'));
    GT_ASSERT(focused(t, a.dup1));
    GT_ASSERT(gates_input_mnemonic(t, 'd'));
    GT_ASSERT(focused(t, a.dup2));
    GT_ASSERT(gates_input_mnemonic(t, 'd'));
    GT_ASSERT(focused(t, a.dup1));
    dispatch(t);
    GT_ASSERT(a.rec.n == 0);
    /* Disabled, hidden, unknown and non-letters do not count. */
    GT_ASSERT(!gates_input_mnemonic(t, 'g'));
    GT_ASSERT_OK(gates_node_set_hidden(t, a.ok, true));
    GT_ASSERT(!gates_input_mnemonic(t, 'o'));
    GT_ASSERT(!gates_input_mnemonic(t, 'z'));
    GT_ASSERT(!gates_input_mnemonic(t, '&'));
    GT_ASSERT(!gates_input_mnemonic(t, 0x4E00));
    GT_ASSERT(a.clicks == 1);
    /* The target can be removed; a bad target is refused. */
    GT_ASSERT_OK(gates_label_set_target(t, a.name_label, GATES_NODE_NULL));
    GT_ASSERT(gates_node_eq(gates_label_target(t, a.name_label), GATES_NODE_NULL));
    GT_ASSERT(!gates_input_mnemonic(t, 'n'));
    GT_ASSERT(gates_label_set_target(t, a.ok, a.name_box) == PROVEN_ERR_INVALID_ARG);
    /* A label whose target cannot take focus is no mnemonic. */
    GT_ASSERT_OK(gates_label_set_target(t, a.name_label, a.name_box));
    GT_ASSERT_OK(gates_widget_set_disabled(t, a.name_box, true));
    GT_ASSERT(!gates_input_mnemonic(t, 'n'));
    GT_ASSERT_OK(gates_widget_set_disabled(t, a.name_box, false));
    GT_ASSERT(gates_input_mnemonic(t, 'n'));
    GT_ASSERT(gates_label_set_target(t, a.name_label, a.name_label) == PROVEN_ERR_INVALID_ARG);
    gates_tree_destroy(t);
}

static void test_mnemonic_in_dialog(void) {
    mapp_t a;
    make_mapp(&a);
    gates_tree_t *t = a.t;
    gates_node_t dlg, content, yes;
    GT_ASSERT_OK(gates_dialog_open(t, &(gates_dialog_desc_t){ .title = GATES_STR("Ask") }, &dlg, &content));
    GT_ASSERT_OK(gates_button_create(t, content, GATES_STR("&Yes"), nullptr, nullptr, &yes));
    layout(t);
    GT_ASSERT(!gates_input_mnemonic(t, 'o'));      /* below the dialog: out of scope */
    GT_ASSERT(a.clicks == 0);
    GT_ASSERT(gates_input_mnemonic(t, 'y'));
    gates_tree_destroy(t);
}

/* -- keymap (A4) --------------------------------------------------------------------- */

static void test_keymap(void) {
    gates_tree_t *t;
    rec_t r = {0};
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    gates_command_desc_t save = cmd(1, "&Save", (gates_shortcut_t){ .letter = 'S', .ctrl = true }, &r);
    gates_command_desc_t open = cmd(2, "&Open", (gates_shortcut_t){ .letter = 'O', .ctrl = true }, &r);
    gates_command_desc_t help = cmd(3, "&Help", (gates_shortcut_t){ .key = GATES_KEY_F1 }, &r);
    GT_ASSERT_OK(gates_command_register(t, root, &save));
    GT_ASSERT_OK(gates_command_register(t, root, &open));
    GT_ASSERT_OK(gates_command_register(t, root, &help));
    GT_ASSERT(gates_command_count(t, root) == 3);
    bool seen[4] = {0};
    for (gates_u32 i = 0; i < 3; i++) seen[gates_command_at(t, root, i)] = true;
    GT_ASSERT(seen[1] && seen[2] && seen[3]);
    GT_ASSERT(gates_command_at(t, root, 3) == 0);

    /* Rebinding: the old chord stops, the new one runs. */
    gates_command_id_t conflict = 99;
    GT_ASSERT_OK(gates_command_set_shortcut(t, root, 1, (gates_shortcut_t){ .letter = 'S', .ctrl = true, .shift = true }, &conflict));
    GT_ASSERT(conflict == 0);
    layout(t);
    GT_ASSERT(!keyx(t, GATES_KEY_NONE, true, false, 'S'));
    GT_ASSERT(keyx(t, GATES_KEY_NONE, true, true, 'S'));
    dispatch(t);
    GT_ASSERT(r.cmd[1] == 1);
    /* Conflicts name the other command; nothing changes. */
    GT_ASSERT(gates_command_set_shortcut(t, root, 1, (gates_shortcut_t){ .letter = 'O', .ctrl = true }, &conflict) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT(conflict == 2);
    GT_ASSERT(gates_command_shortcut(t, root, 1).shift);
    /* Invalid shortcuts and missing commands. */
    GT_ASSERT(gates_command_set_shortcut(t, root, 1, (gates_shortcut_t){ .letter = 'S' }, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_command_set_shortcut(t, root, 1, (gates_shortcut_t){ .letter = 'S', .ctrl = true, .alt = true }, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_command_set_shortcut(t, root, 9, (gates_shortcut_t){ .key = GATES_KEY_F2 }, nullptr) == PROVEN_ERR_NOT_FOUND);
    /* Removing a shortcut; the same shortcut again is fine. */
    GT_ASSERT_OK(gates_command_set_shortcut(t, root, 3, (gates_shortcut_t){0}, nullptr));
    GT_ASSERT(!key(t, GATES_KEY_F1));
    GT_ASSERT_OK(gates_command_set_shortcut(t, root, 2, (gates_shortcut_t){ .letter = 'O', .ctrl = true }, nullptr));

    /* Format and parse. */
    char buf[32];
    gates_shortcut_t k = { .letter = 'S', .ctrl = true, .shift = true };
    GT_ASSERT(gates_shortcut_format(&k, buf, sizeof buf) == 12 && strcmp(buf, "Ctrl+Shift+S") == 0);
    k = (gates_shortcut_t){ .key = GATES_KEY_F5 };
    GT_ASSERT(gates_shortcut_format(&k, buf, sizeof buf) == 2 && strcmp(buf, "F5") == 0);
    k = (gates_shortcut_t){ .key = GATES_KEY_F12, .shift = true };
    gates_shortcut_format(&k, buf, sizeof buf);
    GT_ASSERT(strcmp(buf, "Shift+F12") == 0);
    k = (gates_shortcut_t){ .key = GATES_KEY_DELETE, .ctrl = true };
    gates_shortcut_format(&k, buf, sizeof buf);
    GT_ASSERT(strcmp(buf, "Ctrl+Del") == 0);
    k = (gates_shortcut_t){0};
    GT_ASSERT(gates_shortcut_format(&k, buf, sizeof buf) == 0 && buf[0] == 0);
    k = (gates_shortcut_t){ .letter = 'S', .ctrl = true, .shift = true };
    GT_ASSERT(gates_shortcut_format(&k, buf, 5) == 12 && strcmp(buf, "Ctrl") == 0); /* cut, NUL */
    GT_ASSERT(gates_shortcut_format(&k, nullptr, 0) == 12);

    static const char *round[] = { "Ctrl+S", "Ctrl+Shift+S", "F1", "Shift+F12", "Ctrl+Del",
                                   "Ctrl+Enter", "Ctrl+Home", "Ctrl+PgDn", "Ctrl+9", "Ctrl+Left",
                                   "Ctrl+Space", "Ctrl+Tab", "Ctrl+Esc", "Ctrl+Backspace" };
    for (gates_usize_t i = 0; i < sizeof round / sizeof round[0]; i++) {
        gates_shortcut_t p;
        GT_ASSERT_OK(gates_shortcut_parse((gates_str_t){ .ptr = (const gates_u8 *)round[i], .size = strlen(round[i]) }, &p));
        gates_shortcut_format(&p, buf, sizeof buf);
        GT_ASSERT(strcmp(buf, round[i]) == 0);
    }
    gates_shortcut_t p;
    GT_ASSERT_OK(gates_shortcut_parse(GATES_STR("control+shift+delete"), &p));
    GT_ASSERT(p.ctrl && p.shift && p.key == GATES_KEY_DELETE);
    GT_ASSERT_OK(gates_shortcut_parse(GATES_STR("ctrl+pageup"), &p));
    GT_ASSERT(p.key == GATES_KEY_PAGE_UP);
    GT_ASSERT_OK(gates_shortcut_parse(GATES_STR(""), &p));
    GT_ASSERT(p.key == GATES_KEY_NONE && p.letter == 0 && !p.ctrl);
    static const char *bad[] = { "S", "Alt+F", "Ctrl+Alt+S", "Ctrl+Foo", "Ctrl+", "+S",
                                 "Ctrl++S", "Ctrl+SS", "Shift+S", "F13", "Ctrl+S+T", "ctrl s",
                                 "Ctrl+F13", "F01", "F0", "Fx" };
    for (gates_usize_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GT_ASSERT(gates_shortcut_parse((gates_str_t){ .ptr = (const gates_u8 *)bad[i], .size = strlen(bad[i]) }, &p) == PROVEN_ERR_INVALID_ARG);
    }
    gates_tree_destroy(t);
}

/* -- menu bar ------------------------------------------------------------------------ */

typedef struct bapp_t {
    gates_tree_t *t;
    gates_node_t bar, box, btn;
    rec_t rec;
} bapp_t;

enum { C_NEW = 1, C_OPEN, C_QUIT, C_UNDO, C_CUT, C_PASTE, C_ABOUT, C_F10 = 20 };

static void make_bapp(bapp_t *a, gates_allocator_t alloc) {
    memset(a, 0, sizeof *a);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){ .allocator = alloc }, &a->t));
    gates_tree_t *t = a->t;
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_command_desc_t d[] = {
        cmd(C_NEW, "&New", (gates_shortcut_t){ .letter = 'N', .ctrl = true }, &a->rec),
        cmd(C_OPEN, "&Open...", (gates_shortcut_t){0}, &a->rec),
        cmd(C_QUIT, "E&xit", (gates_shortcut_t){0}, &a->rec),
        cmd(C_UNDO, "&Undo", (gates_shortcut_t){0}, &a->rec),
        cmd(C_CUT, "Cu&t", (gates_shortcut_t){0}, &a->rec),
        cmd(C_PASTE, "&Paste", (gates_shortcut_t){0}, &a->rec),
        cmd(C_ABOUT, "&About", (gates_shortcut_t){0}, &a->rec),
    };
    for (gates_usize_t i = 0; i < sizeof d / sizeof d[0]; i++) {
        GT_ASSERT_OK(gates_command_register(t, root, &d[i]));
    }
    GT_ASSERT_OK(gates_command_set_enabled(t, root, C_UNDO, false));
    GT_ASSERT_OK(gates_menubar_create(t, root, root, &a->bar));
    static const gates_command_id_t file[] = { C_NEW, C_OPEN, 0, C_QUIT };
    static const gates_command_id_t edit[] = { C_UNDO, 0, C_CUT, C_PASTE };
    static const gates_command_id_t help[] = { C_ABOUT };
    gates_u32 idx = 99;
    GT_ASSERT_OK(gates_menubar_add(t, a->bar, GATES_STR("&File"), file, 4, &idx));
    GT_ASSERT(idx == 0);
    GT_ASSERT_OK(gates_menubar_add(t, a->bar, GATES_STR("&Edit"), edit, 4, &idx));
    GT_ASSERT_OK(gates_menubar_add(t, a->bar, GATES_STR("&Help"), help, 1, nullptr));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("text"), 10, &a->box));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("&Go"), nullptr, nullptr, &a->btn));
    layout(t);
    gates_tree_set_focus(t, a->box);
}

/* The x centre of title i: titles are laid out left to right from the bar's x;
 * found by probing the bar's row with hit tests of the open index. */
static gates_point_t title_point(bapp_t *a, gates_u32 i) {
    gates_rect_t r = gates_node_layout_rect(a->t, a->bar);
    gates_i32 adv = be->metrics(be->ctx, GATES_FONT_UI).advance;
    /* Each title is its display text plus one advance on each side. */
    gates_i32 x = r.x;
    for (gates_u32 k = 0; k < i; k++) {
        gates_str_t s = gates_menubar_title(a->t, a->bar, k);
        x += (gates_i32)(s.size - 1) * adv + 2 * adv;
    }
    return (gates_point_t){ x + adv + 2, r.y + r.h / 2 };
}

static void test_menubar_build(void) {
    bapp_t a;
    make_bapp(&a, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    GT_ASSERT(gates_node_kind(t, a.bar) == GATES_NODE_MENUBAR);
    GT_ASSERT(gates_menubar_count(t, a.bar) == 3);
    GT_ASSERT(seq(gates_menubar_title(t, a.bar, 1), GATES_STR("&Edit")));
    GT_ASSERT(gates_menubar_title(t, a.bar, 3).size == 0);
    gates_node_t second;
    GT_ASSERT(gates_menubar_create(t, gates_tree_root(t), gates_tree_root(t), &second) == PROVEN_ERR_INVALID_STATE);
    static const gates_command_id_t one[] = { C_NEW };
    GT_ASSERT(gates_menubar_add(t, a.bar, GATES_STR("x"), one, 0, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_menubar_add(t, a.bar, GATES_STR("x"), nullptr, 1, nullptr) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_menubar_add(t, a.btn, GATES_STR("x"), one, 1, nullptr) == PROVEN_ERR_INVALID_ARG);
    /* Height: a menu row; width: the whole row of the column. */
    gates_rect_t r = gates_node_layout_rect(t, a.bar);
    GT_ASSERT(r.h >= 24 && r.w == VW);
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == -1);
    /* Paint: titles without markup; a bottom line. */
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    char buf[256];
    draw_texts(&dl, buf, sizeof buf);
    GT_ASSERT(strstr(buf, "FileEditHelp") != nullptr);
    gates_draw_list_deinit(&dl);
    /* Destroying the bar frees the tree's slot for a new one. */
    GT_ASSERT_OK(gates_node_destroy(t, a.bar));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    GT_ASSERT_OK(gates_menubar_create(t, gates_tree_root(t), gates_tree_root(t), &second));
    gates_tree_destroy(t);
}

static void test_menubar_pointer(void) {
    bapp_t a;
    make_bapp(&a, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    click_at(t, title_point(&a, 0));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 0);
    GT_ASSERT(gates_tree_overlay_count(t) == 1);
    gates_node_t menu = gates_menubar_menu(t, a.bar);
    GT_ASSERT(gates_overlay_is_open(t, menu));
    layout(t);
    /* The menu hangs below its title. */
    gates_rect_t mr = gates_node_layout_rect(t, menu);
    gates_rect_t br = gates_node_layout_rect(t, a.bar);
    GT_ASSERT(mr.y == br.y + br.h && mr.x == br.x);
    /* Moving over another title switches menus. */
    pointer(t, GATES_POINTER_MOVE, title_point(&a, 1));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 1);
    GT_ASSERT(gates_tree_overlay_count(t) == 1);
    GT_ASSERT(!gates_overlay_is_open(t, menu));
    layout(t);
    GT_ASSERT(gates_node_layout_rect(t, gates_menubar_menu(t, a.bar)).x > br.x);
    /* A click on the open title closes it and leaves menu mode. */
    click_at(t, title_point(&a, 1));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == -1);
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    /* Choosing an entry invokes it and leaves menu mode. */
    click_at(t, title_point(&a, 0));
    layout(t);
    menu = gates_menubar_menu(t, a.bar);
    gates_rect_t m = gates_node_layout_rect(t, menu);
    click_at(t, (gates_point_t){ m.x + 10, m.y + 4 + 2 });  /* first row: New */
    dispatch(t);
    GT_ASSERT(a.rec.cmd[C_NEW] == 1);
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    /* An outside click only closes. */
    click_at(t, title_point(&a, 2));
    layout(t);
    click_at(t, center(t, a.btn));
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    gates_tree_destroy(t);
}

static void test_menubar_keyboard(void) {
    bapp_t a;
    make_bapp(&a, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    /* F10 enters menu mode on the first title; the focus stays where it was. */
    GT_ASSERT(key(t, GATES_KEY_F10));
    GT_ASSERT(gates_menubar_active(t, a.bar));
    GT_ASSERT(gates_menubar_highlighted(t, a.bar) == 0);
    GT_ASSERT(gates_tree_cues_visible(t));
    GT_ASSERT(focused(t, a.box));
    GT_ASSERT(key(t, GATES_KEY_RIGHT));
    GT_ASSERT(gates_menubar_highlighted(t, a.bar) == 1);
    GT_ASSERT(key(t, GATES_KEY_LEFT));
    GT_ASSERT(key(t, GATES_KEY_LEFT));
    GT_ASSERT(gates_menubar_highlighted(t, a.bar) == 2);  /* wraps */
    GT_ASSERT(key(t, GATES_KEY_RIGHT));
    /* Down opens with the first enabled entry selected; Right goes on to the
     * next menu, whose first enabled entry skips a disabled Undo. */
    GT_ASSERT(key(t, GATES_KEY_DOWN));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 0);
    GT_ASSERT(key(t, GATES_KEY_RIGHT));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 1);
    GT_ASSERT(gates_tree_overlay_count(t) == 1);
    GT_ASSERT(key(t, GATES_KEY_ENTER));                 /* Cut */
    dispatch(t);
    GT_ASSERT(a.rec.cmd[C_CUT] == 1);
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    /* Escape: open menu -> highlighted title -> out. */
    GT_ASSERT(key(t, GATES_KEY_F10));
    GT_ASSERT(key(t, GATES_KEY_ENTER));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 0);
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == -1);
    GT_ASSERT(gates_menubar_active(t, a.bar) && gates_menubar_highlighted(t, a.bar) == 0);
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(focused(t, a.box));
    /* Letters: a title's mnemonic opens it, an entry's mnemonic chooses it, and
     * the character that follows is not typed into the focused box. */
    GT_ASSERT(key(t, GATES_KEY_F10));
    GT_ASSERT(letter(t, 'E'));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 1);
    GT_ASSERT(gates_input_char(t, 'e') != GATES_INPUT_FAILED);
    GT_ASSERT(letter(t, 'P'));
    GT_ASSERT(gates_input_char(t, 'p') != GATES_INPUT_FAILED);
    dispatch(t);
    GT_ASSERT(a.rec.cmd[C_PASTE] == 1);
    GT_ASSERT(seq(gates_textbox_text(t, a.box), GATES_STR("text")));
    /* Space chooses too, and its space is not typed either. */
    GT_ASSERT(key(t, GATES_KEY_F10));
    GT_ASSERT(key(t, GATES_KEY_DOWN));
    GT_ASSERT(key(t, GATES_KEY_SPACE));
    GT_ASSERT(gates_input_char(t, ' ') != GATES_INPUT_FAILED);
    dispatch(t);
    GT_ASSERT(a.rec.cmd[C_NEW] == 1);
    GT_ASSERT(seq(gates_textbox_text(t, a.box), GATES_STR("text")));
    /* ...but the next ordinary character is typed. */
    GT_ASSERT(letter(t, 'Q') == false);
    GT_ASSERT(gates_input_char(t, 'q') == GATES_INPUT_CONSUMED);
    GT_ASSERT(seq(gates_textbox_text(t, a.box), GATES_STR("textq")));
    /* A key a menu took but that made no character (an arrow) does not eat the
     * next key's character. */
    GT_ASSERT(key(t, GATES_KEY_F10));
    GT_ASSERT(key(t, GATES_KEY_RIGHT));
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(letter(t, 'R') == false);
    GT_ASSERT(gates_input_char(t, 'r') == GATES_INPUT_CONSUMED);
    GT_ASSERT(seq(gates_textbox_text(t, a.box), GATES_STR("textqr")));
    /* A disabled entry's letter does nothing: the selection stays. */
    GT_ASSERT_OK(gates_menubar_open(t, a.bar, 1));
    GT_ASSERT(letter(t, 'U'));
    gates_access_ref_t fr = gates_access_focus_ref(t);
    GT_ASSERT(fr.item == C_CUT);
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 1);
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    /* An unknown letter in menu mode is swallowed; Tab leaves menu mode and moves on. */
    GT_ASSERT(key(t, GATES_KEY_F10));
    GT_ASSERT(letter(t, 'Z'));
    GT_ASSERT(gates_menubar_active(t, a.bar));
    GT_ASSERT(key(t, GATES_KEY_TAB));
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(focused(t, a.btn));
    /* Alt alone enters and leaves; Alt+letter opens a title directly. */
    GT_ASSERT(gates_input_menu_key(t));
    GT_ASSERT(gates_menubar_active(t, a.bar));
    GT_ASSERT(gates_input_menu_key(t));
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(gates_input_mnemonic(t, 'h'));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 2);
    /* Losing the window focus closes the menu and leaves menu mode, also when
     * only a title is highlighted. */
    gates_tree_dismiss_menus(t);
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(gates_input_menu_key(t));
    gates_tree_dismiss_menus(t);
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    /* The program can open a menu. */
    GT_ASSERT_OK(gates_menubar_open(t, a.bar, 1));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 1);
    GT_ASSERT(gates_menubar_open(t, a.bar, 3) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    /* A mnemonic of a menu title wins over a control's. */
    GT_ASSERT(gates_input_mnemonic(t, 'g'));             /* the Go button */
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    gates_tree_destroy(t);
}

static void test_menubar_f10_and_dialog(void) {
    bapp_t a;
    make_bapp(&a, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    gates_node_t root = gates_tree_root(t);
    gates_command_desc_t f10 = cmd(C_F10, "F10 thing", (gates_shortcut_t){ .key = GATES_KEY_F10 }, &a.rec);
    GT_ASSERT_OK(gates_command_register(t, root, &f10));
    /* With a menu bar, F10 is the menu key. */
    GT_ASSERT(key(t, GATES_KEY_F10));
    dispatch(t);
    GT_ASSERT(a.rec.cmd[C_F10] == 0);
    GT_ASSERT(gates_menubar_active(t, a.bar));
    GT_ASSERT(key(t, GATES_KEY_F10));                    /* F10 again leaves */
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    /* A modal dialog makes the bar unreachable. */
    gates_node_t dlg, content, b;
    GT_ASSERT_OK(gates_dialog_open(t, &(gates_dialog_desc_t){ .title = GATES_STR("d") }, &dlg, &content));
    GT_ASSERT_OK(gates_button_create(t, content, GATES_STR("x"), nullptr, nullptr, &b));
    layout(t);
    GT_ASSERT(!gates_input_menu_key(t));
    GT_ASSERT(!gates_input_mnemonic(t, 'f'));
    GT_ASSERT(gates_menubar_open(t, a.bar, 0) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT_OK(gates_dialog_close(t, dlg, GATES_DIALOG_CANCELED));
    dispatch(t);
    /* A hidden bar: F10 is a command again. */
    GT_ASSERT_OK(gates_node_set_hidden(t, a.bar, true));
    layout(t);
    GT_ASSERT(key(t, GATES_KEY_F10));
    dispatch(t);
    GT_ASSERT(a.rec.cmd[C_F10] == 1);
    GT_ASSERT(!gates_input_menu_key(t));
    /* Destroying the bar while its menu is open closes the menu. */
    GT_ASSERT_OK(gates_node_set_hidden(t, a.bar, false));
    layout(t);
    GT_ASSERT_OK(gates_menubar_open(t, a.bar, 0));
    GT_ASSERT_OK(gates_node_destroy(t, a.bar));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    dispatch(t);
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    GT_ASSERT(!gates_input_menu_key(t));
    gates_tree_destroy(t);
}

/* Menu entries show their mnemonic markup as text without '&', and a context
 * menu's letters choose entries too. */
static void test_menu_mnemonics(void) {
    bapp_t a;
    make_bapp(&a, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    static const gates_command_id_t ids[] = { C_NEW, C_QUIT };
    gates_node_t m;
    GT_ASSERT_OK(gates_menu_open(t, (gates_point_t){ 50, 80 }, gates_tree_root(t), ids, 2, &m));
    layout(t);
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_OK(gates_paint_tree(t, &dl, theme, be));
    char buf[512];
    draw_texts(&dl, buf, sizeof buf);
    GT_ASSERT(strstr(buf, "Exit") != nullptr && strstr(buf, "E&xit") == nullptr);
    GT_ASSERT(strstr(buf, "Ctrl+N") != nullptr);
    gates_draw_list_deinit(&dl);
    GT_ASSERT(letter(t, 'X'));
    dispatch(t);
    GT_ASSERT(a.rec.cmd[C_QUIT] == 1);
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    gates_tree_destroy(t);
}


/* -- accessibility ------------------------------------------------------------------- */

static void test_access(void) {
    bapp_t a;
    make_bapp(&a, (gates_allocator_t){0});
    gates_tree_t *t = a.t;
    gates_access_info_t info;
    GT_ASSERT_OK(gates_access_info(t, a.bar, 0, &info));
    GT_ASSERT(info.role == GATES_ROLE_MENU_BAR);
    GT_ASSERT(info.item_count == 3);
    GT_ASSERT(gates_access_item_at(t, a.bar, 0) == 1 && gates_access_item_at(t, a.bar, 2) == 3);
    GT_ASSERT(gates_access_item_at(t, a.bar, 3) == 0);
    /* Titles: names without markup, Alt keys, expandable. */
    GT_ASSERT_OK(gates_access_info(t, a.bar, 2, &info));
    GT_ASSERT(info.role == GATES_ROLE_MENU_ITEM);
    GT_ASSERT(seq(info.name, GATES_STR("Edit")));
    GT_ASSERT(seq(info.access_key, GATES_STR("Alt+E")));
    GT_ASSERT((info.states & GATES_ACCESS_EXPANDABLE) && !(info.states & GATES_ACCESS_EXPANDED));
    GT_ASSERT(info.actions & GATES_ACCESS_EXPAND);
    GT_ASSERT(info.set_position == 2 && info.set_size == 3);
    gates_point_t tp = { info.bounds.x + 2, info.bounds.y + 2 };
    gates_access_ref_t at = gates_access_at_point(t, tp);
    GT_ASSERT(gates_node_eq(at.node, a.bar) && at.item == 2);
    GT_ASSERT(gates_access_info(t, a.bar, 4, &info) == PROVEN_ERR_INVALID_ARG);
    /* Walking: the bar's children are its titles. */
    gates_access_ref_t c = gates_access_first_child(t, (gates_access_ref_t){ a.bar, 0 });
    GT_ASSERT(gates_node_eq(c.node, a.bar) && c.item == 1);
    c = gates_access_next(t, c);
    GT_ASSERT(c.item == 2);
    /* Buttons and labels: markup-free names and access keys. */
    GT_ASSERT_OK(gates_access_info(t, a.btn, 0, &info));
    GT_ASSERT(seq(info.name, GATES_STR("Go")) && seq(info.access_key, GATES_STR("Alt+G")));
    GT_ASSERT(info.accelerator.size == 0);
    /* Menu mode: the highlighted title has focus; expand opens, collapse closes. */
    GT_ASSERT(key(t, GATES_KEY_F10));
    gates_access_ref_t f = gates_access_focus_ref(t);
    GT_ASSERT(gates_node_eq(f.node, a.bar) && f.item == 1);
    GT_ASSERT(key(t, GATES_KEY_ESCAPE));
    GT_ASSERT_OK(gates_access_expand(t, a.bar, 2, true));
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 1);
    GT_ASSERT_OK(gates_access_info(t, a.bar, 2, &info));
    GT_ASSERT(info.states & GATES_ACCESS_EXPANDED);
    /* The open menu's entries: names, letter keys, accelerators; focus on the first. */
    gates_node_t menu = gates_menubar_menu(t, a.bar);
    f = gates_access_focus_ref(t);
    GT_ASSERT(gates_node_eq(f.node, menu) && f.item == C_CUT);
    GT_ASSERT_OK(gates_access_info(t, menu, C_CUT, &info));
    GT_ASSERT(seq(info.name, GATES_STR("Cut")) && seq(info.access_key, GATES_STR("T")));
    GT_ASSERT_OK(gates_access_expand(t, a.bar, 2, false));
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT_OK(gates_access_invoke(t, a.bar, 1));        /* invoke toggles */
    GT_ASSERT(gates_menubar_open_index(t, a.bar) == 0);
    menu = gates_menubar_menu(t, a.bar);
    GT_ASSERT_OK(gates_access_info(t, menu, C_NEW, &info));
    GT_ASSERT(seq(info.accelerator, GATES_STR("Ctrl+N")));
    GT_ASSERT_OK(gates_access_invoke(t, a.bar, 1));
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    GT_ASSERT(gates_access_expand(t, a.bar, 9, true) == PROVEN_ERR_INVALID_ARG);
    /* A label targeting an edit gives the edit its access key and a clean name. */
    gates_node_t lab, box;
    GT_ASSERT_OK(gates_label_create(t, gates_tree_root(t), GATES_STR("&Size"), &lab));
    GT_ASSERT_OK(gates_textbox_create(t, gates_tree_root(t), GATES_STR(""), 5, &box));
    GT_ASSERT_OK(gates_label_set_target(t, lab, box));
    GT_ASSERT_OK(gates_node_set_labelled_by(t, box, lab));
    layout(t);
    GT_ASSERT_OK(gates_access_info(t, box, 0, &info));
    GT_ASSERT(seq(info.name, GATES_STR("Size")) && seq(info.access_key, GATES_STR("Alt+S")));
    GT_ASSERT_OK(gates_access_info(t, lab, 0, &info));
    GT_ASSERT(seq(info.name, GATES_STR("Size")));
    /* A button bound to a command reports its shortcut. */
    gates_node_t bound;
    GT_ASSERT_OK(gates_button_create(t, gates_tree_root(t), GATES_STR(""), nullptr, nullptr, &bound));
    GT_ASSERT_OK(gates_button_set_command(t, bound, gates_tree_root(t), C_NEW));
    layout(t);
    GT_ASSERT_OK(gates_access_info(t, bound, 0, &info));
    GT_ASSERT(seq(info.name, GATES_STR("New")) && seq(info.accelerator, GATES_STR("Ctrl+N")));
    /* The audit finds nothing to complain about (the fixture's box gets a name). */
    GT_ASSERT_OK(gates_node_set_access_name(t, a.box, GATES_STR("Notes")));
    gates_access_issue_t issues[8];
    gates_u32 ni = gates_access_audit(t, theme, issues, 8);
    GT_ASSERT(ni == 0);
    for (gates_u32 i = 0; i < ni && i < 8; i++) {
        printf("  issue rule %d node kind %d item %llu\n", (int)issues[i].rule,
               (int)gates_node_kind(t, issues[i].node), (unsigned long long)issues[i].item);
    }
    gates_tree_destroy(t);
}

/* -- allocation failure -------------------------------------------------------------- */

typedef struct fail_alloc_t {
    gates_allocator_t inner;
    bool fail;
} fail_alloc_t;
static proven_result_mem_mut_t fa_alloc(void *ctx, gates_usize_t size, gates_usize_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}
static proven_result_mem_mut_t fa_realloc(void *ctx, void *p, gates_usize_t old, gates_usize_t size,
                                          gates_usize_t align) {
    fail_alloc_t *f = ctx;
    if (f->fail) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    return f->inner.realloc_fn(f->inner.ctx, p, old, size, align);
}
static void fa_free(void *ctx, void *p) {
    fail_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, p);
}

static void test_allocation_failure(void) {
    fail_alloc_t fa = { .inner = proven_heap_allocator() };
    gates_allocator_t alloc = { .ctx = &fa, .alloc_fn = fa_alloc, .realloc_fn = fa_realloc,
                                .free_fn = fa_free };
    bapp_t a;
    make_bapp(&a, alloc);
    gates_tree_t *t = a.t;
    static const gates_command_id_t ids[] = { C_NEW };
    fa.fail = true;
    GT_ASSERT(gates_menubar_add(t, a.bar, GATES_STR("&More"), ids, 1, nullptr) == PROVEN_ERR_NOMEM);
    GT_ASSERT(gates_menubar_count(t, a.bar) == 3);
    GT_ASSERT(!gates_input_menu_key(t) || true);            /* no crash either way */
    fa.fail = false;
    if (gates_menubar_active(t, a.bar)) (void)gates_input_menu_key(t);
    GT_ASSERT_OK(gates_menubar_add(t, a.bar, GATES_STR("&More"), ids, 1, nullptr));
    GT_ASSERT(gates_menubar_count(t, a.bar) == 4);
    /* Opening a menu that cannot be allocated leaves menu mode off, the error recorded. */
    fa.fail = true;
    GT_ASSERT(gates_menubar_open(t, a.bar, 0) == PROVEN_ERR_NOMEM);
    fa.fail = false;
    GT_ASSERT(gates_tree_overlay_count(t) == 0);
    GT_ASSERT(!gates_menubar_active(t, a.bar));
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    theme = gates_theme_light();
    test_mnemonic_parse();
    test_mnemonic_geometry_and_paint();
    test_underline_inside_line();
    test_mnemonic_activation();
    test_mnemonic_in_dialog();
    test_keymap();
    test_menubar_build();
    test_menubar_pointer();
    test_menubar_keyboard();
    test_menubar_f10_and_dialog();
    test_menu_mnemonics();
    test_access();
    test_allocation_failure();
    return gt_report("test_frame");
}
