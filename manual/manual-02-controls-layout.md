# Chapter 2 - Controls and layout

Headers: `gates/widget.h`, `gates/layout.h`, `gates/geometry.h`.

## The control matrix

Every control is one node. The table is the whole finite set in 0.9.0: what the person does
with it, the keys, the events the program receives, and what a screen reader hears (chapter 9).

| Control | Create | The person | Keys | Events | Accessible as |
|---|---|---|---|---|---|
| panel | `gates_panel_create` | sees a group | - | - | nothing, or a group when named |
| label | `gates_label_create` | reads text | - | - | text |
| button | `gates_button_create` | asks for one action | Space (on release), Enter | ACTIVATED | button (Invoke) |
| check box | `gates_checkbox_create` | turns an option on or off | Space | VALUE_CHANGED | check box (Toggle) |
| text box | `gates_textbox_create` | types one line | editing keys, Ctrl+C/X/V/Z/Y, the IME; double click a word, triple click all, Shift+click extends | TEXT_CHANGED, PREEDIT_CHANGED, LIMIT_EXCEEDED | edit (Value, Text) |
| editor | `gates_editor_create` | writes many lines | editing keys, Ctrl+arrows, PageUp/PageDown, Ctrl+C/X/V/Z/Y, the IME; Tab when asked; double click a word, triple click a line, Shift+click extends | TEXT_CHANGED, SELECTION_CHANGED, PREEDIT_CHANGED | edit (Value, Text by line) (chapter 6) |
| radio group | `gates_radio_create` | picks one of a few | arrows, Home, End, Space | VALUE_CHANGED (result = option id) | group of radio buttons |
| choice | `gates_choice_create` | picks one from a list | Space, Enter, Alt+Down open; arrows, Enter, Escape | VALUE_CHANGED | combo box (Selection, Expand) |
| progress | `gates_progress_create` | sees how far work has come | - | - | progress bar (percent) |
| separator | `gates_separator_create` | sees a division | - | - | separator |
| view | `gates_view_create`, `gates_log_create` | chooses in a list, table, tree or log | arrows, PageUp/PageDown, Home, End, Enter; tree: Left/Right; typing jumps to a row; Ctrl+C copies the row; Ctrl+Left/Right pick the column F2 and Space use; with multi_select Shift/Ctrl select many, Ctrl+A all | SELECTION_CHANGED, ACTIVATED, SORT_REQUESTED, EXPAND_REQUESTED, CELL_EDITED | list, table, tree (chapter 5) |
| form | `gates_form_create` | fills labelled fields | - | the editors' events | group of fields (chapter 3) |
| property grid | `gates_propgrid_create` | edits a record's typed fields | Tab, the editors' keys | VALUE_CHANGED (result = property id) | groups of named editors (chapter 5) |
| dialog, menu | `gates_dialog_open`, `gates_menu_open` | answers once, picks a command | Enter, Escape, arrows | DIALOG_CLOSED, MENU_CLOSED | window, menu (chapter 4) |
| menu bar | `gates_menubar_create` | picks a command from a menu | F10 or Alt, arrows, letters, Escape | MENU_CLOSED, commands | menu bar (chapter 11) |
| toolbar | `gates_toolbar_create` | runs a command with one click | arrows, Space, Enter | commands | tool bar (chapter 11) |
| status bar | `gates_statusbar_create` | reads the state of the program | - | - | status bar (chapter 11) |
| spin box | `gates_spin_create` | types or steps a number | Up/Down, PgUp/PgDn, Enter; an arrow held repeats; the wheel when focused | VALUE_CHANGED (value) | spinner (RangeValue) (chapter 12) |
| slider | `gates_slider_create` | drags a number along a track | arrows, PgUp/PgDn, Home, End; the wheel when focused | VALUE_CHANGED (value) | slider (RangeValue) (chapter 12) |
| group box | `gates_group_create` | sees (and folds) a set of controls | Space, Enter on the title | VALUE_CHANGED (checked = expanded) | group (ExpandCollapse) (chapter 12) |
| image | `gates_image_create` | sees a picture | - | - | image when named (chapter 13) |
| tabs | `gates_tabs_create` | switches between pages | arrows, Ctrl+Tab, Ctrl+PgUp/PgDn | VALUE_CHANGED (result = index) | tab (Selection) (chapter 11) |

