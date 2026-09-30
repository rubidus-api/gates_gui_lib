/* gallery - every control of gates in one window (sample application, 0.3.0).
 *
 * Shows: the application frame - a menu bar (File, View, Help) with mnemonics,
 * a toolbar of the same commands, tabs with one page per control family, a
 * status bar with a clock, tooltips everywhere - and the controls on the pages:
 * buttons and check boxes, text boxes with labels that carry access keys,
 * radio groups and a choice, a table in a split, progress and separators, a
 * dialog, and (0.4.0) spin boxes, a slider, a collapsible group in a grid,
 * wrapped chips and a total recomputed once per turn, and (0.5.0) pictures,
 * toolbar icons and the native file, folder, colour and message dialogs, and
 * (0.6.0) data: a table edited in place with check, progress and icon cells
 * and a column menu, a property grid for the selected row, Undo/Redo in the
 * Edit menu, and a background job with progress and Cancel, and (0.7.0) a
 * multi-line editor with C highlighting, wrap, line numbers and find. The theme
 * (system, light, dark) and the zoom are commands. The arrangement - selected
 * tab, split, table columns, window placement - is kept in
 * %LOCALAPPDATA%\gates-gallery.ini and comes back at the next start.
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
    CMD_UNDO = 60, CMD_REDO, CMD_COLUMNS,
    CMD_OPEN_PIC = 40, CMD_SAVE_AS, CMD_FOLDER, CMD_COLOUR, CMD_ASK, CMD_OPEN_MANY,
    CMD_SAVE = 1, CMD_QUIT, CMD_SYSTEM, CMD_LIGHT, CMD_DARK, CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_ZOOM_RESET,
    CMD_ABOUT, CMD_ABOUT_OK, CMD_STEP,
};

#define ROWS 200
#define ITEMS 12

/* Data page (0.6.0): the program's own records. */
enum { D_NAME = 1, D_DONE, D_CHECKED, D_FILE };          /* table columns */
enum { P_NAME = 1, P_DONE, P_PRIORITY, P_ESTIMATE };    /* properties */

typedef struct item_t {
    gates_item_id_t id;
    char name[24];
    bool done;
    gates_u32 checked;           /* per mille, set by the background job */
    char file[24];
    gates_u32 priority;          /* 1..3 */
    gates_i64 estimate;          /* hours */
} item_t;

/* One undoable change of one field. */
typedef struct change_t {
    gates_item_id_t id;
    int field;                   /* a property id */
    char before[24], after[24];  /* text fields */
    gates_i64 was, now;          /* the others */
} change_t;

