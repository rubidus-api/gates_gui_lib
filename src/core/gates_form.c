/* gates_gui_lib - form helper: labelled field rows with help and error lines
 * on the FORM layout (plan-0010 stage 2, RFC-0003 7). The form keeps a table
 * of field id -> row, editor and error line; it holds no values. Platform-free. */
#include <gates/form.h>
#include <gates/layout.h>
#include "gates_tree_internal.h"

#include <string.h>

typedef struct gates_i_field {
    gates_u32 id;
    gates_node_t row;
    gates_node_t editor;
    gates_node_t error;
    gates_node_t label;
    gates_node_t help;           /* null when the field has no help line */
    bool required;
} gates_i_field_t;

typedef enum field_kind_t {
    FIELD_TEXT,
    FIELD_CHECKBOX,
    FIELD_CHOICE,
    FIELD_RADIO,
} field_kind_t;

void gates_i_form_free(gates_tree_t *tree, gates_widget_state_t *st) {
    if (st->fields != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, st->fields);
    }
    st->fields = nullptr;
    st->field_count = 0;
    st->field_cap = 0;
}

static gates_widget_state_t *form_state(const gates_tree_t *tree, gates_node_t form) {
    if (tree == nullptr || !gates_i_valid(tree, form) ||
        gates_i_slot(tree, form.index)->kind != GATES_NODE_FORM) {
        return nullptr;
    }
    return gates_i_state(tree, gates_i_slot(tree, form.index)->state_index);
}

/* Drops fields whose row the application destroyed; their ids are free again. */
static void compact(const gates_tree_t *tree, gates_widget_state_t *st) {
    gates_u32 out = 0;
    for (gates_u32 i = 0; i < st->field_count; i++) {
        if (gates_i_valid(tree, st->fields[i].row)) {
            st->fields[out++] = st->fields[i];
        }
    }
    st->field_count = out;
}

static const gates_i_field_t *find(const gates_tree_t *tree, const gates_widget_state_t *st,
                                   gates_u32 id) {
    for (gates_u32 i = 0; st != nullptr && id != 0 && i < st->field_count; i++) {
        if (st->fields[i].id == id && gates_i_valid(tree, st->fields[i].row)) {
            return &st->fields[i];
        }
    }
    return nullptr;
}

