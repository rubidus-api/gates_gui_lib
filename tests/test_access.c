/* T038: accessibility model (plan-0014 stage 1): roles, the name order,
 * descriptions, automation ids, states, values, virtual items, actions through
 * the input paths, the change log, and the enforced rules. */
#include <gates/ui.h>
#include <gates/widget.h>
#include <gates/event.h>
#include <gates/command.h>
#include <gates/overlay.h>
#include <gates/form.h>
#include <gates/access.h>
#include <gates/view.h>
#include <stdio.h>
#include "gates_test.h"

#include <string.h>

static const gates_text_backend_t *be;

static void layout(gates_tree_t *t) {
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 600, 500 }, be));
}

static bool str_is(gates_str_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.size == n && (n == 0 || memcmp(s.ptr, lit, n) == 0);
}

static gates_access_info_t info(gates_tree_t *t, gates_node_t n, gates_u64 item) {
    gates_access_info_t i;
    GT_ASSERT_OK(gates_access_info(t, n, item, &i));
    return i;
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

static int g_saves;
static void on_save(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree; (void)id; (void)user;
    g_saves++;
}

static const gates_option_t langs[] = {
    { .id = 1, .label = GATES_STR_INIT("English") },
    { .id = 2, .label = GATES_STR_INIT("Korean") },
    { .id = 3, .label = GATES_STR_INIT("Latin"), .disabled = true },
};

typedef struct app_t {
    gates_tree_t *t;
    gates_node_t form, name, mail, lang, theme, news, save, status, bar, sep, plain;
    rec_t rec;
} app_t;

static void make_app(app_t *a) {
    memset(a, 0, sizeof *a);
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &a->t));
    gates_tree_t *t = a->t;
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_form_create(t, root, &a->form));
    gates_field_desc_t d = { .label = GATES_STR("Name"), .required = true, .max_bytes = 8 };
    GT_ASSERT_OK(gates_form_add_text(t, a->form, 11, &d, &a->name));
    d = (gates_field_desc_t){ .label = GATES_STR("E-mail"), .help = GATES_STR("for receipts"),
                              .text = GATES_STR("x@y.z") };
    GT_ASSERT_OK(gates_form_add_text(t, a->form, 12, &d, &a->mail));
    d = (gates_field_desc_t){ .label = GATES_STR("Language"), .options = langs, .option_count = 3,
                              .selected_id = 1 };
    GT_ASSERT_OK(gates_form_add_choice(t, a->form, 13, &d, &a->lang));
    GT_ASSERT_OK(gates_form_add_radio(t, a->form, 14, &d, &a->theme));
    d = (gates_field_desc_t){ .label = GATES_STR("News"), .text = GATES_STR("send news") };
    GT_ASSERT_OK(gates_form_add_checkbox(t, a->form, 15, &d, &a->news));
    gates_command_desc_t save = { .id = 7, .label = GATES_STR("Save"), .enabled = true,
                                  .invoke = on_save, .shortcut = { .key = GATES_KEY_F5 } };
    GT_ASSERT_OK(gates_command_register(t, root, &save));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR(""), nullptr, nullptr, &a->save));
    GT_ASSERT_OK(gates_button_set_command(t, a->save, root, 7));
    GT_ASSERT_OK(gates_label_create(t, root, GATES_STR("ready"), &a->status));
    GT_ASSERT_OK(gates_progress_create(t, root, 250, &a->bar));
    GT_ASSERT_OK(gates_separator_create(t, root, &a->sep));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("Plain"), nullptr, nullptr, &a->plain));
    GT_ASSERT_OK(gates_widget_set_handler(t, a->mail, record, &a->rec));
    GT_ASSERT_OK(gates_widget_set_handler(t, a->news, record, &a->rec));
    GT_ASSERT_OK(gates_widget_set_handler(t, a->theme, record, &a->rec));
    GT_ASSERT_OK(gates_widget_set_handler(t, a->plain, record, &a->rec));
    layout(t);
}

/* -- roles, names, descriptions, ids ------------------------------------------------------ */

static void test_names(void) {
    app_t a;
    make_app(&a);
    gates_tree_t *t = a.t;
    GT_ASSERT(info(t, gates_tree_root(t), 0).role == GATES_ROLE_WINDOW);
    GT_ASSERT(info(t, a.form, 0).role == GATES_ROLE_FORM);
    gates_access_info_t n = info(t, a.name, 0);
    GT_ASSERT(n.role == GATES_ROLE_EDIT);
    GT_ASSERT(str_is(n.name, "Name"));                      /* the label, marker stripped */
    GT_ASSERT((n.states & GATES_ACCESS_REQUIRED) != 0);     /* ...said by a state */
    GT_ASSERT(str_is(n.automation_id, "field-11"));
    GT_ASSERT(!gates_node_eq(n.labelled_by, GATES_NODE_NULL));
    GT_ASSERT(info(t, n.labelled_by, 0).role == GATES_ROLE_TEXT);
    gates_access_info_t m = info(t, a.mail, 0);
    GT_ASSERT(str_is(m.name, "E-mail") && (m.states & GATES_ACCESS_REQUIRED) == 0);
    GT_ASSERT(str_is(m.description, "for receipts"));
    GT_ASSERT(str_is(m.value, "x@y.z") && m.caret == 5 && m.anchor == 5);
    GT_ASSERT((m.actions & GATES_ACCESS_SET_VALUE) != 0);
    /* An error comes first in the description, and goes with the error. */
    GT_ASSERT_OK(gates_form_set_error(t, a.form, 12, GATES_STR("not an address")));
    m = info(t, a.mail, 0);
    GT_ASSERT(str_is(m.description, "not an address. for receipts"));
    GT_ASSERT((m.states & GATES_ACCESS_INVALID) != 0);
    GT_ASSERT_OK(gates_form_set_error(t, a.form, 12, GATES_STR("")));
    GT_ASSERT(str_is(info(t, a.mail, 0).description, "for receipts"));
    /* Own text: a bound button shows its command's label; the id follows the command. */
    gates_access_info_t s = info(t, a.save, 0);
    GT_ASSERT(s.role == GATES_ROLE_BUTTON && str_is(s.name, "Save") && str_is(s.automation_id, "cmd-7"));
    GT_ASSERT(str_is(info(t, a.plain, 0).name, "Plain") && info(t, a.plain, 0).automation_id.size == 0);
    GT_ASSERT(str_is(info(t, a.news, 0).name, "News"));     /* the form label wins over the caption */
    GT_ASSERT(info(t, a.status, 0).role == GATES_ROLE_TEXT && str_is(info(t, a.status, 0).name, "ready"));
    /* Explicit beats everything; an empty one clears it. */
    GT_ASSERT_OK(gates_node_set_access_name(t, a.name, GATES_STR("Full name")));
    GT_ASSERT(str_is(info(t, a.name, 0).name, "Full name"));
    GT_ASSERT_OK(gates_node_set_access_name(t, a.name, GATES_STR("")));
    GT_ASSERT(str_is(info(t, a.name, 0).name, "Name"));
    /* A label tied to a control names it (above the form label, below explicit). */
    gates_node_t cap;
    GT_ASSERT_OK(gates_label_create(t, gates_tree_root(t), GATES_STR("Your full name"), &cap));
    GT_ASSERT_OK(gates_node_set_labelled_by(t, a.name, cap));
    gates_access_info_t tied = info(t, a.name, 0);
    GT_ASSERT(str_is(tied.name, "Your full name") && gates_node_eq(tied.labelled_by, cap));
    GT_ASSERT(gates_node_set_labelled_by(t, a.name, a.plain) == PROVEN_ERR_INVALID_ARG); /* not a label */
    GT_ASSERT_OK(gates_node_set_labelled_by(t, a.name, GATES_NODE_NULL));
    GT_ASSERT(str_is(info(t, a.name, 0).name, "Name"));
    GT_ASSERT_OK(gates_node_set_automation_id(t, a.plain, GATES_STR("plain-button")));
    GT_ASSERT(str_is(info(t, a.plain, 0).automation_id, "plain-button"));
    /* Progress and separator. */
    gates_access_info_t p = info(t, a.bar, 0);
    GT_ASSERT(p.role == GATES_ROLE_PROGRESS_BAR && p.has_range && p.range_value == 250 && p.range_max == 1000);
    GT_ASSERT(info(t, a.sep, 0).role == GATES_ROLE_SEPARATOR);
    /* A dialog is named by its title and is modal. */
    gates_node_t dlg, content;
    GT_ASSERT_OK(gates_dialog_open(t, &(gates_dialog_desc_t){ .title = GATES_STR("Confirm") }, &dlg, &content));
    layout(t);
    gates_access_info_t dg = info(t, dlg, 0);
    GT_ASSERT(dg.role == GATES_ROLE_DIALOG && str_is(dg.name, "Confirm") && (dg.states & GATES_ACCESS_MODAL));
    GT_ASSERT_OK(gates_dialog_close(t, dlg, GATES_DIALOG_CANCELED));
    gates_tree_destroy(t);
}

