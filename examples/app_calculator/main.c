/* app_calculator - a pocket calculator (sample application, 0.1.0).
 *
 * Shows: a grid built from rows of growing buttons, one handler for many
 * buttons, keyboard input through the window's character callback, symbols
 * with spoken names (the division key reads "Divide", not a glyph), and a live
 * display that screen readers announce.
 *
 * Use: click the keys, or type: digits, . or , , + - * / (or x), % and =.
 * Enter also gives the result while no key has focus (a focused key takes
 * Enter, as buttons do); Backspace removes a digit, Delete or C clears,
 * Escape quits.
 *
 * Check: 12 + 30 = shows 42; 7 / 0 = shows "Cannot divide by zero"; 50 % of
 * 200 (200 * 50 %) shows 100; Narrator reads each result. */
#include <gates/gates.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

typedef struct app_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t expr, display;
    double acc;                  /* left operand */
    char op;                     /* pending operator: + - * / or 0 */
    char entry[24];              /* the number being typed */
    bool entering;               /* entry is being typed (else it shows a result) */
    bool error;
} app_t;

typedef struct calc_key_t {
    const char *label;           /* shown */
    const char *spoken;          /* accessible name, when the label is a symbol */
    char code;                   /* what the key does */
} calc_key_t;

/* U+00B1, U+00F7, U+00D7, U+2212, U+2190 written as escapes: sources stay ASCII. */
static const calc_key_t keys[5][4] = {
    { { "C", "Clear", 'C' }, { "\xC2\xB1", "Change sign", 'n' }, { "%", "Percent", '%' }, { "\xC3\xB7", "Divide", '/' } },
    { { "7", nullptr, '7' }, { "8", nullptr, '8' }, { "9", nullptr, '9' }, { "\xC3\x97", "Multiply", '*' } },
    { { "4", nullptr, '4' }, { "5", nullptr, '5' }, { "6", nullptr, '6' }, { "\xE2\x88\x92", "Minus", '-' } },
    { { "1", nullptr, '1' }, { "2", nullptr, '2' }, { "3", nullptr, '3' }, { "+", "Plus", '+' } },
    { { "0", nullptr, '0' }, { ".", "Decimal point", '.' }, { "\xE2\x86\x90", "Backspace", 'b' }, { "=", "Equals", '=' } },
};
static gates_node_t key_nodes[5][4];

static const char *op_symbol(char op) {
    switch (op) {
    case '+': return "+";
    case '-': return "\xE2\x88\x92";
    case '*': return "\xC3\x97";
    case '/': return "\xC3\xB7";
    default:  return "";
    }
}

static double entry_value(const app_t *a) {
    return a->entry[0] != '\0' ? strtod(a->entry, nullptr) : 0.0;
}

static void set_text(app_t *a, gates_node_t n, const char *s) {
    (void)gates_widget_set_text(a->tree, n, (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) });
}

static void show(app_t *a) {
    char buf[64];
    if (a->error) {
        set_text(a, a->display, "Cannot divide by zero");
        set_text(a, a->expr, " ");
        return;
    }
    set_text(a, a->display, a->entry[0] != '\0' ? a->entry : "0");
    if (a->op != 0) {
        snprintf(buf, sizeof buf, "%.12g %s", a->acc, op_symbol(a->op));
        set_text(a, a->expr, buf);
    } else {
        set_text(a, a->expr, " ");
    }
}

static void show_number(app_t *a, double v) {
    if (v == 0.0) v = 0.0; /* no "-0" */
    snprintf(a->entry, sizeof a->entry, "%.12g", v);
}

static void clear(app_t *a) {
    a->acc = 0.0;
    a->op = 0;
    a->entry[0] = '\0';
    a->entering = false;
    a->error = false;
}

/* Applies the pending operator to acc and the entry; false on division by zero. */
static bool apply(app_t *a) {
    double b = entry_value(a);
    switch (a->op) {
    case '+': a->acc += b; break;
    case '-': a->acc -= b; break;
    case '*': a->acc *= b; break;
    case '/':
        if (b == 0.0) return false;
        a->acc /= b;
        break;
    default: a->acc = b; break;
    }
    return isfinite(a->acc);
}

