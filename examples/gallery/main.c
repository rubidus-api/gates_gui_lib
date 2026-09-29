/* gallery - every control of gates in one window (sample application, 0.3.0).
 *
 * Shows: the application frame - a menu bar (File, View, Help) with mnemonics,
 * a toolbar of the same commands, tabs with one page per control family, a
 * status bar with a clock, tooltips everywhere - and the controls on the pages:
 * buttons and check boxes, text boxes with labels that carry access keys,
 * radio groups and a choice, a table in a split, progress and separators, a
 * dialog, and (0.4.0) spin boxes, a slider, a collapsible group in a grid,
 * wrapped chips and a total recomputed once per turn. The theme (system, light, dark) and the zoom are commands. The
 * arrangement - selected tab, split, table columns, window placement - is kept
 * in %LOCALAPPDATA%\gates-gallery.ini and comes back at the next start.
 *
 * Use: Alt (or F10) enters the menu bar; Alt+letter opens a menu or activates a
 * control; Ctrl+Tab changes pages; rest the pointer on a control for its
 * tooltip. Ctrl+Q quits.
 *
 * Check: Alt shows the underlines; F10, Right, Down opens View; Ctrl+Tab moves
 * through the pages and the focus follows; narrowing the window moves toolbar
 * buttons into the ">>" menu; restarting restores the page, split and size. */
#include <gates/gates.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum {
    CMD_SAVE = 1, CMD_QUIT, CMD_SYSTEM, CMD_LIGHT, CMD_DARK, CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ZOOM_RESET,
    CMD_ABOUT, CMD_ABOUT_OK, CMD_STEP,
};

#define ROWS 200

typedef struct app_t {
    gates_app_t *app;
    gates_window_t *win;
    gates_tree_t *tree;
    gates_node_t root, tabs, split, table, clock, zoom_seg, status, progress, about;
    gates_node_t qty, price, volume, total;
    char path[512];
    char cell[64];
} app_t;

static gates_str_t cs(const char *s) { return (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) }; }

/* -- the table's model: generated rows ------------------------------------------------ */

static gates_u64 m_count(void *u) { (void)u; return ROWS; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) { (void)u; return row < ROWS ? row + 1 : 0; }
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    (void)u;
    if (id == 0 || id > ROWS) return false;
    *row = id - 1;
    return true;
}
static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    app_t *a = u;
    int n = col == 1 ? snprintf(a->cell, sizeof a->cell, "item-%03llu", (unsigned long long)id)
                     : snprintf(a->cell, sizeof a->cell, "%llu KB", (unsigned long long)(id * 37 % 1000));
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)a->cell, .size = (gates_usize_t)n };
    return GATES_OK;
}

/* -- state kept between runs ---------------------------------------------------------- */

static void save_state(app_t *a) {
    if (a->path[0] == 0) return;
    gates_u8 buf[2048];
    gates_usize_t n = 0, pn = 0;
    if (!gates_is_ok(gates_state_save(a->tree, buf, sizeof buf - 128, &n))) return;
    const char *key = "window ";
    memcpy(buf + n, key, strlen(key));
    if (gates_is_ok(gates_window_placement(a->win, buf + n + strlen(key), 96, &pn))) {
        n += strlen(key) + pn;
        buf[n++] = '\n';
    }
    FILE *f = fopen(a->path, "wb");
    if (f == nullptr) return;
    (void)fwrite(buf, 1, n, f);
    fclose(f);
    (void)gates_widget_set_text(a->tree, a->status, cs("Arrangement saved"));
}

static void load_state(app_t *a) {
    const char *dir = getenv("LOCALAPPDATA");
    if (dir == nullptr || strlen(dir) + 24 >= sizeof a->path) return;
    snprintf(a->path, sizeof a->path, "%s\\gates-gallery.ini", dir);
    FILE *f = fopen(a->path, "rb");
    if (f == nullptr) return;
    static gates_u8 buf[4096];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    (void)gates_state_load(a->tree, (gates_str_t){ .ptr = buf, .size = n }, nullptr);
    for (size_t i = 0; i + 7 < n; i++) { /* the window line is the program's own */
        if ((i == 0 || buf[i - 1] == '\n') && memcmp(buf + i, "window ", 7) == 0) {
            size_t e = i + 7;
            while (e < n && buf[e] != '\n') e++;
            (void)gates_window_set_placement(a->win, (gates_str_t){ .ptr = buf + i + 7, .size = e - i - 7 });
        }
    }
}

/* -- commands -------------------------------------------------------------------------- */