Tab and Shift+Tab move focus through every enabled, shown control in tree order;
`gates_widget_set_focusable` takes one out of the order. A disabled control
(`gates_widget_set_disabled`) is drawn dimmed, ignores input and is skipped by Tab. A hidden
node (`gates_node_set_hidden`) and everything under it take no space, are not drawn and cannot
be reached. Radio groups and choices name their options by stable ids (`gates_option_t`), never
by position; id 0 means "nothing selected".

The older `on_click` / `on_toggle` arguments of the create functions run inside input routing
and exist for compatibility; new code passes null and uses a handler.

## Layout

Layout is intrinsic: every control knows the size it wants, and a container arranges its
children. The program chooses the container's kind (`gates_layout_set`):

| Kind | Arranges children |
|---|---|
| `GATES_LAYOUT_KIND_COLUMN` | top to bottom, each as wide as the column |
| `GATES_LAYOUT_KIND_ROW` | left to right, each as tall as the row |
| `GATES_LAYOUT_KIND_STACK` | on top of each other; only the active one shows (pages) |
| `GATES_LAYOUT_KIND_SPLIT` | two panes and a handle the person drags |
| `GATES_LAYOUT_KIND_SCROLL` | a column in a viewport, with wheel and scroll bar (a press on the track pages) |
| `GATES_LAYOUT_KIND_FORM` | label beside editor, row by row (chapter 3) |
| `GATES_LAYOUT_KIND_ABSOLUTE` | at rectangles the program gives |

Padding and gap are set per container; `gates_layout_set_child_grow` lets a child take a share
of the space left over, `gates_layout_set_child_align` places a child that does not stretch.
Children never overlap in normal flow. A window lays its tree out before painting, in logical
units; without a window, `gates_layout_run` does it.

<!-- example: manual/examples/ex_03_layout.c -->
```c
/* manual example (host): layout - a column with a growing row.
 * expect: the list takes the rest; buttons in a row */
#include <gates/gates.h>

#include <stdio.h>

#define TRY(x) do { if (!gates_is_ok(x)) return 1; } while (0)

int main(void) {
    gates_tree_t *t = nullptr;
    TRY(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), title, list, bar, ok, cancel;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 8));   /* logical units: 1/96 inch */
    TRY(gates_layout_set_gap(t, root, 6));
    TRY(gates_label_create(t, root, GATES_STR("Files"), &title));
    TRY(gates_panel_create(t, root, &list));
    TRY(gates_layout_set_child_grow(t, list, 1)); /* takes the space left over */
    TRY(gates_panel_create(t, root, &bar));
    TRY(gates_layout_set(t, bar, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, bar, 6));
    TRY(gates_button_create(t, bar, GATES_STR("OK"), nullptr, nullptr, &ok));
    TRY(gates_button_create(t, bar, GATES_STR("Cancel"), nullptr, nullptr, &cancel));
    TRY(gates_layout_run(t, (gates_size_t){ 240, 320 }, gates_text_backend_builtin()));

    gates_rect_t l = gates_node_layout_rect(t, list), a = gates_node_layout_rect(t, ok),
                 b = gates_node_layout_rect(t, cancel), r = gates_node_layout_rect(t, bar);
    /* The list ends one gap above the button row, which ends at the padding. */
    bool rest = l.y + l.h + 6 == r.y && r.y + r.h == 320 - 8;
    printf("%s; buttons %s\n", rest ? "the list takes the rest" : "the list does not grow",
           a.y == b.y && b.x > a.x ? "in a row" : "not in a row");
    gates_tree_destroy(t);
    return 0;
}
```

A scroll container follows keyboard focus: Tab into a control below the viewport and it
scrolls into view. It scrolls up and down only unless you ask for more (0.10.0):
`gates_layout_set_scroll_sideways(tree, area, true)` lays the children out as wide as the
widest of them, and when that is wider than the viewport a bar appears along the bottom. The
wheel's sideways motion (Shift+wheel on Windows) moves it, Tab brings the focused control
into view across as well, and assistive technology can scroll it. `gates_layout_set_scroll_x`
and `gates_layout_scroll_x` set and read the offset. Leave it off for forms: there, children
as wide as the viewport are what you want. A stack page that is not active is neither drawn nor reachable.
