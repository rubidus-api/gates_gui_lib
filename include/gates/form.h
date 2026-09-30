/* gates_gui_lib - forms: labelled fields with help and error text.
 *
 * A form is a node with the FORM layout: every row is a label beside an
 * editor (the label column is as wide as the widest label); when the form is
 * narrower than that label column plus 12 average character widths, each label goes above its
 * editor instead. Under each editor the row can show a help line and an error
 * line. Rows are addressed by the application's field ids, which stay stable
 * when rows are hidden or moved.
 *
 * The form holds no data and validates nothing. The application keeps its
 * draft, reads the editors (or follows their events), checks the values, puts
 * messages on the fields with gates_form_set_error, and saves after a
 * successful submit. Every add builds its row completely before attaching it:
 * on failure nothing of it is left. Platform-free. */
#ifndef GATES_FORM_H
#define GATES_FORM_H

#include <gates/tree.h>
#include <gates/widget.h>

typedef struct gates_field_desc_t {
    gates_str_t label;           /* copied; shown in the label column */
    gates_str_t help;            /* copied; optional line under the editor */
    bool required;               /* the label gets a " *" marker */
    bool read_only;              /* text: read-only box; other editors: disabled */
    /* Text fields: initial text, width in average characters (0 -> 24), maximum length in
     * bytes (0 = none), password. Checkbox fields: `text` is the box's caption. */
    gates_str_t text;
    gates_u32 cols;
    gates_u32 max_bytes;
    bool password;
    bool checked;                /* checkbox */
    /* Choice and radio fields. */
    const gates_option_t *options;
    gates_u32 option_count;
    gates_u32 selected_id;
} gates_field_desc_t;

/* A form node: the rows added below line up as label | editor. */
[[nodiscard]] gates_err_t gates_form_create(gates_tree_t *tree, gates_node_t parent,
                                            gates_node_t *out_form);

/* Each adds one row at the end. field_id: nonzero and unique in the form
 * (INVALID_ARG otherwise). *out_editor (may be null) receives the editor node,
 * for handlers and reading values. */
[[nodiscard]] gates_err_t gates_form_add_text(gates_tree_t *tree, gates_node_t form,
                                              gates_u32 field_id, const gates_field_desc_t *desc,
                                              gates_node_t *out_editor);
[[nodiscard]] gates_err_t gates_form_add_checkbox(gates_tree_t *tree, gates_node_t form,
                                                  gates_u32 field_id,
                                                  const gates_field_desc_t *desc,
                                                  gates_node_t *out_editor);
[[nodiscard]] gates_err_t gates_form_add_choice(gates_tree_t *tree, gates_node_t form,
                                                gates_u32 field_id, const gates_field_desc_t *desc,
                                                gates_node_t *out_editor);
[[nodiscard]] gates_err_t gates_form_add_radio(gates_tree_t *tree, gates_node_t form,
                                               gates_u32 field_id, const gates_field_desc_t *desc,
                                               gates_node_t *out_editor);

/* Shows `message` (copied) under the field's editor in the error colour and
 * marks a text editor invalid; an empty message clears both. */
[[nodiscard]] gates_err_t gates_form_set_error(gates_tree_t *tree, gates_node_t form,
                                               gates_u32 field_id, gates_str_t message);
/* The field's editor, row, or GATES_NODE_NULL for an unknown id. */
gates_node_t gates_form_editor(const gates_tree_t *tree, gates_node_t form, gates_u32 field_id);
gates_node_t gates_form_row(const gates_tree_t *tree, gates_node_t form, gates_u32 field_id);
/* The field id whose editor is `editor`, or 0 (useful in a shared handler). */
gates_u32 gates_form_field_of(const gates_tree_t *tree, gates_node_t form, gates_node_t editor);
/* Hides or shows a whole row (label, editor, help, error). */
[[nodiscard]] gates_err_t gates_form_set_row_hidden(gates_tree_t *tree, gates_node_t form,
                                                    gates_u32 field_id, bool hidden);
gates_u32 gates_form_field_count(const gates_tree_t *tree, gates_node_t form);

#endif /* GATES_FORM_H */
