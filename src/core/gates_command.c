/* gates_gui_lib - command registry, shortcut matching and queued invocation
 * (plan-0009, RFC-0003 5.2). Platform-free. */
#include <gates/command.h>
#include <gates/widget.h>
#include "gates_tree_internal.h"

#include <string.h>

static bool shortcut_empty(const gates_shortcut_t *k) {
    return k->key == GATES_KEY_NONE && k->letter == 0;
}

static bool is_fkey(gates_key_t k) {
    return k >= GATES_KEY_F1 && k <= GATES_KEY_F12;
}

/* Ctrl without Alt, or a function key; the key is a semantic key or a
 * letter/digit, never both. */
static bool shortcut_valid(const gates_shortcut_t *k) {
    if (shortcut_empty(k)) {
        return true;
    }
    if (k->key != GATES_KEY_NONE && k->letter != 0) {
        return false;
    }
    if (k->letter != 0 && !((k->letter >= 'A' && k->letter <= 'Z') ||
                            (k->letter >= '0' && k->letter <= '9'))) {
        return false;
    }
    if (k->alt) {
        return false;
    }
    return k->ctrl || is_fkey(k->key);
}

static bool shortcut_eq(const gates_shortcut_t *a, const gates_shortcut_t *b) {
    return a->key == b->key && a->letter == b->letter && a->ctrl == b->ctrl &&
           a->shift == b->shift && a->alt == b->alt;
}

static bool scope_ok(const gates_tree_t *tree, gates_node_t scope) {
    return gates_i_valid(tree, scope);
}

gates_i_command_t *gates_i_command_find(const gates_tree_t *tree, gates_u32 scope_index,
                                        gates_u32 scope_generation, gates_command_id_t id) {
    for (gates_u32 i = 0; i < tree->command_count; i++) {
        gates_i_command_t *c = &tree->commands[i];
        if (c->alive && c->id == id && c->scope_index == scope_index &&
            c->scope_generation == scope_generation) {
            return c;
        }
    }
    return nullptr;
}

static gates_i_command_t *find_h(const gates_tree_t *tree, gates_node_t scope,
                                 gates_command_id_t id) {
    if (!scope_ok(tree, scope) || id == 0) {
        return nullptr;
    }
    return gates_i_command_find(tree, scope.index, scope.generation, id);
}

/* Repaint (and re-measure) everything a command change can affect: bound
 * buttons are few and found by walking the widget states. */
static void commands_changed(gates_tree_t *tree, bool relabel) {
    gates_i_mark_dirty(tree, GATES_NONE, GATES_DIRTY_PAINT | (relabel ? GATES_DIRTY_LAYOUT : 0u));
    gates_i_focus_check(tree);
}