typedef struct app_t {
    gates_app_t *app;
    gates_window_t *win;
    gates_tree_t *tree;
    gates_node_t root, tabs, split, table, clock, zoom_seg, status, progress, about;
    gates_node_t qty, price, volume, total;
    gates_node_t picture, swatch, media_status;
    gates_color_t colour;
    char path[512];
    char cell[64];
    /* Data page */
    item_t items[ITEMS];
    gates_node_t dtable, props, job_bar, job_label, job_start, job_cancel;
    gates_image_id_t file_icon;
    gates_undo_t *undo;
    change_t pending;            /* a table edit, pushed on CELL_EDITED */
    bool have_pending;
    gates_task_t *job;
    char job_names[ITEMS][24];   /* the job's own copy: it never reads the live items */
    char dcell[48];
    /* Editor page */
    gates_node_t code, find_box, code_status;
    /* Views page (0.9.0): the table selects many rows; the program keeps which. */
    bool picked[ROWS];
    gates_node_t pick_status;
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

/* The table's selection is the program's: one flag per row here (a large model would keep ranges). */
static gates_u64 m_next_selected(void *u, gates_u64 row) {
    app_t *a = u;
    for (gates_u64 r = row; r < ROWS; r++) {
        if (a->picked[r]) return r;
    }
    return GATES_ROW_NONE;
}

static void on_table(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    if (ev->kind != GATES_EVENT_SELECT_REQUESTED) return;
    gates_u64 t = ev->item - 1, an = ev->anchor != 0 ? ev->anchor - 1 : t;
    gates_u64 lo = an < t ? an : t, hi = an < t ? t : an;
    switch ((gates_select_request_t)ev->result) {
    case GATES_SELECT_ONE: memset(a->picked, 0, sizeof a->picked); a->picked[t] = true; break;
    case GATES_SELECT_TOGGLE: a->picked[t] = !a->picked[t]; break;
    case GATES_SELECT_RANGE: memset(a->picked, 0, sizeof a->picked); /* fall through */
    case GATES_SELECT_ADD_RANGE: for (gates_u64 r = lo; r <= hi && r < ROWS; r++) a->picked[r] = true; break;
    case GATES_SELECT_ALL: for (gates_u64 r = 0; r < ROWS; r++) a->picked[r] = true; break;
    }
    (void)gates_view_model_changed(tree, ev->source);
    int n = 0;
    for (gates_u64 r = 0; r < ROWS; r++) n += a->picked[r];
    char line[64];
    snprintf(line, sizeof line, "%d of %d rows selected", n, ROWS);
    (void)gates_widget_set_text(tree, a->pick_status, cs(line));
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
    case CMD_UNDO: (void)gates_undo_undo(a->undo); break;
    case CMD_REDO: (void)gates_undo_redo(a->undo); break;
    case CMD_COLUMNS: {
        (void)gates_tabs_set_selected(tree, a->tabs, 7); /* the Data page */
        gates_rect_t h = gates_view_part_rect(tree, a->dtable, GATES_VIEW_PART_HEADER, 0);
        (void)gates_view_open_column_menu(tree, a->dtable, (gates_point_t){ h.x, h.y + h.h }, nullptr);
        break;
    }
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
        { "E&mail", "", "Typed with the keyboard or an input method", false },
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
    TRY(gates_view_create(t, a->split, &(gates_view_desc_t){ .columns = cols, .column_count = 2, .header = true,
                                                             .multi_select = true },
                          &a->table));
    gates_rows_model_t model = { .user = a, .count = m_count, .id_at = m_id_at, .index_of = m_index_of,
                                 .cell = m_cell, .next_selected = m_next_selected };
    TRY(gates_view_set_model(t, a->table, &model));
    TRY(gates_node_set_access_name(t, a->table, cs("Items")));
    TRY(gates_node_set_automation_id(t, a->table, cs("views.table")));
    TRY(tip(a, a->table, "A table: Shift and Ctrl select many rows; drag a header edge to resize a column"));
    TRY(gates_widget_set_handler(t, a->table, on_table, a));
    TRY(gates_panel_create(t, a->split, &left));
    TRY(gates_layout_set(t, left, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, left, 8));
    TRY(gates_label_create(t, left, cs("Drag the handle; the split, the column widths and the page are kept for the "
                                       "next start."), &info));
    TRY(gates_label_create(t, left, cs("No rows selected"), &a->pick_status));
    TRY(gates_node_set_live(t, a->pick_status, GATES_LIVE_POLITE));
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

/* Pictures (0.5.0): generated images, WIC decoding, native dialogs. */
static gates_image_id_t make_image(gates_tree_t *t, int w, int h, int kind, gates_color_t c) {
    gates_u8 *px = malloc((size_t)w * (size_t)h * 4);
    if (px == nullptr) return 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            gates_u8 *p = px + (y * w + x) * 4;
            if (kind == 0) { /* a soft gradient picture */
                p[0] = (gates_u8)(40 + 180 * x / w); p[1] = (gates_u8)(90 + 120 * y / h); p[2] = 200; p[3] = 255;
            } else {         /* an icon: a ring, a filled circle, or a square */
                int dx = 2 * x - w + 1, dy = 2 * y - h + 1, r2 = dx * dx + dy * dy, R = w - 2;
                bool on = kind == 1 ? (r2 <= R * R && r2 >= (R - 5) * (R - 5)) : kind == 2 ? r2 <= R * R
                                                                                            : (x > 1 && y > 1 && x < w - 2 && y < h - 2);
                p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = on ? 255 : 0;
            }
        }
    }
    gates_image_id_t id = 0;
    (void)gates_image_add_rgba(t, w, h, px, 0, &id);
    free(px);
    return id;
}

static void media_say(app_t *a, const char *what, const gates_u8 *text, gates_usize_t n) {
    char line[600];
    snprintf(line, sizeof line, "%s%.*s", what, (int)n, (const char *)text);
    (void)gates_widget_set_text(a->tree, a->media_status, cs(line));
}

static void on_media(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    gates_u8 path[512];
    gates_usize_t n = 0;
    gates_file_dialog_t pics = { .title = cs("Open a picture"),
                                 .filters = cs("Pictures|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.ico|All files|*.*") };
    switch (id) {
    case CMD_OPEN_PIC:
        if (gates_is_ok(gates_window_open_file(a->win, &pics, path, sizeof path, &n)) && n > 0) {
            gates_image_id_t img = 0;
            gates_err_t err = gates_image_load_file(tree, (gates_str_t){ .ptr = path, .size = n }, &img);
            if (gates_is_ok(err)) {
                gates_image_id_t old = gates_image_node_image(tree, a->picture);
                (void)gates_image_node_set(tree, a->picture, img);
                if (old != 0) (void)gates_image_remove(tree, old);
                media_say(a, "Showing ", path, n);
            } else {
                media_say(a, "Not a picture Windows can read: ", path, n);
            }
        } else {
            media_say(a, "No picture chosen", path, 0);
        }
        break;
    case CMD_OPEN_MANY: {
        /* Several paths, each ended by a NUL: say how many and name the first. */
        static gates_u8 many[8192];
        gates_u32 count = 0;
        gates_err_t err = gates_window_open_files(a->win, &(gates_file_dialog_t){ .title = cs("Open several files") },
                                                  many, sizeof many, &n, &count);
        if (gates_is_ok(err) && count > 0) {
            char line[64];
            snprintf(line, sizeof line, "%u files, the first: ", count);
            media_say(a, line, many, strlen((const char *)many));
        } else {
            media_say(a, err == PROVEN_ERR_OVERFLOW ? "Too many files for the buffer" : "No files chosen", path, 0);
        }
        break;
    }
    case CMD_SAVE_AS: {
        gates_file_dialog_t d = { .title = cs("Save the notes"), .filters = cs("Text files|*.txt|All files|*.*"),
                                  .name = cs("notes.txt") };
        if (gates_is_ok(gates_window_save_file(a->win, &d, path, sizeof path, &n)) && n > 0) {
            media_say(a, "Would save to ", path, n);   /* the gallery writes nothing */
        } else {
            media_say(a, "Not saved", path, 0);
        }
        break;
    }
    case CMD_FOLDER:
        if (gates_is_ok(gates_window_choose_folder(a->win, &(gates_file_dialog_t){ .title = cs("Pick a folder") }, path,
                                                   sizeof path, &n)) && n > 0) {
            media_say(a, "Folder ", path, n);
        } else {
            media_say(a, "No folder chosen", path, 0);
        }
        break;
    case CMD_COLOUR: {
        bool chosen = false;
        if (gates_is_ok(gates_window_choose_color(a->win, &a->colour, &chosen)) && chosen) {
            gates_image_id_t old = gates_image_node_image(tree, a->swatch);
            (void)gates_image_node_set(tree, a->swatch, make_image(tree, 24, 24, 3, a->colour));
            if (old != 0) (void)gates_image_remove(tree, old);
            char rgb[32];
            int k = snprintf(rgb, sizeof rgb, "%u, %u, %u", a->colour.r, a->colour.g, a->colour.b);
            media_say(a, "Colour ", (const gates_u8 *)rgb, (gates_usize_t)k);
        }
        break;
    }
    case CMD_ASK: {
        gates_answer_t r = gates_window_message(a->win, cs("A question"), cs("Keep the changes?"),
                                                GATES_MESSAGE_YES_NO_CANCEL, GATES_MESSAGE_QUESTION);
        const char *w = r == GATES_ANSWER_YES ? "yes" : r == GATES_ANSWER_NO ? "no" : "cancel";
        media_say(a, "Answer: ", (const gates_u8 *)w, strlen(w));
        break;
    }
    default: break;
    }
}

