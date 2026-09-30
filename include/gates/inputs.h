/* gates_gui_lib - number input: spin box and slider (plan-0019, RFC-0005).
 *
 * Both hold an integer value in a range with a step and a page step. A
 * decimal quantity uses a scale: value 125 with scale 100 is shown as 1.25.
 * A person's change queues GATES_EVENT_VALUE_CHANGED with ev->value = the new
 * value (the latest when changes coalesce); gates_range_set_value is silent.
 *
 * Spin box: a text box with up/down arrows. Up/Down step, PgUp/PgDn page (the
 * text box keeps Left/Right/Home/End for its caret). Typing marks the box
 * invalid until the text is a number in range; Enter or leaving the box
 * commits it - text that is not a number in range goes back to the value.
 * A click on an arrow steps (the focus goes to the box); held, it repeats
 * after a pause while the pointer stays on it (a tree with a clock). The
 * wheel steps a focused spin box or slider (unfocused, it scrolls the page).
 *
 * Slider: a track with a thumb, horizontal or vertical. Left/Down step down,
 * Right/Up step up, PgUp/PgDn page, Home/End go to the ends; dragging the
 * thumb follows the pointer in steps; a press on the track pages toward it.
 * Optional tick marks. Platform-free. */
#ifndef GATES_INPUTS_H
#define GATES_INPUTS_H

#include <gates/tree.h>

typedef struct gates_range_t {
    gates_i64 min, max;          /* min <= max */
    gates_i64 step;              /* >= 1; 0 -> 1 */
    gates_i64 page;              /* >= step; 0 -> 10 steps */
    gates_i64 value;             /* clamped into [min, max] */
    gates_u32 scale;             /* 0 or 1 = whole numbers; 10, 100, 1000 = one to three decimals */
} gates_range_t;

/* INVALID_ARG for min > max, a negative step or page, or another scale. */
[[nodiscard]] gates_err_t gates_spin_create(gates_tree_t *tree, gates_node_t parent,
                                            const gates_range_t *range, gates_node_t *out_spin);
[[nodiscard]] gates_err_t gates_slider_create(gates_tree_t *tree, gates_node_t parent,
                                              const gates_range_t *range, bool vertical,
                                              gates_node_t *out_slider);

/* Silent; clamped into the range. INVALID_ARG for other nodes. */
[[nodiscard]] gates_err_t gates_range_set_value(gates_tree_t *tree, gates_node_t node, gates_i64 value);
gates_i64 gates_range_value(const gates_tree_t *tree, gates_node_t node);
/* New limits and steps (the value is clamped into them, silently). */
[[nodiscard]] gates_err_t gates_range_set(gates_tree_t *tree, gates_node_t node, const gates_range_t *range);
gates_range_t gates_range_get(const gates_tree_t *tree, gates_node_t node);
/* Slider tick marks every `steps` steps (0 = none). */
[[nodiscard]] gates_err_t gates_slider_set_ticks(gates_tree_t *tree, gates_node_t slider, gates_u32 steps);
/* The spin box's text box (for a label target, an access name, a width). */
gates_node_t gates_spin_box(const gates_tree_t *tree, gates_node_t spin);

/* Formats `value` with the scale's decimals ("-1.25"); returns the length it
 * needs, NUL-terminated when cap > 0. Parses the same form (a leading '+' or
 * '-', digits, at most the scale's decimals; ',' is read as '.'); false for
 * anything else, including overflow. */
gates_usize_t gates_range_format(gates_i64 value, gates_u32 scale, char *buf, gates_usize_t cap);
bool gates_range_parse(gates_str_t text, gates_u32 scale, gates_i64 *out);

#endif /* GATES_INPUTS_H */
