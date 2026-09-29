# Chapter 11 - The application frame

Headers: `gates/frame.h`, `gates/command.h`.

A desktop tool usually has a frame around its content: a menu bar, keyboard access keys on its
labels and buttons, and shortcuts a person can learn and change. All of it is driven by the
commands of chapter 4, so one action keeps one label, one enabled state and one shortcut
wherever it appears.

## Mnemonics

In the text of a button, a check box, a command, a menu bar title or a tab, `&x` marks `x` as
the mnemonic when `x` is a letter or digit: `"&Save"`, `"E&xit"`, `"Page &2"`. Alt+x then
activates the control - a button is pressed, a check box toggled, a menu bar title opened. Two
`&` show one (`"Fish && chips"`), and any other `&` is shown as it is, so `"Save & close"` needs
no escaping. The mnemonic takes no room: `"&Save"` is exactly as wide as `"Save"`.

The letter is underlined while keyboard cues are visible, as in other Windows programs: from
the moment Alt is pressed or the menu bar is entered from the keyboard until the next pointer
press, or all the time when the "underline access keys" setting is on.

Labels often show data, such as file names, so a label parses `&` only when it has a target:
`gates_label_set_target(tree, label, box)` makes Alt+x focus `box`. Give the box the label as
its accessible name too (`gates_node_set_labelled_by`), and screen readers announce the key
with the name.

When several controls share a letter, each Alt+x moves the focus to the next of them and
activates nothing; the person presses Space or Enter. Only controls the keyboard can reach
count: a disabled button, a hidden panel or the window behind a modal dialog is skipped.
Mnemonics are ASCII letters and digits; a title in another script adds one in brackets after
its text, `(&F)`, as Korean and Japanese Windows programs do.

## The menu bar

A tree has at most one menu bar: `gates_menubar_create(tree, parent, scope, &bar)`. Each
`gates_menubar_add(tree, bar, title, ids, count, nullptr)` adds a title over a list of
commands of `scope`, where id 0 draws a separator - the same list a context menu takes. The
menus are ordinary menu overlays, so everything chapter 4 says about menus holds: they close on
Escape, an outside click or when the window loses focus, a chosen entry is checked again
before its command runs, and a handler set on the open menu hears MENU_CLOSED.

With the pointer, a click on a title opens its menu below it; while one is open, moving over
another title switches to it, and a click on the open title closes it. From the keyboard:

| Keys | In menu mode (a title highlighted) | In an open menu |
|---|---|---|
| F10, or Alt pressed and released alone | enter / leave menu mode | leave |
| Left / Right | the previous / next title | the neighbouring menu, opened |
| Down, Up, Enter, Space | open the highlighted title | move / choose |
| a title's mnemonic letter | open that title | - |
| an entry's mnemonic letter | - | choose that entry |
| Escape | leave menu mode | back to the highlighted title |

Alt+x with a title's letter opens that menu directly. Titles come before controls, so give a
control a letter no title uses. F10 is the menu key only while a menu bar is reachable; in a
window without one it stays an ordinary command shortcut. The bar is not a Tab stop, and a
modal dialog makes it unreachable. Menus have no submenus in this version.

On Windows, gates takes Alt+letter only when it matches a mnemonic; Alt+F4, Alt+Tab and
Alt+Space keep doing what Windows does.

## Keymap: listing and rebinding shortcuts

The shortcut stored in each command is the keymap. `gates_command_count` and
`gates_command_at` list a scope's commands (for a keymap editor or a help screen);
`gates_command_set_shortcut` rebinds one and refuses a shortcut another command of the scope
already has, naming that command in `*conflict`. `gates_shortcut_format` prints a shortcut as
menus and screen readers show it (`"Ctrl+Shift+S"`, `"F5"`, `"Ctrl+Del"`), and
`gates_shortcut_parse` reads the same text back, so a program can keep a person's keymap in a
file of lines such as `save=Ctrl+S`. Rules from chapter 4 still hold: a shortcut needs Ctrl, or
is a function key; Alt is never part of one.

<!-- example: manual/examples/ex_11_menubar.c -->
```c
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
```

## Toolbar

`gates_toolbar_create(tree, parent, scope, &bar)` makes a row of compact buttons, and each
`gates_toolbar_add(tree, bar, id)` adds one bound to a command of `scope` (id 0 adds a
separator). A button shows its command's label without the mnemonic markup, looks disabled
while the command is disabled, and looks pressed while the command is checked - a toggle such
as Bold is a checked command. Buttons are flat until the pointer is over them.

A click invokes the command and leaves the keyboard focus where it was, so Cut and Paste
buttons act on the text box the person is typing in. From the keyboard the toolbar is one Tab
stop: Left, Right, Home and End move between its enabled buttons, and Space or Enter invokes
the one with the focus. When the toolbar is narrower than its buttons, those that do not fit
are left out and a `>>` button at its end lists their commands in a menu.

## Status bar

`gates_statusbar_create(tree, parent, &bar)` makes the strip along the bottom of a window, and
`gates_statusbar_add(tree, bar, text, grow, &segment)` adds a segment: a label, so its text
changes with `gates_widget_set_text`. Segments are separated by thin lines; a segment with
`grow` takes a share of the spare width, like a layout child. Status text is not announced on
every change - make a segment a live region (`gates_node_set_live`) only when its news matters.

