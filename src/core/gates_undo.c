/* gates_gui_lib - undo stack (0.6.0): entries with undo
 * and redo functions, merge runs, a bound, a clean mark and commands kept in
 * step. Not a node; platform-free. */
#include <gates/undo.h>
#include <proven/heap.h>
#include "gates_tree_internal.h"

#include <string.h>

typedef struct entry_t {
    gates_u8 *label;
    gates_u32 label_len;
    gates_err_t (*undo)(void *);
    void *undo_data;
    void (*undo_drop)(void *);
    gates_err_t (*redo)(void *);
    void *redo_data;
    void (*redo_drop)(void *);
    gates_u32 merge_key;
} entry_t;

struct gates_undo {
    gates_allocator_t alloc;
    entry_t *entries;
    gates_u32 n, cap, max;
    gates_u32 pos;               /* entries applied: undo takes entries[pos - 1] */
    gates_i64 clean;             /* the clean state's pos, -1 when it cannot come back */
    bool merge_open;
    bool busy;
    /* The bound commands. */
    gates_tree_t *tree;
    gates_node_t scope;
    gates_command_id_t undo_cmd, redo_cmd;
    gates_u8 *words;             /* undo word, then redo word */
    gates_u32 undo_word_len, redo_word_len;
};

static void drop_entry(entry_t *e) {
    if (e->undo_drop != nullptr) e->undo_drop(e->undo_data);
    if (e->redo_data != e->undo_data && e->redo_drop != nullptr) e->redo_drop(e->redo_data);
}

static void free_entry(gates_undo_t *u, entry_t *e) {
    drop_entry(e);
    if (e->label != nullptr) u->alloc.free_fn(u->alloc.ctx, e->label);
    *e = (entry_t){0};
}

/* "Word Label" (or the word alone) on one command; enabled when there is an
 * entry `e` to run. A scope that went away refuses both calls. */
static void sync_one(gates_undo_t *u, gates_command_id_t id, const gates_u8 *word, gates_u32 wlen, const entry_t *e) {
    (void)gates_command_set_enabled(u->tree, u->scope, id, e != nullptr);
    gates_u32 llen = e != nullptr ? e->label_len : 0;
    gates_usize_t n = wlen + (llen > 0 ? 1u + llen : 0u);
    proven_result_mem_mut_t r = u->alloc.alloc_fn(u->alloc.ctx, n > 0 ? n : 1, 1);
    if (!proven_is_ok(r.err)) return; /* the label stays as it was */
    gates_u8 *b = (gates_u8 *)r.value.ptr;
    memcpy(b, word, wlen);
    if (llen > 0) {
        b[wlen] = ' ';
        memcpy(b + wlen + 1, e->label, llen);
    }
    (void)gates_command_set_label(u->tree, u->scope, id, (gates_str_t){ .ptr = b, .size = n });
    u->alloc.free_fn(u->alloc.ctx, b);
}

static void sync(gates_undo_t *u) {
    if (u->tree == nullptr) return;
    sync_one(u, u->undo_cmd, u->words, u->undo_word_len, u->pos > 0 ? &u->entries[u->pos - 1] : nullptr);
    sync_one(u, u->redo_cmd, u->words + u->undo_word_len, u->redo_word_len, u->pos < u->n ? &u->entries[u->pos] : nullptr);
}

gates_err_t gates_undo_create(gates_allocator_t alloc, gates_u32 max_entries, gates_undo_t **out) {
    if (out == nullptr) return PROVEN_ERR_INVALID_ARG;
    *out = nullptr;
    if (alloc.alloc_fn == nullptr) alloc = proven_heap_allocator();
    proven_result_mem_mut_t r = alloc.alloc_fn(alloc.ctx, sizeof(gates_undo_t), alignof(gates_undo_t));
    if (!proven_is_ok(r.err)) return r.err;
    gates_undo_t *u = (gates_undo_t *)r.value.ptr;
    *u = (gates_undo_t){ .alloc = alloc, .max = max_entries != 0 ? max_entries : 100u };
    *out = u;
    return GATES_OK;
}

