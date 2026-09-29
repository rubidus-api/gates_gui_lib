/* gates_gui_lib - property grid (plan-0021): categories as collapsible group
 * boxes of label/editor grids, built from the ordinary controls; one handler
 * hears every property change by id. Platform-free. */
#include <gates/propgrid.h>
#include <gates/layout.h>
#include <gates/access.h>
#include "gates_tree_internal.h"

#include <string.h>

enum { PROP_TEXT, PROP_BOOL, PROP_CHOICE, PROP_NUMBER };

typedef struct pg_prop {
    gates_prop_id_t id;
    gates_node_t editor;
    int kind;
} pg_prop;

typedef struct pg_cat {
    gates_u8 *title;             /* owned copy */
    gates_u32 len;
    gates_node_t group, content;
} pg_cat;

struct gates_i_propgrid {
    gates_node_t self;
    gates_node_t general;        /* properties without a category */
    pg_prop *props;
    gates_u32 nprops, prop_cap;
    pg_cat *cats;
    gates_u32 ncats, cat_cap;
    gates_event_fn fn;
    void *user;
};

void gates_i_propgrid_free(gates_tree_t *tree, gates_widget_state_t *st) {
    struct gates_i_propgrid *pg = st->pgrid;
    if (pg == nullptr) return;
    gates_allocator_t a = tree->alloc;
    for (gates_u32 i = 0; i < pg->ncats; i++) a.free_fn(a.ctx, pg->cats[i].title);
    if (pg->cats != nullptr) a.free_fn(a.ctx, pg->cats);
    if (pg->props != nullptr) a.free_fn(a.ctx, pg->props);
    a.free_fn(a.ctx, pg);
    st->pgrid = nullptr;
}

static struct gates_i_propgrid *pg_of(const gates_tree_t *tree, gates_node_t grid) {
    if (tree == nullptr || !gates_i_valid(tree, grid)) return nullptr;
    const gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, grid.index)->state_index);
    return st != nullptr ? st->pgrid : nullptr;
}

/* An editor's change, heard by the grid's own bubble handler, becomes one
 * VALUE_CHANGED from the grid carrying the property id. */
static void on_bubble(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    struct gates_i_propgrid *pg = user;
    if (pg->fn == nullptr) return;
    for (gates_u32 i = 0; i < pg->nprops; i++) {
        const pg_prop *p = &pg->props[i];
        if (!gates_node_eq(ev->source, p->editor)) continue;
        bool wanted = p->kind == PROP_TEXT ? ev->kind == GATES_EVENT_TEXT_CHANGED : ev->kind == GATES_EVENT_VALUE_CHANGED;
        if (!wanted) return;
        gates_event_t out = *ev;
        out.kind = GATES_EVENT_VALUE_CHANGED;
        out.source = pg->self;
        out.result = p->id;
        if (p->kind == PROP_CHOICE) out.value = ev->result; /* the chosen option */
        pg->fn(tree, &out, pg->user);
        return;
    }
}

/* A two-column grid of names and editors; the editors take the spare width. */
static gates_err_t make_grid(gates_tree_t *tree, gates_node_t panel) {
    gates_err_t err = gates_layout_set(tree, panel, GATES_LAYOUT_KIND_GRID);
    if (gates_is_ok(err)) err = gates_layout_set_grid(tree, panel, 2);
    if (gates_is_ok(err)) err = gates_layout_set_grid_column_grow(tree, panel, 1, 1);
    return err;
}