static gates_err_t page_pictures(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    gates_node_t row, b;
    static const struct { gates_command_id_t id; const char *label; } media[] = {
        { CMD_OPEN_PIC, "O&pen picture..." }, { CMD_SAVE_AS, "Save &as..." }, { CMD_FOLDER, "Choose fol&der..." },
        { CMD_COLOUR, "Choose &colour..." }, { CMD_ASK, "As&k..." }, { CMD_OPEN_MANY, "Open man&y files..." },
    };
    TRY(gates_panel_create(t, page, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_WRAP));
    TRY(gates_layout_set_gap(t, row, 8));
    for (size_t i = 0; i < sizeof media / sizeof media[0]; i++) {
        gates_command_desc_t d = { .id = media[i].id, .label = cs(media[i].label), .enabled = true,
                                   .invoke = on_media, .user = a };
        TRY(gates_command_register(t, a->root, &d));
        TRY(gates_button_create(t, row, cs(""), nullptr, nullptr, &b));
        TRY(gates_button_set_command(t, b, a->root, media[i].id));
    }
    a->colour = GATES_RGB(30, 120, 200);
    TRY(gates_image_create(t, row, make_image(t, 24, 24, 3, a->colour), &a->swatch));
    TRY(gates_label_create(t, page, cs("No picture chosen"), &a->media_status));
    TRY(gates_node_set_live(t, a->media_status, GATES_LIVE_POLITE));
    TRY(gates_image_create(t, page, make_image(t, 240, 150, 0, a->colour), &a->picture));
    TRY(gates_image_node_set_size(t, a->picture, (gates_size_t){ 320, 200 }));
    TRY(gates_layout_set_child_align(t, a->picture, GATES_ALIGN_START_V));
    TRY(gates_node_set_access_name(t, a->picture, cs("The chosen picture")));
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


/* -- Data (0.6.0): an editable table, a property grid, undo, a background job ------------------ */

static item_t *item_of(app_t *a, gates_item_id_t id) {
    return id >= 1 && id <= ITEMS ? &a->items[id - 1] : nullptr;
}

static gates_u64 d_count(void *u) { (void)u; return ITEMS; }
static gates_item_id_t d_id_at(void *u, gates_u64 row) { (void)u; return row < ITEMS ? row + 1 : 0; }
static bool d_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    (void)u;
    if (id == 0 || id > ITEMS) return false;
    *row = id - 1;
    return true;
}

static gates_err_t d_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    app_t *a = u;
    item_t *it = item_of(a, id);
    if (it == nullptr) return PROVEN_ERR_INVALID_ARG;
    int n = 0;
    switch (col) {
    case D_NAME:
        n = (int)strlen(it->name);
        memcpy(a->dcell, it->name, (size_t)n); /* both live in app_t: no snprintf between them */
        break;
    case D_DONE: out->checked = it->done; break;
    case D_CHECKED:
        out->permille = it->checked;
        n = snprintf(a->dcell, sizeof a->dcell, "%u%%", it->checked / 10);
        break;
    default:
        out->icon = a->file_icon;
        n = (int)strlen(it->file);
        memcpy(a->dcell, it->file, (size_t)n);
        break;
    }
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)a->dcell, .size = (gates_usize_t)n };
    return GATES_OK;
}

/* A person changed a cell: the model checks it and keeps it; the change is
 * recorded for Undo when CELL_EDITED arrives. */
