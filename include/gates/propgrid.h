/* gates_gui_lib - property grid (0.6.0): a record's typed
 * fields as rows of a name and an editor, grouped by category.
 *
 * A property grid is a panel built from the ordinary controls: each category
 * is a collapsible group box (its title a Tab stop), holding a two-column
 * grid of labels and editors - a text box, a check box, a choice or a spin
 * box. Properties without a category come first, outside any group. Each
 * editor is named by its label for assistive technology, and Tab walks the
 * editors in order. A property is known by a stable id (nonzero, unique in
 * the grid); gates_propgrid_editor gives its editor, whose own functions set
 * and read the value (gates_textbox_set_text, gates_checkbox_set_checked,
 * gates_options_set_selected, gates_range_set_value, ...).
 *
 * Changes a person makes are reported to the grid's handler as one kind of
 * event: GATES_EVENT_VALUE_CHANGED with ev->source = the grid, ev->result =
 * the property id, and the value in ev->text (text), ev->checked (bool) or
 * ev->value (a number, or the chosen option id). Text reports every committed
 * change, as a text box does. Program changes through the editors are
 * silent. Platform-free. */
#ifndef GATES_PROPGRID_H
#define GATES_PROPGRID_H

#include <gates/tree.h>
#include <gates/event.h>
#include <gates/widget.h>
#include <gates/inputs.h>

typedef gates_u32 gates_prop_id_t;

/* An empty property grid; add properties below. */
[[nodiscard]] gates_err_t gates_propgrid_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_grid);

/* Adds a property at the end of its category (made, as a collapsible group
 * after the others, the first time it is named; empty = no category). The
 * name and category are copied. INVALID_ARG for id 0, an id already in the
 * grid, or what the editor itself refuses (a bad option list or range);
 * nothing is left behind on failure. */
[[nodiscard]] gates_err_t gates_propgrid_add_text(gates_tree_t *tree, gates_node_t grid, gates_str_t category,
                                                  gates_prop_id_t id, gates_str_t name, gates_str_t value);
[[nodiscard]] gates_err_t gates_propgrid_add_bool(gates_tree_t *tree, gates_node_t grid, gates_str_t category,
                                                  gates_prop_id_t id, gates_str_t name, bool value);
[[nodiscard]] gates_err_t gates_propgrid_add_choice(gates_tree_t *tree, gates_node_t grid, gates_str_t category,
                                                    gates_prop_id_t id, gates_str_t name,
                                                    const gates_option_t *options, gates_u32 count,
                                                    gates_u32 selected_id);
[[nodiscard]] gates_err_t gates_propgrid_add_number(gates_tree_t *tree, gates_node_t grid, gates_str_t category,
                                                    gates_prop_id_t id, gates_str_t name, const gates_range_t *range);

/* The property's editor, or GATES_NODE_NULL for an unknown id. */
gates_node_t gates_propgrid_editor(const gates_tree_t *tree, gates_node_t grid, gates_prop_id_t id);
/* The category's group box, or GATES_NODE_NULL (to fold it, or save its state). */
gates_node_t gates_propgrid_category(const gates_tree_t *tree, gates_node_t grid, gates_str_t category);
gates_u32 gates_propgrid_count(const gates_tree_t *tree, gates_node_t grid);

/* The handler for property changes (null removes it). `user` is borrowed
 * while registered. The grid hears its editors through a bubble handler of
 * its own: do not set another on the grid node itself. */
[[nodiscard]] gates_err_t gates_propgrid_set_handler(gates_tree_t *tree, gates_node_t grid, gates_event_fn fn,
                                                     void *user);

#endif /* GATES_PROPGRID_H */