/* -- states, values, items -------------------------------------------------------------------- */

static void test_states_items(void) {
    app_t a;
    make_app(&a);
    gates_tree_t *t = a.t;
    gates_tree_set_focus(t, a.mail);
    gates_access_info_t m = info(t, a.mail, 0);
    GT_ASSERT((m.states & GATES_ACCESS_FOCUSED) && (m.states & GATES_ACCESS_FOCUSABLE));
    GT_ASSERT((m.actions & GATES_ACCESS_FOCUS) != 0);
    GT_ASSERT_OK(gates_textbox_set_read_only(t, a.mail, true));
    m = info(t, a.mail, 0);
    GT_ASSERT((m.states & GATES_ACCESS_READ_ONLY) && (m.actions & GATES_ACCESS_SET_VALUE) == 0);
    GT_ASSERT_OK(gates_textbox_set_read_only(t, a.mail, false));
    GT_ASSERT_OK(gates_textbox_set_password(t, a.name, true));
    GT_ASSERT_OK(gates_textbox_set_text(t, a.name, GATES_STR("secret")));
    gates_access_info_t n = info(t, a.name, 0);
    GT_ASSERT((n.states & GATES_ACCESS_PASSWORD) && n.value.size == 0);   /* never exposed */
    GT_ASSERT_OK(gates_widget_set_disabled(t, a.plain, true));
    GT_ASSERT((info(t, a.plain, 0).states & GATES_ACCESS_DISABLED) && info(t, a.plain, 0).actions == 0);
    GT_ASSERT_OK(gates_checkbox_set_checked(t, a.news, true));
    GT_ASSERT((info(t, a.news, 0).states & GATES_ACCESS_CHECKED) != 0);
    GT_ASSERT_OK(gates_form_set_row_hidden(t, a.form, 15, true));
    GT_ASSERT((info(t, a.news, 0).states & GATES_ACCESS_OFFSCREEN) != 0);
    GT_ASSERT_OK(gates_form_set_row_hidden(t, a.form, 15, false));
    layout(t);
    GT_ASSERT((info(t, a.news, 0).states & GATES_ACCESS_OFFSCREEN) == 0);

    /* Radio: a group with a value and one item per option. */
    gates_access_info_t r = info(t, a.theme, 0);
    GT_ASSERT(r.role == GATES_ROLE_RADIO_GROUP && str_is(r.value, "English") && r.item_count == 3);
    GT_ASSERT(gates_access_item_at(t, a.theme, 1) == 2 && gates_access_item_at(t, a.theme, 9) == 0);
    /* Strings last until the next info call: check each before fetching another. */
    gates_access_info_t r1 = info(t, a.theme, 1);
    GT_ASSERT(r1.role == GATES_ROLE_RADIO_ITEM && str_is(r1.name, "English") && (r1.states & GATES_ACCESS_SELECTED));
    GT_ASSERT(str_is(r1.automation_id, "item-1") && (r1.actions & GATES_ACCESS_SELECT));
    gates_access_info_t r3 = info(t, a.theme, 3);
    GT_ASSERT((r3.states & GATES_ACCESS_DISABLED) && r3.actions == 0);
    gates_rect_t rr = gates_node_layout_rect(t, a.theme);
    GT_ASSERT(r1.bounds.y == rr.y && info(t, a.theme, 2).bounds.y == rr.y + r1.bounds.h);
    GT_ASSERT(r1.bounds.h >= GATES_ACCESS_MIN_TARGET);
    gates_access_info_t bad;
    GT_ASSERT(gates_access_info(t, a.theme, 42, &bad) == PROVEN_ERR_INVALID_ARG);

    /* Choice: a collapsed combo box whose items are offscreen until it opens. */
    gates_access_info_t c = info(t, a.lang, 0);
    GT_ASSERT(c.role == GATES_ROLE_COMBO_BOX && str_is(c.value, "English"));
    GT_ASSERT((c.states & GATES_ACCESS_EXPANDABLE) && !(c.states & GATES_ACCESS_EXPANDED));
    GT_ASSERT((info(t, a.lang, 2).states & GATES_ACCESS_OFFSCREEN) != 0);
    GT_ASSERT_OK(gates_access_expand(t, a.lang, 0, true));
    layout(t);
    GT_ASSERT((info(t, a.lang, 0).states & GATES_ACCESS_EXPANDED) != 0);
    gates_access_info_t c2 = info(t, a.lang, 2);
    GT_ASSERT(c2.role == GATES_ROLE_LIST_ITEM && !(c2.states & GATES_ACCESS_OFFSCREEN) && c2.bounds.h > 0);
    GT_ASSERT_OK(gates_access_expand(t, a.lang, 0, false));
    GT_ASSERT((info(t, a.lang, 0).states & GATES_ACCESS_EXPANDED) == 0);

    /* Menu entries are items; separators are not. */
    gates_command_desc_t cut = { .id = 21, .label = GATES_STR("Cut"), .enabled = true, .invoke = on_save };
    gates_command_desc_t off = { .id = 22, .label = GATES_STR("Paste"), .enabled = false, .invoke = on_save };
    GT_ASSERT_OK(gates_command_register(t, gates_tree_root(t), &cut));
    GT_ASSERT_OK(gates_command_register(t, gates_tree_root(t), &off));
    gates_command_id_t ids[] = { 21, 0, 22 };
    gates_node_t menu;
    GT_ASSERT_OK(gates_menu_open(t, (gates_point_t){ 10, 10 }, gates_tree_root(t), ids, 3, &menu));
    layout(t);
    GT_ASSERT(info(t, menu, 0).role == GATES_ROLE_MENU && gates_access_item_count(t, menu) == 2);
    GT_ASSERT(gates_access_item_at(t, menu, 1) == 22);
    gates_access_info_t e1 = info(t, menu, 21);
    GT_ASSERT(e1.role == GATES_ROLE_MENU_ITEM && str_is(e1.name, "Cut") && (e1.actions & GATES_ACCESS_INVOKE));
    gates_access_info_t e2 = info(t, menu, 22);
    GT_ASSERT((e2.states & GATES_ACCESS_DISABLED) && e2.bounds.h >= GATES_ACCESS_MIN_TARGET);
    GT_ASSERT(gates_access_invoke(t, menu, 22) == PROVEN_ERR_INVALID_STATE);
    g_saves = 0;
    GT_ASSERT_OK(gates_access_invoke(t, menu, 21));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(g_saves == 1 && !gates_overlay_is_open(t, menu));
    gates_tree_destroy(t);
}