static void show_zoom(app_t *a) {
    char z[32];
    snprintf(z, sizeof z, "Zoom %u%%", gates_window_zoom(a->win));
    (void)gates_widget_set_text(a->tree, a->zoom_seg, cs(z));
}

static void set_theme(app_t *a, gates_command_id_t id) {
    gates_window_set_theme_mode(a->win, id == CMD_LIGHT ? GATES_THEME_LIGHT : id == CMD_DARK ? GATES_THEME_DARK
                                                                                             : GATES_THEME_SYSTEM);
    for (gates_command_id_t c = CMD_SYSTEM; c <= CMD_DARK; c++) {
        (void)gates_command_set_checked(a->tree, a->root, c, c == id);
    }
}

static gates_err_t open_about(app_t *a);

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    switch (id) {
    case CMD_SAVE: save_state(a); break;
    case CMD_QUIT: save_state(a); gates_app_quit(a->app); break;
    case CMD_SYSTEM: case CMD_LIGHT: case CMD_DARK: set_theme(a, id); break;
    case CMD_ZOOM_IN: gates_window_set_zoom(a->win, gates_window_zoom(a->win) + 25); show_zoom(a); break;
    case CMD_ZOOM_OUT: gates_window_set_zoom(a->win, gates_window_zoom(a->win) - 25); show_zoom(a); break;
    case CMD_ZOOM_RESET: gates_window_set_zoom(a->win, 100); show_zoom(a); break;
    case CMD_ABOUT: (void)open_about(a); break;
    case CMD_ABOUT_OK: (void)gates_dialog_close(tree, a->about, GATES_DIALOG_ACCEPTED); break;
    case CMD_STEP: {
        gates_i32 v = gates_progress_value(tree, a->progress) + 100;
        (void)gates_progress_set_value(tree, a->progress, v > 1000 ? 0 : v);
        break;
    }
    default: break;
    }
}

static gates_err_t open_about(app_t *a) {
    gates_node_t content, text, ok;
    TRY(gates_dialog_open(a->tree, &(gates_dialog_desc_t){ .title = cs("About the gallery") }, &a->about, &content));
    char line[96];
    snprintf(line, sizeof line, "gates %s - a small retained GUI library in C23.", gates_version_string());
    TRY(gates_label_create(a->tree, content, cs(line), &text));
    gates_command_desc_t okc = { .id = CMD_ABOUT_OK, .label = cs("OK"), .role = GATES_COMMAND_DEFAULT,
                                 .enabled = true, .invoke = on_command, .user = a };
    TRY(gates_command_register(a->tree, a->about, &okc));
    TRY(gates_button_create(a->tree, content, cs(""), nullptr, nullptr, &ok));
    TRY(gates_button_set_command(a->tree, ok, a->about, CMD_ABOUT_OK));
    return GATES_OK;
}

static void on_tick(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)node;
    (void)id;
    app_t *a = user;
    time_t now = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &now);
    char buf[16];
    strftime(buf, sizeof buf, "%H:%M:%S", &tm);
    (void)gates_widget_set_text(tree, a->clock, cs(buf));
}

/* -- building -------------------------------------------------------------------------- */

static gates_err_t tip(app_t *a, gates_node_t n, const char *text) {
    return gates_node_set_tooltip(a->tree, n, cs(text));
}