gates_err_t gates_command_register(gates_tree_t *tree, gates_node_t scope,
                                   const gates_command_desc_t *desc) {
    if (tree == nullptr || desc == nullptr || !scope_ok(tree, scope) || desc->id == 0 ||
        desc->invoke == nullptr || !shortcut_valid(&desc->shortcut) ||
        (desc->label.size > 0 && desc->label.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_u32 free_slot = GATES_NONE;
    for (gates_u32 i = 0; i < tree->command_count; i++) {
        gates_i_command_t *c = &tree->commands[i];
        if (!c->alive) {
            if (free_slot == GATES_NONE) free_slot = i;
            continue;
        }
        if (c->scope_index != scope.index || c->scope_generation != scope.generation) {
            continue;
        }
        if (c->id == desc->id ||
            (!shortcut_empty(&desc->shortcut) && shortcut_eq(&c->shortcut, &desc->shortcut)) ||
            (desc->role != GATES_COMMAND_NORMAL && c->role == desc->role)) {
            return PROVEN_ERR_INVALID_STATE;
        }
    }
    gates_allocator_t a = tree->alloc;
    gates_u8 *label = nullptr;
    if (desc->label.size > 0) {
        proven_result_mem_mut_t r = a.alloc_fn(a.ctx, desc->label.size, 1);
        if (!proven_is_ok(r.err)) {
            return r.err;
        }
        label = (gates_u8 *)r.value.ptr;
        memcpy(label, desc->label.ptr, desc->label.size);
    }
    if (free_slot == GATES_NONE) {
        if (tree->command_count == tree->command_cap) {
            gates_u32 cap = tree->command_cap == 0 ? 8u : tree->command_cap * 2u;
            proven_result_mem_mut_t r =
                tree->commands == nullptr
                    ? a.alloc_fn(a.ctx, cap * sizeof(gates_i_command_t),
                                 alignof(gates_i_command_t))
                    : a.realloc_fn(a.ctx, tree->commands,
                                   tree->command_cap * sizeof(gates_i_command_t),
                                   cap * sizeof(gates_i_command_t), alignof(gates_i_command_t));
            if (!proven_is_ok(r.err)) {
                if (label != nullptr) a.free_fn(a.ctx, label);
                return r.err;
            }
            tree->commands = (gates_i_command_t *)r.value.ptr;
            tree->command_cap = cap;
        }
        free_slot = tree->command_count++;
    }
    tree->commands[free_slot] = (gates_i_command_t){
        .alive = true,
        .scope_index = scope.index,
        .scope_generation = scope.generation,
        .id = desc->id,
        .label = label,
        .label_len = (gates_u32)desc->label.size,
        .shortcut = desc->shortcut,
        .role = desc->role,
        .enabled = desc->enabled,
        .checked = desc->checked,
        .invoke = desc->invoke,
        .user = desc->user,
    };
    commands_changed(tree, true);
    return GATES_OK;
}

gates_err_t gates_command_unregister(gates_tree_t *tree, gates_node_t scope,
                                     gates_command_id_t id) {
    gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    if (c == nullptr) {
        return PROVEN_ERR_NOT_FOUND;
    }
    if (c->label != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, c->label);
    }
    *c = (gates_i_command_t){0}; /* queued invocations find nothing and do nothing */
    commands_changed(tree, true);
    return GATES_OK;
}

gates_err_t gates_command_set_enabled(gates_tree_t *tree, gates_node_t scope,
                                      gates_command_id_t id, bool enabled) {
    gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    if (c == nullptr) {
        return PROVEN_ERR_NOT_FOUND;
    }
    if (c->enabled != enabled) {
        c->enabled = enabled;
        commands_changed(tree, false);
    }
    return GATES_OK;
}

gates_err_t gates_command_set_checked(gates_tree_t *tree, gates_node_t scope,
                                      gates_command_id_t id, bool checked) {
    gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    if (c == nullptr) {
        return PROVEN_ERR_NOT_FOUND;
    }
    if (c->checked != checked) {
        c->checked = checked;
        commands_changed(tree, false);
    }
    return GATES_OK;
}

gates_err_t gates_command_set_label(gates_tree_t *tree, gates_node_t scope,
                                    gates_command_id_t id, gates_str_t label) {
    gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    if (c == nullptr || (label.size > 0 && label.ptr == nullptr)) {
        return c == nullptr ? PROVEN_ERR_NOT_FOUND : PROVEN_ERR_INVALID_ARG;
    }
    if (c->label_len == label.size && (label.size == 0 || memcmp(c->label, label.ptr, label.size) == 0)) {
        return GATES_OK;
    }
    gates_u8 *copy = nullptr;
    if (label.size > 0) {
        proven_result_mem_mut_t r = tree->alloc.alloc_fn(tree->alloc.ctx, label.size, 1);
        if (!proven_is_ok(r.err)) {
            return r.err; /* old label kept */
        }
        copy = (gates_u8 *)r.value.ptr;
        memcpy(copy, label.ptr, label.size);
    }
    if (c->label != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, c->label);
    }
    c->label = copy;
    c->label_len = (gates_u32)label.size;
    commands_changed(tree, true);
    return GATES_OK;
}

bool gates_command_exists(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id) {
    return tree != nullptr && find_h(tree, scope, id) != nullptr;
}