/* -- actions: same paths, same refusals ------------------------------------------------------- */

static void test_actions(void) {
    app_t a;
    make_app(&a);
    gates_tree_t *t = a.t;
    /* Invoke: an activation event, and a bound command through the queue. */
    g_saves = 0;
    GT_ASSERT_OK(gates_access_invoke(t, a.plain, 0));
    GT_ASSERT_OK(gates_access_invoke(t, a.save, 0));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_ACTIVATED && g_saves == 1);
    GT_ASSERT(gates_access_invoke(t, a.news, 0) == PROVEN_ERR_INVALID_ARG);
    /* Toggle. */
    a.rec.n = 0;
    GT_ASSERT_OK(gates_access_toggle(t, a.news));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(gates_checkbox_checked(t, a.news) && a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_VALUE_CHANGED &&
              a.rec.origin[0] == GATES_ORIGIN_USER);
    /* Select: like a click on the row; disabled options refused. */
    a.rec.n = 0;
    GT_ASSERT_OK(gates_access_select(t, a.theme, 2));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(gates_options_selected(t, a.theme) == 2 && a.rec.n == 1 && a.rec.result[0] == 2);
    GT_ASSERT(gates_access_select(t, a.theme, 3) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT(gates_access_select(t, a.theme, 9) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT_OK(gates_access_expand(t, a.lang, 0, true));
    GT_ASSERT_OK(gates_access_select(t, a.lang, 2));
    GT_ASSERT(gates_options_selected(t, a.lang) == 2 && !gates_choice_list_open(t, a.lang));
    /* Set value: a user edit with its event; read-only and limits refuse. */
    a.rec.n = 0;
    GT_ASSERT_OK(gates_access_set_value(t, a.mail, GATES_STR("me@home.org")));
    (void)gates_tree_dispatch_events(t, 0);
    GT_ASSERT(str_is(gates_textbox_text(t, a.mail), "me@home.org"));
    GT_ASSERT(a.rec.n == 1 && a.rec.kind[0] == GATES_EVENT_TEXT_CHANGED && a.rec.origin[0] == GATES_ORIGIN_USER);
    GT_ASSERT(gates_textbox_can_undo(t, a.mail));                        /* an undoable edit */
    GT_ASSERT(gates_access_set_value(t, a.name, GATES_STR("far too long")) == PROVEN_ERR_OUT_OF_BOUNDS);
    GT_ASSERT_OK(gates_textbox_set_read_only(t, a.mail, true));
    GT_ASSERT(gates_access_set_value(t, a.mail, GATES_STR("x")) == PROVEN_ERR_PERMISSION);
    GT_ASSERT(gates_access_set_value(t, a.plain, GATES_STR("x")) == PROVEN_ERR_INVALID_ARG);
    /* Disabled and hidden refuse everything, as the pointer would. */
    GT_ASSERT_OK(gates_widget_set_disabled(t, a.plain, true));
    GT_ASSERT(gates_access_invoke(t, a.plain, 0) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT_OK(gates_form_set_row_hidden(t, a.form, 15, true));
    GT_ASSERT(gates_access_toggle(t, a.news) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT_OK(gates_form_set_row_hidden(t, a.form, 15, false));
    /* Focus: a node, or an item by selecting it. */
    GT_ASSERT_OK(gates_access_focus(t, a.theme, 1));
    GT_ASSERT(gates_node_eq(gates_tree_focus(t), a.theme) && gates_options_selected(t, a.theme) == 1);
    GT_ASSERT(gates_access_focus(t, a.plain, 0) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT(gates_access_focus(t, a.status, 0) == PROVEN_ERR_INVALID_STATE);
    gates_tree_destroy(t);
}

/* -- the change log ---------------------------------------------------------------------------- */

static bool has(const gates_access_change_t *c, gates_u32 n, gates_access_change_kind_t k, gates_node_t node) {
    for (gates_u32 i = 0; i < n; i++) if (c[i].kind == k && gates_node_eq(c[i].node, node)) return true;
    return false;
}

static void test_changes(void) {
    app_t a;
    make_app(&a);
    gates_tree_t *t = a.t;
    gates_access_change_t c[GATES_ACCESS_CHANGES_MAX];
    bool over = false;
    /* Nothing is recorded until enabled. */
    GT_ASSERT_OK(gates_widget_set_text(t, a.status, GATES_STR("x")));
    GT_ASSERT(gates_access_take_changes(t, c, 64, &over) == 0 && !over);
    gates_access_enable(t, true);
    GT_ASSERT(gates_access_enabled(t));
    /* One entry per node and kind until taken. */
    GT_ASSERT_OK(gates_widget_set_text(t, a.status, GATES_STR("one")));
    GT_ASSERT_OK(gates_widget_set_text(t, a.status, GATES_STR("two")));
    gates_u32 n = gates_access_take_changes(t, c, 64, &over);
    int count = 0;
    for (gates_u32 i = 0; i < n; i++) count += c[i].kind == GATES_ACCESS_CHANGED && gates_node_eq(c[i].node, a.status);
    GT_ASSERT(count == 1);
    /* Focus: lost on the old, then gained on the new. */
    gates_tree_set_focus(t, a.name);
    (void)gates_access_take_changes(t, c, 64, nullptr);
    gates_tree_set_focus(t, a.mail);
    n = gates_access_take_changes(t, c, 64, nullptr);
    int lost = -1, gained = -1;
    for (gates_u32 i = 0; i < n; i++) {
        if (c[i].kind == GATES_ACCESS_FOCUS_LOST && gates_node_eq(c[i].node, a.name)) lost = (int)i;
        if (c[i].kind == GATES_ACCESS_FOCUS_GAINED && gates_node_eq(c[i].node, a.mail)) gained = (int)i;
    }
    GT_ASSERT(lost >= 0 && gained > lost);
    /* Structure on the parent; removal before the slot is reused. */
    gates_node_t extra;
    GT_ASSERT_OK(gates_label_create(t, gates_tree_root(t), GATES_STR("extra"), &extra));
    n = gates_access_take_changes(t, c, 64, nullptr);
    GT_ASSERT(has(c, n, GATES_ACCESS_STRUCTURE, gates_tree_root(t)));
    GT_ASSERT_OK(gates_node_destroy(t, extra));
    n = gates_access_take_changes(t, c, 64, nullptr);
    GT_ASSERT(has(c, n, GATES_ACCESS_REMOVED, extra) && has(c, n, GATES_ACCESS_STRUCTURE, gates_tree_root(t)));
    GT_ASSERT_OK(gates_tree_flush_destroys(t));
    gates_node_t reuse;
    GT_ASSERT_OK(gates_label_create(t, gates_tree_root(t), GATES_STR("new"), &reuse));
    gates_access_info_t stale;
    GT_ASSERT(reuse.index == extra.index);                   /* the slot came back */
    GT_ASSERT(gates_access_info(t, extra, 0, &stale) == PROVEN_ERR_INVALID_ARG); /* old key: gone */
    GT_ASSERT_OK(gates_access_info(t, reuse, 0, &stale));
    /* Live regions announce text changes; announcements are recorded. */
    GT_ASSERT_OK(gates_node_set_live(t, a.status, GATES_LIVE_POLITE));
    GT_ASSERT(info(t, a.status, 0).live == GATES_LIVE_POLITE);
    (void)gates_access_take_changes(t, c, 64, nullptr);
    GT_ASSERT_OK(gates_widget_set_text(t, a.status, GATES_STR("saved")));
    n = gates_access_take_changes(t, c, 64, nullptr);
    GT_ASSERT(has(c, n, GATES_ACCESS_LIVE, a.status));
    GT_ASSERT_OK(gates_access_announce(t, GATES_STR("3 results"), true));
    n = gates_access_take_changes(t, c, 64, nullptr);
    bool assertive = false;
    GT_ASSERT(has(c, n, GATES_ACCESS_ANNOUNCE, gates_tree_root(t)));
    GT_ASSERT(str_is(gates_access_announcement(t, &assertive), "3 results") && assertive);
    /* Bounded: past the cap the log says so. */
    gates_node_t many[GATES_ACCESS_CHANGES_MAX + 8];
    for (gates_u32 i = 0; i < GATES_ACCESS_CHANGES_MAX + 8; i++) {
        GT_ASSERT_OK(gates_label_create(t, a.form, GATES_STR("l"), &many[i]));
    }
    for (gates_u32 i = 0; i < GATES_ACCESS_CHANGES_MAX + 8; i++) {
        GT_ASSERT_OK(gates_widget_set_text(t, many[i], GATES_STR("m")));
    }
    n = gates_access_take_changes(t, c, GATES_ACCESS_CHANGES_MAX, &over);
    GT_ASSERT(n == GATES_ACCESS_CHANGES_MAX && over);
    GT_ASSERT(gates_access_take_changes(t, c, 64, &over) == 0 && !over);
    gates_access_enable(t, false);
    GT_ASSERT_OK(gates_widget_set_text(t, a.status, GATES_STR("quiet")));
    GT_ASSERT(gates_access_take_changes(t, c, 64, nullptr) == 0);
    gates_tree_destroy(t);
}

/* -- the accessible tree ------------------------------------------------------------------ */

static bool ref_is(gates_access_ref_t r, gates_node_t n, gates_u64 item) {
    return gates_node_eq(r.node, n) && r.item == item;
}

static gates_point_t mid(gates_rect_t r) {
    return (gates_point_t){ r.x + r.w / 2, r.y + r.h / 2 };
}

static gates_access_ref_t at(gates_node_t n, gates_u64 item) {
    return (gates_access_ref_t){ n, item };
}

static void test_navigation(void) {
    app_t a;
    make_app(&a);
    gates_tree_t *t = a.t;
    gates_node_t root = gates_tree_root(t);
    gates_access_ref_t R = at(root, 0);

    /* Children in order, parents, ends. */
    GT_ASSERT(ref_is(gates_access_first_child(t, R), a.form, 0));
    GT_ASSERT(ref_is(gates_access_last_child(t, R), a.plain, 0));
    GT_ASSERT(ref_is(gates_access_next(t, at(a.form, 0)), a.save, 0));
    GT_ASSERT(ref_is(gates_access_prev(t, at(a.save, 0)), a.form, 0));
    GT_ASSERT(ref_is(gates_access_prev(t, at(a.form, 0)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_next(t, at(a.plain, 0)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_parent(t, at(a.form, 0)), root, 0));
    GT_ASSERT(ref_is(gates_access_parent(t, R), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_first_child(t, at(a.save, 0)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_parent(t, at(GATES_NODE_NULL, 0)), GATES_NODE_NULL, 0));
    /* A field's editor sits a few levels below the root. */
    gates_access_ref_t up = at(a.theme, 0);
    int depth = 0;
    while (!gates_node_eq(up.node, GATES_NODE_NULL) && depth < 16) {
        up = gates_access_parent(t, up);
        depth++;
    }
    GT_ASSERT(depth >= 2 && depth < 16);

    /* Items: after the node's children, in order, parent = the node. */
    GT_ASSERT(ref_is(gates_access_first_child(t, at(a.theme, 0)), a.theme, 1));
    GT_ASSERT(ref_is(gates_access_last_child(t, at(a.theme, 0)), a.theme, 3));
    GT_ASSERT(ref_is(gates_access_next(t, at(a.theme, 1)), a.theme, 2));
    GT_ASSERT(ref_is(gates_access_prev(t, at(a.theme, 2)), a.theme, 1));
    GT_ASSERT(ref_is(gates_access_prev(t, at(a.theme, 1)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_next(t, at(a.theme, 3)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_parent(t, at(a.theme, 2)), a.theme, 0));
    GT_ASSERT(ref_is(gates_access_parent(t, at(a.theme, 9)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_first_child(t, at(a.lang, 0)), a.lang, 1));

    /* Hidden nodes are left out. */
    GT_ASSERT_OK(gates_node_set_hidden(t, a.status, true));
    layout(t);
    GT_ASSERT(ref_is(gates_access_next(t, at(a.save, 0)), a.bar, 0));
    GT_ASSERT(ref_is(gates_access_prev(t, at(a.bar, 0)), a.save, 0));
    GT_ASSERT_OK(gates_node_set_hidden(t, a.status, false));
    layout(t);
    GT_ASSERT(ref_is(gates_access_next(t, at(a.save, 0)), a.status, 0));

    /* Points: a node, a radio row, empty space. */
    GT_ASSERT(ref_is(gates_access_at_point(t, mid(info(t, a.plain, 0).bounds)), a.plain, 0));
    GT_ASSERT(ref_is(gates_access_at_point(t, mid(info(t, a.theme, 2).bounds)), a.theme, 2));
    GT_ASSERT(ref_is(gates_access_at_point(t, (gates_point_t){ 599, 499 }), root, 0));

    /* A choice's open list is not an element; its rows are the choice's items. */
    GT_ASSERT_OK(gates_access_expand(t, a.lang, 0, true));
    layout(t);
    GT_ASSERT(gates_tree_overlay_count(t) == 1);
    GT_ASSERT(ref_is(gates_access_last_child(t, R), a.plain, 0));
    GT_ASSERT(ref_is(gates_access_next(t, at(a.plain, 0)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_at_point(t, mid(info(t, a.lang, 2).bounds)), a.lang, 2));
    gates_node_t list = gates_hit_test(t, mid(info(t, a.lang, 2).bounds));
    GT_ASSERT(gates_node_kind(t, list) == GATES_NODE_MENU);
    GT_ASSERT(gates_access_item_count(t, list) == 0); /* its rows belong to the choice */
    GT_ASSERT_OK(gates_access_expand(t, a.lang, 0, false));
    layout(t);

    /* An open menu follows the root's children; its entries are its items. */
    gates_command_id_t ids[] = { 7 };
    gates_node_t menu;
    GT_ASSERT_OK(gates_menu_open(t, (gates_point_t){ 300, 300 }, root, ids, 1, &menu));
    layout(t);
    GT_ASSERT(ref_is(gates_access_last_child(t, R), menu, 0));
    GT_ASSERT(ref_is(gates_access_next(t, at(a.plain, 0)), menu, 0));
    GT_ASSERT(ref_is(gates_access_prev(t, at(menu, 0)), a.plain, 0));
    GT_ASSERT(ref_is(gates_access_next(t, at(menu, 0)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_parent(t, at(menu, 0)), root, 0));
    GT_ASSERT(ref_is(gates_access_first_child(t, at(menu, 0)), menu, 7));
    GT_ASSERT(ref_is(gates_access_parent(t, at(menu, 7)), menu, 0));
    GT_ASSERT(ref_is(gates_access_at_point(t, mid(info(t, menu, 7).bounds)), menu, 7));
    GT_ASSERT_OK(gates_menu_close(t, menu));
    layout(t);
    GT_ASSERT(ref_is(gates_access_last_child(t, R), a.plain, 0));

    /* Focus as an element: a radio group's selected option, a menu's or an
     * open list's highlighted entry. */
    gates_tree_set_focus(t, GATES_NODE_NULL);
    GT_ASSERT(ref_is(gates_access_focus_ref(t), GATES_NODE_NULL, 0));
    gates_tree_set_focus(t, a.save);
    GT_ASSERT(ref_is(gates_access_focus_ref(t), a.save, 0));
    gates_tree_set_focus(t, a.theme);
    GT_ASSERT(ref_is(gates_access_focus_ref(t), a.theme, 1));
    GT_ASSERT_OK(gates_access_select(t, a.theme, 2));
    GT_ASSERT(ref_is(gates_access_focus_ref(t), a.theme, 2));
    GT_ASSERT_OK(gates_access_expand(t, a.lang, 0, true));
    layout(t);
    GT_ASSERT(ref_is(gates_access_focus_ref(t), a.lang, 1)); /* opens on the current option */
    GT_ASSERT_OK(gates_access_expand(t, a.lang, 0, false));
    layout(t);
    GT_ASSERT_OK(gates_menu_open(t, (gates_point_t){ 300, 300 }, root, ids, 1, &menu));
    layout(t);
    GT_ASSERT(gates_node_eq(gates_access_focus_ref(t).node, menu));
    GT_ASSERT(gates_input_key(t, &(gates_key_event_t){ .key = GATES_KEY_DOWN, .down = true }));
    GT_ASSERT(ref_is(gates_access_focus_ref(t), menu, 7));
    GT_ASSERT_OK(gates_menu_close(t, menu));
    layout(t);

    /* A stale reference answers nothing. */
    gates_node_t gone = a.plain;
    GT_ASSERT_OK(gates_node_destroy(t, gone));
    (void)gates_tree_flush_destroys(t);
    GT_ASSERT(ref_is(gates_access_parent(t, at(gone, 0)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_next(t, at(gone, 0)), GATES_NODE_NULL, 0));
    GT_ASSERT(ref_is(gates_access_last_child(t, R), a.sep, 0));
    gates_tree_destroy(t);
}

/* A scroll area: rows outside its viewport are offscreen, focusing one
 * through the model scrolls it into view as Tab does. */
static void test_scrolled_out(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    gates_node_t sc = GATES_NODE_NULL, rows[30];
    GT_ASSERT_OK(gates_panel_create(t, root, &sc));
    GT_ASSERT_OK(gates_layout_set(t, sc, GATES_LAYOUT_KIND_SCROLL));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, sc, 1));
    for (int i = 0; i < 30; i++) {
        GT_ASSERT_OK(gates_checkbox_create(t, sc, GATES_STR("row"), false, nullptr, nullptr, &rows[i]));
    }
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 120 }, be));
    GT_ASSERT((info(t, rows[0], 0).states & GATES_ACCESS_OFFSCREEN) == 0);
    GT_ASSERT((info(t, rows[20], 0).states & GATES_ACCESS_OFFSCREEN) != 0);
    GT_ASSERT(ref_is(gates_access_next(t, at(rows[0], 0)), rows[1], 0)); /* still in the tree */
    GT_ASSERT_OK(gates_access_focus(t, rows[20], 0));
    GT_ASSERT(gates_layout_scroll_offset(t, sc) > 0);
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 120 }, be));
    GT_ASSERT((info(t, rows[20], 0).states & GATES_ACCESS_OFFSCREEN) == 0);
    GT_ASSERT((info(t, rows[0], 0).states & GATES_ACCESS_OFFSCREEN) != 0);
    gates_tree_destroy(t);
}

/* -- views (stage 2): rows as items ----------------------------------------------------------- */

typedef struct vmodel_t {
    gates_u64 n;
    gates_u64 cell_min, cell_max;    /* rows whose cells were read */
    int cells;
    bool tree;
    char buf[48];
} vmodel_t;

static gates_u64 vm_count(void *u) { return ((vmodel_t *)u)->n; }
static gates_item_id_t vm_id_at(void *u, gates_u64 row) { return row < ((vmodel_t *)u)->n ? row + 1 : 0; }
static bool vm_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    if (id == 0 || id > ((vmodel_t *)u)->n) return false;
    *row = id - 1;
    return true;
}
static gates_err_t vm_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    vmodel_t *m = u;
    m->cells++;
    if (id - 1 < m->cell_min) m->cell_min = id - 1;
    if (id - 1 > m->cell_max) m->cell_max = id - 1;
    int k = snprintf(m->buf, sizeof m->buf, "r%llu c%u", (unsigned long long)id, col);
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)m->buf, .size = (gates_usize_t)k };
    return GATES_OK;
}
static gates_err_t vm_info(void *u, gates_item_id_t id, gates_row_info_t *out) {
    (void)u;
    *out = (gates_row_info_t){ .expandable = id % 3 == 1, .expanded = id == 1,
                               .state = id == 5 ? GATES_ROW_LOADING : GATES_ROW_NORMAL };
    return GATES_OK;
}

typedef struct vrec_t {
    gates_event_kind_t kind[16];
    gates_u64 item[16];
    gates_u32 result[16];
    int n;
} vrec_t;

static void vrecord(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    vrec_t *r = user;
    if (r->n < 16) {
        r->kind[r->n] = ev->kind;
        r->item[r->n] = ev->item;
        r->result[r->n] = ev->result;
        r->n++;
    }
}

static gates_node_t make_view(gates_tree_t *t, vmodel_t *m, const gates_view_desc_t *d, vrec_t *rec) {
    gates_node_t root = gates_tree_root(t), v;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_view_create(t, root, d, &v));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, v, 1));
    GT_ASSERT_OK(gates_node_set_access_name(t, v, GATES_STR("Rows")));
    gates_rows_model_t model = { .user = m, .count = vm_count, .id_at = vm_id_at, .index_of = vm_index_of,
                                 .cell = vm_cell, .row_info = m->tree ? vm_info : nullptr };
    GT_ASSERT_OK(gates_view_set_model(t, v, &model));
    GT_ASSERT_OK(gates_widget_set_handler(t, v, vrecord, rec));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 200 }, be));
    return v;
}