gates_err_t gates_propgrid_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_grid) {
    if (tree == nullptr || out_grid == nullptr ||
        (!gates_node_eq(parent, GATES_NODE_NULL) && !gates_i_valid(tree, parent))) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_grid = GATES_NODE_NULL;
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t r = a.alloc_fn(a.ctx, sizeof(struct gates_i_propgrid), alignof(struct gates_i_propgrid));
    if (!proven_is_ok(r.err)) return r.err;
    struct gates_i_propgrid *pg = (struct gates_i_propgrid *)r.value.ptr;
    memset(pg, 0, sizeof *pg);
    gates_node_t grid = GATES_NODE_NULL;
    gates_node_desc_t nd = { .kind = GATES_NODE_PANEL };
    gates_err_t err = gates_node_create(tree, GATES_NODE_NULL, &nd, &grid);
    gates_u32 state = GATES_NONE;
    bool owned = false;
    if (gates_is_ok(err)) {
        err = gates_i_state_acquire(tree, &state);
        if (gates_is_ok(err)) {
            gates_i_slot(tree, grid.index)->state_index = state;
            gates_i_state(tree, state)->pgrid = pg;
            owned = true;
            pg->self = grid;
        }
    }
    if (gates_is_ok(err)) err = gates_layout_set(tree, grid, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_node_create(tree, grid, &nd, &pg->general);
    if (gates_is_ok(err)) err = make_grid(tree, pg->general);
    if (gates_is_ok(err)) err = gates_node_set_bubble_handler(tree, grid, on_bubble, pg);
    if (gates_is_ok(err) && !gates_node_eq(parent, GATES_NODE_NULL)) err = gates_node_append(tree, parent, grid);
    if (!gates_is_ok(err)) {
        if (!owned) a.free_fn(a.ctx, pg);
        if (!gates_node_eq(grid, GATES_NODE_NULL)) {
            (void)gates_node_set_bubble_handler(tree, grid, nullptr, nullptr);
            gates_i_discard_detached(tree, grid);
        }
        return err;
    }
    *out_grid = grid;
    return GATES_OK;
}

static pg_cat *find_cat(struct gates_i_propgrid *pg, gates_str_t category) {
    for (gates_u32 i = 0; i < pg->ncats; i++) {
        pg_cat *c = &pg->cats[i];
        if (c->len == category.size && memcmp(c->title, category.ptr, category.size) == 0) return c;
    }
    return nullptr;
}

static const pg_prop *find_prop(const struct gates_i_propgrid *pg, gates_prop_id_t id) {
    for (gates_u32 i = 0; i < pg->nprops; i++) {
        if (pg->props[i].id == id) return &pg->props[i];
    }
    return nullptr;
}

/* Room for one more entry in an array of `size`-byte entries. */
static gates_err_t reserve(gates_tree_t *tree, void **arr, gates_u32 n, gates_u32 *cap, gates_usize_t size,
                           gates_usize_t align) {
    if (n < *cap) return GATES_OK;
    gates_u32 nc = *cap == 0 ? 8u : *cap * 2u;
    gates_allocator_t a = tree->alloc;
    proven_result_mem_mut_t r = *arr == nullptr ? a.alloc_fn(a.ctx, nc * size, align)
                                                : a.realloc_fn(a.ctx, *arr, *cap * size, nc * size, align);
    if (!proven_is_ok(r.err)) return r.err;
    *arr = r.value.ptr;
    *cap = nc;
    return GATES_OK;
}

typedef struct editor_args_t {
    int kind;
    gates_str_t text;
    bool checked;
    const gates_option_t *options;
    gates_u32 count;
    gates_u32 selected;
    const gates_range_t *range;
} editor_args_t;

static gates_err_t make_editor(gates_tree_t *tree, gates_node_t content, const editor_args_t *e, gates_node_t *out) {
    switch (e->kind) {
    case PROP_TEXT: return gates_textbox_create(tree, content, e->text, 16, out);
    case PROP_BOOL: return gates_checkbox_create(tree, content, GATES_STR(""), e->checked, nullptr, nullptr, out);
    case PROP_CHOICE: return gates_choice_create(tree, content, e->options, e->count, e->selected, out);
    default: return gates_spin_create(tree, content, e->range, out);
    }
}

static gates_err_t add(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id,
                       gates_str_t name, const editor_args_t *e) {
    struct gates_i_propgrid *pg = pg_of(tree, grid);
    if (pg == nullptr || id == 0 || find_prop(pg, id) != nullptr || (name.size > 0 && name.ptr == nullptr) ||
        (category.size > 0 && category.ptr == nullptr) || category.size > 0xFFFFu) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_err_t err = reserve(tree, (void **)&pg->props, pg->nprops, &pg->prop_cap, sizeof(pg_prop), alignof(pg_prop));
    if (!gates_is_ok(err)) return err;
    pg_cat *cat = category.size > 0 ? find_cat(pg, category) : nullptr;
    gates_node_t content = cat != nullptr ? cat->content : pg->general;
    pg_cat fresh = {0};
    if (category.size > 0 && cat == nullptr) {
        /* A new category: its group box goes after the others. */
        err = reserve(tree, (void **)&pg->cats, pg->ncats, &pg->cat_cap, sizeof(pg_cat), alignof(pg_cat));
        if (!gates_is_ok(err)) return err;
        proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, category.size, 1);
        if (!proven_is_ok(r.err)) return r.err;
        fresh.title = (gates_u8 *)r.value.ptr;
        memcpy(fresh.title, category.ptr, category.size);
        fresh.len = (gates_u32)category.size;
        err = gates_group_create(tree, grid, category, true, &fresh.group, &fresh.content);
        if (gates_is_ok(err)) err = make_grid(tree, fresh.content);
        if (!gates_is_ok(err)) {
            tree->alloc.free_fn(tree->alloc.ctx, fresh.title);
            gates_i_node_undo(tree, fresh.group);
            return err;
        }
        content = fresh.content;
    }
    gates_node_t label = GATES_NODE_NULL, editor = GATES_NODE_NULL;
    err = gates_label_create(tree, content, name, &label);
    if (gates_is_ok(err)) err = make_editor(tree, content, e, &editor);
    if (gates_is_ok(err)) err = gates_node_set_labelled_by(tree, editor, label);
    if (!gates_is_ok(err)) {
        gates_i_node_undo(tree, editor);
        gates_i_node_undo(tree, label);
        if (fresh.title != nullptr) {
            tree->alloc.free_fn(tree->alloc.ctx, fresh.title);
            gates_i_node_undo(tree, fresh.group);
        }
        return err;
    }
    if (fresh.title != nullptr) pg->cats[pg->ncats++] = fresh;
    pg->props[pg->nprops++] = (pg_prop){ .id = id, .editor = editor, .kind = e->kind };
    return GATES_OK;
}