## Tooltips

`gates_node_set_tooltip(tree, node, text)` gives a node a short help text. It shows in a small
box below the node (above near the bottom of the window) after the pointer rests on it for
`GATES_TOOLTIP_DELAY_MS` (500 ms), or that long after the keyboard focus arrives, and it hides
on a press, a key, when the pointer or focus leaves, and after `GATES_TOOLTIP_SHOW_MS`. Moving
from one tooltip to the next while one shows switches at once. Toolbar buttons have one without
asking: the command's label and shortcut, `"Paste (Ctrl+V)"`. Tooltips never take input, and
screen readers read the text as the node's help text. They are timed by the tree's clock, and
nothing is timed while no hovered or focused node has one.

<!-- example: manual/examples/ex_11_toolbar.c -->
```c
/* manual example (host): a toolbar, a status bar and a tooltip.
 * expect: pasted 1, focus kept; tooltip "Paste (Ctrl+V)" after 500 ms; status Pasted */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { CMD_CUT = 1, CMD_PASTE };

typedef struct app_t {
    gates_node_t status;
    int pasted;
} app_t;

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    if (id == CMD_PASTE) {
        a->pasted++;
        (void)gates_widget_set_text(tree, a->status, GATES_STR("Pasted"));
    }
}

/* A test clock: a window's tree has a real one. */
static gates_u64 now_ms;
static gates_u64 clock_now(void *ctx) { (void)ctx; return now_ms; }
static void clock_changed(void *ctx) { (void)ctx; }

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_tree_set_clock(t, clock_now, clock_changed, nullptr);
    gates_node_t root = gates_tree_root(t), bar, body, status_bar;
    app_t a = {0};
    gates_command_desc_t cmds[] = {
        { .id = CMD_CUT, .label = GATES_STR("Cu&t"), .shortcut = { .key = GATES_KEY_X, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = &a },
        { .id = CMD_PASTE, .label = GATES_STR("&Paste"), .shortcut = { .key = GATES_KEY_V, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = &a },
    };
    if (!gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_command_register(t, root, &cmds[0])) ||
        !gates_is_ok(gates_command_register(t, root, &cmds[1])) ||
        !gates_is_ok(gates_toolbar_create(t, root, root, &bar)) ||
        !gates_is_ok(gates_toolbar_add(t, bar, CMD_CUT)) ||
        !gates_is_ok(gates_toolbar_add(t, bar, CMD_PASTE)) ||
        !gates_is_ok(gates_textbox_create(t, root, GATES_STR(""), 30, &body)) ||
        !gates_is_ok(gates_node_set_access_name(t, body, GATES_STR("Text"))) ||
        !gates_is_ok(gates_layout_set_child_grow(t, body, 1)) ||
        !gates_is_ok(gates_statusbar_create(t, root, &status_bar)) ||
        !gates_is_ok(gates_statusbar_add(t, status_bar, GATES_STR("Ready"), 1, &a.status)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 300 }, gates_text_backend_builtin()))) {
        return 1;
    }
    gates_tree_set_focus(t, body);

    /* Find the Paste button through the accessibility model: item 2 of the bar. */
    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, bar, 2, &info))) return 1;
    gates_point_t p = { info.bounds.x + info.bounds.w / 2, info.bounds.y + info.bounds.h / 2 };

    /* Rest the pointer on it: the tooltip shows after GATES_TOOLTIP_DELAY_MS. */
    gates_pointer_event_t move = { .action = GATES_POINTER_MOVE, .pos = p };
    (void)gates_input_pointer(t, &move);
    now_ms += GATES_TOOLTIP_DELAY_MS;
    (void)gates_tree_run_timers(t);
    gates_str_t tip = {0};
    char tip_text[64] = "";
    if (gates_tooltip_shown(t, nullptr, nullptr, &tip, nullptr) && tip.size < sizeof tip_text) {
        memcpy(tip_text, tip.ptr, tip.size);
    }

    /* A click invokes the command; the focus stays in the text box. */
    gates_pointer_event_t down = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT, .pos = p };
    gates_pointer_event_t up = { .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT, .pos = p };
    (void)gates_input_pointer(t, &down);
    (void)gates_input_pointer(t, &up);
    (void)gates_tree_dispatch_events(t, 0);

    gates_str_t s = gates_widget_text(t, a.status);
    printf("pasted %d, focus %s; tooltip \"%s\" after %u ms; status %.*s\n", a.pasted,
           gates_node_eq(gates_tree_focus(t), body) ? "kept" : "moved", tip_text, GATES_TOOLTIP_DELAY_MS,
           (int)s.size, (const char *)s.ptr);
    gates_tree_destroy(t);
    return 0;
}
```

## Accessibility

The menu bar is a MenuBar element whose titles are menu items with ExpandCollapse; in menu
mode the highlighted title has the keyboard focus. Every control with a mnemonic reports it as
its access key ("Alt+F"; an entry of an open menu reports its letter), menu entries and
buttons bound to a command report the command's shortcut as their accelerator key, and
accessible names never contain the `&` markup. The toolbar is a ToolBar element whose buttons
are items (a separator is none; `>>` is an item called More while it is shown), and the status
bar is a StatusBar element with its segments as text.