static void test_view_items(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    vmodel_t m = { .n = 1000000, .cell_min = UINT64_MAX };
    vrec_t rec = {0};
    gates_node_t v = make_view(t, &m, &(gates_view_desc_t){0}, &rec);

    gates_access_info_t vi = info(t, v, 0);
    GT_ASSERT(vi.role == GATES_ROLE_LIST && (vi.actions & GATES_ACCESS_SCROLL) != 0);
    gates_u64 shown = gates_access_item_count(t, v);
    GT_ASSERT(shown > 3 && shown <= GATES_VIEW_MAX_ROWS);
    GT_ASSERT(gates_access_item_at(t, v, 0) == 1 && gates_access_item_at(t, v, shown - 1) == shown);
    GT_ASSERT(gates_access_item_at(t, v, shown) == 0);
    gates_access_info_t r2 = info(t, v, 2);
    GT_ASSERT(r2.role == GATES_ROLE_LIST_ITEM && str_is(r2.name, "r2 c0"));
    GT_ASSERT(r2.set_position == 2 && r2.set_size == 1000000);
    GT_ASSERT((r2.actions & (GATES_ACCESS_SELECT | GATES_ACCESS_INVOKE)) == (GATES_ACCESS_SELECT | GATES_ACCESS_INVOKE));
    GT_ASSERT((r2.states & (GATES_ACCESS_SELECTED | GATES_ACCESS_OFFSCREEN)) == 0);
    GT_ASSERT(!gates_rect_is_empty(r2.bounds));
    gates_access_info_t tmp;
    GT_ASSERT(gates_access_info(t, v, shown + 5, &tmp) == PROVEN_ERR_INVALID_ARG); /* not shown */
    /* The accessible tree: the shown rows under the view. */
    GT_ASSERT(ref_is(gates_access_first_child(t, at(v, 0)), v, 1));
    GT_ASSERT(ref_is(gates_access_next(t, at(v, 1)), v, 2));
    GT_ASSERT(ref_is(gates_access_last_child(t, at(v, 0)), v, shown));
    GT_ASSERT(ref_is(gates_access_at_point(t, mid(r2.bounds)), v, 2));

    /* Select and invoke go through the input paths. */
    GT_ASSERT_OK(gates_access_select(t, v, 3));
    (void)gates_tree_dispatch_events(t, 16);
    GT_ASSERT(rec.n == 1 && rec.kind[0] == GATES_EVENT_SELECTION_CHANGED);
    GT_ASSERT(gates_view_selected(t, v) == 3);
    GT_ASSERT((info(t, v, 3).states & GATES_ACCESS_SELECTED) != 0);
    gates_tree_set_focus(t, v);
    GT_ASSERT(ref_is(gates_access_focus_ref(t), v, 3));
    GT_ASSERT_OK(gates_access_invoke(t, v, 4));
    (void)gates_tree_dispatch_events(t, 16);
    GT_ASSERT(rec.n == 3 && rec.kind[1] == GATES_EVENT_SELECTION_CHANGED);
    GT_ASSERT(rec.kind[rec.n - 1] == GATES_EVENT_ACTIVATED && rec.item[rec.n - 1] == 4);
    GT_ASSERT(gates_access_select(t, v, 2000000) == PROVEN_ERR_INVALID_ARG);

    /* Scrolling: to the end, a page back; the selection stays an item when scrolled away. */
    gates_u32 pos = 1, page = 0;
    GT_ASSERT(gates_access_scroll_info(t, v, &pos, &page) && pos == 0 && page < 10);
    GT_ASSERT_OK(gates_access_scroll_to(t, v, GATES_ACCESS_SCROLL_MAX));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 300, 200 }, be));
    GT_ASSERT(gates_access_scroll_info(t, v, &pos, &page) && pos == GATES_ACCESS_SCROLL_MAX);
    GT_ASSERT(gates_access_item_at(t, v, gates_access_item_count(t, v) - 1) == 1000000);
    gates_access_info_t sel = info(t, v, 4);
    GT_ASSERT((sel.states & (GATES_ACCESS_SELECTED | GATES_ACCESS_OFFSCREEN)) ==
              (GATES_ACCESS_SELECTED | GATES_ACCESS_OFFSCREEN) && sel.name.size == 0);
    GT_ASSERT(ref_is(gates_access_next(t, at(v, 4)), GATES_NODE_NULL, 0)); /* not among the shown */
    GT_ASSERT(gates_access_info(t, v, 2, &tmp) == PROVEN_ERR_INVALID_ARG);  /* scrolled away */
    GT_ASSERT_OK(gates_access_scroll_by(t, v, -1, true));
    GT_ASSERT(gates_view_first_row(t, v) < 1000000 - shown);
    GT_ASSERT_OK(gates_access_scroll_to(t, v, 0));
    GT_ASSERT(gates_view_first_row(t, v) == 0);
    /* Cells were read for shown rows only (the top rows and the last page). */
    GT_ASSERT(m.cells > 0 && m.cell_max < 1000000);
    GT_ASSERT(m.cell_max < shown || m.cell_max >= 1000000 - shown - 1);
    gates_tree_destroy(t);
}

