# Chapter 0 - Start here

## What gates is for

gates is a GUI library for programs whose job is to let a person look at something and change
it: a settings dialog, a record inspector, a log viewer, a build tool. It is written in C23,
keeps a tree of nodes that describes the interface, and does the work that such programs
otherwise redo badly: keyboard focus, text entry with the installed input method, forms,
lists over large data, commands shared by buttons and shortcuts, dialogs, threads that feed
the interface, themes, scaling, and accessibility.

It is not a browser engine, not a game UI, and not a pixel-exact drawing kit. Think of HTML
and CSS: the program says what things are and how they relate; how they finally look is the
business of the backend and the theme. A feature in gates is finished when the interaction it
serves works - reachable, usable with pointer, keyboard and input method, readable by a screen
reader - not when it matches a picture.

Version 0.6.0 has one backend: Win32 with a software renderer. The core (everything except
the window) is platform-free and runs anywhere a C23 compiler does, which is how its tests run.

## The package

A release is a folder, `gates-0.6.0/`:

| Path | What |
|---|---|
| `include/gates/` | the public headers; `gates/gates.h` includes all of them |
| `include/proven/` | the headers of the proven library that gates builds on |
| `lib/win64/libgates.a` | gates for Windows (core + Win32 backend), mingw-w64 |
| `lib/host/libgates_core.a` | the platform-free core, for tests and headless tools |
| `lib/*/libproven.a` | proven, the foundation library (allocators, strings, results) |
| `manual/`, `manual-ko/` | this manual in English and Korean |
| `examples/consumer/` | a program built from the package alone, with its Makefile |

gates and proven are separate libraries: link both, gates first. A program that already uses
proven (0.1.x) may link its own copy instead of the one in the package.

## The first program

A window with a label and a button; pressing the button changes the label.

<!-- example: manual/examples/ex_00_hello.c -->
```c
/* manual example (windows): the first program - a window, a label, a button.
 * Build: see chapter 0 (links -lgates -lproven and the Windows libraries). */
#include <gates/gates.h>

#include <stdio.h>

typedef struct hello_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t count_label;
    int clicks;
} hello_t;

/* The button tells us it was pressed; we change the label. Nothing is read
 * back from widgets while painting. */
static void on_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    hello_t *h = user;
    if (ev->kind != GATES_EVENT_ACTIVATED) return;
    h->clicks++;
    char text[48];
    int n = snprintf(text, sizeof text, "Pressed %d time%s", h->clicks, h->clicks == 1 ? "" : "s");
    (void)gates_widget_set_text(tree, h->count_label, (gates_str_t){ .ptr = (const gates_u8 *)text, .size = (gates_usize_t)n });
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    hello_t *h = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) gates_app_quit(h->app);
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) return 1;
    hello_t h = { .app = app };
    gates_window_desc_t desc = { .title = GATES_STR("Hello, gates"), .size = { 320, 160 } };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &h };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    h.tree = gates_window_tree(win);
    gates_node_t root = gates_tree_root(h.tree), button;
    gates_err_t err = gates_layout_set(h.tree, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_layout_set_padding(h.tree, root, 12);
    if (gates_is_ok(err)) err = gates_label_create(h.tree, root, GATES_STR("Not pressed yet"), &h.count_label);
    if (gates_is_ok(err)) err = gates_button_create(h.tree, root, GATES_STR("Press me"), nullptr, nullptr, &button);
    if (gates_is_ok(err)) err = gates_widget_set_handler(h.tree, button, on_button, &h);
    if (gates_is_ok(err)) err = gates_app_run(app); /* returns when the last window closes */
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
```

Build it with mingw-w64 (on Linux, or MSYS2 on Windows), from the package folder:

```sh
x86_64-w64-mingw32-gcc -std=c23 -O2 -Iinclude hello.c -Llib/win64 -lgates -lproven \
    -lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 -luiautomationcore -lole32 -loleaut32 \
    -luuid -mwindows -o hello.exe
```