bool gates_command_enabled(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id) {
    const gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    return c != nullptr && c->enabled;
}

bool gates_command_checked(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id) {
    const gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    return c != nullptr && c->checked;
}

gates_str_t gates_command_label(const gates_tree_t *tree, gates_node_t scope,
                                gates_command_id_t id) {
    const gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    if (c == nullptr || c->label == nullptr) {
        return (gates_str_t){0};
    }
    return (gates_str_t){ .ptr = c->label, .size = c->label_len };
}

void gates_i_command_push(gates_tree_t *tree, const gates_i_command_t *c,
                          gates_event_origin_t origin) {
    if (tree->event_len >= tree->event_cap) {
        return; /* caller skipped reservation */
    }
    tree->events[tree->event_len++] = (gates_i_event_t){
        .node_index = c->scope_index,
        .generation = c->scope_generation,
        .kind = (gates_u8)GATES_I_EVENT_COMMAND,
        .origin = (gates_u8)origin,
        .aux = c->id,
    };
}

gates_err_t gates_command_invoke(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id) {
    gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    if (c == nullptr) {
        return PROVEN_ERR_NOT_FOUND;
    }
    gates_err_t err = gates_i_event_reserve(tree, 1, 0);
    if (!gates_is_ok(err)) {
        return err;
    }
    gates_i_command_push(tree, c, GATES_ORIGIN_PROGRAM);
    return GATES_OK;
}

/* Scope chain: the active scope, then the tree root (stage 2 adds nesting). */
static gates_i_command_t *in_scope_chain(const gates_tree_t *tree,
                                         bool (*match)(const gates_i_command_t *, const void *),
                                         const void *arg) {
    gates_u32 scopes[2] = { gates_i_scope_root(tree), tree->root };
    /* A modal dialog is the whole chain: the window's shortcuts wait. */
    gates_u32 n = (scopes[0] == scopes[1] || gates_i_overlay_is_dialog_scope(tree, scopes[0]))
                      ? 1u
                      : 2u;
    for (gates_u32 s = 0; s < n; s++) {
        gates_u32 gen = gates_i_slot(tree, scopes[s])->generation;
        for (gates_u32 i = 0; i < tree->command_count; i++) {
            gates_i_command_t *c = &tree->commands[i];
            if (c->alive && c->scope_index == scopes[s] && c->scope_generation == gen &&
                match(c, arg)) {
                return c;
            }
        }
    }
    return nullptr;
}

static bool match_key(const gates_i_command_t *c, const void *arg) {
    const gates_key_event_t *ev = arg;
    const gates_shortcut_t *k = &c->shortcut;
    if (shortcut_empty(k)) {
        return false;
    }
    bool same_key = k->key != GATES_KEY_NONE ? k->key == ev->key
                                              : (k->letter != 0 && k->letter == ev->letter);
    return same_key && k->ctrl == ev->ctrl && k->shift == ev->shift && k->alt == ev->alt;
}

gates_i_command_t *gates_i_command_for_key(const gates_tree_t *tree,
                                           const gates_key_event_t *ev) {
    if (ev->ctrl && ev->alt) {
        return nullptr; /* AltGr */
    }
    if (!ev->ctrl && !is_fkey(ev->key)) {
        return nullptr; /* typed characters are never shortcuts */
    }
    return in_scope_chain(tree, match_key, ev);
}

static bool match_role(const gates_i_command_t *c, const void *arg) {
    return c->role == *(const gates_command_role_t *)arg;
}

gates_i_command_t *gates_i_command_with_role(const gates_tree_t *tree,
                                             gates_command_role_t role) {
    /* Default/cancel belong to the active scope only: an outer form's OK must
     * not answer Enter inside a dialog. */
    gates_u32 scope = gates_i_scope_root(tree);
    gates_u32 gen = gates_i_slot(tree, scope)->generation;
    for (gates_u32 i = 0; i < tree->command_count; i++) {
        gates_i_command_t *c = &tree->commands[i];
        if (c->alive && c->scope_index == scope && c->scope_generation == gen &&
            match_role(c, &role)) {
            return c;
        }
    }
    return nullptr;
}