static void press(app_t *a, char code) {
    if (a->error && code != 'C') clear(a);
    size_t len = strlen(a->entry);
    if (code >= '0' && code <= '9') {
        if (!a->entering) {
            a->entry[0] = '\0';
            len = 0;
            a->entering = true;
        }
        if (len == 1 && a->entry[0] == '0') len = 0; /* no leading zeros */
        if (len < 15) {
            a->entry[len] = code;
            a->entry[len + 1] = '\0';
        }
    } else if (code == '.') {
        if (!a->entering) {
            strcpy(a->entry, "0");
            a->entering = true;
        } else if (a->entry[0] == '\0') {
            strcpy(a->entry, "0");
        }
        if (strchr(a->entry, '.') == nullptr && strlen(a->entry) < 15) strcat(a->entry, ".");
    } else if (code == 'b') {
        if (a->entering && len > 0) a->entry[len - 1] = '\0';
    } else if (code == 'C') {
        clear(a);
    } else if (code == 'n') {
        show_number(a, -entry_value(a));
    } else if (code == '%') {
        /* 200 * 50 % = 100: percent of the left operand for + - and a fraction for * / */
        double v = entry_value(a) / 100.0;
        show_number(a, (a->op == '+' || a->op == '-') ? a->acc * v : v);
        a->entering = false;
    } else if (code == '+' || code == '-' || code == '*' || code == '/') {
        if (a->entering || a->op == 0) {
            if (!apply(a)) {
                a->error = true;
                show(a);
                return;
            }
        }
        a->op = code;
        show_number(a, a->acc);
        a->entering = false;
    } else if (code == '=') {
        if (a->op != 0) {
            if (!apply(a)) {
                a->error = true;
                show(a);
                return;
            }
            a->op = 0;
            show_number(a, a->acc);
        }
        a->entering = false;
    }
    show(a);
}

static void on_key_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    app_t *a = user;
    if (ev->kind != GATES_EVENT_ACTIVATED) return;
    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            if (gates_node_eq(key_nodes[r][c], ev->source)) press(a, keys[r][c].code);
        }
    }
}

/* Characters no control took: the calculator's keyboard. */
static void on_char(gates_window_t *win, gates_u32 ch, void *user) {
    (void)win;
    app_t *a = user;
    if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' ||
        ch == '=') {
        press(a, (char)ch);
    } else if (ch == ',') {
        press(a, '.');
    } else if (ch == 'x' || ch == 'X') {
        press(a, '*');
    } else if (ch == 'c' || ch == 'C') {
        press(a, 'C');
    }
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    app_t *a = user;
    if (!ev->down) return;
    if (ev->key == GATES_KEY_ESCAPE) gates_app_quit(a->app);
    else if (ev->key == GATES_KEY_ENTER) press(a, '=');
    else if (ev->key == GATES_KEY_BACKSPACE) press(a, 'b');
    else if (ev->key == GATES_KEY_DELETE) press(a, 'C');
}

static gates_err_t build(app_t *a) {
    gates_tree_t *t = a->tree;
    gates_node_t root = gates_tree_root(t), grid;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 6));
    /* The display's text is its name: a screen reader reads the number itself. */
    TRY(gates_label_create(t, root, GATES_STR(" "), &a->expr));
    TRY(gates_label_create(t, root, GATES_STR("0"), &a->display));
    TRY(gates_node_set_live(t, a->display, GATES_LIVE_POLITE));
    TRY(gates_panel_create(t, root, &grid));
    TRY(gates_layout_set_child_grow(t, grid, 1));
    TRY(gates_layout_set(t, grid, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_gap(t, grid, 6));
    /* One handler for all twenty keys (0.4.0 bubbling): ev->source says which. */
    TRY(gates_node_set_bubble_handler(t, grid, on_key_button, a));
    for (int r = 0; r < 5; r++) {
        gates_node_t row;
        TRY(gates_panel_create(t, grid, &row));
        TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
        TRY(gates_layout_set_gap(t, row, 6));
        TRY(gates_layout_set_child_grow(t, row, 1));
        for (int c = 0; c < 4; c++) {
            const calc_key_t *k = &keys[r][c];
            gates_node_t b;
            TRY(gates_button_create(t, row, (gates_str_t){ .ptr = (const gates_u8 *)k->label, .size = strlen(k->label) },
                                    nullptr, nullptr, &b));
            TRY(gates_layout_set_child_grow(t, b, 1));
            if (k->spoken != nullptr) {
                TRY(gates_node_set_access_name(t, b, (gates_str_t){ .ptr = (const gates_u8 *)k->spoken,
                                                                    .size = strlen(k->spoken) }));
            }
            key_nodes[r][c] = b;
        }
    }
    return GATES_OK;
}

int main(void) {
    static app_t a;
    clear(&a);
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &a.app))) return 1;
    gates_window_callbacks_t cb = { .on_key = on_key, .on_char = on_char, .user_data = &a };
    gates_window_desc_t desc = { .title = GATES_STR("Calculator"), .size = { 300, 380 } };
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
