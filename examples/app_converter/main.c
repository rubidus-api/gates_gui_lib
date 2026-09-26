/* app_converter - a unit converter (sample application, 0.1.0).
 *
 * Shows: a form with a choice, a text field, two choices and a radio group;
 * replacing a choice's options when another choice changes; converting as the
 * person types, with a form error instead of a result while the value is not
 * a number; a result line that screen readers announce; a Swap command.
 *
 * Use: pick a quantity, type a value, pick the units; the result follows.
 * Ctrl+W (or Swap) exchanges the two units; Escape quits.
 *
 * Check: Length, 12, inch -> centimetre shows "12 in = 30.48 cm"; typing
 * "12a" shows "enter a number" under Value and no result; Temperature, 100,
 * Celsius -> Fahrenheit shows 212; Swap turns it around. */
#include <gates/gates.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { F_QUANTITY = 1, F_VALUE, F_FROM, F_TO, F_DIGITS };
enum { CMD_SWAP = 1 };

typedef struct unit_t {
    const char *name;            /* shown in the choices */
    const char *symbol;          /* shown in the result */
    double scale;                /* value in the base unit = (v + offset) * scale */
    double offset;
} unit_t;

typedef struct quantity_t {
    const char *name;
    const unit_t *units;
    gates_u32 count;
    gates_u32 from, to;          /* default option ids (1-based unit index) */
} quantity_t;

static const unit_t lengths[] = {
    { "millimetre", "mm", 0.001, 0 }, { "centimetre", "cm", 0.01, 0 }, { "metre", "m", 1, 0 },
    { "kilometre", "km", 1000, 0 }, { "inch", "in", 0.0254, 0 }, { "foot", "ft", 0.3048, 0 },
    { "yard", "yd", 0.9144, 0 }, { "mile", "mi", 1609.344, 0 },
};
static const unit_t masses[] = {
    { "gram", "g", 0.001, 0 }, { "kilogram", "kg", 1, 0 }, { "tonne", "t", 1000, 0 },
    { "ounce", "oz", 0.028349523125, 0 }, { "pound", "lb", 0.45359237, 0 },
};
static const unit_t temperatures[] = {
    { "Celsius", "\xC2\xB0" "C", 1, 273.15 }, { "Fahrenheit", "\xC2\xB0" "F", 5.0 / 9.0, 459.67 },
    { "kelvin", "K", 1, 0 },
};
static const unit_t volumes[] = {
    { "millilitre", "mL", 0.001, 0 }, { "litre", "L", 1, 0 }, { "cubic metre", "m\xC2\xB3", 1000, 0 },
    { "US gallon", "US gal", 3.785411784, 0 }, { "imperial gallon", "imp gal", 4.54609, 0 },
};
static const quantity_t quantities[] = {
    { "Length", lengths, 8, 5, 2 },
    { "Mass", masses, 5, 5, 2 },
    { "Temperature", temperatures, 3, 1, 2 },
    { "Volume", volumes, 5, 4, 2 },
};

typedef struct app_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t form, quantity, value, from, to, digits, result;
    gates_option_t unit_opts[8];
} app_t;

static const quantity_t *current(app_t *a) {
    gates_u32 q = gates_options_selected(a->tree, a->quantity);
    return q >= 1 && q <= 4 ? &quantities[q - 1] : &quantities[0];
}

static void set_result(app_t *a, const char *s) {
    (void)gates_widget_set_text(a->tree, a->result, (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) });
}

/* Parses the whole field as a number ("1,5" is read as 1.5); false when it is not one. */
static bool parse(gates_str_t s, double *out) {
    char buf[64];
    if (s.size == 0 || s.size >= sizeof buf) return false;
    for (size_t i = 0; i < s.size; i++) buf[i] = s.ptr[i] == ',' ? '.' : (char)s.ptr[i];
    buf[s.size] = '\0';
    char *end = nullptr;
    *out = strtod(buf, &end);
    while (end != nullptr && *end == ' ') end++;
    return end != buf && end != nullptr && *end == '\0';
}

static void convert(app_t *a) {
    const quantity_t *q = current(a);
    gates_str_t text = gates_textbox_text(a->tree, a->value);
    double v = 0;
    if (text.size == 0) {
        (void)gates_form_set_error(a->tree, a->form, F_VALUE, GATES_STR(""));
        set_result(a, "Type a value to convert.");
        return;
    }
    if (!parse(text, &v)) {
        (void)gates_form_set_error(a->tree, a->form, F_VALUE, GATES_STR("enter a number"));
        set_result(a, " ");
        return;
    }
    (void)gates_form_set_error(a->tree, a->form, F_VALUE, GATES_STR(""));
    gates_u32 f = gates_options_selected(a->tree, a->from), t = gates_options_selected(a->tree, a->to);
    if (f < 1 || f > q->count || t < 1 || t > q->count) return;
    const unit_t *uf = &q->units[f - 1], *ut = &q->units[t - 1];
    double base = (v + uf->offset) * uf->scale;
    double r = base / ut->scale - ut->offset;
    gates_u32 digits = gates_options_selected(a->tree, a->digits);
    int prec = digits == 1 ? 2 : digits == 3 ? 10 : 6;
    if (r == 0.0) r = 0.0;
    char buf[160];
    snprintf(buf, sizeof buf, "%.*g %s = %.*g %s", 12, v, uf->symbol, prec, r, ut->symbol);
    set_result(a, buf);
}

