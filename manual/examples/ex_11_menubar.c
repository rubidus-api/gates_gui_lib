/* manual example (host): a menu bar, its keyboard and its mnemonics.
 * expect: new 1, open 1; F10 highlights File; access key Alt+F; shortcut Ctrl+Shift+N */
#include <gates/gates.h>

#include <stdio.h>

enum { CMD_NEW = 1, CMD_OPEN, CMD_QUIT };

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    ((int *)user)[id]++;
}

static bool press(gates_tree_t *t, gates_key_t key) {
    gates_key_event_t e = { .key = key, .down = true };
    return gates_input_key(t, &e);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), bar;
    int runs[4] = {0};
    /* "&" marks the mnemonic: Alt+N, and N while the menu is open. */
    gates_command_desc_t cmds[] = {
        { .id = CMD_NEW, .label = GATES_STR("&New"), .shortcut = { .letter = 'N', .ctrl = true },
          .enabled = true, .invoke = on_command, .user = runs },
        { .id = CMD_OPEN, .label = GATES_STR("&Open..."), .enabled = true, .invoke = on_command, .user = runs },
        { .id = CMD_QUIT, .label = GATES_STR("E&xit"), .enabled = true, .invoke = on_command, .user = runs },
    };
    for (int i = 0; i < 3; i++) {
        if (!gates_is_ok(gates_command_register(t, root, &cmds[i]))) return 1;
    }
    static const gates_command_id_t file[] = { CMD_NEW, CMD_OPEN, 0, CMD_QUIT }; /* 0: separator */
    if (!gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_menubar_create(t, root, root, &bar)) ||
        !gates_is_ok(gates_menubar_add(t, bar, GATES_STR("&File"), file, 4, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 300 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* F10 enters menu mode, Down opens File on its first entry, Enter chooses it. */
    (void)press(t, GATES_KEY_F10);
    bool highlighted = gates_menubar_highlighted(t, bar) == 0;
    (void)press(t, GATES_KEY_DOWN);
    (void)press(t, GATES_KEY_ENTER);
    /* Alt+F opens File directly (the platform sends Alt+letter here); O chooses Open. */
    (void)gates_input_mnemonic(t, 'f');
    gates_key_event_t o = { .letter = 'O', .down = true };
    (void)gates_input_key(t, &o);
    (void)gates_tree_dispatch_events(t, 0);

    /* The keymap: rebind New, and print the shortcut as menus do. */
    gates_command_id_t conflict = 0;
    if (!gates_is_ok(gates_command_set_shortcut(t, root, CMD_NEW,
                                                (gates_shortcut_t){ .letter = 'N', .ctrl = true, .shift = true },
                                                &conflict))) {
        return 1;
    }
    char keys[32];
    gates_shortcut_t now = gates_command_shortcut(t, root, CMD_NEW);
    (void)gates_shortcut_format(&now, keys, sizeof keys);

    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, bar, 1, &info))) return 1;   /* item 1: the File title */
    printf("new %d, open %d; F10 %s File; access key %.*s; shortcut %s\n", runs[CMD_NEW], runs[CMD_OPEN],
           highlighted ? "highlights" : "misses", (int)info.access_key.size, (const char *)info.access_key.ptr,
           keys);
    gates_tree_destroy(t);
    return 0;
}