static void test_view_table_tree(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    vmodel_t m = { .n = 50, .cell_min = UINT64_MAX };
    vrec_t rec = {0};
    static const gates_column_desc_t cols[] = {
        { .id = 7, .label = GATES_STR_INIT("Name"), .width = 80 },
        { .id = 9, .label = GATES_STR_INIT("Size"), .width = 60 },
    };
    gates_node_t v = make_view(t, &m, &(gates_view_desc_t){ .columns = cols, .column_count = 2, .header = true }, &rec);
    gates_access_info_t vi = info(t, v, 0);
    GT_ASSERT(vi.role == GATES_ROLE_TABLE && vi.column_count == 2);
    gates_access_info_t row = info(t, v, 1);
    GT_ASSERT(row.role == GATES_ROLE_ROW && str_is(row.name, "r1 c7, r1 c9") && row.column_count == 2);
    gates_tree_destroy(t);

    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    vmodel_t tm = { .n = 50, .cell_min = UINT64_MAX, .tree = true };
    vrec_t trec = {0};
    v = make_view(t, &tm, &(gates_view_desc_t){ .tree = true }, &trec);
    GT_ASSERT(info(t, v, 0).role == GATES_ROLE_TREE);
    gates_access_info_t r1 = info(t, v, 1), r4 = info(t, v, 4), r5 = info(t, v, 5), r2 = info(t, v, 2);
    (void)r1;
    GT_ASSERT(info(t, v, 1).role == GATES_ROLE_TREE_ITEM && info(t, v, 1).level == 1);
    GT_ASSERT((info(t, v, 1).states & (GATES_ACCESS_EXPANDABLE | GATES_ACCESS_EXPANDED)) ==
              (GATES_ACCESS_EXPANDABLE | GATES_ACCESS_EXPANDED));
    GT_ASSERT((r4.states & (GATES_ACCESS_EXPANDABLE | GATES_ACCESS_EXPANDED)) == GATES_ACCESS_EXPANDABLE);
    GT_ASSERT((r4.actions & GATES_ACCESS_EXPAND) != 0 && (r2.actions & GATES_ACCESS_EXPAND) == 0);
    GT_ASSERT((r5.states & GATES_ACCESS_BUSY) != 0);
    /* Expanding is a request to the model. */
    GT_ASSERT_OK(gates_access_expand(t, v, 4, true));
    GT_ASSERT_OK(gates_access_expand(t, v, 1, true)); /* already open: nothing to ask */
    GT_ASSERT(gates_access_expand(t, v, 2, true) == PROVEN_ERR_INVALID_ARG);
    (void)gates_tree_dispatch_events(t, 16);
    GT_ASSERT(trec.n == 1 && trec.kind[0] == GATES_EVENT_EXPAND_REQUESTED && trec.item[0] == 4 && trec.result[0] == 1);
    GT_ASSERT_OK(gates_access_expand(t, v, 1, false));
    (void)gates_tree_dispatch_events(t, 16);
    GT_ASSERT(trec.n == 2 && trec.item[1] == 1 && trec.result[1] == 0);
    gates_tree_destroy(t);

    /* A view whose rows all fit does not scroll. */
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    vmodel_t few = { .n = 3, .cell_min = UINT64_MAX };
    vrec_t frec = {0};
    v = make_view(t, &few, &(gates_view_desc_t){0}, &frec);
    gates_u32 fp = 7, fg = 7;
    GT_ASSERT(!gates_access_scroll_info(t, v, &fp, &fg) && fp == 0 && fg == 0);
    GT_ASSERT((info(t, v, 0).actions & GATES_ACCESS_SCROLL) == 0);
    GT_ASSERT(gates_access_item_count(t, v) == 3);
    gates_tree_destroy(t);
}