void gates_i_commands_free(gates_tree_t *tree) {
    for (gates_u32 i = 0; i < tree->command_count; i++) {
        if (tree->commands[i].label != nullptr) {
            tree->alloc.free_fn(tree->alloc.ctx, tree->commands[i].label);
        }
    }
    if (tree->commands != nullptr) {
        tree->alloc.free_fn(tree->alloc.ctx, tree->commands);
    }
    tree->commands = nullptr;
    tree->command_count = 0;
    tree->command_cap = 0;
}

/* -- button binding ----------------------------------------------------------- */

static const gates_i_command_t *bound(const gates_tree_t *tree, const gates_widget_state_t *st) {
    if (st->cmd_id == 0) {
        return nullptr;
    }
    return gates_i_command_find(tree, st->cmd_scope_index, st->cmd_scope_generation, st->cmd_id);
}

bool gates_i_widget_inert(const gates_tree_t *tree, const gates_widget_state_t *st) {
    if (st->disabled) {
        return true;
    }
    if (st->has_options && !gates_i_options_any_enabled(st)) {
        return true; /* a radio group or choice with nothing to choose */
    }
    if (st->tbar != nullptr && !gates_i_toolbar_any_enabled(tree, st)) {
        return true; /* a toolbar whose commands are all disabled (plan-0018) */
    }
    if (st->cmd_id == 0) {
        return false;
    }
    const gates_i_command_t *c = bound(tree, st);
    return c == nullptr || !c->enabled;
}

gates_str_t gates_i_widget_label(const gates_tree_t *tree, const gates_widget_state_t *st) {
    const gates_i_command_t *c = bound(tree, st);
    if (c != nullptr) {
        return (gates_str_t){ .ptr = c->label, .size = c->label_len };
    }
    if (st->text == nullptr) {
        return (gates_str_t){0};
    }
    return (gates_str_t){ .ptr = st->text, .size = st->text_len };
}