gates_err_t gates_propgrid_add_text(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id,
                                    gates_str_t name, gates_str_t value) {
    return add(tree, grid, category, id, name, &(editor_args_t){ .kind = PROP_TEXT, .text = value });
}

gates_err_t gates_propgrid_add_bool(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id,
                                    gates_str_t name, bool value) {
    return add(tree, grid, category, id, name, &(editor_args_t){ .kind = PROP_BOOL, .checked = value });
}

gates_err_t gates_propgrid_add_choice(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id,
                                      gates_str_t name, const gates_option_t *options, gates_u32 count,
                                      gates_u32 selected_id) {
    return add(tree, grid, category, id, name,
               &(editor_args_t){ .kind = PROP_CHOICE, .options = options, .count = count, .selected = selected_id });
}

gates_err_t gates_propgrid_add_number(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id,
                                      gates_str_t name, const gates_range_t *range) {
    if (range == nullptr) return PROVEN_ERR_INVALID_ARG;
    return add(tree, grid, category, id, name, &(editor_args_t){ .kind = PROP_NUMBER, .range = range });
}

gates_node_t gates_propgrid_editor(const gates_tree_t *tree, gates_node_t grid, gates_prop_id_t id) {
    const struct gates_i_propgrid *pg = pg_of(tree, grid);
    const pg_prop *p = pg != nullptr ? find_prop(pg, id) : nullptr;
    return p != nullptr ? p->editor : GATES_NODE_NULL;
}

gates_node_t gates_propgrid_category(const gates_tree_t *tree, gates_node_t grid, gates_str_t category) {
    struct gates_i_propgrid *pg = pg_of(tree, grid);
    if (pg == nullptr || category.size == 0 || category.ptr == nullptr) return GATES_NODE_NULL;
    const pg_cat *c = find_cat(pg, category);
    return c != nullptr ? c->group : GATES_NODE_NULL;
}

gates_u32 gates_propgrid_count(const gates_tree_t *tree, gates_node_t grid) {
    const struct gates_i_propgrid *pg = pg_of(tree, grid);
    return pg != nullptr ? pg->nprops : 0;
}

gates_err_t gates_propgrid_set_handler(gates_tree_t *tree, gates_node_t grid, gates_event_fn fn, void *user) {
    struct gates_i_propgrid *pg = pg_of(tree, grid);
    if (pg == nullptr) return PROVEN_ERR_INVALID_ARG;
    pg->fn = fn;
    pg->user = user;
    return GATES_OK;
}