Three things in it are the whole shape of a gates program. The program builds nodes into the
window's tree (`gates_label_create`, `gates_button_create`). It learns what the person did
from events (`gates_widget_set_handler`), never by reading widgets while the window paints.
And `gates_app_run` owns the loop until the last window closes.

## Without a window

The core needs no window: build a tree, lay it out with the builtin text backend, and ask
what is where. Tests and command-line tools use gates this way.

<!-- example: manual/examples/ex_00_headless.c -->
```c
/* manual example (host): gates without a window - build, lay out, inspect.
 * expect: Press me: button at 0,16 320x26 */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t root = gates_tree_root(tree), label, button;
    gates_err_t err = gates_layout_set(tree, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_label_create(tree, root, GATES_STR("Not pressed yet"), &label);
    if (gates_is_ok(err)) err = gates_button_create(tree, root, GATES_STR("Press me"), nullptr, nullptr, &button);
    /* Layout needs text metrics: the builtin backend works everywhere. */
    if (gates_is_ok(err)) err = gates_layout_run(tree, (gates_size_t){ 320, 160 }, gates_text_backend_builtin());
    gates_access_info_t info;
    if (gates_is_ok(err)) err = gates_access_info(tree, button, 0, &info);
    if (gates_is_ok(err)) {
        printf("%.*s: %s at %d,%d %dx%d\n", (int)info.name.size, (const char *)info.name.ptr,
               info.role == GATES_ROLE_BUTTON ? "button" : "?", info.bounds.x, info.bounds.y, info.bounds.w,
               info.bounds.h);
    }
    gates_tree_destroy(tree);
    return gates_is_ok(err) ? 0 : 1;
}
```

```sh
cc -std=c23 -Iinclude headless.c -Llib/host -lgates_core -lproven -lm -o headless
```

It prints `Press me: button at 0,16 320x26`: the column put the button under the label and
stretched it to the full width.

## The headers

`gates/gates.h` includes everything. The headers, in the order this manual meets them:

| Header | What it holds | Chapter |
|---|---|---|
| `gates/types.h` | integers, strings (`gates_str_t`), errors, the allocator | 1 |
| `gates/version.h` | the version and `gates_version()` | 10 |
| `gates/tree.h` | the node tree, handles, focus, hidden nodes | 1 |
| `gates/event.h` | typed events and their delivery | 1 |
| `gates/widget.h` | labels, buttons, check boxes, text boxes, radio groups, choices, progress | 2 |
| `gates/layout.h` | row, column, stack, split, scroll, form layouts | 2 |
| `gates/geometry.h` | points, sizes, rectangles, logical units | 2, 8 |
| `gates/form.h` | forms with labels, help and error lines | 3 |
| `gates/command.h` | commands shared by buttons, shortcuts and menus | 4 |
| `gates/overlay.h` | modal dialogs and context menus | 4 |
| `gates/view.h` | lists, tables, trees and logs over your data | 5 |
| `gates/text_edit.h` | the text edit core under every text box | 6 |
| `gates/clipboard.h` | the clipboard boundary | 6 |
| `gates/input.h` | pointer and key events | 6 |
| `gates/ui.h` | input routing, hit testing, painting a tree | 6 |
| `gates/post.h` | posting from worker threads | 7 |
| `gates/timer.h` | timers on the UI thread | 7 |
| `gates/theme.h` | themes: colour tokens, light, dark, high contrast | 8 |
| `gates/draw.h` | the draw command list | 8 |
| `gates/render.h` | the software renderer | 8 |
| `gates/text.h` | fonts, text metrics and text backends | 8 |
| `gates/access.h` | accessibility: roles, names, actions, the audit | 9 |
| `gates/app.h` | the application and its loop | 10 |
| `gates/window.h` | windows, themes, zoom, announcements | 10 |