gates_err_t gates_button_set_command(gates_tree_t *tree, gates_node_t button, gates_node_t scope,
                                     gates_command_id_t id) {
    if (!gates_i_valid(tree, button) ||
        gates_i_slot(tree, button.index)->kind != GATES_NODE_BUTTON) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_widget_state_t *st = gates_i_state(tree, gates_i_slot(tree, button.index)->state_index);
    if (st == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (id != 0 && !scope_ok(tree, scope)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    st->cmd_id = id;
    st->cmd_scope_index = id != 0 ? scope.index : 0;
    st->cmd_scope_generation = id != 0 ? scope.generation : 0;
    gates_i_mark_dirty(tree, button.index, GATES_DIRTY_LAYOUT | GATES_DIRTY_PAINT);
    gates_i_focus_check(tree);
    return GATES_OK;
}

/* -- keymap (plan-0018, RFC-0005 A4) ------------------------------------------------ */

/* Key names as shortcuts print and parse them; aliases after the first name. */
static const struct {
    gates_key_t key;
    const char *name;
    const char *alias;
} key_names[] = {
    { GATES_KEY_LEFT, "Left", nullptr },        { GATES_KEY_RIGHT, "Right", nullptr },
    { GATES_KEY_UP, "Up", nullptr },            { GATES_KEY_DOWN, "Down", nullptr },
    { GATES_KEY_HOME, "Home", nullptr },        { GATES_KEY_END, "End", nullptr },
    { GATES_KEY_BACKSPACE, "Backspace", nullptr }, { GATES_KEY_DELETE, "Del", "Delete" },
    { GATES_KEY_ENTER, "Enter", "Return" },     { GATES_KEY_TAB, "Tab", nullptr },
    { GATES_KEY_ESCAPE, "Esc", "Escape" },      { GATES_KEY_SPACE, "Space", nullptr },
    { GATES_KEY_PAGE_UP, "PgUp", "PageUp" },    { GATES_KEY_PAGE_DOWN, "PgDn", "PageDown" },
};
/* Semantic letter keys print as their letter (the parser gives the letter form). */
static const struct {
    gates_key_t key;
    char letter;
} letter_keys[] = {
    { GATES_KEY_A, 'A' }, { GATES_KEY_C, 'C' }, { GATES_KEY_X, 'X' },
    { GATES_KEY_V, 'V' }, { GATES_KEY_Z, 'Z' }, { GATES_KEY_Y, 'Y' },
};

gates_usize_t gates_i_shortcut_text(const gates_shortcut_t *k, char *buf, gates_usize_t cap) {
    char name[12] = {0};
    if (k->letter != 0) {
        name[0] = (char)k->letter;
    } else if (is_fkey(k->key)) {
        int f = (int)(k->key - GATES_KEY_F1) + 1;
        name[0] = 'F';
        name[1] = (char)('0' + (f >= 10 ? 1 : f));
        if (f >= 10) name[2] = (char)('0' + f - 10);
    } else {
        for (gates_usize_t q = 0; q < sizeof key_names / sizeof key_names[0]; q++) {
            if (key_names[q].key == k->key) {
                strncpy(name, key_names[q].name, sizeof name - 1);
            }
        }
        for (gates_usize_t q = 0; q < sizeof letter_keys / sizeof letter_keys[0]; q++) {
            if (letter_keys[q].key == k->key) {
                name[0] = letter_keys[q].letter;
            }
        }
    }
    if (name[0] == 0) {
        if (cap > 0) buf[0] = 0;
        return 0; /* no shortcut */
    }
    const char *parts[4] = { k->ctrl ? "Ctrl+" : "", k->shift ? "Shift+" : "", name, "" };
    gates_usize_t n = 0;
    for (int p = 0; p < 3; p++) {
        for (const char *c = parts[p]; *c; c++, n++) {
            if (n + 1 < cap) buf[n] = *c;
        }
    }
    if (cap > 0) buf[n < cap ? n : cap - 1] = 0;
    return n;
}

gates_usize_t gates_shortcut_format(const gates_shortcut_t *shortcut, char *buf, gates_usize_t cap) {
    if (shortcut == nullptr) {
        if (buf != nullptr && cap > 0) buf[0] = 0;
        return 0;
    }
    return gates_i_shortcut_text(shortcut, buf != nullptr ? buf : (char[1]){0}, buf != nullptr ? cap : 0);
}

static bool name_eq(const gates_u8 *p, gates_usize_t n, const char *name) {
    if (name == nullptr || strlen(name) != n) {
        return false;
    }
    for (gates_usize_t i = 0; i < n; i++) {
        gates_u8 a = p[i], b = (gates_u8)name[i];
        if (a >= 'a' && a <= 'z') a = (gates_u8)(a - 32);
        if (b >= 'a' && b <= 'z') b = (gates_u8)(b - 32);
        if (a != b) return false;
    }
    return true;
}

gates_err_t gates_shortcut_parse(gates_str_t text, gates_shortcut_t *out) {
    if (out == nullptr || (text.size > 0 && text.ptr == nullptr)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_shortcut_t k = {0};
    if (text.size == 0) {
        *out = k;
        return GATES_OK;
    }
    gates_usize_t start = 0;
    bool have_key = false;
    for (gates_usize_t i = 0; i <= text.size; i++) {
        if (i < text.size && text.ptr[i] != '+') continue;
        const gates_u8 *p = text.ptr + start;
        gates_usize_t n = i - start;
        start = i + 1;
        if (n == 0 || have_key) {
            return PROVEN_ERR_INVALID_ARG; /* "Ctrl+", "+S", "Ctrl++S", "Ctrl+S+T" */
        }
        bool last = i == text.size;
        if (!last) {
            if (name_eq(p, n, "Ctrl") || name_eq(p, n, "Control")) k.ctrl = true;
            else if (name_eq(p, n, "Shift")) k.shift = true;
            else if (name_eq(p, n, "Alt")) k.alt = true;
            else return PROVEN_ERR_INVALID_ARG;
            continue;
        }
        have_key = true;
        if (n == 1 && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z') ||
                       (p[0] >= '0' && p[0] <= '9'))) {
            k.letter = (p[0] >= 'a' && p[0] <= 'z') ? (gates_u8)(p[0] - 32) : p[0];
            continue;
        }
        if ((p[0] == 'F' || p[0] == 'f') && (n == 2 || n == 3)) {
            int f = 0;
            bool digits = true;
            for (gates_usize_t d = 1; d < n; d++) {
                if (p[d] < '0' || p[d] > '9') digits = false;
                f = f * 10 + (p[d] - '0');
            }
            if (digits && f >= 1 && f <= 12 && !(n == 3 && p[1] == '0')) {
                k.key = (gates_key_t)(GATES_KEY_F1 + (f - 1));
                continue;
            }
            return PROVEN_ERR_INVALID_ARG;
        }
        bool found = false;
        for (gates_usize_t q = 0; q < sizeof key_names / sizeof key_names[0] && !found; q++) {
            if (name_eq(p, n, key_names[q].name) || name_eq(p, n, key_names[q].alias)) {
                k.key = key_names[q].key;
                found = true;
            }
        }
        if (!found) {
            return PROVEN_ERR_INVALID_ARG;
        }
    }
    if (!shortcut_valid(&k)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out = k;
    return GATES_OK;
}

gates_err_t gates_command_set_shortcut(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id,
                                       gates_shortcut_t shortcut, gates_command_id_t *conflict) {
    if (conflict != nullptr) {
        *conflict = 0;
    }
    gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    if (c == nullptr) {
        return tree == nullptr ? PROVEN_ERR_INVALID_ARG : PROVEN_ERR_NOT_FOUND;
    }
    if (!shortcut_valid(&shortcut)) {
        return PROVEN_ERR_INVALID_ARG;
    }
    if (!shortcut_empty(&shortcut)) {
        for (gates_u32 i = 0; i < tree->command_count; i++) {
            const gates_i_command_t *o = &tree->commands[i];
            if (o != c && o->alive && o->scope_index == scope.index &&
                o->scope_generation == scope.generation && shortcut_eq(&o->shortcut, &shortcut)) {
                if (conflict != nullptr) {
                    *conflict = o->id;
                }
                return PROVEN_ERR_INVALID_STATE;
            }
        }
    }
    if (!shortcut_eq(&c->shortcut, &shortcut)) {
        c->shortcut = shortcut;
        commands_changed(tree, true); /* menus show the new chord */
    }
    return GATES_OK;
}

gates_shortcut_t gates_command_shortcut(const gates_tree_t *tree, gates_node_t scope,
                                        gates_command_id_t id) {
    const gates_i_command_t *c = tree != nullptr ? find_h(tree, scope, id) : nullptr;
    return c != nullptr ? c->shortcut : (gates_shortcut_t){0};
}

gates_u32 gates_command_count(const gates_tree_t *tree, gates_node_t scope) {
    if (tree == nullptr || !scope_ok(tree, scope)) {
        return 0;
    }
    gates_u32 n = 0;
    for (gates_u32 i = 0; i < tree->command_count; i++) {
        const gates_i_command_t *c = &tree->commands[i];
        n += (c->alive && c->scope_index == scope.index && c->scope_generation == scope.generation) ? 1u : 0u;
    }
    return n;
}

gates_command_id_t gates_command_at(const gates_tree_t *tree, gates_node_t scope, gates_u32 index) {
    if (tree == nullptr || !scope_ok(tree, scope)) {
        return 0;
    }
    for (gates_u32 i = 0; i < tree->command_count; i++) {
        const gates_i_command_t *c = &tree->commands[i];
        if (c->alive && c->scope_index == scope.index && c->scope_generation == scope.generation) {
            if (index == 0) return c->id;
            index--;
        }
    }
    return 0;
}