/* A scroll area scrolls through the model too. */
static void test_area_scroll(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), sc, n;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_panel_create(t, root, &sc));
    GT_ASSERT_OK(gates_layout_set(t, sc, GATES_LAYOUT_KIND_SCROLL));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, sc, 1));
    for (int i = 0; i < 30; i++) GT_ASSERT_OK(gates_checkbox_create(t, sc, GATES_STR("row"), false, nullptr, nullptr, &n));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 120 }, be));
    gates_u32 pos = 1, page = 0;
    GT_ASSERT(gates_access_scroll_info(t, sc, &pos, &page) && pos == 0 && page > 0 && page < GATES_ACCESS_SCROLL_MAX);
    GT_ASSERT((info(t, sc, 0).actions & GATES_ACCESS_SCROLL) != 0);
    GT_ASSERT_OK(gates_access_scroll_by(t, sc, 1, false));
    gates_i32 line_step = gates_layout_scroll_offset(t, sc);
    GT_ASSERT(line_step > 0);
    GT_ASSERT_OK(gates_access_scroll_by(t, sc, 1, true));
    GT_ASSERT(gates_layout_scroll_offset(t, sc) - line_step > 2 * line_step); /* a page is many lines */
    GT_ASSERT_OK(gates_access_scroll_to(t, sc, GATES_ACCESS_SCROLL_MAX));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 120 }, be));
    GT_ASSERT(gates_access_scroll_info(t, sc, &pos, &page) && pos == GATES_ACCESS_SCROLL_MAX);
    GT_ASSERT(!gates_access_scroll_info(t, root, &pos, &page) && pos == 0);
    GT_ASSERT(gates_access_scroll_to(t, root, 0) == PROVEN_ERR_INVALID_ARG);
    gates_tree_destroy(t);
    /* A scroll area whose content fits does not scroll. */
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    root = gates_tree_root(t);
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_panel_create(t, root, &sc));
    GT_ASSERT_OK(gates_layout_set(t, sc, GATES_LAYOUT_KIND_SCROLL));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, sc, 1));
    GT_ASSERT_OK(gates_checkbox_create(t, sc, GATES_STR("one"), false, nullptr, nullptr, &n));
    GT_ASSERT_OK(gates_layout_run(t, (gates_size_t){ 200, 120 }, be));
    GT_ASSERT(!gates_access_scroll_info(t, sc, &pos, &page));
    GT_ASSERT(gates_access_scroll_by(t, sc, 1, true) == PROVEN_ERR_INVALID_ARG);
    gates_tree_destroy(t);
}