static gates_err_t page_buttons(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    gates_node_t row, b, c;
    TRY(gates_panel_create(t, page, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_button_create(t, row, cs("&Step progress"), nullptr, nullptr, &b));
    TRY(gates_button_set_command(t, b, a->root, CMD_STEP));
    TRY(tip(a, b, "Moves the bar on the Progress page by a tenth"));
    TRY(gates_button_create(t, row, cs("&About..."), nullptr, nullptr, &b));
    TRY(gates_button_set_command(t, b, a->root, CMD_ABOUT));
    TRY(gates_button_create(t, row, cs("Disabled"), nullptr, nullptr, &b));
    TRY(gates_widget_set_disabled(t, b, true));
    TRY(gates_checkbox_create(t, page, cs("W&rap long lines"), true, nullptr, nullptr, &c));
    TRY(tip(a, c, "A check box: Space or Alt+R toggles it"));
    TRY(gates_checkbox_create(t, page, cs("Show hi&dden files"), false, nullptr, nullptr, &c));
    TRY(gates_label_create(t, page, cs("Buttons with a command follow it: disable the command, and every button and "
                                       "menu entry for it is disabled."), &c));
    return GATES_OK;
}

static gates_err_t page_text(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    gates_node_t l, box;
    static const struct { const char *label, *text, *tip; bool password; } fields[] = {
        { "&Name", "Ada", "A text box; Alt+N comes here from its label", false },
        { "&Email", "", "Typed with the keyboard or an input method", false },
        { "&Password", "secret", "Shown as stars, never copied", true },
    };
    for (int i = 0; i < 3; i++) {
        TRY(gates_label_create(t, page, cs(fields[i].label), &l));
        TRY(gates_textbox_create(t, page, cs(fields[i].text), 30, &box));
        TRY(gates_label_set_target(t, l, box));
        TRY(gates_node_set_labelled_by(t, box, l));
        TRY(gates_layout_set_child_align(t, box, GATES_ALIGN_START_V));
        TRY(tip(a, box, fields[i].tip));
        if (fields[i].password) TRY(gates_textbox_set_password(t, box, true));
    }
    return GATES_OK;
}

static gates_err_t page_options(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    gates_node_t l, r, c;
    static const gates_option_t sizes[] = { { .id = 1, .label = GATES_STR_INIT("Small") },
                                            { .id = 2, .label = GATES_STR_INIT("Medium") },
                                            { .id = 3, .label = GATES_STR_INIT("Large") } };
    static const gates_option_t colours[] = { { .id = 1, .label = GATES_STR_INIT("Red") },
                                              { .id = 2, .label = GATES_STR_INIT("Green") },
                                              { .id = 3, .label = GATES_STR_INIT("Blue") },
                                              { .id = 4, .label = GATES_STR_INIT("Grey"), .disabled = true } };
    TRY(gates_label_create(t, page, cs("&Size"), &l));
    TRY(gates_radio_create(t, page, sizes, 3, 2, &r));
    TRY(gates_label_set_target(t, l, r));
    TRY(gates_node_set_labelled_by(t, r, l));
    TRY(tip(a, r, "A radio group: arrows move and choose"));
    TRY(gates_label_create(t, page, cs("&Colour"), &l));
    TRY(gates_choice_create(t, page, colours, 4, 3, &c));
    TRY(gates_label_set_target(t, l, c));
    TRY(gates_node_set_labelled_by(t, c, l));
    TRY(gates_layout_set_child_align(t, c, GATES_ALIGN_START_V));
    TRY(tip(a, c, "A choice: Alt+Down or Space opens the list"));
    return GATES_OK;
}

static gates_err_t page_views(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    gates_node_t left, info;
    TRY(gates_layout_set_child_grow(t, page, 1));
    TRY(gates_panel_create(t, page, &a->split));
    TRY(gates_layout_set(t, a->split, GATES_LAYOUT_KIND_SPLIT));
    TRY(gates_layout_set_split(t, a->split, GATES_SPLIT_HORIZONTAL, 650));
    TRY(gates_layout_set_child_grow(t, a->split, 1));
    TRY(gates_node_set_automation_id(t, a->split, cs("views.split")));
    static const gates_column_desc_t cols[] = { { .id = 1, .label = GATES_STR_INIT("Name"), .width = 120 },
                                                { .id = 2, .label = GATES_STR_INIT("Size"), .width = 80 } };
    TRY(gates_view_create(t, a->split, &(gates_view_desc_t){ .columns = cols, .column_count = 2, .header = true },
                          &a->table));
    gates_rows_model_t model = { .user = a, .count = m_count, .id_at = m_id_at, .index_of = m_index_of,
                                 .cell = m_cell };
    TRY(gates_view_set_model(t, a->table, &model));
    TRY(gates_node_set_access_name(t, a->table, cs("Items")));
    TRY(gates_node_set_automation_id(t, a->table, cs("views.table")));
    TRY(tip(a, a->table, "A table: drag a header edge to resize a column"));
    TRY(gates_panel_create(t, a->split, &left));
    TRY(gates_layout_set(t, left, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, left, 8));
    TRY(gates_label_create(t, left, cs("Drag the handle; the split, the column widths and the page are kept for the "
                                       "next start."), &info));
    return GATES_OK;
}

/* Inputs (0.4.0): one bubble handler hears every control of the page; the total
 * is recomputed once after however many changes a turn brings. */
static void recompute(gates_tree_t *tree, gates_u32 key, void *user) {
    (void)key;
    app_t *a = user;
    char p[32], tot[32], line[96];
    gates_i64 q = gates_range_value(tree, a->qty), pr = gates_range_value(tree, a->price);
    (void)gates_range_format(pr, 100, p, sizeof p);
    (void)gates_range_format(q * pr, 100, tot, sizeof tot);
    snprintf(line, sizeof line, "%lld x %s = %s (volume %lld)", (long long)q, p, tot,
             (long long)gates_range_value(tree, a->volume));
    (void)gates_widget_set_text(tree, a->total, cs(line));
}

static void on_inputs(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    if (ev->kind == GATES_EVENT_VALUE_CHANGED) (void)gates_tree_defer(tree, 1, recompute, user);
}

static gates_err_t page_inputs(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    gates_node_t grid, l, group, content, adv, chips, chip;
    TRY(gates_node_set_bubble_handler(t, page, on_inputs, a));
    TRY(gates_panel_create(t, page, &grid));
    TRY(gates_layout_set(t, grid, GATES_LAYOUT_KIND_GRID));
    TRY(gates_layout_set_gap(t, grid, 8));
    TRY(gates_layout_set_grid_column_grow(t, grid, 1, 1));
    TRY(gates_label_create(t, grid, cs("&Quantity"), &l));
    TRY(gates_spin_create(t, grid, &(gates_range_t){ .min = 1, .max = 999, .value = 3 }, &a->qty));
    TRY(gates_label_set_target(t, l, a->qty));
    TRY(gates_node_set_labelled_by(t, a->qty, l));
    TRY(gates_layout_set_child_align(t, a->qty, GATES_ALIGN_START_V));
    TRY(gates_label_create(t, grid, cs("P&rice"), &l));
    TRY(gates_spin_create(t, grid, &(gates_range_t){ .min = 0, .max = 100000, .step = 5, .page = 100, .value = 1250,
                                                     .scale = 100 }, &a->price));
    TRY(gates_label_set_target(t, l, a->price));
    TRY(gates_node_set_labelled_by(t, a->price, l));
    TRY(gates_layout_set_child_align(t, a->price, GATES_ALIGN_START_V));
    TRY(gates_label_create(t, grid, cs("Vo&lume"), &l));
    TRY(gates_slider_create(t, grid, &(gates_range_t){ .min = 0, .max = 100, .step = 5, .page = 20, .value = 40 },
                            false, &a->volume));
    TRY(gates_slider_set_ticks(t, a->volume, 2));
    TRY(gates_label_set_target(t, l, a->volume));
    TRY(gates_node_set_labelled_by(t, a->volume, l));
    TRY(tip(a, a->volume, "A slider: arrows step, PgUp/PgDn page, drag the thumb"));
    TRY(gates_label_create(t, page, cs(""), &a->total));
    TRY(gates_node_set_live(t, a->total, GATES_LIVE_POLITE));
    TRY(gates_group_create(t, page, cs("&More settings"), true, &group, &content));
    TRY(gates_checkbox_create(t, content, cs("Round to whole units"), false, nullptr, nullptr, &adv));
    TRY(gates_checkbox_create(t, content, cs("Keep a log"), true, nullptr, nullptr, &adv));
    TRY(gates_label_create(t, page, cs("Tags (a wrap layout: narrow the window)"), &l));
    TRY(gates_panel_create(t, page, &chips));
    TRY(gates_layout_set(t, chips, GATES_LAYOUT_KIND_WRAP));
    TRY(gates_layout_set_gap(t, chips, 6));
    static const char *tags[] = { "red", "green", "blue", "urgent", "later", "someday", "home", "work", "travel" };
    for (size_t i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        TRY(gates_button_create(t, chips, cs(tags[i]), nullptr, nullptr, &chip));
    }
    recompute(t, 1, a);
    return GATES_OK;
}

static gates_err_t page_progress(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    gates_node_t l, sep;
    TRY(gates_label_create(t, page, cs("Progress (Step progress on the Buttons page, or the toolbar)"), &l));
    TRY(gates_progress_create(t, page, 300, &a->progress));
    TRY(tip(a, a->progress, "Progress in tenths"));
    TRY(gates_separator_create(t, page, &sep));
    TRY(gates_label_create(t, page, cs("A separator above; the status bar below keeps the time."), &l));
    return GATES_OK;
}

static gates_err_t build(app_t *a) {
    gates_tree_t *t = a->tree;
    gates_node_t root = a->root = gates_tree_root(t), bar, tools, sb, page;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    static const struct { gates_command_id_t id; const char *label; gates_shortcut_t k; } cmds[] = {
        { CMD_SAVE, "&Save arrangement", { .letter = 'S', .ctrl = true } },
        { CMD_QUIT, "E&xit", { .letter = 'Q', .ctrl = true } },
        { CMD_SYSTEM, "&System theme", {0} },
        { CMD_LIGHT, "&Light", {0} },
        { CMD_DARK, "&Dark", {0} },
        { CMD_ZOOM_IN, "Zoom &in", { .key = GATES_KEY_F7 } },
        { CMD_ZOOM_OUT, "Zoom &out", { .key = GATES_KEY_F6 } },
        { CMD_ZOOM_RESET, "&Reset zoom", { .letter = '0', .ctrl = true } },
        { CMD_ABOUT, "&About", { .key = GATES_KEY_F1 } },
        { CMD_STEP, "Ste&p", { .letter = 'P', .ctrl = true } },
    };
    for (size_t i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        gates_command_desc_t d = { .id = cmds[i].id, .label = cs(cmds[i].label), .shortcut = cmds[i].k,
                                   .enabled = true, .checked = cmds[i].id == CMD_SYSTEM,
                                   .invoke = on_command, .user = a };
        TRY(gates_command_register(t, root, &d));
    }
    TRY(gates_menubar_create(t, root, root, &bar));
    static const gates_command_id_t file[] = { CMD_SAVE, 0, CMD_QUIT };
    static const gates_command_id_t view[] = { CMD_SYSTEM, CMD_LIGHT, CMD_DARK, 0, CMD_ZOOM_IN, CMD_ZOOM_OUT,
                                               CMD_ZOOM_RESET };
    static const gates_command_id_t help[] = { CMD_ABOUT };
    TRY(gates_menubar_add(t, bar, cs("&File"), file, 3, nullptr));
    TRY(gates_menubar_add(t, bar, cs("&View"), view, 7, nullptr));
    TRY(gates_menubar_add(t, bar, cs("&Help"), help, 1, nullptr));

    TRY(gates_toolbar_create(t, root, root, &tools));
    static const gates_command_id_t tb[] = { CMD_SAVE, 0, CMD_LIGHT, CMD_DARK, 0, CMD_ZOOM_IN, CMD_ZOOM_OUT,
                                             CMD_ZOOM_RESET, 0, CMD_STEP, CMD_ABOUT };
    for (size_t i = 0; i < sizeof tb / sizeof tb[0]; i++) TRY(gates_toolbar_add(t, tools, tb[i]));
    TRY(gates_node_set_access_name(t, tools, cs("Tools")));

    TRY(gates_tabs_create(t, root, &a->tabs));
    TRY(gates_layout_set_child_grow(t, a->tabs, 1));
    TRY(gates_node_set_automation_id(t, a->tabs, cs("gallery.tabs")));
    TRY(gates_tabs_add(t, a->tabs, cs("&Buttons"), &page));
    TRY(page_buttons(a, page));
    TRY(gates_tabs_add(t, a->tabs, cs("&Text"), &page));
    TRY(page_text(a, page));
    TRY(gates_tabs_add(t, a->tabs, cs("&Options"), &page));
    TRY(page_options(a, page));
    TRY(gates_tabs_add(t, a->tabs, cs("Vie&ws"), &page));
    TRY(page_views(a, page));
    TRY(gates_tabs_add(t, a->tabs, cs("Pro&gress"), &page));
    TRY(page_progress(a, page));
    TRY(gates_tabs_add(t, a->tabs, cs("&Inputs"), &page));
    TRY(page_inputs(a, page));

    TRY(gates_statusbar_create(t, root, &sb));
    TRY(gates_statusbar_add(t, sb, cs("Ready - Alt shows the access keys"), 1, &a->status));
    TRY(gates_statusbar_add(t, sb, cs("Zoom 100%"), 0, &a->zoom_seg));
    TRY(gates_statusbar_add(t, sb, cs("--:--:--"), 0, &a->clock));
    TRY(tip(a, a->clock, "The time, updated every second"));
    gates_timer_id_t id;
    TRY(gates_timer_start(t, root, 1000, true, on_tick, a, &id));
    on_tick(t, root, id, a);
    return GATES_OK;
}

static bool on_close(gates_window_t *win, void *user) {
    (void)win;
    save_state(user);
    return true;
}

int main(void) {
    static app_t a;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &a.app))) return 1;
    gates_window_callbacks_t cb = { .on_close = on_close, .user_data = &a };
    gates_window_desc_t desc = { .title = cs("gates gallery"), .size = { 720, 520 } };
    if (!gates_is_ok(gates_window_create(a.app, &desc, &cb, &a.win))) {
        gates_app_destroy(a.app);
        return 1;
    }
    a.tree = gates_window_tree(a.win);
    gates_err_t err = build(&a);
    if (gates_is_ok(err)) {
        load_state(&a);
        err = gates_app_run(a.app);
    }
    gates_window_destroy(a.win);
    gates_app_destroy(a.app);
    return gates_is_ok(err) ? 0 : 1;
}