static gates_err_t d_set_cell(void *u, gates_item_id_t id, gates_column_id_t col, const gates_cell_t *v) {
    app_t *a = u;
    item_t *it = item_of(a, id);
    if (it == nullptr) return PROVEN_ERR_INVALID_ARG;
    change_t c = { .id = id };
    if (col == D_DONE) {
        c.field = P_DONE;
        c.was = it->done;
        c.now = v->checked;
        it->done = v->checked;
    } else {
        char *field = col == D_NAME ? it->name : it->file;
        if (v->text.size == 0 || v->text.size >= sizeof it->name) return PROVEN_ERR_INVALID_ARG;
        c.field = col == D_NAME ? P_NAME : 100; /* 100: the file name (not a property) */
        snprintf(c.before, sizeof c.before, "%s", field);
        memcpy(field, v->text.ptr, v->text.size);
        field[v->text.size] = '\0';
        snprintf(c.after, sizeof c.after, "%s", field);
    }
    a->pending = c;
    a->have_pending = true;
    return GATES_OK;
}

/* Shows the selected item in the property grid (silently). */
static void show_props(app_t *a) {
    gates_tree_t *t = a->tree;
    item_t *it = item_of(a, gates_view_selected(t, a->dtable));
    if (it == nullptr) return;
    gates_node_t name = gates_propgrid_editor(t, a->props, P_NAME);
    if (!gates_node_eq(gates_tree_focus(t), name)) (void)gates_textbox_set_text(t, name, cs(it->name));
    (void)gates_checkbox_set_checked(t, gates_propgrid_editor(t, a->props, P_DONE), it->done);
    (void)gates_options_set_selected(t, gates_propgrid_editor(t, a->props, P_PRIORITY), it->priority);
    (void)gates_range_set_value(t, gates_propgrid_editor(t, a->props, P_ESTIMATE), it->estimate);
}

static void data_changed(app_t *a) {
    (void)gates_view_model_changed(a->tree, a->dtable);
    show_props(a);
}

/* Puts one side of a change into the item. */
static gates_err_t apply(app_t *a, const change_t *c, bool after) {
    item_t *it = item_of(a, c->id);
    if (it == nullptr) return PROVEN_ERR_NOT_FOUND;
    const char *text = after ? c->after : c->before;
    gates_i64 v = after ? c->now : c->was;
    switch (c->field) {
    case P_NAME: snprintf(it->name, sizeof it->name, "%s", text); break;
    case 100: snprintf(it->file, sizeof it->file, "%s", text); break;
    case P_DONE: it->done = v != 0; break;
    case P_PRIORITY: it->priority = (gates_u32)v; break;
    default: it->estimate = v; break;
    }
    (void)gates_view_set_selected(a->tree, a->dtable, c->id);
    (void)gates_view_scroll_to(a->tree, a->dtable, c->id);
    data_changed(a);
    return GATES_OK;
}

static app_t *g_app;
static gates_err_t undo_change(void *d) { return apply(g_app, d, false); }
static gates_err_t redo_change(void *d) { return apply(g_app, d, true); }

static void record(app_t *a, const change_t *c) {
    static const char *const labels[] = { "", "Rename", "Done", "Priority", "Estimate" };
    change_t *copy = malloc(sizeof *copy);
    if (copy == nullptr) return;
    *copy = *c;
    const char *label = c->field == 100 ? "Rename file" : labels[c->field];
    /* Typing into one property merges into one entry per item and field. */
    gates_u32 merge = c->field == P_NAME || c->field == P_ESTIMATE ? (gates_u32)(c->id * 8 + (gates_u32)c->field) : 0;
    gates_undo_entry_t e = { .label = cs(label), .undo = undo_change, .undo_data = copy, .redo = redo_change,
                             .redo_data = copy, .drop = free, .merge_key = merge };
    (void)gates_undo_push(a->undo, &e);
}

static void on_dtable(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    (void)tree;
    if (ev->kind == GATES_EVENT_SELECTION_CHANGED) {
        gates_undo_break_merge(a->undo);
        show_props(a);
    } else if (ev->kind == GATES_EVENT_CELL_EDITED && a->have_pending) {
        a->have_pending = false;
        record(a, &a->pending);
        show_props(a);
    }
}

static void on_props(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    item_t *it = item_of(a, gates_view_selected(tree, a->dtable));
    if (it == nullptr || ev->kind != GATES_EVENT_VALUE_CHANGED) return;
    change_t c = { .id = it->id, .field = (int)ev->result };
    switch (ev->result) {
    case P_NAME: {
        gates_node_t box = gates_propgrid_editor(tree, a->props, P_NAME);
        bool ok = ev->text.size > 0 && ev->text.size < sizeof it->name;
        (void)gates_textbox_set_invalid(tree, box, !ok);
        if (!ok) return; /* an empty name stays in the box, marked, and is not kept */
        snprintf(c.before, sizeof c.before, "%s", it->name);
        memcpy(it->name, ev->text.ptr, ev->text.size);
        it->name[ev->text.size] = '\0';
        snprintf(c.after, sizeof c.after, "%s", it->name);
        break;
    }
    case P_DONE: c.was = it->done; c.now = ev->checked; it->done = ev->checked; break;
    case P_PRIORITY: c.was = it->priority; c.now = ev->value; it->priority = (gates_u32)ev->value; break;
    default: c.was = it->estimate; c.now = ev->value; it->estimate = ev->value; break;
    }
    record(a, &c);
    (void)gates_view_model_changed(tree, a->dtable);
}