/* -- text of an edit (stage 2): rectangles, points, selection ------------------------------------ */

static void test_text(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), box, pw, off;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("ab\xed\x95\x9c" "c"), 20, &box)); /* a b (wide) c */
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("secret"), 20, &pw));
    GT_ASSERT_OK(gates_textbox_set_password(t, pw, true));
    GT_ASSERT_OK(gates_textbox_create(t, root, GATES_STR("off"), 20, &off));
    GT_ASSERT_OK(gates_widget_set_disabled(t, off, true));
    layout(t);
    gates_rect_t a, b, w, e;
    GT_ASSERT(gates_access_text_rect(t, box, 0, 1, &a));
    GT_ASSERT(gates_access_text_rect(t, box, 1, 2, &b));
    GT_ASSERT(gates_access_text_rect(t, box, 2, 5, &w));       /* the wide character: two cells */
    GT_ASSERT(a.w > 0 && b.x == a.x + a.w && w.x == b.x + b.w && w.w == 2 * a.w && a.y == w.y);
    GT_ASSERT(gates_access_text_rect(t, box, 6, 6, &e) && e.w == 0 && e.x == w.x + w.w + a.w); /* the end */
    GT_ASSERT(gates_access_text_rect(t, box, 0, 99, &e) && e.w == 5 * a.w);  /* clamped to the text */
    gates_rect_t inner = info(t, box, 0).bounds;
    GT_ASSERT(a.x >= inner.x && a.x + a.w <= inner.x + inner.w);
    /* Offsets from points: nearest boundary, never inside a character. */
    GT_ASSERT(gates_access_text_offset_at(t, box, (gates_point_t){ a.x + 1, a.y }) == 0);
    GT_ASSERT(gates_access_text_offset_at(t, box, (gates_point_t){ b.x + b.w - 1, a.y }) == 2);
    gates_u32 inside = gates_access_text_offset_at(t, box, (gates_point_t){ w.x + w.w / 2, a.y });
    GT_ASSERT(inside == 2 || inside == 5);
    GT_ASSERT(gates_access_text_offset_at(t, box, (gates_point_t){ inner.x + inner.w - 1, a.y }) == 6);
    /* Selection through the model. */
    GT_ASSERT_OK(gates_access_select_text(t, box, 1, 5));
    gates_u32 an = 0, ca = 0;
    GT_ASSERT_OK(gates_textbox_selection(t, box, &an, &ca));
    GT_ASSERT(an == 1 && ca == 5);
    gates_access_info_t bi = info(t, box, 0);
    GT_ASSERT(bi.anchor == 1 && bi.caret == 5);
    GT_ASSERT(gates_access_select_text(t, box, 3, 5) == PROVEN_ERR_INVALID_ARG); /* inside a character */
    /* Passwords answer nothing; a disabled box refuses. */
    GT_ASSERT(!gates_access_text_rect(t, pw, 0, 1, &e));
    GT_ASSERT(gates_access_select_text(t, pw, 0, 1) == PROVEN_ERR_INVALID_ARG);
    GT_ASSERT(gates_access_select_text(t, off, 0, 1) == PROVEN_ERR_INVALID_STATE);
    GT_ASSERT(!gates_access_text_rect(t, root, 0, 1, &e));
    gates_tree_destroy(t);
}

/* Long strings: the scratch grows while one info is filled; every string must
 * still point at live bytes (ASan catches a stale one). */
