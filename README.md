[한국어](README.ko.md) | **English** — **Gates GUI Library v0.13.0** — [ZIP(examples Windows x64)](https://github.com/rubidus-api/gates_gui_lib/releases/download/v0.13.0/gates-0.13.0-examples-win64.zip) · [ZIP(SDK)](https://github.com/rubidus-api/gates_gui_lib/releases/download/v0.13.0/gates-0.13.0-sdk.zip) · [ZIP(source)](https://github.com/rubidus-api/gates_gui_lib/releases/download/v0.13.0/gates-0.13.0-src.zip) · [ZIP(manual web)](https://github.com/rubidus-api/gates_gui_lib/releases/download/v0.13.0/gates-0.13.0-manual-web.zip) · [PDF(en)](https://github.com/rubidus-api/gates_gui_lib/releases/download/v0.13.0/gates-manual-0.13.0-en.pdf) · [PDF(ko)](https://github.com/rubidus-api/gates_gui_lib/releases/download/v0.13.0/gates-manual-0.13.0-ko.pdf)

# Gates GUI Library

A small retained-mode GUI library in C23 for tool-style Windows programs - settings panels,
inspectors, log viewers, file browsers, build tools. Version **0.13.0**.

| | |
|---|---|
| ![To-do list](screenshots/app_todo.png) | ![Folder browser](screenshots/app_files.png) |
| `app_todo` - a to-do list | `app_files` - a folder browser fed by a worker thread |
| ![Unit converter](screenshots/app_converter.png) | ![Calculator](screenshots/app_calculator.png) |
| `app_converter` - a form that converts as you type | `app_calculator` - buttons and keyboard |

![Gallery](screenshots/gallery.png)

`gallery` - the application frame: a menu bar with access keys, a toolbar, tabs, a table in a
split, a status bar with a clock.

The screenshots are the sample applications in `examples/`, running on Windows 11.

## What it is

gates is an interaction layer, not a picture. A program states what it needs from a person -
information to show, choices to offer, values to edit, commands to run, progress to report - as
a tree of nodes with roles, states and layout intent. gates carries that to the person through
the platform's GUI and carries the person's answers back as events. Think of HTML and CSS: the
document says what things are; how they finally look is the business of the backend and the
theme. A feature is done when the interaction it serves works - reachable, usable with pointer,
keyboard and input method, readable by a screen reader - not when it matches a mock-up.

What you get:

- **Proportional text** - the platform's UI font by default, a fixed-pitch font per node where
  alignment matters; exact caret, selection and hit testing either way.
- **Controls and layout** - labels, buttons, check boxes, one-line text boxes, radio groups,
  choices, progress bars, separators; row, column, stack, split, scroll and form layouts; all
  coordinates in logical units (1/96 inch), scaled per monitor.
- **Text entry** - the installed IME (Korean, Japanese, Chinese, ...) inline in every text box,
  clipboard, undo, read-only and password boxes, and a length limit that asks instead of
  silently cutting.
- **Forms, commands, dialogs, menus** - labelled fields with help and error lines, one command
  model for buttons, shortcuts and context menus, modal dialogs without nested loops.
- **Views over your data** - lists, tables, trees and logs of any size as one node each; gates
  reads only the rows on screen and keeps the selection by item id.
- **Workers and timers** - a bounded, never-blocking sender for results from worker threads;
  timers that cancel with their node.
- **Themes and accessibility** - light, dark and high contrast following the system; a UI
  Automation provider, so Narrator and automation tools can read and use every control; an
  audit of enforced rules (names, 24 x 24 targets, keyboard reach, contrast, focus cues).

It is not a browser engine, a game UI or a pixel-exact drawing kit. Version 0.13.0 has one
backend: Win32 with a software renderer. The core is platform-free C23 and runs its tests
anywhere.

## Download

From the [releases page](https://github.com/rubidus-api/gates_gui_lib/releases):

| File | What |
|---|---|
| `gates-<version>-examples-win64.zip` | every example as a ready-to-run Windows 10/11 x64 program - no installation |
| `gates-<version>-sdk.zip` | the package: headers, static libraries (`lib/win64` for mingw-w64), manual, a consumer example |
| `gates-<version>-src.zip` | the source code |

## A first program

```c
#include <gates/gates.h>

static void on_press(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    if (ev->kind == GATES_EVENT_ACTIVATED)
        (void)gates_widget_set_text(tree, *(gates_node_t *)user, GATES_STR("Pressed!"));
}

int main(void) {
    gates_app_t *app = nullptr;
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) return 1;
    gates_window_desc_t desc = { .title = GATES_STR("Hello, gates"), .size = { 320, 160 } };
    if (!gates_is_ok(gates_window_create(app, &desc, nullptr, &win))) return 1;
    gates_tree_t *t = gates_window_tree(win);
    gates_node_t root = gates_tree_root(t), label, button;
    (void)gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    (void)gates_label_create(t, root, GATES_STR("Not pressed yet"), &label);
    (void)gates_button_create(t, root, GATES_STR("Press me"), nullptr, nullptr, &button);
    (void)gates_widget_set_handler(t, button, on_press, &label);
    gates_err_t err = gates_app_run(app);
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
```

Build it with mingw-w64 against the SDK:

```sh
x86_64-w64-mingw32-gcc -std=c23 -O2 -Igates-0.13.0/include hello.c -Lgates-0.13.0/lib/win64 \
    -lgates -lproven -lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 -luiautomationcore \
    -lole32 -loleaut32 -luuid -mwindows -o hello.exe
```

Chapter 0 of the manual walks through it, with a headless variant for tests.

## Building from source

Needs a C23 compiler (GCC 14+ or Clang 18+) and `make`; the Windows programs need
mingw-w64 (GCC 14+), on Linux or MSYS2.

```sh
make test           # the core test suites (gcc); make test CC=clang for clang
make win            # every example as a Windows program, into build/win/
make dist           # the package: dist/gates-<version>/ (headers, libraries, manual)
make dist-bin       # the example binaries: dist/gates-<version>-examples-win64/
make install PREFIX=/some/dir
make package-check  # the package builds a consumer from its own files alone
make manual-check   # every program in the manual builds (and the headless ones run)
```

gates and its foundation library proven are separate static libraries: link
`-lgates -lproven` plus the Windows libraries above, or `-lgates_core -lproven -lm` for
headless use on any system.

## Documentation

- [Manual](manual/manual.md) (English) and [매뉴얼](manual-ko/manual-ko.md) (Korean): from the
  first program to deployment and troubleshooting; every program it prints is compiled by the
  build. Each release also carries the guide as a PDF and the whole manual, with the API
  reference, as a web edition (a zip).
- [examples/README.md](examples/README.md): one example per control and interaction, the
  reference and sample applications, each with what to check by hand.
- [Specification](spec/spec.md): what gates guarantees - the tree, errors, layout, input and
  events, text, views, threads, accessibility and limits.
- `include/gates/*.h`: the public API; each header opens with what it is for and its rules.
- [CHANGELOG.md](CHANGELOG.md): what changed.

## Status

0.13.0 shows what a press will do: the pointer turns into resize arrows over a table's column edge and a split's handle, and into the text beam over text. 0.12.0 let a program choose fonts by name: one face for the whole program (also while it runs) or a face per element. 0.11.0 added a built-in CP949/EUC-KR converter measured on Windows, text conversion without allocation (into a caller's buffer, in pieces), UTF-8 command lines and console output on Windows, menu names and the selection for screen readers, saved sideways scrolling, a live system font change, and type-ahead in the current column. 0.10.0 added submenus, tab titles that do not fit, a tooltip that screen readers see, table cells that screen readers read and change, scroll areas that scroll sideways, font sizes per node, icons drawn at the right pixel size for the scale, a selection store with drag-to-select, text encodings at the edge (UTF-16, UTF-32, EUC-KR/CP949 and other code pages to and from UTF-8), an API reference of every public function, and the manual as a PDF and a web edition. 0.9.0 added multi-selection in views (the program keeps the selection, so a million selected rows cost nothing), posting queues that grow as bursts need, and a public specification. 0.8.0 rounded out what came before: Shift+click, word and line clicks and row-wise Home/End in
the editor and text boxes, spin arrows that repeat and a wheel for focused numbers, grid rows
that grow, type-ahead, row copy and a current column in tables, a follow event for logs,
several files from one open dialog, forms that measure their stacked height, and error
reporting for input through the window. 0.7.0 added a multi-line text editor over a gap
buffer: it paints only the lines it shows, wraps, numbers lines, highlights through the
program's styler, finds, indents, composes with input methods at the caret and reads by line to
screen readers. 0.6.0 added data controls: table cells
that show check boxes, progress bars and icons and can be edited in place, columns people hide and reorder from a header menu, a property grid, an undo
stack for the program's own data, and background tasks with progress and Cancel. 0.5.0 added
images and icons (PNG, JPEG and more through Windows Imaging Component) and the native file,
folder, colour and message dialogs. 0.4.0 added number input (a spin box and a slider), group boxes that can collapse, grid and wrap
layouts, event bubbling to a container and deferred calls. 0.3.0 added the application frame: a menu bar, a toolbar, a status bar, tabs, tooltips,
Alt-letter access keys, a keymap programs can rebind, and saved UI arrangement (splits, tabs,
columns, window placement); the `gallery` example shows it all. 0.2.0 lifted the monospace restriction: text is set in the system's proportional UI font, with
a fixed-pitch font for logs and code chosen per node. The core suites (over 14,000 checks, plus a randomized model check of the text buffer with
2.7 million more) pass on GCC and Clang and
under AddressSanitizer and UndefinedBehaviorSanitizer; every example builds warning-free with
mingw-w64; the interactive behaviour - input, the Korean IME, themes and scaling, UI
Automation and Narrator, the example programs - is checked on Windows 11. Releases with the
same minor version are source compatible with each other; binary compatibility is not promised
yet, so rebuild with every update.

## Repository layout

| Path | What |
|---|---|
| `include/gates/` | the public headers (`gates/gates.h` includes all of them) |
| `src/core/`, `src/render/`, `src/text/` | the platform-free core, renderer and text |
| `src/platform/win32/` | the Win32 backend (window, input, IME, clipboard, UI Automation) |
| `examples/` | one program per control, reference and sample applications, a package consumer |
| `manual/`, `manual-ko/` | the manual in English and Korean, and its example programs |
| `tests/` | the test suites and the package and manual checks |
| `vendor/` | proven_c_lib and font8x8, vendored unchanged |
| `screenshots/` | the pictures above |

## Third-party code

`vendor/proven/` (proven_c_lib, MIT; its `LICENSE` and `THIRD_PARTY_NOTICES.md` are included)
and `vendor/font8x8/` (public domain, stated in the file header) are vendored unchanged; each
has a `VENDORED.md` with its origin and version.

## License

MIT - see [LICENSE](LICENSE).