/* The background job: "checks" each item (a slow computation), reporting as it goes. */
static bool slow_check(gates_task_t *task, gates_u32 seed) {
    gates_u32 primes = 0;
    for (gates_u32 n = 2; n < 2000000u; n++) { /* about half a second */
        if (n % 20000u == 0 && gates_task_cancelled(task)) return false;
        bool p = true;
        for (gates_u32 d = 2; d * d <= n; d++) {
            if (n % d == 0) { p = false; break; }
        }
        primes += p ? 1u : 0u;
    }
    return (primes ^ seed) != 0xFFFFFFFFu;
}

static gates_err_t job_work(gates_task_t *task, void *user) {
    app_t *a = user;
    for (gates_u32 i = 0; i < ITEMS; i++) {
        if (!slow_check(task, i)) return PROVEN_ERR_EOF;
        char text[64];
        int n = snprintf(text, sizeof text, "Checked %s (%u of %u)", a->job_names[i], i + 1, ITEMS);
        (void)gates_task_report(task, (i + 1) * 1000u / ITEMS, (gates_str_t){ (const gates_u8 *)text, (gates_usize_t)n });
    }
    return GATES_OK;
}

static void job_progress(gates_tree_t *tree, gates_task_t *task, gates_u32 permille, gates_str_t text, void *user) {
    (void)task;
    app_t *a = user;
    (void)gates_progress_set_value(tree, a->job_bar, (gates_i32)permille);
    (void)gates_widget_set_text(tree, a->job_label, text);
    gates_u32 done = (permille * ITEMS + 500u) / 1000u; /* the nearest whole item */
    for (gates_u32 i = 0; i < ITEMS; i++) a->items[i].checked = i < done ? 1000u : 0u;
    (void)gates_view_model_changed(tree, a->dtable);
}

static void job_done(gates_tree_t *tree, gates_task_t *task, gates_err_t result, bool cancelled, void *user) {
    (void)task;
    app_t *a = user;
    a->job = nullptr;
    (void)gates_widget_set_text(tree, a->job_label, cs(cancelled ? "Cancelled" : gates_is_ok(result) ? "All items checked"
                                                                                                    : "Stopped"));
    (void)gates_widget_set_disabled(tree, a->job_cancel, true);
    (void)gates_widget_set_disabled(tree, a->job_start, false);
}

static void on_job_start(gates_tree_t *tree, gates_node_t node, void *user) {
    (void)node;
    app_t *a = user;
    if (a->job != nullptr) return;
    for (int i = 0; i < ITEMS; i++) {
        snprintf(a->job_names[i], sizeof a->job_names[i], "%s", a->items[i].name);
        a->items[i].checked = 0;
    }
    gates_task_desc_t d = { .work = job_work, .work_user = a, .on_progress = job_progress, .on_done = job_done,
                            .ui_user = a };
    if (!gates_is_ok(gates_task_start(tree, &d, &a->job))) {
        (void)gates_widget_set_text(tree, a->job_label, cs("Could not start the job"));
        return;
    }
    (void)gates_progress_set_value(tree, a->job_bar, 0);
    (void)gates_widget_set_text(tree, a->job_label, cs("Checking..."));
    (void)gates_widget_set_disabled(tree, a->job_cancel, false);
    gates_tree_set_focus(tree, a->job_cancel);
    (void)gates_widget_set_disabled(tree, a->job_start, true);
    (void)gates_view_model_changed(tree, a->dtable);
}

static void on_job_cancel(gates_tree_t *tree, gates_node_t node, void *user) {
    (void)tree;
    (void)node;
    app_t *a = user;
    if (a->job != nullptr) gates_task_cancel(a->job);
}