/* A new quantity: its units into both choices, with sensible defaults. */
static gates_err_t fill_units(app_t *a) {
    const quantity_t *q = current(a);
    for (gates_u32 i = 0; i < q->count; i++) {
        a->unit_opts[i] = (gates_option_t){ .id = i + 1,
                                            .label = { .ptr = (const gates_u8 *)q->units[i].name,
                                                       .size = strlen(q->units[i].name) } };
    }
    TRY(gates_options_set(a->tree, a->from, a->unit_opts, q->count));
    TRY(gates_options_set(a->tree, a->to, a->unit_opts, q->count));
    TRY(gates_options_set_selected(a->tree, a->from, q->from));
    TRY(gates_options_set_selected(a->tree, a->to, q->to));
    return GATES_OK;
}

static void on_field(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    app_t *a = user;
    if (ev->kind == GATES_EVENT_VALUE_CHANGED && gates_node_eq(ev->source, a->quantity)) (void)fill_units(a);
    if (ev->kind == GATES_EVENT_VALUE_CHANGED || ev->kind == GATES_EVENT_TEXT_CHANGED) convert(a);
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    if (id != CMD_SWAP) return;
    gates_u32 f = gates_options_selected(tree, a->from), t = gates_options_selected(tree, a->to);
    (void)gates_options_set_selected(tree, a->from, t);
    (void)gates_options_set_selected(tree, a->to, f);
    convert(a);
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    app_t *a = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) gates_app_quit(a->app);
}

static gates_err_t build(app_t *a) {
    gates_tree_t *t = a->tree;
    gates_node_t root = gates_tree_root(t), swap;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 10));

    static const gates_option_t kinds[] = {
        { .id = 1, .label = GATES_STR_INIT("Length") }, { .id = 2, .label = GATES_STR_INIT("Mass") },
        { .id = 3, .label = GATES_STR_INIT("Temperature") }, { .id = 4, .label = GATES_STR_INIT("Volume") },
    };
    static const gates_option_t precision[] = {
        { .id = 1, .label = GATES_STR_INIT("short (2 digits)") },
        { .id = 2, .label = GATES_STR_INIT("normal (6 digits)") },
        { .id = 3, .label = GATES_STR_INIT("long (10 digits)") },
    };
    TRY(gates_form_create(t, root, &a->form));
    gates_field_desc_t d = { .label = GATES_STR("Quantity"), .options = kinds, .option_count = 4, .selected_id = 1 };
    TRY(gates_form_add_choice(t, a->form, F_QUANTITY, &d, &a->quantity));
    d = (gates_field_desc_t){ .label = GATES_STR("Value"), .text = GATES_STR("12"), .cols = 16, .max_bytes = 40,
                              .help = GATES_STR("a number; 1.5 or 1,5") };
    TRY(gates_form_add_text(t, a->form, F_VALUE, &d, &a->value));
    static const gates_option_t none[] = { { .id = 1, .label = GATES_STR_INIT("-") } };
    d = (gates_field_desc_t){ .label = GATES_STR("From"), .options = none, .option_count = 1, .selected_id = 1 };
    TRY(gates_form_add_choice(t, a->form, F_FROM, &d, &a->from));
    d = (gates_field_desc_t){ .label = GATES_STR("To"), .options = none, .option_count = 1, .selected_id = 1 };
    TRY(gates_form_add_choice(t, a->form, F_TO, &d, &a->to));
    d = (gates_field_desc_t){ .label = GATES_STR("Precision"), .options = precision, .option_count = 3, .selected_id = 2 };
    TRY(gates_form_add_radio(t, a->form, F_DIGITS, &d, &a->digits));
    TRY(fill_units(a));

    gates_command_desc_t c = { .id = CMD_SWAP, .label = GATES_STR("Swap units (Ctrl+W)"),
                               .shortcut = { .letter = 'W', .ctrl = true }, .enabled = true, .invoke = on_command, .user = a };
    TRY(gates_command_register(t, root, &c));
    TRY(gates_button_create(t, root, GATES_STR(""), nullptr, nullptr, &swap));
    TRY(gates_button_set_command(t, swap, root, CMD_SWAP));
    TRY(gates_layout_set_child_align(t, swap, GATES_ALIGN_START_V));

    TRY(gates_label_create(t, root, GATES_STR(" "), &a->result)); /* its text is what is read */
    TRY(gates_node_set_live(t, a->result, GATES_LIVE_POLITE));

    gates_node_t fields[] = { a->quantity, a->value, a->from, a->to, a->digits };
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) TRY(gates_widget_set_handler(t, fields[i], on_field, a));
    convert(a);
    gates_tree_set_focus(t, a->value);
    return GATES_OK;
}

int main(void) {
    static app_t a;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &a.app))) return 1;
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &a };
    gates_window_desc_t desc = { .title = GATES_STR("Unit converter"), .size = { 460, 420 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(a.app, &desc, &cb, &win))) {
        gates_app_destroy(a.app);
        return 1;
    }
    a.tree = gates_window_tree(win);
    gates_err_t err = build(&a);
    if (gates_is_ok(err)) err = gates_app_run(a.app);
    gates_window_destroy(win);
    gates_app_destroy(a.app);
    return gates_is_ok(err) ? 0 : 1;
}