void gates_undo_destroy(gates_undo_t *u) {
    if (u == nullptr) return;
    for (gates_u32 i = 0; i < u->n; i++) free_entry(u, &u->entries[i]);
    if (u->entries != nullptr) u->alloc.free_fn(u->alloc.ctx, u->entries);
    if (u->words != nullptr) u->alloc.free_fn(u->alloc.ctx, u->words);
    u->alloc.free_fn(u->alloc.ctx, u);
}

/* Refused entries still hand over their data: drop it. */
static gates_err_t refuse(const gates_undo_entry_t *e, gates_err_t err) {
    gates_undo_entry_t copy = *e;
    if (copy.drop != nullptr) {
        copy.drop(copy.undo_data);
        if (copy.redo_data != copy.undo_data) copy.drop(copy.redo_data);
    }
    return err;
}

gates_err_t gates_undo_push(gates_undo_t *u, const gates_undo_entry_t *e) {
    if (e == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (u == nullptr || e->undo == nullptr || e->redo == nullptr || (e->label.size > 0 && e->label.ptr == nullptr) ||
        e->label.size > 0xFFFFu) {
        return refuse(e, PROVEN_ERR_INVALID_ARG);
    }
    if (u->busy) return refuse(e, PROVEN_ERR_BUSY);
    /* What could have been redone is gone. */
    for (gates_u32 i = u->pos; i < u->n; i++) free_entry(u, &u->entries[i]);
    u->n = u->pos;
    if (u->clean > (gates_i64)u->pos) u->clean = -1;
    if (e->merge_key != 0 && u->merge_open && u->pos > 0 && u->entries[u->pos - 1].merge_key == e->merge_key) {
        entry_t *top = &u->entries[u->pos - 1];
        if (top->redo_data != top->undo_data && top->redo_drop != nullptr) top->redo_drop(top->redo_data);
        if (e->undo_data != e->redo_data && e->drop != nullptr) e->drop(e->undo_data);
        top->redo = e->redo;
        top->redo_data = e->redo_data;
        top->redo_drop = e->drop;
        sync(u);
        return GATES_OK;
    }
    if (u->n == u->cap && u->cap < u->max) {
        gates_u32 nc = u->cap == 0 ? 8u : u->cap * 2u;
        if (nc > u->max) nc = u->max;
        proven_result_mem_mut_t r =
            u->entries == nullptr
                ? u->alloc.alloc_fn(u->alloc.ctx, nc * sizeof(entry_t), alignof(entry_t))
                : u->alloc.realloc_fn(u->alloc.ctx, u->entries, u->cap * sizeof(entry_t), nc * sizeof(entry_t),
                                      alignof(entry_t));
        if (!proven_is_ok(r.err)) return refuse(e, r.err);
        u->entries = (entry_t *)r.value.ptr;
        u->cap = nc;
    }
    gates_u8 *label = nullptr;
    if (e->label.size > 0) {
        proven_result_mem_mut_t r = u->alloc.alloc_fn(u->alloc.ctx, e->label.size, 1);
        if (!proven_is_ok(r.err)) return refuse(e, r.err);
        label = (gates_u8 *)r.value.ptr;
        memcpy(label, e->label.ptr, e->label.size);
    }
    if (u->n == u->max) {
        /* Full: the oldest entry goes, and the state before it with it. */
        free_entry(u, &u->entries[0]);
        memmove(&u->entries[0], &u->entries[1], (gates_usize_t)(u->n - 1) * sizeof(entry_t));
        u->n--;
        u->pos--;
        if (u->clean >= 0) u->clean--;
    }
    u->entries[u->n++] = (entry_t){ .label = label, .label_len = (gates_u32)e->label.size,
                                    .undo = e->undo, .undo_data = e->undo_data, .undo_drop = e->drop,
                                    .redo = e->redo, .redo_data = e->redo_data, .redo_drop = e->drop,
                                    .merge_key = e->merge_key };
    u->pos = u->n;
    u->merge_open = true; /* a run needs the same nonzero key anyway */
    sync(u);
    return GATES_OK;
}

gates_err_t gates_undo_undo(gates_undo_t *u) {
    if (u == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (u->busy) return PROVEN_ERR_BUSY;
    if (u->pos == 0) return PROVEN_ERR_INVALID_STATE;
    u->merge_open = false;
    entry_t *e = &u->entries[u->pos - 1];
    u->busy = true;
    gates_err_t err = e->undo(e->undo_data);
    u->busy = false;
    if (gates_is_ok(err)) u->pos--;
    sync(u);
    return err;
}

gates_err_t gates_undo_redo(gates_undo_t *u) {
    if (u == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (u->busy) return PROVEN_ERR_BUSY;
    if (u->pos == u->n) return PROVEN_ERR_INVALID_STATE;
    u->merge_open = false;
    entry_t *e = &u->entries[u->pos];
    u->busy = true;
    gates_err_t err = e->redo(e->redo_data);
    u->busy = false;
    if (gates_is_ok(err)) u->pos++;
    sync(u);
    return err;
}

bool gates_undo_can_undo(const gates_undo_t *u) { return u != nullptr && u->pos > 0; }
bool gates_undo_can_redo(const gates_undo_t *u) { return u != nullptr && u->pos < u->n; }

gates_str_t gates_undo_undo_label(const gates_undo_t *u) {
    if (!gates_undo_can_undo(u)) return (gates_str_t){0};
    const entry_t *e = &u->entries[u->pos - 1];
    return (gates_str_t){ .ptr = e->label, .size = e->label_len };
}

gates_str_t gates_undo_redo_label(const gates_undo_t *u) {
    if (!gates_undo_can_redo(u)) return (gates_str_t){0};
    const entry_t *e = &u->entries[u->pos];
    return (gates_str_t){ .ptr = e->label, .size = e->label_len };
}

void gates_undo_break_merge(gates_undo_t *u) {
    if (u != nullptr) u->merge_open = false;
}

void gates_undo_mark_clean(gates_undo_t *u) {
    if (u == nullptr) return;
    u->clean = u->pos;
    u->merge_open = false; /* the saved state must not change under the mark */
}

bool gates_undo_is_clean(const gates_undo_t *u) {
    return u != nullptr && u->clean == (gates_i64)u->pos;
}

void gates_undo_clear(gates_undo_t *u) {
    if (u == nullptr || u->busy) return;
    for (gates_u32 i = 0; i < u->n; i++) free_entry(u, &u->entries[i]);
    u->n = u->pos = 0;
    u->clean = 0;
    u->merge_open = false;
    sync(u);
}

gates_err_t gates_undo_bind(gates_undo_t *u, gates_tree_t *tree, gates_node_t scope, gates_command_id_t undo_cmd,
                            gates_command_id_t redo_cmd, gates_str_t undo_word, gates_str_t redo_word) {
    if (u == nullptr) return PROVEN_ERR_INVALID_ARG;
    if (tree == nullptr) {
        if (u->words != nullptr) u->alloc.free_fn(u->alloc.ctx, u->words);
        u->words = nullptr;
        u->tree = nullptr;
        return GATES_OK;
    }
    if ((undo_word.size > 0 && undo_word.ptr == nullptr) || (redo_word.size > 0 && redo_word.ptr == nullptr) ||
        undo_word.size + redo_word.size > 0xFFFFu) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (!gates_command_exists(tree, scope, undo_cmd) || !gates_command_exists(tree, scope, redo_cmd)) {
        return PROVEN_ERR_NOT_FOUND;
    }
    gates_usize_t n = undo_word.size + redo_word.size;
    proven_result_mem_mut_t r = u->alloc.alloc_fn(u->alloc.ctx, n > 0 ? n : 1, 1);
    if (!proven_is_ok(r.err)) return r.err;
    gates_u8 *w = (gates_u8 *)r.value.ptr;
    if (undo_word.size > 0) memcpy(w, undo_word.ptr, undo_word.size);
    if (redo_word.size > 0) memcpy(w + undo_word.size, redo_word.ptr, redo_word.size);
    if (u->words != nullptr) u->alloc.free_fn(u->alloc.ctx, u->words);
    u->words = w;
    u->undo_word_len = (gates_u32)undo_word.size;
    u->redo_word_len = (gates_u32)redo_word.size;
    u->tree = tree;
    u->scope = scope;
    u->undo_cmd = undo_cmd;
    u->redo_cmd = redo_cmd;
    sync(u);
    return GATES_OK;
}