static gates_err_t page_data(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    static const char *const names[ITEMS] = { "Budget", "Roadmap", "Invoices", "Minutes", "Designs", "Contracts",
                                              "Backlog", "Reports", "Photos", "Manual", "Payroll", "Archive" };
    static const char *const files[ITEMS] = { "budget.xlsx", "roadmap.md", "invoices.pdf", "minutes.txt",
                                              "designs.fig", "contracts.pdf", "backlog.csv", "reports.docx",
                                              "photos.zip", "manual.pdf", "payroll.xlsx", "archive.tar" };
    for (int i = 0; i < ITEMS; i++) {
        a->items[i] = (item_t){ .id = (gates_item_id_t)(i + 1), .done = i % 3 == 0, .priority = 1u + (gates_u32)(i % 3),
                                .estimate = 2 + i };
        snprintf(a->items[i].name, sizeof a->items[i].name, "%s", names[i]);
        snprintf(a->items[i].file, sizeof a->items[i].file, "%s", files[i]);
    }
    a->file_icon = make_image(t, 16, 16, 3, GATES_RGB(90, 120, 170));
    gates_node_t split, row;
    TRY(gates_layout_set_child_grow(t, page, 1));
    TRY(gates_panel_create(t, page, &split));
    TRY(gates_layout_set(t, split, GATES_LAYOUT_KIND_SPLIT));
    TRY(gates_layout_set_split(t, split, GATES_SPLIT_HORIZONTAL, 620));
    TRY(gates_layout_set_child_grow(t, split, 1));
    TRY(gates_node_set_automation_id(t, split, cs("data.split")));
    static const gates_column_desc_t cols[] = {
        { .id = D_NAME, .label = GATES_STR_INIT("Name"), .width = 110, .editable = true },
        { .id = D_DONE, .label = GATES_STR_INIT("Done"), .width = 60, .kind = GATES_CELL_CHECK, .editable = true },
        { .id = D_CHECKED, .label = GATES_STR_INIT("Checked"), .width = 110, .kind = GATES_CELL_PROGRESS },
        { .id = D_FILE, .label = GATES_STR_INIT("File"), .width = 140, .kind = GATES_CELL_ICON_TEXT, .editable = true },
    };
    TRY(gates_view_create(t, split, &(gates_view_desc_t){ .columns = cols, .column_count = 4, .header = true,
                                                          .column_menu = true }, &a->dtable));
    gates_rows_model_t model = { .user = a, .count = d_count, .id_at = d_id_at, .index_of = d_index_of,
                                 .cell = d_cell, .set_cell = d_set_cell };
    TRY(gates_view_set_model(t, a->dtable, &model));
    TRY(gates_node_set_access_name(t, a->dtable, cs("Documents")));
    TRY(gates_node_set_automation_id(t, a->dtable, cs("data.table")));
    TRY(gates_widget_set_handler(t, a->dtable, on_dtable, a));
    TRY(tip(a, a->dtable, "F2 or a double click edits a name; Space ticks Done; right-click the header for columns"));
    TRY(gates_propgrid_create(t, split, &a->props));
    static const gates_option_t prios[] = { { .id = 1, .label = GATES_STR_INIT("Low") },
                                            { .id = 2, .label = GATES_STR_INIT("Normal") },
                                            { .id = 3, .label = GATES_STR_INIT("High") } };
    TRY(gates_propgrid_add_text(t, a->props, cs("Item"), P_NAME, cs("Name"), cs("")));
    TRY(gates_propgrid_add_bool(t, a->props, cs("Item"), P_DONE, cs("Done"), false));
    TRY(gates_propgrid_add_choice(t, a->props, cs("Schedule"), P_PRIORITY, cs("Priority"), prios, 3, 2));
    TRY(gates_propgrid_add_number(t, a->props, cs("Schedule"), P_ESTIMATE, cs("Estimate (h)"),
                                  &(gates_range_t){ .min = 0, .max = 999, .value = 1 }));
    TRY(gates_propgrid_set_handler(t, a->props, on_props, a));
    TRY(gates_view_set_selected(t, a->dtable, 1));
    show_props(a);
    TRY(gates_panel_create(t, page, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_button_create(t, row, cs("Chec&k all"), on_job_start, a, &a->job_start));
    TRY(tip(a, a->job_start, "A background job: the window stays responsive while it runs"));
    TRY(gates_button_create(t, row, cs("Ca&ncel"), on_job_cancel, a, &a->job_cancel));
    TRY(gates_widget_set_disabled(t, a->job_cancel, true));
    TRY(gates_progress_create(t, row, 0, &a->job_bar));
    TRY(gates_layout_set_child_grow(t, a->job_bar, 1));
    TRY(gates_layout_set_child_align(t, a->job_bar, GATES_ALIGN_CENTER_V));
    TRY(gates_node_set_access_name(t, a->job_bar, cs("Job progress")));
    TRY(gates_label_create(t, row, cs("Not started"), &a->job_label));
    TRY(gates_node_set_live(t, a->job_label, GATES_LIVE_POLITE));
    return GATES_OK;
}

/* -- Text editor (0.7.0): a multi-line editor with C highlighting ------------------------------ */

enum { ST_PLAIN, ST_KEYWORD, ST_COMMENT, ST_STRING, ST_NUMBER, ST_LINE_COMMENT };

static bool ident_char(gates_u8 c) {
    return c == '_' || (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z');
}

/* A small C highlighter. It is asked for whole lines from a stale line on; a
 * block comment still open at `from` is found by looking back for its start. */
static void c_styler(void *user, gates_text_buffer_t *b, gates_u32 from, gates_u32 to) {
    (void)user;
    static const char *const kw[] = { "int", "char", "void", "return", "if", "else", "for", "while", "static",
                                      "const", "struct", "typedef", "bool", "true", "false", "nullptr", "include" };
    bool in_comment = from > 1 && gates_text_buffer_style(b, from - 2) == ST_COMMENT &&
                      !(gates_text_buffer_byte(b, from - 3) == '*' && gates_text_buffer_byte(b, from - 2) == '/');
    gates_text_buffer_set_style(b, from, to, ST_PLAIN);
    for (gates_u32 i = from; i < to;) {
        gates_u8 c = gates_text_buffer_byte(b, i);
        if (in_comment) {
            gates_u32 s = i;
            while (i < to && !(gates_text_buffer_byte(b, i) == '*' && gates_text_buffer_byte(b, i + 1) == '/')) i++;
            if (i < to) i += 2, in_comment = false;
            gates_text_buffer_set_style(b, s, i, ST_COMMENT);
        } else if (c == '/' && gates_text_buffer_byte(b, i + 1) == '*') {
            in_comment = true;
        } else if (c == '/' && gates_text_buffer_byte(b, i + 1) == '/') {
            gates_u32 s = i;
            while (i < to && gates_text_buffer_byte(b, i) != '\n') i++;
            gates_text_buffer_set_style(b, s, i, ST_LINE_COMMENT); /* never carries to the next line */
        } else if (c == '"') {
            gates_u32 s = i++;
            while (i < to && gates_text_buffer_byte(b, i) != '"' && gates_text_buffer_byte(b, i) != '\n') {
                i += gates_text_buffer_byte(b, i) == '\\' ? 2 : 1;
            }
            if (i < to && gates_text_buffer_byte(b, i) == '"') i++;
            gates_text_buffer_set_style(b, s, i, ST_STRING);
        } else if (c >= '0' && c <= '9') {
            gates_u32 s = i;
            while (i < to && ident_char(gates_text_buffer_byte(b, i))) i++;
            gates_text_buffer_set_style(b, s, i, ST_NUMBER);
        } else if (ident_char(c)) {
            gates_u32 s = i;
            char word[16];
            gates_u32 n = 0;
            while (i < to && ident_char(gates_text_buffer_byte(b, i))) {
                if (n < sizeof word - 1) word[n++] = (char)gates_text_buffer_byte(b, i);
                i++;
            }
            word[n] = 0;
            for (size_t k = 0; k < sizeof kw / sizeof kw[0]; k++) {
                if (strcmp(word, kw[k]) == 0) gates_text_buffer_set_style(b, s, i, ST_KEYWORD);
            }
        } else {
            i++;
        }
    }
}

static void show_position(app_t *a) {
    const gates_text_buffer_t *b = gates_editor_buffer(a->tree, a->code);
    gates_u32 caret = 0;
    gates_editor_selection(a->tree, a->code, nullptr, &caret);
    gates_u32 line = gates_text_buffer_line_of(b, caret);
    char text[80];
    snprintf(text, sizeof text, "Line %u, column %u%s", line + 1, caret - gates_text_buffer_line_start(b, line) + 1,
             gates_editor_modified(a->tree, a->code) ? " - modified" : "");
    (void)gates_widget_set_text(a->tree, a->code_status, cs(text));
}

static void on_code(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)ev;
    show_position(user);
}

static void on_wrap(gates_tree_t *tree, gates_node_t node, bool checked, void *user) {
    (void)node;
    (void)gates_editor_set_wrap(tree, ((app_t *)user)->code, checked);
}

static void on_numbers(gates_tree_t *tree, gates_node_t node, bool checked, void *user) {
    (void)node;
    (void)gates_editor_set_line_numbers(tree, ((app_t *)user)->code, checked);
}

static void on_find(gates_tree_t *tree, gates_node_t node, void *user) {
    (void)node;
    app_t *a = user;
    gates_str_t needle = gates_textbox_text(tree, a->find_box);
    bool found = needle.size > 0 && gates_editor_find(tree, a->code, needle, GATES_FIND_IGNORE_CASE, true);
    if (found) gates_tree_set_focus(tree, a->code);
    (void)gates_textbox_set_invalid(tree, a->find_box, needle.size > 0 && !found);
    show_position(a);
}

static gates_err_t page_editor(app_t *a, gates_node_t page) {
    gates_tree_t *t = a->tree;
    static const char sample[] =
        "/* A tiny program - edit me. Tab indents, Shift+Tab unindents,\n"
        " * Ctrl+Tab leaves the editor. */\n"
        "#include <stdio.h>\n"
        "\n"
        "static int square(int n) {\n"
        "\treturn n * n; // a comment\n"
        "}\n"
        "\n"
        "int main(void) {\n"
        "\tfor (int i = 1; i <= 10; i++) {\n"
        "\t\tprintf(\"%d squared is %d\\n\", i, square(i));\n"
        "\t}\n"
        "\treturn 0;\n"
        "}\n";
    gates_node_t row, l, next, wrap, numbers;
    TRY(gates_layout_set_child_grow(t, page, 1));
    TRY(gates_panel_create(t, page, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_checkbox_create(t, row, cs("Word wra&p"), false, on_wrap, a, &wrap));
    TRY(gates_checkbox_create(t, row, cs("Line nu&mbers"), true, on_numbers, a, &numbers));
    TRY(gates_label_create(t, row, cs("Fin&d"), &l));
    TRY(gates_textbox_create(t, row, cs("square"), 12, &a->find_box));
    TRY(gates_label_set_target(t, l, a->find_box));
    TRY(gates_node_set_labelled_by(t, a->find_box, l));
    TRY(gates_layout_set_child_align(t, a->find_box, GATES_ALIGN_CENTER_V));
    TRY(gates_button_create(t, row, cs("&Next"), on_find, a, &next));
    TRY(tip(a, next, "Selects the next match (wrapping to the start)"));
    gates_editor_desc_t d = { .tab_inserts = true, .auto_indent = true, .line_numbers = true, .rows = 12, .cols = 50 };
    TRY(gates_editor_create(t, page, &d, &a->code));
    TRY(gates_layout_set_child_grow(t, a->code, 1));
    TRY(gates_node_set_access_name(t, a->code, cs("Code")));
    TRY(gates_editor_set_text(t, a->code, (gates_str_t){ (const gates_u8 *)sample, sizeof sample - 1 }));
    static const gates_editor_style_t styles[] = {
        { .token = GATES_COLOR_CONTROL_FG }, { .token = GATES_COLOR_FOCUS_RING },
        { .token = GATES_COLOR_CONTROL_DISABLED_FG }, { .token = GATES_COLOR_ERROR }, { .token = GATES_COLOR_FOCUS_RING },
        { .token = GATES_COLOR_CONTROL_DISABLED_FG },
    };
    TRY(gates_editor_set_styles(t, a->code, styles, sizeof styles / sizeof styles[0]));
    TRY(gates_editor_set_styler(t, a->code, c_styler, nullptr));
    TRY(gates_widget_set_handler(t, a->code, on_code, a));
    TRY(tip(a, a->code, "A multi-line editor: C highlighting, Tab indents, Ctrl+Z undoes"));
    TRY(gates_label_create(t, page, cs(""), &a->code_status));
    TRY(gates_node_set_live(t, a->code_status, GATES_LIVE_POLITE));
    show_position(a);
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
        { CMD_UNDO, "&Undo", { .letter = 'Z', .ctrl = true } },
        { CMD_REDO, "&Redo", { .letter = 'Y', .ctrl = true } },
        { CMD_COLUMNS, "&Columns...", {0} },
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
    static const gates_command_id_t edit[] = { CMD_UNDO, CMD_REDO, 0, CMD_COLUMNS };
    TRY(gates_menubar_add(t, bar, cs("&File"), file, 3, nullptr));
    TRY(gates_menubar_add(t, bar, cs("&Edit"), edit, 4, nullptr));
    TRY(gates_menubar_add(t, bar, cs("&View"), view, 7, nullptr));
    TRY(gates_menubar_add(t, bar, cs("&Help"), help, 1, nullptr));

    TRY(gates_toolbar_create(t, root, root, &tools));
    static const gates_command_id_t tb[] = { CMD_SAVE, 0, CMD_LIGHT, CMD_DARK, 0, CMD_ZOOM_IN, CMD_ZOOM_OUT,
                                             CMD_ZOOM_RESET, 0, CMD_STEP, CMD_ABOUT };
    for (size_t i = 0; i < sizeof tb / sizeof tb[0]; i++) TRY(gates_toolbar_add(t, tools, tb[i]));
    TRY(gates_node_set_access_name(t, tools, cs("Tools")));
    gates_color_t ink = GATES_RGB(60, 90, 140);
    TRY(gates_command_set_icon(t, root, CMD_SAVE, make_image(t, 16, 16, 3, ink)));
    TRY(gates_command_set_icon(t, root, CMD_LIGHT, make_image(t, 16, 16, 1, GATES_RGB(230, 170, 0))));
    TRY(gates_command_set_icon(t, root, CMD_DARK, make_image(t, 16, 16, 2, GATES_RGB(70, 70, 90))));
    TRY(gates_command_set_icon(t, root, CMD_ABOUT, make_image(t, 16, 16, 1, ink)));

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
    TRY(gates_tabs_add(t, a->tabs, cs("Pict&ures"), &page));
    TRY(page_pictures(a, page));
    TRY(gates_tabs_add(t, a->tabs, cs("Data && &jobs"), &page));
    TRY(page_data(a, page));
    TRY(gates_tabs_add(t, a->tabs, cs("Te&xt editor"), &page));
    TRY(page_editor(a, page));
    TRY(gates_undo_create((gates_allocator_t){0}, 200, &a->undo));
    TRY(gates_undo_bind(a->undo, t, root, CMD_UNDO, CMD_REDO, cs("Undo"), cs("Redo")));

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

/* Input the tree could not complete (usually out of memory): say so. */
static void on_input_error(gates_window_t *win, gates_err_t err, void *user) {
    (void)win;
    app_t *a = user;
    (void)gates_widget_set_text(a->tree, a->status, cs(err == PROVEN_ERR_NOMEM ? "Out of memory: the last action was not done" : "The last action failed"));
}

static bool on_close(gates_window_t *win, void *user) {
    (void)win;
    save_state(user);
    return true;
}

int main(void) {
    static app_t a;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &a.app))) return 1;
    gates_window_callbacks_t cb = { .on_close = on_close, .user_data = &a, .on_input_error = on_input_error };
    gates_window_desc_t desc = { .title = cs("gates gallery"), .size = { 720, 520 } };
    if (!gates_is_ok(gates_window_create(a.app, &desc, &cb, &a.win))) {
        gates_app_destroy(a.app);
        return 1;
    }
    a.tree = gates_window_tree(a.win);
    g_app = &a;
    gates_err_t err = build(&a);
    if (gates_is_ok(err)) {
        load_state(&a);
        err = gates_app_run(a.app);
    }
    gates_undo_destroy(a.undo); /* before its tree goes */
    gates_window_destroy(a.win); /* a job still running is cancelled and waited for */
    gates_app_destroy(a.app);
    return gates_is_ok(err) ? 0 : 1;
}