static void test_long_strings(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), form, box;
    GT_ASSERT_OK(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    GT_ASSERT_OK(gates_form_create(t, root, &form));
    static char name[2001], help[2001], text[2001];
    memset(name, 'n', 2000);
    memset(help, 'h', 2000);
    memset(text, 't', 2000);
    gates_field_desc_t d = { .label = GATES_STR("Field"), .help = (gates_str_t){ .ptr = (const gates_u8 *)help, .size = 2000 },
                             .text = (gates_str_t){ .ptr = (const gates_u8 *)text, .size = 2000 } };
    GT_ASSERT_OK(gates_form_add_text(t, form, 31, &d, &box));
    GT_ASSERT_OK(gates_node_set_access_name(t, box, (gates_str_t){ .ptr = (const gates_u8 *)name, .size = 2000 }));
    GT_ASSERT_OK(gates_node_set_automation_id(t, box, GATES_STR("long-box")));
    layout(t);
    gates_access_info_t i = info(t, box, 0);
    GT_ASSERT(i.name.size == 2000 && memcmp(i.name.ptr, name, 2000) == 0);
    GT_ASSERT(i.description.size == 2000 && memcmp(i.description.ptr, help, 2000) == 0);
    GT_ASSERT(i.value.size == 2000 && memcmp(i.value.ptr, text, 2000) == 0);
    GT_ASSERT(str_is(i.automation_id, "long-box"));
    gates_tree_destroy(t);
}

/* A live label announces a change of its text, not the same text again. */
static void test_live_same_text(void) {
    gates_tree_t *t = nullptr;
    GT_ASSERT_OK(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t status;
    GT_ASSERT_OK(gates_label_create(t, gates_tree_root(t), GATES_STR("12"), &status));
    GT_ASSERT_OK(gates_node_set_live(t, status, GATES_LIVE_POLITE));
    gates_access_enable(t, true);
    gates_access_change_t ch[16];
    (void)gates_access_take_changes(t, ch, 16, nullptr);
    GT_ASSERT_OK(gates_widget_set_text(t, status, GATES_STR("12")));
    gates_u32 n = gates_access_take_changes(t, ch, 16, nullptr);
    GT_ASSERT(n == 0);
    GT_ASSERT_OK(gates_widget_set_text(t, status, GATES_STR("42")));
    n = gates_access_take_changes(t, ch, 16, nullptr);
    bool live = false;
    for (gates_u32 k = 0; k < n; k++) live = live || ch[k].kind == GATES_ACCESS_LIVE;
    GT_ASSERT(live);
    gates_tree_destroy(t);
}

/* -- the enforced rules --------------------------------------------------------------------------- */

static bool found(const gates_access_issue_t *is, gates_u32 n, gates_access_rule_t r, gates_node_t node) {
    for (gates_u32 i = 0; i < n && i < 64; i++) if (is[i].rule == r && gates_node_eq(is[i].node, node)) return true;
    return false;
}

static void test_audit(void) {
    gates_access_issue_t is[64];
    GT_ASSERT(gates_theme_audit(gates_theme_light(), is, 64) == 0);
    GT_ASSERT(gates_theme_audit(gates_theme_dark(), is, 64) == 0);
    gates_theme_t hc;
    gates_theme_high_contrast(nullptr, &hc);
    GT_ASSERT(gates_theme_audit(&hc, is, 64) == 0);
    gates_theme_t weak = *gates_theme_light();
    weak.colors[GATES_COLOR_CONTROL_DISABLED_FG] = (gates_color_t){ 220, 220, 220, 255 };
    weak.focus_width = 1;
    gates_u32 n = gates_theme_audit(&weak, is, 64);
    GT_ASSERT(n == 3 && is[0].rule == GATES_RULE_CONTRAST && is[2].rule == GATES_RULE_FOCUS_CUE);

    /* A well-made form passes. */
    app_t a;
    make_app(&a);
    gates_tree_t *t = a.t;
    n = gates_access_audit(t, gates_theme_light(), is, 64);
    GT_ASSERT(n == 0);
    /* Each rule, one at a time. */
    gates_node_t root = gates_tree_root(t);
    gates_node_t anon, tiny, hidden_btn, twin1, twin2, lonely;
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR(""), nullptr, nullptr, &anon));
    GT_ASSERT_OK(gates_panel_create(t, root, &hidden_btn)); /* holder for an absolute layout */
    GT_ASSERT_OK(gates_layout_set(t, hidden_btn, GATES_LAYOUT_KIND_ABSOLUTE));
    GT_ASSERT_OK(gates_layout_set_child_grow(t, hidden_btn, 1));
    GT_ASSERT_OK(gates_button_create(t, hidden_btn, GATES_STR("x"), nullptr, nullptr, &tiny));
    GT_ASSERT_OK(gates_layout_set_abs_rect(t, tiny, (gates_rect_t){ 0, 0, 12, 12 }));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("twin"), nullptr, nullptr, &twin1));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("twin"), nullptr, nullptr, &twin2));
    GT_ASSERT_OK(gates_node_set_automation_id(t, twin1, GATES_STR("same")));
    GT_ASSERT_OK(gates_node_set_automation_id(t, twin2, GATES_STR("same")));
    GT_ASSERT_OK(gates_button_create(t, root, GATES_STR("mouse only"), nullptr, nullptr, &lonely));
    GT_ASSERT_OK(gates_widget_set_focusable(t, lonely, false));
    gates_node_t anon_radio;
    GT_ASSERT_OK(gates_radio_create(t, root, langs, 2, 1, &anon_radio));
    layout(t);
    n = gates_access_audit(t, gates_theme_light(), is, 64);
    GT_ASSERT(found(is, n, GATES_RULE_NO_NAME, anon));
    GT_ASSERT(found(is, n, GATES_RULE_NO_NAME, anon_radio));
    GT_ASSERT(found(is, n, GATES_RULE_TARGET_SIZE, tiny));
    GT_ASSERT(found(is, n, GATES_RULE_DUPLICATE_ID, twin2) && !found(is, n, GATES_RULE_DUPLICATE_ID, twin1));
    GT_ASSERT(found(is, n, GATES_RULE_KEYBOARD, lonely));
    GT_ASSERT(n == 5);
    /* Fixing them clears the report. */
    GT_ASSERT_OK(gates_node_set_access_name(t, anon, GATES_STR("Help")));
    GT_ASSERT_OK(gates_node_set_access_name(t, anon_radio, GATES_STR("Language")));
    GT_ASSERT_OK(gates_layout_set_abs_rect(t, tiny, (gates_rect_t){ 0, 0, 24, 24 }));
    GT_ASSERT_OK(gates_node_set_automation_id(t, twin2, GATES_STR("other")));
    GT_ASSERT_OK(gates_widget_set_focusable(t, lonely, true));
    layout(t);
    GT_ASSERT(gates_access_audit(t, gates_theme_light(), is, 64) == 0);
    /* Default ids collide too: two forms with the same field id. */
    gates_node_t form2, dup;
    GT_ASSERT_OK(gates_form_create(t, root, &form2));
    gates_field_desc_t d = { .label = GATES_STR("Other name") };
    GT_ASSERT_OK(gates_form_add_text(t, form2, 11, &d, &dup));
    layout(t);
    n = gates_access_audit(t, nullptr, is, 64);
    GT_ASSERT(n == 1 && found(is, n, GATES_RULE_DUPLICATE_ID, dup));
    gates_tree_destroy(t);
}

int main(void) {
    be = gates_text_backend_builtin();
    test_names();
    test_states_items();
    test_actions();
    test_changes();
    test_navigation();
    test_scrolled_out();
    test_view_items();
    test_view_table_tree();
    test_area_scroll();
    test_text();
    test_long_strings();
    test_live_same_text();
    test_audit();
    return gt_report("test_access");
}