gates_err_t gates_form_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_form) {
    if (tree == nullptr || out_form == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_form = GATES_NODE_NULL;
    if (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_desc_t nd = { .kind = GATES_NODE_FORM };
    gates_node_t form = GATES_NODE_NULL;
    gates_err_t err = gates_node_create(tree, GATES_NODE_NULL, &nd, &form);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_u32 state = GATES_NONE;
    err = gates_i_state_acquire(tree, &state);
    if (gates_is_ok(err)) {
        gates_i_slot(tree, form.index)->state_index = state;
        err = gates_layout_set(tree, form, GATES_LAYOUT_KIND_FORM);
    }
    if (gates_is_ok(err)) err = gates_layout_set_gap(tree, form, 8);
    if (gates_is_ok(err) && !gates_node_eq(parent, GATES_NODE_NULL)) {
        err = gates_node_append(tree, parent, form);
    }
    if (!gates_is_ok(err)) {
        gates_i_discard_detached(tree, form);
        return err;
    }
    *out_form = form;
    return GATES_OK;
}

/* Creates the editor of one kind inside `cell` and applies the descriptor. */
static gates_err_t make_editor(gates_tree_t *tree, gates_node_t cell, field_kind_t kind,
                               const gates_field_desc_t *d, gates_node_t *editor) {
    gates_err_t err;
    switch (kind) {
    case FIELD_TEXT:
        err = gates_textbox_create(tree, cell, d->text, d->cols != 0 ? d->cols : 24u, editor);
        if (gates_is_ok(err) && d->max_bytes != 0) {
            err = gates_textbox_set_max_bytes(tree, *editor, d->max_bytes);
        }
        if (gates_is_ok(err) && d->password) err = gates_textbox_set_password(tree, *editor, true);
        if (gates_is_ok(err) && d->read_only) err = gates_textbox_set_read_only(tree, *editor, true);
        break;
    case FIELD_CHECKBOX:
        err = gates_checkbox_create(tree, cell, d->text, d->checked, nullptr, nullptr, editor);
        break;
    case FIELD_CHOICE:
        err = gates_choice_create(tree, cell, d->options, d->option_count, d->selected_id, editor);
        break;
    case FIELD_RADIO:
    default:
        err = gates_radio_create(tree, cell, d->options, d->option_count, d->selected_id, editor);
        break;
    }
    if (gates_is_ok(err) && d->read_only && kind != FIELD_TEXT) {
        err = gates_widget_set_disabled(tree, *editor, true);
    }
    if (gates_is_ok(err)) {
        err = gates_layout_set_child_align(tree, *editor, GATES_ALIGN_START_V);
    }
    return err;
}

/* Builds row = [label, cell = [editor, help?, error]] detached; nothing is
 * attached until every part exists. */
static gates_err_t build_row(gates_tree_t *tree, field_kind_t kind, const gates_field_desc_t *d,
                             gates_node_t *out_row, gates_node_t *out_editor,
                             gates_node_t *out_error, gates_node_t *out_label,
                             gates_node_t *out_help) {
    gates_node_desc_t nd = { .kind = GATES_NODE_CUSTOM };
    gates_node_t row = GATES_NODE_NULL;
    gates_err_t err = gates_node_create(tree, GATES_NODE_NULL, &nd, &row);
    if (!gates_is_ok(err)) {
        return err;
    }
    /* "Label *" for a required field. */
    gates_u8 *marked = nullptr;
    gates_str_t label = d->label;
    if (d->required) {
        proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, label.size + 2, 1);
        err = r.err;
        if (gates_is_ok(err)) {
            marked = (gates_u8 *)r.value.ptr;
            if (label.size > 0) memcpy(marked, label.ptr, label.size);
            marked[label.size] = ' ';
            marked[label.size + 1] = '*';
            label = (gates_str_t){ .ptr = marked, .size = d->label.size + 2 };
        }
    }
    gates_node_t lab = GATES_NODE_NULL, cell = GATES_NODE_NULL, editor = GATES_NODE_NULL;
    gates_node_t help = GATES_NODE_NULL, error = GATES_NODE_NULL;
    if (gates_is_ok(err)) err = gates_label_create(tree, row, label, &lab);
    if (marked != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, marked);
    }
    if (gates_is_ok(err)) err = gates_node_create(tree, row, &nd, &cell);
    if (gates_is_ok(err)) err = gates_layout_set(tree, cell, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_layout_set_gap(tree, cell, 2);
    if (gates_is_ok(err)) err = make_editor(tree, cell, kind, d, &editor);
    if (gates_is_ok(err) && d->help.size > 0) err = gates_label_create(tree, cell, d->help, &help);
    if (gates_is_ok(err)) err = gates_label_create(tree, cell, (gates_str_t){0}, &error);
    if (gates_is_ok(err)) err = gates_node_set_hidden(tree, error, true);
    if (!gates_is_ok(err)) {
        gates_i_discard_detached(tree, row);
        return err;
    }
    gates_i_state(tree, gates_i_slot(tree, error.index)->state_index)->invalid = true; /* error colour */
    *out_row = row;
    *out_editor = editor;
    *out_error = error;
    *out_label = lab;
    *out_help = help;
    return GATES_OK;
}

static gates_err_t add_field(gates_tree_t *tree, gates_node_t form, gates_u32 id,
                             const gates_field_desc_t *desc, field_kind_t kind,
                             gates_node_t *out_editor) {
    if (out_editor != nullptr) {
        *out_editor = GATES_NODE_NULL;
    }
    gates_widget_state_t *st = form_state(tree, form);
    if (st == nullptr || desc == nullptr || id == 0 ||
        (desc->label.size > 0 && desc->label.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    compact(tree, st);
    if (find(tree, st, id) != nullptr) {
        return PROVEN_ERR_INVALID_ARG; /* ids are unique in a form */
    }
    if (st->field_count == st->field_cap) {
        gates_u32 cap = st->field_cap != 0 ? st->field_cap * 2 : 8;
        gates_allocator_t a = tree->alloc;
        proven_result_mem_mut_t r =
            st->fields == nullptr
                ? a.alloc_fn(a.ctx, cap * sizeof(gates_i_field_t), alignof(gates_i_field_t))
                : a.realloc_fn(a.ctx, st->fields, st->field_cap * sizeof(gates_i_field_t),
                               cap * sizeof(gates_i_field_t), alignof(gates_i_field_t));
        if (!proven_is_ok(r.err)) {
            return r.err;
        }
        st->fields = (gates_i_field_t *)r.value.ptr;
        st->field_cap = cap;
    }
    gates_node_t row, editor, error, label, help;
    gates_err_t err = build_row(tree, kind, desc, &row, &editor, &error, &label, &help);
    if (!gates_is_ok(err)) {
        return err;
    }
    err = gates_node_append(tree, form, row);
    if (!gates_is_ok(err)) {
        gates_i_discard_detached(tree, row);
        return err;
    }
    st = form_state(tree, form); /* the state pool may have moved while building */
    st->fields[st->field_count++] = (gates_i_field_t){
        .id = id, .row = row, .editor = editor, .error = error, .label = label, .help = help,
        .required = desc->required,
    };
    if (out_editor != nullptr) {
        *out_editor = editor;
    }
    return GATES_OK;
}

gates_err_t gates_form_add_text(gates_tree_t *tree, gates_node_t form, gates_u32 field_id,
                                const gates_field_desc_t *desc, gates_node_t *out_editor) {
    return add_field(tree, form, field_id, desc, FIELD_TEXT, out_editor);
}

gates_err_t gates_form_add_checkbox(gates_tree_t *tree, gates_node_t form, gates_u32 field_id,
                                    const gates_field_desc_t *desc, gates_node_t *out_editor) {
    return add_field(tree, form, field_id, desc, FIELD_CHECKBOX, out_editor);
}

gates_err_t gates_form_add_choice(gates_tree_t *tree, gates_node_t form, gates_u32 field_id,
                                  const gates_field_desc_t *desc, gates_node_t *out_editor) {
    return add_field(tree, form, field_id, desc, FIELD_CHOICE, out_editor);
}

gates_err_t gates_form_add_radio(gates_tree_t *tree, gates_node_t form, gates_u32 field_id,
                                 const gates_field_desc_t *desc, gates_node_t *out_editor) {
    return add_field(tree, form, field_id, desc, FIELD_RADIO, out_editor);
}

gates_err_t gates_form_set_error(gates_tree_t *tree, gates_node_t form, gates_u32 field_id,
                                 gates_str_t message) {
    const gates_i_field_t *f = find(tree, form_state(tree, form), field_id);
    if (f == nullptr || !gates_i_valid(tree, f->error)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_node_t error = f->error, editor = f->editor;
    gates_err_t err = gates_widget_set_text(tree, error, message); /* the only allocation */
    if (!gates_is_ok(err)) {
        return err;
    }
    bool shown = message.size > 0;
    err = gates_node_set_hidden(tree, error, !shown);
    if (gates_is_ok(err) && gates_i_valid(tree, editor) &&
        gates_i_slot(tree, editor.index)->kind == GATES_NODE_TEXTBOX) {
        err = gates_textbox_set_invalid(tree, editor, shown);
    }
    return err;
}

gates_node_t gates_form_editor(const gates_tree_t *tree, gates_node_t form, gates_u32 field_id) {
    const gates_i_field_t *f = find(tree, form_state(tree, form), field_id);
    return f != nullptr && gates_i_valid(tree, f->editor) ? f->editor : GATES_NODE_NULL;
}

gates_node_t gates_form_row(const gates_tree_t *tree, gates_node_t form, gates_u32 field_id) {
    const gates_i_field_t *f = find(tree, form_state(tree, form), field_id);
    return f != nullptr ? f->row : GATES_NODE_NULL;
}

gates_u32 gates_form_field_of(const gates_tree_t *tree, gates_node_t form, gates_node_t editor) {
    const gates_widget_state_t *st = form_state(tree, form);
    for (gates_u32 i = 0; st != nullptr && i < st->field_count; i++) {
        if (gates_node_eq(st->fields[i].editor, editor) && gates_i_valid(tree, st->fields[i].row)) {
            return st->fields[i].id;
        }
    }
    return 0;
}

gates_err_t gates_form_set_row_hidden(gates_tree_t *tree, gates_node_t form, gates_u32 field_id,
                                      bool hidden) {
    const gates_i_field_t *f = find(tree, form_state(tree, form), field_id);
    if (f == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    return gates_node_set_hidden(tree, f->row, hidden);
}

gates_u32 gates_form_field_count(const gates_tree_t *tree, gates_node_t form) {
    const gates_widget_state_t *st = form_state(tree, form);
    gates_u32 n = 0;
    for (gates_u32 i = 0; st != nullptr && i < st->field_count; i++) {
        n += gates_i_valid(tree, st->fields[i].row) ? 1u : 0u;
    }
    return n;
}

bool gates_i_form_field_info(const gates_tree_t *tree, gates_u32 editor_idx,
                             gates_i_field_info_t *out) {
    /* editor -> cell -> row -> form */
    gates_u32 cell = gates_i_slot(tree, editor_idx)->parent;
    gates_u32 row = cell != GATES_NONE ? gates_i_slot(tree, cell)->parent : GATES_NONE;
    gates_u32 form = row != GATES_NONE ? gates_i_slot(tree, row)->parent : GATES_NONE;
    if (form == GATES_NONE || gates_i_slot(tree, form)->kind != GATES_NODE_FORM) {
        return false;
    }
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, form)->state_index);
    for (gates_u32 i = 0; st != nullptr && i < st->field_count; i++) {
        const gates_i_field_t *f = &st->fields[i];
        if (f->editor.index == editor_idx && gates_i_valid(tree, f->editor)) {
            *out = (gates_i_field_info_t){ .id = f->id, .label = f->label, .help = f->help,
                                           .error = f->error, .required = f->required };
            return true;
        }
    }
    return false;
}
