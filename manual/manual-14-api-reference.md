# Chapter 14 - API reference

<!-- made by tests/api_reference.sh from include/gates/*.h: edit the headers, then run it -->

Every public function of gates, header by header, with the comment its header gives it.
The other chapters explain how the pieces work together; this one is for looking a
function up. Types, constants and struct fields are described in the headers themselves.
Declarations are shown on one line each; `[[nodiscard]]` marks a result that must be looked
at.

## gates/access.h

Accessibility model.

gates describes every node to assistive technology and automation tools in
platform-free terms: a role, a name, a description, states, a value, the
actions it accepts, and virtual items for controls that draw their own rows
(radio options, choice options, menu entries, view rows). A platform adapter
(Win32: UI Automation) turns this into its own objects and events. Items are
addressed by (node, item id) - never by a node per row - and item 0 is the
node itself.

Name, in this order (the first that gives text wins):
  1. an explicit name (gates_node_set_access_name);
  2. the text of a label tied to it (gates_node_set_labelled_by, like
     HTML's &lt;label for>);
  3. for a form editor, its field's label (without the " \*" required marker;
     the REQUIRED state says it);
  4. the node's own text: a label's text, a button's label (a bound command's
     label), a checkbox's caption, a dialog's title;
  5. for an item: its option label, command label, or the row's first cell.
A menu without an explicit name (0.10.0): a menu bar menu is named by its
title, a submenu by the entry that opened it, a menu the program opened by
the node that had focus then (its explicit name, else its own text).
Description: a form field's error, then its help, joined by ". ".
Automation id: explicit (gates_node_set_automation_id), else "field-&lt;id>" for
a form editor, "cmd-&lt;id>" for a button bound to a command, else empty; items
are "item-&lt;id>". Ids are meant to be stable across runs of the application.

Actions go through the same paths as input: events, commands, limits and
read-only rules apply, and they are refused exactly when input would be.
Setting a text value is a user edit (TEXT_CHANGED fires). Expanding is a
request (a choice opens its list; a tree row asks its model).
Views (list, table, tree, log): the rows shown now are the items (and the
selected row wherever it is: first when above them, last when below,
0.10.0), item id = row id; a row's name is its first
cell (a table row: its cells joined by ", "); gates reads cells of shown
rows only.
Everything here is for the UI thread. Platform-free.

Fills \*out for (node, item). INVALID_ARG when the node is gone (a stale
handle never answers for the node that now uses its slot) or the item is
not one of the node's. See the note on the strings above.

```c
[[nodiscard]] gates_err_t gates_access_info(gates_tree_t *tree, gates_node_t node, gates_u64 item, gates_access_info_t *out);
```

The node's items in order (0 when it has none), and the item at an index.

```c
gates_u64 gates_access_item_count(gates_tree_t *tree, gates_node_t node);
gates_u64 gates_access_item_at(gates_tree_t *tree, gates_node_t node, gates_u64 index);
```

### the accessible tree

What a platform adapter exposes, as (node, item) references: under a node,
its shown children in order (hidden nodes and inactive stack pages are left
out), then its items; under the root, after those, the open dialogs and
menus (a choice's open list is not an element of its own: its entries are
the choice's items). Every step answers the null reference (node null)
where there is nothing, or when the reference is stale.

Cells of a table row (0.10.0), a level below the items: cell 1..count are
the row's shown columns in display order (a platform adapter makes them the
row's children). gates_access_cell_at finds the cell under a point of the
row gates_access_at_point returned (0 = none). Name
and value are the cell's text, the description its column's label; a check
cell is CHECKABLE (CHECKED when set), a progress cell has a range (0-1000,
read as a percent); an editable cell accepts TOGGLE (check) or SET_VALUE
(text), which go through the model's set_cell like a person's edit and
report CELL_EDITED. Bounds are the cell's, OFFSCREEN when scrolled out.

```c
gates_u32 gates_access_cell_count(gates_tree_t *tree, gates_node_t node, gates_u64 item);
gates_u32 gates_access_cell_at(gates_tree_t *tree, gates_node_t node, gates_point_t p);
[[nodiscard]] gates_err_t gates_access_cell_info(gates_tree_t *tree, gates_node_t node, gates_u64 item, gates_u32 cell, gates_access_info_t *out);
[[nodiscard]] gates_err_t gates_access_cell_toggle(gates_tree_t *tree, gates_node_t node, gates_u64 item, gates_u32 cell);
[[nodiscard]] gates_err_t gates_access_cell_set_value(gates_tree_t *tree, gates_node_t node, gates_u64 item, gates_u32 cell, gates_str_t text);
```

The element tree, walked like the node tree: an item's parent is its node,
a node's children are its shown children then its items; overlays come
after the root's children. A ref with no answer has node GATES_NODE_NULL.

```c
gates_access_ref_t gates_access_parent(gates_tree_t *tree, gates_access_ref_t ref);
gates_access_ref_t gates_access_first_child(gates_tree_t *tree, gates_access_ref_t ref);
gates_access_ref_t gates_access_last_child(gates_tree_t *tree, gates_access_ref_t ref);
gates_access_ref_t gates_access_next(gates_tree_t *tree, gates_access_ref_t ref);
gates_access_ref_t gates_access_prev(gates_tree_t *tree, gates_access_ref_t ref);
```

The deepest element at a point (logical, window coordinates): overlays
first, as for the pointer; an item when the point is on one.

```c
gates_access_ref_t gates_access_at_point(gates_tree_t *tree, gates_point_t p);
```

Where keyboard focus is, as an element: the highlighted entry of the open
menu (or of a choice's open list, as the choice's item), else the focused
node - for a radio group, its selected option. Null when nothing has focus.

```c
gates_access_ref_t gates_access_focus_ref(gates_tree_t *tree);
```

Application-supplied properties (copied). An empty string clears.

```c
[[nodiscard]] gates_err_t gates_node_set_access_name(gates_tree_t *tree, gates_node_t node, gates_str_t name);
```

Ties a visible label to a control: the label's text becomes the control's
name, and assistive technology can move between them. Null unties.

```c
[[nodiscard]] gates_err_t gates_node_set_labelled_by(gates_tree_t *tree, gates_node_t node, gates_node_t label);
[[nodiscard]] gates_err_t gates_node_set_automation_id(gates_tree_t *tree, gates_node_t node, gates_str_t id);
[[nodiscard]] gates_err_t gates_node_set_live(gates_tree_t *tree, gates_node_t node, gates_live_t live);
```

### actions

Refused as input would be: INVALID_STATE disabled/inert, PERMISSION
read-only, INVALID_ARG not accepted by the node.

```c
[[nodiscard]] gates_err_t gates_access_invoke(gates_tree_t *tree, gates_node_t node, gates_u64 item);
[[nodiscard]] gates_err_t gates_access_toggle(gates_tree_t *tree, gates_node_t node);
[[nodiscard]] gates_err_t gates_access_select(gates_tree_t *tree, gates_node_t node, gates_u64 item);
```

A multi-select view's row joins or leaves the selection (a TOGGLE request
when its state differs, else nothing); INVALID_ARG for other nodes (0.9.0).

```c
[[nodiscard]] gates_err_t gates_access_set_item_selected(gates_tree_t *tree, gates_node_t node, gates_u64 item, bool selected);
[[nodiscard]] gates_err_t gates_access_expand(gates_tree_t *tree, gates_node_t node, gates_u64 item, bool expand);
[[nodiscard]] gates_err_t gates_access_set_value(gates_tree_t *tree, gates_node_t node, gates_str_t text);
```

A spin box's or slider's value (0.4.0), as a person's change: clamped into
the range, any value in it (not only whole steps), reported with VALUE_CHANGED.

```c
[[nodiscard]] gates_err_t gates_access_set_range_value(gates_tree_t *tree, gates_node_t node, gates_i64 value);
```

Focus a node; for an item, selecting it is how it takes focus.

```c
[[nodiscard]] gates_err_t gates_access_focus(gates_tree_t *tree, gates_node_t node, gates_u64 item);
```

Text of an edit (for text APIs such as the UIA Text pattern). Offsets are
UTF-8 bytes into the info's value. The window rectangle of text[start, end)
on screen, clipped to the visible part (false when none of it shows; an
empty range gives a zero-width rectangle at its place); the offset nearest
a point; and a selection made through the model (anchor and caret; no text
changes; refused as input would be). Password boxes answer nothing.

```c
bool gates_access_text_rect(gates_tree_t *tree, gates_node_t node, gates_u32 start, gates_u32 end, gates_rect_t *out);
```

A multi-line editor's range covers rows: one rectangle per shown row
(0.7.0), at most cap; a text box gives at most one. Returns how many.

```c
gates_u32 gates_access_text_rects(gates_tree_t *tree, gates_node_t node, gates_u32 start, gates_u32 end, gates_rect_t *out, gates_u32 cap);
gates_u32 gates_access_text_offset_at(gates_tree_t *tree, gates_node_t node, gates_point_t p);
```

A text line as a person sees it (the UIA Line unit, 0.8.0): the shown row
holding the offset, [begin, end), where end is the next row's start (after
the line break on a line's last row). An editor with wrap answers by rows,
one without by lines; false for other nodes (a text box is one line).

```c
bool gates_access_text_line(gates_tree_t *tree, gates_node_t node, gates_u32 offset, gates_u32 *begin, gates_u32 *end);
[[nodiscard]] gates_err_t gates_access_select_text(gates_tree_t *tree, gates_node_t node, gates_u32 anchor, gates_u32 caret);
```

Vertical scrolling of a view or a scroll area, in 1/GATES_ACCESS_SCROLL_MAX
of its range: \*pos is where it is, \*page how much of the whole shows.
False when the node cannot scroll (everything fits).

```c
bool gates_access_scroll_info(gates_tree_t *tree, gates_node_t node, gates_u32 *pos, gates_u32 *page);
[[nodiscard]] gates_err_t gates_access_scroll_to(gates_tree_t *tree, gates_node_t node, gates_u32 pos);
```

Sideways, for a table wider than its view or a scroll area that scrolls
sideways (0.10.0): the same units.

```c
bool gates_access_hscroll_info(gates_tree_t *tree, gates_node_t node, gates_u32 *pos, gates_u32 *page);
[[nodiscard]] gates_err_t gates_access_hscroll_to(gates_tree_t *tree, gates_node_t node, gates_u32 pos);
```

By `amount` lines (rows), or pages when `page`; negative goes up.

```c
[[nodiscard]] gates_err_t gates_access_scroll_by(gates_tree_t *tree, gates_node_t node, gates_i32 amount, bool page);
```

### changes, for the platform adapter

While enabled, the tree records what changed: a node whose state, text or
items changed (CHANGED), a parent whose children changed (STRUCTURE), focus
leaving and reaching a node (FOCUS_LOST then FOCUS_GAINED), a node destroyed
(REMOVED, recorded before its slot can be reused), a live region's new text
(LIVE), and announcements. One entry per (node, item, kind) until taken; at
most GATES_ACCESS_CHANGES_MAX, beyond that `overflow` is set and the adapter
should treat the whole tree as changed.

Change recording (for a platform adapter): off by default; turning it off
drops what was recorded.

```c
void gates_access_enable(gates_tree_t *tree, bool enable);
bool gates_access_enabled(const gates_tree_t *tree);
```

Moves up to `cap` recorded changes into out (oldest first); returns how
many. \*overflow (may be null) reports lost changes since the last take.

```c
gates_u32 gates_access_take_changes(gates_tree_t *tree, gates_access_change_t *out, gates_u32 cap, bool *overflow);
```

An announcement for screen readers (copied); the adapter speaks it with its
platform's notification. The latest text is gates_access_announcement.

```c
[[nodiscard]] gates_err_t gates_access_announce(gates_tree_t *tree, gates_str_t text, bool assertive);
gates_str_t gates_access_announcement(const gates_tree_t *tree, bool *assertive);
```

### the enforced rules

gates_access_audit checks a laid-out tree against the rules gates holds its
applications and examples to; tests and the examples run it.

Returns the number of issues found (all of them, even beyond `cap`). Theme
may be null (theme rules skipped). The tree must have been laid out.

```c
gates_u32 gates_access_audit(gates_tree_t *tree, const gates_theme_t *theme, gates_access_issue_t *out, gates_u32 cap);
```

The theme rules alone.

```c
gates_u32 gates_theme_audit(const gates_theme_t *theme, gates_access_issue_t *out, gates_u32 cap);
```

## gates/app.h

Application/platform boundary.
Opaque, platform-free header; the implementation lives in src/platform/.
Win32 backend: message pump; posting from workers and timers (gates/post.h,
gates/timer.h).

The application: one per process, made and used on the UI thread.

```c
[[nodiscard]] gates_err_t gates_app_create(const gates_app_desc_t *desc, gates_app_t **out_app);
void gates_app_destroy(gates_app_t *app);
```

Runs the platform event pump on the calling (UI) thread; returns after
gates_app_quit() or when the last window closes.

```c
[[nodiscard]] gates_err_t gates_app_run(gates_app_t *app);
void gates_app_quit(gates_app_t *app);
```

Changes the face of GATES_FONT_UI while running (0.12.0), as ui_font does at
creation - a settings screen's "font" choice; empty goes back to the
platform's. Every window measures and lays out again. NOT_FOUND when the face
is not installed (the platform's is used then); INVALID_ARG for a name that is
not UTF-8 or too long for the platform. A named face (gates_font_named) set on
a node stays as it is.

```c
[[nodiscard]] gates_err_t gates_app_set_ui_font(gates_app_t *app, gates_str_t face);
```

## gates/clipboard.h

Clipboard boundary.

The core never talks to an operating system clipboard. A platform (the
Win32 window) or a test installs a provider on the tree; textboxes use it
for copy, cut and paste. Text crosses the boundary as UTF-8 only; the
provider converts and validates (the Win32 provider turns a lone UTF-16
surrogate into U+FFFD). The core re-validates what it receives.

Copies the provider (its ctx is borrowed); null removes it, after which
copy, cut and paste do nothing.

```c
void gates_tree_set_clipboard(gates_tree_t *tree, const gates_clipboard_t *clipboard);
```

## gates/command.h

One command model for buttons, shortcuts and menus.

A command is an action the application offers, with a label, an optional
shortcut, and enabled/checked state that the application keeps current.
Commands live in a scope: the tree root for the whole window, or a dialog.
Buttons (and later menus) refer to a command by scope and id and take its
label and enabled state from it. Invoking a command - by shortcut, bound
button, menu or gates_command_invoke - is queued and delivered at the same
safe point as widget events; the command is looked up again and must still
be enabled when it runs. Each delivered invocation runs at most once.
Handlers run on the UI thread, return quickly, and may unregister commands
or destroy nodes. Platform-free.

INVALID_ARG: id 0, null invoke, bad scope or an invalid shortcut.
INVALID_STATE: the scope already has this id, this shortcut, or a command
with the same DEFAULT/CANCEL role.

```c
[[nodiscard]] gates_err_t gates_command_register(gates_tree_t *tree, gates_node_t scope, const gates_command_desc_t *desc);
[[nodiscard]] gates_err_t gates_command_unregister(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
```

Setters repaint bound buttons; identical values are no-ops.

```c
[[nodiscard]] gates_err_t gates_command_set_enabled(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id, bool enabled);
[[nodiscard]] gates_err_t gates_command_set_checked(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id, bool checked);
[[nodiscard]] gates_err_t gates_command_set_label(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id, gates_str_t label);
bool gates_command_exists(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
```

Submenus (0.10.0): in a menu, this command's entry opens a menu of `ids`
(commands of the same scope, 0 = separator; copied) instead of invoking it;
a count of 0 makes it an ordinary entry again. Invoked any other way
(shortcut, button) it runs as usual. INVALID_ARG when ids contain the
command itself.

```c
[[nodiscard]] gates_err_t gates_command_set_submenu(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id, const gates_command_id_t *ids, gates_u32 count);
```

The submenu's entries (borrowed until the next change), or null.

```c
const gates_command_id_t *gates_command_submenu(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id, gates_u32 *count);
bool gates_command_enabled(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
bool gates_command_checked(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
gates_str_t gates_command_label(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
```

Queues an invocation (delivered by gates_tree_dispatch_events).
NOT_FOUND when the command does not exist.

```c
[[nodiscard]] gates_err_t gates_command_invoke(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
```

### keymap (0.3.0)

The per-command shortcut is the keymap: a program can list a scope's commands,
rebind them (a user's keymap from a file) and print shortcuts the same way
menus and accessibility do.

Rebinds (an all-zero shortcut removes it). INVALID_ARG for an invalid
shortcut; INVALID_STATE when another command of the scope has it, with that
command's id in \*conflict (optional; 0 otherwise). NOT_FOUND: no command.

```c
[[nodiscard]] gates_err_t gates_command_set_shortcut(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id, gates_shortcut_t shortcut, gates_command_id_t *conflict);
```

The command's shortcut (all zero when none or no command).

```c
gates_shortcut_t gates_command_shortcut(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
```

The scope's live commands, in storage order (stable while none of the scope
is registered or unregistered). gates_command_at answers 0 past the end.

```c
gates_u32 gates_command_count(const gates_tree_t *tree, gates_node_t scope);
gates_command_id_t gates_command_at(const gates_tree_t *tree, gates_node_t scope, gates_u32 index);
```

"Ctrl+Shift+S", "F5", "Ctrl+Del" (empty for no shortcut) written to buf with a
terminating NUL when cap > 0 (cut to fit); returns the length it needs,
without the NUL.

```c
gates_usize_t gates_shortcut_format(const gates_shortcut_t *shortcut, char *buf, gates_usize_t cap);
```

Reads what gates_shortcut_format writes (case-insensitive; "Control", "Delete",
"Escape", "PageUp" and "PageDown" are accepted too). Empty text is the empty
shortcut. INVALID_ARG for unknown names or an invalid shortcut (no Ctrl and
no function key, or Alt).

```c
[[nodiscard]] gates_err_t gates_shortcut_parse(gates_str_t text, gates_shortcut_t *out);
```

Binds a button to a command: it shows the command's label, looks disabled
(and cannot be focused or pressed) while the command is disabled or gone,
and activating it invokes the command. id 0 unbinds.

```c
[[nodiscard]] gates_err_t gates_button_set_command(gates_tree_t *tree, gates_node_t button, gates_node_t scope, gates_command_id_t id);
```

## gates/draw.h

Draw command list.

Widgets never call a renderer directly; they emit commands into a
gates_draw_list_t, which a renderer consumes. Primitives:
solid rect, border rect, line, clip push/pop; TEXT and IMAGE
(an image of gates/image.h drawn scaled into its rect). Platform-free.

Zeroed allocator -> proven heap allocator; initial_capacity 0 -> default.

```c
[[nodiscard]] gates_err_t gates_draw_list_init(gates_draw_list_t *dl, gates_allocator_t alloc, gates_u32 initial_capacity);
void gates_draw_list_deinit(gates_draw_list_t *dl);
```

Clears commands and the clip depth; keeps the allocation for reuse.

```c
void gates_draw_list_reset(gates_draw_list_t *dl);
```

Commands (logical units): a filled rect, a border `thickness` wide inside
rect, a line (endpoints included), and a clip - pushed clips intersect,
and a pop without a push is INVALID_STATE.

```c
[[nodiscard]] gates_err_t gates_draw_rect(gates_draw_list_t *dl, gates_rect_t rect, gates_color_t color);
[[nodiscard]] gates_err_t gates_draw_border(gates_draw_list_t *dl, gates_rect_t rect, gates_i32 thickness, gates_color_t color);
[[nodiscard]] gates_err_t gates_draw_line(gates_draw_list_t *dl, gates_point_t p0, gates_point_t p1, gates_color_t color);
[[nodiscard]] gates_err_t gates_draw_clip_push(gates_draw_list_t *dl, gates_rect_t rect);
[[nodiscard]] gates_err_t gates_draw_clip_pop(gates_draw_list_t *dl);
```

Emits TEXT for the assigned rect; text bytes are copied into the list.

```c
[[nodiscard]] gates_err_t gates_draw_text(gates_draw_list_t *dl, gates_rect_t rect, gates_str_t text, gates_i32 font, gates_color_t color);
```

Emits IMAGE: the image drawn scaled into rect (bilinear, alpha blended). The
image is borrowed: it must live until the list is rendered.

```c
[[nodiscard]] gates_err_t gates_draw_image(gates_draw_list_t *dl, gates_rect_t rect, const struct gates_image *image);
```

Borrowed view of a TEXT command's bytes (valid until reset/deinit).

```c
gates_str_t gates_draw_cmd_text(const gates_draw_list_t *dl, const gates_draw_cmd_t *cmd);
```

The number of commands.

```c
static inline gates_u32 gates_draw_list_len(const gates_draw_list_t *dl);
```

nullptr when out of range.

```c
const gates_draw_cmd_t *gates_draw_list_at(const gates_draw_list_t *dl, gates_u32 i);
```

True when every CLIP_PUSH has been popped (renderers require this).

```c
static inline bool gates_draw_list_balanced(const gates_draw_list_t *dl);
```

## gates/editor.h

Multi-line text editor (0.7.0).

One node over a text buffer (gates/text_buffer.h): it lays out and paints
only the lines it shows, so a long text costs what the view costs. Tabs
expand to the next multiple of the tab width (in space advances); widths
come from the text backend, so proportional and monospace fonts both work.

Keys: arrows (Ctrl: by word), Up/Down keep the column the caret came from,
Home (first non-blank, then the line start) and End, Ctrl+Home/End,
PageUp/PageDown, Shift extends every move; Backspace/Delete (Ctrl: a word),
Enter (the line's own line ending), Tab when tab_inserts (otherwise Tab
moves focus, so the keyboard is never trapped), Ctrl+A/C/X/V/Z/Y. Pointer:
a press places the caret (Shift extends), a drag selects, a double click
selects a word, a triple click the line with its break, the wheel scrolls
(Shift: sideways), the scrollbars work as everywhere.

Options: soft wrap (rows break after the last blank that fits, else at the
last code point; with wrap, Up/Down move by rows, Home and End go to the
row's start and end first, then the line's, and the scrollbar counts
lines; a caret at a row's end stays shown there), a line number gutter, auto-indent (Enter repeats the line's
leading blanks). When tab_inserts, Tab and Shift+Tab indent and unindent
the selected lines (or type a tab), and Ctrl+Tab / Ctrl+Shift+Tab move
focus, so the keyboard is still never trapped.

Highlighting: a style table maps style bytes (gates/text_buffer.h) to
colours. A styler callback, when set, is asked before painting to style the
text from the first line whose styles may be stale up to the last line
shown ("from" is always a line start); it sets styles with
gates_text_buffer_set_style on the buffer it is given and must not change
the text. An edit makes its line stale again. Selected text is drawn in
the selection colour.

A person's edit is undoable (typing and deleting runs merge), reports
GATES_EVENT_TEXT_CHANGED (ev->text is empty: read what you need), and moves
the caret; caret and selection moves report GATES_EVENT_SELECTION_CHANGED
(ev->item = the caret offset). Program changes are silent. Offsets are UTF-8
bytes; the editor keeps the caret and edits on code point boundaries and
refuses text that is not UTF-8. Platform-free.

A multi-line editor set up by `desc` (a tab_width of 0 counts as 4).

```c
[[nodiscard]] gates_err_t gates_editor_create(gates_tree_t *tree, gates_node_t parent, const gates_editor_desc_t *desc, gates_node_t *out_editor);
```

The text, to read (lines, spans, find). Change it through the editor.

```c
const gates_text_buffer_t *gates_editor_buffer(const gates_tree_t *tree, gates_node_t editor);
gates_u32 gates_editor_length(const gates_tree_t *tree, gates_node_t editor);
```

Replaces all text: caret at 0, scrolled to the top, history cleared and
the text counted as unmodified. INVALID_ARG for text that is not UTF-8.

```c
[[nodiscard]] gates_err_t gates_editor_set_text(gates_tree_t *tree, gates_node_t editor, gates_str_t text);
```

Replaces [begin, end) (code point boundaries); `undoable` records it in the
history as one step. The caret keeps its place in the text. Silent.

```c
[[nodiscard]] gates_err_t gates_editor_replace(gates_tree_t *tree, gates_node_t editor, gates_u32 begin, gates_u32 end, gates_str_t text, bool undoable);
```

The selection is [min(anchor, caret), max(...)); both are clamped to code
point boundaries. Setting it scrolls the caret into view; silent.

```c
void gates_editor_selection(const gates_tree_t *tree, gates_node_t editor, gates_u32 *anchor, gates_u32 *caret);
[[nodiscard]] gates_err_t gates_editor_set_selection(gates_tree_t *tree, gates_node_t editor, gates_u32 anchor, gates_u32 caret);
```

Scrolls so the offset's line is shown (and its column, without wrap).

```c
[[nodiscard]] gates_err_t gates_editor_scroll_to(gates_tree_t *tree, gates_node_t editor, gates_u32 offset);
```

The first line shown, and how many whole lines fit (valid after layout).

```c
gates_u32 gates_editor_first_line(const gates_tree_t *tree, gates_node_t editor);
gates_u32 gates_editor_visible_lines(const gates_tree_t *tree, gates_node_t editor);
```

History of a person's edits (and undoable program edits).

```c
[[nodiscard]] gates_err_t gates_editor_undo(gates_tree_t *tree, gates_node_t editor);
[[nodiscard]] gates_err_t gates_editor_redo(gates_tree_t *tree, gates_node_t editor);
bool gates_editor_can_undo(const gates_tree_t *tree, gates_node_t editor);
bool gates_editor_can_redo(const gates_tree_t *tree, gates_node_t editor);
```

Modified: the text differs from the last set_text or set_unmodified (undo
back to it counts as unmodified again).

```c
bool gates_editor_modified(const gates_tree_t *tree, gates_node_t editor);
void gates_editor_set_unmodified(gates_tree_t *tree, gates_node_t editor);
```

Highlighting. Style byte i draws with styles[i] (i < count; style 0 and
bytes past the table draw as plain text). Copied; count at most 256.

```c
[[nodiscard]] gates_err_t gates_editor_set_styles(gates_tree_t *tree, gates_node_t editor, const gates_editor_style_t *styles, gates_u32 count);
```

Null removes it. Setting one makes every line stale.

```c
[[nodiscard]] gates_err_t gates_editor_set_styler(gates_tree_t *tree, gates_node_t editor, gates_editor_styler_fn fn, void *user);
```

Styles a range from the program (a search hit, a diagnostic); a styler may restyle it.

```c
[[nodiscard]] gates_err_t gates_editor_set_style(gates_tree_t *tree, gates_node_t editor, gates_u32 begin, gates_u32 end, gates_u8 style);
```

Marks that move with edits (gates/text_buffer.h); read them through the buffer.

```c
[[nodiscard]] gates_err_t gates_editor_mark_add(gates_tree_t *tree, gates_node_t editor, gates_u32 offset, gates_mark_gravity_t gravity, gates_mark_id_t *out);
[[nodiscard]] gates_err_t gates_editor_mark_remove(gates_tree_t *tree, gates_node_t editor, gates_mark_id_t id);
```

Finds the needle after the selection (before it with GATES_FIND_BACKWARD),
starting over at the other end when wrap_around; selects it and scrolls to
it. false when there is none. Silent.

```c
bool gates_editor_find(gates_tree_t *tree, gates_node_t editor, gates_str_t needle, gates_u32 flags, bool wrap_around);
```

Options that can change later (the description's wrap and line_numbers).

```c
[[nodiscard]] gates_err_t gates_editor_set_wrap(gates_tree_t *tree, gates_node_t editor, bool wrap);
[[nodiscard]] gates_err_t gates_editor_set_line_numbers(gates_tree_t *tree, gates_node_t editor, bool on);
```

Read-only: the text can be selected and copied, not changed by a person;
the program still changes it.

```c
[[nodiscard]] gates_err_t gates_editor_set_read_only(gates_tree_t *tree, gates_node_t editor, bool read_only);
bool gates_editor_read_only(const gates_tree_t *tree, gates_node_t editor);
```

## gates/encoding.h

Text encodings at the edge (0.10.0).

Inside gates every string is UTF-8: the API, the text buffer, the editor.
Text that comes from or goes to the outside - a file, the console, another
program, an older system - may be in another encoding. These functions turn
it into UTF-8 on the way in and back on the way out, so a program keeps one
encoding inside and meets each environment in its own.

Encodings: UTF-8, UTF-16 and UTF-32 (little or big endian), done here on
every platform; and code pages by their Windows numbers (949 = CP949, the
Korean Windows ANSI code page, all 11172 Hangul syllables; 51949 = EUC-KR
as such, the 2350 of KS X 1001 - the rest are unmappable there; 932, 936,
950, 1252, ...; 0 = the system's), done by a code-page
converter the platform installs (the Win32 backend does when the app is
created). The UTF code pages 65001, 1200/1201 and 12000/12001 need no
converter. Without one, other code pages are UNSUPPORTED.

Malformed input (bytes that are not text in that encoding, a lone UTF-16
surrogate, a UTF-32 value past U+10FFFF, a cut-off last character) becomes
U+FFFD by default; with GATES_ENCODING_STRICT the call refuses it with
PROVEN_ERR_INVALID_ENCODING and \*bad_at says where (a byte offset in the
input). Going out, a character the target cannot hold becomes the target's
replacement ('?' in code pages, U+FFFD in the UTF forms never happens);
strict refuses it the same way. Results are allocated from `alloc`
({0} = the heap) and freed by the caller with the same allocator; on any
error nothing is allocated. Platform-free; safe from any thread once the
converter is installed.

Input in `enc` to UTF-8. A byte order mark of that encoding at the start is
not text and is skipped. INVALID_ARG for a null pointer with a size, an
unknown kind; UNSUPPORTED for a code page without a converter;
INVALID_ENCODING (strict). \*bad_at (may be null) is set on INVALID_ENCODING.

```c
[[nodiscard]] gates_err_t gates_encoding_to_utf8(gates_encoding_t enc, const void *in, gates_usize_t size, gates_u32 flags, gates_allocator_t alloc, gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at);
```

UTF-8 to `enc` (a byte order mark first when enc.bom). Malformed UTF-8 in
`text` is treated as above (\*bad_at is then an offset into `text`).

```c
[[nodiscard]] gates_err_t gates_encoding_from_utf8(gates_encoding_t enc, gates_str_t text, gates_u32 flags, gates_allocator_t alloc, gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at);
```

A guess for bytes of unknown origin: a byte order mark decides (its length
is returned); else a text of whole 16-bit units with zeros in at least
three of four odd bytes and none in the even ones is UTF-16LE (the other
way round, BE); else valid UTF-8 (all ASCII included) is UTF-8; else the
system code page. Returns the BOM length (0 without one).

```c
gates_usize_t gates_encoding_detect(const void *in, gates_usize_t size, gates_encoding_t *out);
```

true when `text` is valid UTF-8 (no overlong forms, surrogates or values
past U+10FFFF); \*bad_at (may be null) receives the first bad offset.

```c
bool gates_utf8_valid(gates_str_t text, gates_usize_t *bad_at);
```

### without allocating (0.10.0): the UTF forms

For targets that count their memory (a microcontroller, an RTOS GUI): the same
conversions into a buffer the caller owns, and in pieces. Code pages need the
allocating calls above (UNSUPPORTED here).

Into out (cap bytes; no zero terminator is added). \*needed always receives
the full size; out null with cap 0 asks only that; a cap that is too small
is OVERFLOW and writes nothing. Otherwise as gates_encoding_to_utf8 /
_from_utf8 (marks, replacement, strict).

```c
[[nodiscard]] gates_err_t gates_encoding_to_utf8_buf(gates_encoding_t enc, const void *in, gates_usize_t size, gates_u32 flags, gates_u8 *out, gates_usize_t cap, gates_usize_t *needed, gates_usize_t *bad_at);
[[nodiscard]] gates_err_t gates_encoding_from_utf8_buf(gates_encoding_t enc, gates_str_t text, gates_u32 flags, gates_u8 *out, gates_usize_t cap, gates_usize_t *needed, gates_usize_t *bad_at);
```

to_utf8: `enc` to UTF-8 (a leading mark skipped); else UTF-8 to `enc` (a mark
first when enc.bom). UNSUPPORTED for a code page.

```c
[[nodiscard]] gates_err_t gates_encoding_stream_init(gates_encoding_stream_t *st, gates_encoding_t enc, gates_u32 flags, bool to_utf8);
```

Converts what fits into out: \*used receives the input bytes taken (all of
them, unless out filled up - feed the rest again), \*written the bytes put out.
`last` says no more input follows: a character still cut then is malformed
(replaced, or refused when strict). Strict refusal: INVALID_ENCODING with
\*bad_at the offset from the start of the stream; \*used and \*written say
what was done before it.

```c
[[nodiscard]] gates_err_t gates_encoding_stream_feed(gates_encoding_stream_t *st, const void *in, gates_usize_t size, bool last, gates_u8 *out, gates_usize_t cap, gates_usize_t *used, gates_usize_t *written, gates_usize_t *bad_at);
```

### the code-page converter (for platform backends and tests)

Code pages need tables the platform has. A converter turns bytes of a code
page into UTF-8 and back with the same rules as above (replace, or strict
with \*bad_at); it allocates its result from `alloc`. system_codepage says
what code page 0 means (Windows: GetACP, 949 on Korean Windows);
console_codepage what the console uses (0 when there is none). One
converter for the process; set it before converting from other threads.

Copied; null removes it.

```c
void gates_encoding_set_codepage_converter(const gates_codepage_converter_t *converter);
bool gates_encoding_has_codepage_converter(void);
```

The converter's answers (0 without a converter).

```c
gates_u32 gates_encoding_system_codepage(void);
gates_u32 gates_encoding_console_codepage(void);
```

### command lines and the console (0.10.0)

A Windows program's main() gets its arguments in the ANSI code page (949 on
Korean Windows), and printf's UTF-8 shows garbled on a console that is not
set to UTF-8. These give the program UTF-8 both ways.

Splits a command line (UTF-8) into arguments by the Windows rules: the first
(the program) ends at a blank unless quoted; after it blanks separate, 2n
backslashes before a quote give n and start or end quoting, 2n+1 give n and a
literal quote, other backslashes stay, and "" inside quotes is one quote. One
allocation from `alloc` ({0} = the heap) holds the pointer array (argv[argc]
is null) and the strings: free argv with the same allocator.

```c
[[nodiscard]] gates_err_t gates_args_split(gates_str_t command_line, gates_allocator_t alloc, int *argc, char ***argv);
```

Win32: this process's command line (GetCommandLineW) as UTF-8 arguments, as
gates_args_split makes them. Only in builds with src/platform/win32.

```c
[[nodiscard]] gates_err_t gates_args_utf8_win32(gates_allocator_t alloc, int *argc, char ***argv);
```

Win32: writes UTF-8 text to standard output (or standard error): through
WriteConsoleW on a console, so every character shows whatever its code page;
redirected to a file or a pipe, the UTF-8 bytes as they are. IO when Windows
refuses the write; INVALID_ENCODING (nothing written) for text that is not
UTF-8. Only in builds with src/platform/win32.

```c
[[nodiscard]] gates_err_t gates_console_write_win32(gates_str_t text, bool to_stderr);
```

A built-in converter for 949 (CP949) and 51949 (EUC-KR) for targets with no
OS to ask - a microcontroller, an RTOS GUI (0.11.0). Its tables were
measured on Windows 11, so it converts as Windows does: every CP949 code
both ways, EUC-KR as the part of it Windows maps in 51949 (with the C1 bytes
0x80-0x9F as themselves) - but not Windows' quirks: 51949's lone C9 read as
U+0000 and characters it writes and cannot read back are refused, and B4D3 is
U+B2D2 as KS X 1001 says (Windows' 51949 reads U+B2D6). Other code pages
are UNSUPPORTED; system_codepage answers 949. It costs about 120 KB of
read-only data, linked only into programs that call this. Install it with
gates_encoding_set_codepage_converter. Platform-free.

```c
const gates_codepage_converter_t *gates_codepage_converter_cp949(void);
```

The Win32 converter (MultiByteToWideChar / WideCharToMultiByte). Only in
builds that include src/platform/win32; gates_app_create installs it when
no converter is set.

```c
const gates_codepage_converter_t *gates_codepage_converter_win32(void);
```

## gates/event.h

Typed change notifications.

The application learns what a person did through events, not by polling
widgets during paint. User edits queue an event after the edit succeeded;
the window delivers queued events at a safe point after the input message
(gates_tree_dispatch_events). Programmatic setters are silent; call
gates_widget_notify to announce a programmatic change explicitly.

Handlers run on the UI thread. A handler may change or destroy any node,
including its source; events for nodes that are gone are skipped. Nested
changes queue further events for the next dispatch call instead of being
delivered recursively.

The older per-widget callbacks (gates_button_create's on_click,
gates_checkbox_create's on_toggle) still run synchronously inside pointer
routing. New code should use handlers. Platform-free.

One handler per widget (textbox, checkbox, button, radio, choice, view, dialog, menu); null removes it and drops
events it has not received yet. `user` is borrowed while registered.
Changes of the same kind and origin coalesce to the latest state before
delivery; activations never coalesce. Without a handler nothing is queued.

```c
[[nodiscard]] gates_err_t gates_widget_set_handler(gates_tree_t *tree, gates_node_t node, gates_event_fn fn, void *user);
```

Queues a PROGRAM-origin event describing the widget's current state (text,
checked, or an activation for a button). OK and nothing queued when the
widget has no handler; INVALID_ARG for widgets without events.

```c
[[nodiscard]] gates_err_t gates_widget_notify(gates_tree_t *tree, gates_node_t node);
```

Change counter: increments whenever the committed text or checked state
changes, by the user or the program. Identical values do not count.

```c
gates_u32 gates_widget_revision(const gates_tree_t *tree, gates_node_t node);
```

Delivers queued events in order, at most `max_events` (0 = all that were
queued when the call started). Returns how many remain queued; the caller
schedules another call rather than looping. Calling it from inside a
handler delivers nothing.

```c
gates_u32 gates_tree_dispatch_events(gates_tree_t *tree, gates_u32 max_events);
gates_u32 gates_tree_pending_events(const gates_tree_t *tree);
```

### bubbling (0.4.0)

A bubble handler on any node (a panel, a form, the root) receives the events
of its descendants that have no handler of their own - the nearest such
ancestor only; ev->source is the descendant. An overlay (a dialog or menu) is
a root of its own: its content bubbles to handlers inside it, never to the
window below. null removes it. `user` is borrowed while registered.

```c
[[nodiscard]] gates_err_t gates_node_set_bubble_handler(gates_tree_t *tree, gates_node_t node, gates_event_fn fn, void *user);
```

### deferred calls (0.4.0)

gates_tree_defer asks for fn(tree, key, user) to run once at the next safe
point, however often it is asked before then: one call per key, with the
fn and user of the latest request ("recompute the total after any field
changed"). The safe point is gates_tree_dispatch_events, after the queued
events are delivered; a call asked for during a deferred call runs in the
next dispatch (its return value counts it as remaining work).

```c
[[nodiscard]] gates_err_t gates_tree_defer(gates_tree_t *tree, gates_u32 key, gates_defer_fn fn, void *user);
```

true when a call for `key` was waiting and is now dropped.

```c
bool gates_tree_cancel_defer(gates_tree_t *tree, gates_u32 key);
```

## gates/form.h

Forms: labelled fields with help and error text.

A form is a node with the FORM layout: every row is a label beside an
editor (the label column is as wide as the widest label); when the form is
narrower than that label column plus 12 average character widths, each label goes above its
editor instead. Under each editor the row can show a help line and an error
line. Rows are addressed by the application's field ids, which stay stable
when rows are hidden or moved.

The form holds no data and validates nothing. The application keeps its
draft, reads the editors (or follows their events), checks the values, puts
messages on the fields with gates_form_set_error, and saves after a
successful submit. Every add builds its row completely before attaching it:
on failure nothing of it is left. Platform-free.

A form node: the rows added below line up as label | editor.

```c
[[nodiscard]] gates_err_t gates_form_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_form);
```

Each adds one row at the end. field_id: nonzero and unique in the form
(INVALID_ARG otherwise). \*out_editor (may be null) receives the editor node,
for handlers and reading values.

```c
[[nodiscard]] gates_err_t gates_form_add_text(gates_tree_t *tree, gates_node_t form, gates_u32 field_id, const gates_field_desc_t *desc, gates_node_t *out_editor);
[[nodiscard]] gates_err_t gates_form_add_checkbox(gates_tree_t *tree, gates_node_t form, gates_u32 field_id, const gates_field_desc_t *desc, gates_node_t *out_editor);
[[nodiscard]] gates_err_t gates_form_add_choice(gates_tree_t *tree, gates_node_t form, gates_u32 field_id, const gates_field_desc_t *desc, gates_node_t *out_editor);
[[nodiscard]] gates_err_t gates_form_add_radio(gates_tree_t *tree, gates_node_t form, gates_u32 field_id, const gates_field_desc_t *desc, gates_node_t *out_editor);
```

Shows `message` (copied) under the field's editor in the error colour and
marks a text editor invalid; an empty message clears both.

```c
[[nodiscard]] gates_err_t gates_form_set_error(gates_tree_t *tree, gates_node_t form, gates_u32 field_id, gates_str_t message);
```

The field's editor, row, or GATES_NODE_NULL for an unknown id.

```c
gates_node_t gates_form_editor(const gates_tree_t *tree, gates_node_t form, gates_u32 field_id);
gates_node_t gates_form_row(const gates_tree_t *tree, gates_node_t form, gates_u32 field_id);
```

The field id whose editor is `editor`, or 0 (useful in a shared handler).

```c
gates_u32 gates_form_field_of(const gates_tree_t *tree, gates_node_t form, gates_node_t editor);
```

Hides or shows a whole row (label, editor, help, error).

```c
[[nodiscard]] gates_err_t gates_form_set_row_hidden(gates_tree_t *tree, gates_node_t form, gates_u32 field_id, bool hidden);
gates_u32 gates_form_field_count(const gates_tree_t *tree, gates_node_t form);
```

## gates/frame.h

The application frame (0.3.0): mnemonics, menu bar,
toolbar, status bar, tooltips, tabs.

Mnemonics. In the text of a button, check box, command, menu bar title or
tab, "&x" marks x as the mnemonic when x is a letter or digit (ASCII); "&&"
shows one '&'; any other '&' is shown as it is, so "Save & close" needs no
escaping. A label parses '&' only when it has a target (it often shows data
such as file names). Alt+x activates the control: a button is pressed, a
check box toggled, a label's target focused, a menu bar title opened. When
several reachable controls share the letter, each Alt+x moves the focus to
the next of them without activating anything. Menu bar titles come first.
The mnemonic letter is underlined while keyboard cues are visible: from an
Alt press or keyboard menu mode until the next pointer press, or always when
the platform says so. gates_widget_text returns the text as it was set;
accessible names drop the markup.

Menu bar. One per tree: a row of titles, each over a list of commands of the
bar's scope (id 0 = separator), shown as an ordinary menu overlay (same
dismissal rules, same re-checked invocation, same MENU_CLOSED report). A
click on a title opens its menu; while one is open, moving over another title
switches to it and a click on the open title closes it. Keyboard: F10 (when
there is a reachable menu bar; otherwise F10 stays a command shortcut) or Alt
released alone enters menu mode - the first title is highlighted; Left/Right
move, Down/Up/Enter/Space open, a title's mnemonic letter opens it, Escape
leaves. In an open menu Left/Right go to the neighbouring menu, Escape goes
back to the highlighted title, an entry's mnemonic letter chooses it. The bar
is not a Tab stop; a modal dialog makes it unreachable. Entries may open
submenus (gates_command_set_submenu, gates/overlay.h); in one, Right on an
entry without a submenu goes to the next title and Left closes the submenu.
Platform-free.

### mnemonics

The mnemonic of a text: 'A'-'Z' or '0'-'9' (upper case) for the first "&x",
or 0 when there is none.

```c
gates_u8 gates_mnemonic_of(gates_str_t text);
```

Gives a label a target: its text becomes mnemonic markup and Alt+x focuses
the target. GATES_NODE_NULL removes the target (the text is plain again).

```c
[[nodiscard]] gates_err_t gates_label_set_target(gates_tree_t *tree, gates_node_t label, gates_node_t target);
gates_node_t gates_label_target(const gates_tree_t *tree, gates_node_t label);
```

Alt+codepoint (the platform's system character). true when a mnemonic took
it; false leaves it to the platform (Alt+F4, Alt+Space and unknown letters).

```c
bool gates_input_mnemonic(gates_tree_t *tree, gates_u32 codepoint);
```

Alt was pressed and released alone (or the platform's menu key): enters menu
mode on the menu bar, or leaves it when already in it. false when there is no
reachable menu bar (the platform then does its own thing).

```c
bool gates_input_menu_key(gates_tree_t *tree);
```

Alt went down: keyboard cues become visible until the next pointer press.

```c
void gates_input_show_cues(gates_tree_t *tree);
```

The platform's setting "always underline access keys".

```c
void gates_tree_set_cues_always(gates_tree_t *tree, bool always);
bool gates_tree_cues_visible(const gates_tree_t *tree);
```

### menu bar

Creates the tree's menu bar (INVALID_STATE when it already has a live one).
Its menus list commands of `scope`.

```c
[[nodiscard]] gates_err_t gates_menubar_create(gates_tree_t *tree, gates_node_t parent, gates_node_t scope, gates_node_t *out_bar);
```

Adds a menu: its title (copied, mnemonic markup) and its command ids (copied;
0 = separator; count >= 1). \*out_index (optional) receives its position.

```c
[[nodiscard]] gates_err_t gates_menubar_add(gates_tree_t *tree, gates_node_t bar, gates_str_t title, const gates_command_id_t *ids, gates_u32 count, gates_u32 *out_index);
gates_u32 gates_menubar_count(const gates_tree_t *tree, gates_node_t bar);
gates_str_t gates_menubar_title(const gates_tree_t *tree, gates_node_t bar, gates_u32 index);
```

Opens menu `index` as if chosen from the keyboard (menu mode, first entry
selected). OUT_OF_BOUNDS for a bad index; INVALID_STATE when the bar is not
reachable.

```c
[[nodiscard]] gates_err_t gates_menubar_open(gates_tree_t *tree, gates_node_t bar, gates_u32 index);
```

The open menu's index and overlay node, or -1 / GATES_NODE_NULL.

```c
gates_i32 gates_menubar_open_index(const gates_tree_t *tree, gates_node_t bar);
gates_node_t gates_menubar_menu(const gates_tree_t *tree, gates_node_t bar);
```

In menu mode (a title highlighted or a menu open from the bar).

```c
bool gates_menubar_active(const gates_tree_t *tree, gates_node_t bar);
```

The highlighted title in menu mode, or -1.

```c
gates_i32 gates_menubar_highlighted(const gates_tree_t *tree, gates_node_t bar);
```

### toolbar

A row of compact buttons, each bound to a command of the bar's scope (label,
enabled and checked state come from the command; a checked command shows its
button pressed). Flat until hovered or focused. One Tab stop: Left/Right/
Home/End move between buttons, Space or Enter invokes. A click invokes
without taking the focus away from where the person was working (so Cut and
Paste buttons act on the focused text box). Buttons that do not fit are left
out and a ">>" button at the end lists their commands in a menu. Every
button's tooltip is its command's label and shortcut.

A toolbar in `parent` whose buttons run commands of `scope`.

```c
[[nodiscard]] gates_err_t gates_toolbar_create(gates_tree_t *tree, gates_node_t parent, gates_node_t scope, gates_node_t *out_bar);
```

Adds a button for command `id` (0 = a separator).

```c
[[nodiscard]] gates_err_t gates_toolbar_add(gates_tree_t *tree, gates_node_t bar, gates_command_id_t id);
```

Entries (buttons and separators), and how many are shown after the last layout
(the rest are in the ">>" menu).

```c
gates_u32 gates_toolbar_count(const gates_tree_t *tree, gates_node_t bar);
gates_u32 gates_toolbar_shown(const gates_tree_t *tree, gates_node_t bar);
```

### status bar

A row of text segments along the bottom of a window, separated by thin
lines. A segment is a label: change it with gates_widget_set_text. Segments
are not announced unless the program makes one a live region
(gates_node_set_live), so a clock does not chatter.

A status bar in `parent`; add segments with gates_statusbar_add.

```c
[[nodiscard]] gates_err_t gates_statusbar_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_bar);
```

Adds a segment (a label, text copied). grow > 0 takes a share of the spare
width, like a layout child's grow.

```c
[[nodiscard]] gates_err_t gates_statusbar_add(gates_tree_t *tree, gates_node_t bar, gates_str_t text, gates_u8 grow, gates_node_t *out_segment);
```

### tooltips

A short help text shown in a small box below a node (above when there is no
room) after the pointer rests on it for GATES_TOOLTIP_DELAY_MS, or that long
after keyboard focus reaches it; it hides on a press, a key, when the pointer
or the focus leaves, when the node is disabled, hidden or destroyed, and
after GATES_TOOLTIP_SHOW_MS. Moving from one node with a tooltip to another
while one is shown switches at once; a modal dialog opening hides it. It never
takes input. Assistive technology reads it as the node's help text, and the
shown tooltip is also an element (a ToolTip, announced when it opens): item
GATES_ACCESS_TOOLTIP_ITEM of the root (0.10.0). Tooltips need the tree's clock
(every window has one); nothing is timed while no hovered or focused node has
a tooltip.

Copied; an empty text removes it.

```c
[[nodiscard]] gates_err_t gates_node_set_tooltip(gates_tree_t *tree, gates_node_t node, gates_str_t text);
gates_str_t gates_node_tooltip(const gates_tree_t *tree, gates_node_t node);
```

The tooltip shown now: true with its node, item (a toolbar button: its
entry index + 1, else 0), text (borrowed until the next change) and box
(window coordinates, after layout).

```c
bool gates_tooltip_shown(const gates_tree_t *tree, gates_node_t *node, gates_u64 *item, gates_str_t *text, gates_rect_t *box);
```

### tabs

A strip of titles over pages, one page shown at a time. gates_tabs_add
returns the page: a column panel for the application's controls. The strip
is one Tab stop (the selected tab): Left/Right/Home/End select at once (no
wrapping); Ctrl+Tab / Ctrl+Shift+Tab and Ctrl+PgDn / Ctrl+PgUp switch from
anywhere inside the tabs (wrapping); a click on a title selects it and
focuses the strip; a title's mnemonic selects it. When the focus was inside
the page that goes away, it moves into the new page (its first control), or
to the strip. A person's switch queues GATES_EVENT_VALUE_CHANGED on the tabs
node (ev->result = the new index); the program's gates_tabs_set_selected is
silent. Titles that do not fit (0.10.0): the strip shows whole titles, always
the selected one (from the first title while it fits, else ending at it),
and a ">>" button - a press, or Alt+Down on the strip - opens a menu of every
tab (the selected one checked; choosing one switches). Assistive technology
sees ">>" as a "More tabs" item after the titles.

An empty tabs node; add tabs with gates_tabs_add.

```c
[[nodiscard]] gates_err_t gates_tabs_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_tabs);
```

Adds a tab with a title (copied, mnemonic markup) and returns its page.

```c
[[nodiscard]] gates_err_t gates_tabs_add(gates_tree_t *tree, gates_node_t tabs, gates_str_t title, gates_node_t *out_page);
[[nodiscard]] gates_err_t gates_tabs_set_title(gates_tree_t *tree, gates_node_t tabs, gates_u32 index, gates_str_t title);
gates_u32 gates_tabs_count(const gates_tree_t *tree, gates_node_t tabs);
gates_str_t gates_tabs_title(const gates_tree_t *tree, gates_node_t tabs, gates_u32 index);
gates_node_t gates_tabs_page(const gates_tree_t *tree, gates_node_t tabs, gates_u32 index);
```

Silent. OUT_OF_BOUNDS for a bad index.

```c
[[nodiscard]] gates_err_t gates_tabs_set_selected(gates_tree_t *tree, gates_node_t tabs, gates_u32 index);
gates_u32 gates_tabs_selected(const gates_tree_t *tree, gates_node_t tabs);
```

## gates/gates.h

The whole public API in one include.

Programs may include this or only the headers they use; each public header
compiles on its own. Everything named gates_i_\* or kept in src/ is internal
and not part of the API.

## gates/geometry.h

2D geometry primitives.
Pixel geometry is integer (gates_i32); float appears only in vectors that
carry sub-pixel data (pointer deltas). Platform-free.

No area (w or h <= 0).

```c
static inline bool gates_rect_is_empty(gates_rect_t r);
```

The point is inside: left and top edges in, right and bottom out.

```c
static inline bool gates_rect_contains(gates_rect_t r, gates_point_t p);
```

Intersection; empty results are normalized to w=h=0.

```c
static inline gates_rect_t gates_rect_intersect(gates_rect_t a, gates_rect_t b);
```

### logical units

Every coordinate and size the API takes or returns is a logical unit,
1/96 inch. A window scales once at its boundary: drawing goes to device
pixels with these helpers and pointer positions come back through them.
At 96 dpi (100%) both are the identity. `dpi` 0 means 96.

round-half-up(v \* dpi / 96), in integers for any sign. Scale edges, not
lengths: a rect's pixel width is px(x + w) - px(x), so neighbours tile.

```c
static inline gates_i32 gates_px(gates_i32 v, gates_u32 dpi);
```

The logical unit whose scaled span contains the device pixel: the v with
gates_px(v) <= px < gates_px(v + 1), i.e. floor((96 \* (2px + 1) - 1) / (2 dpi)).
The exact inverse of the edge rounding above (a plain px \* 96 / dpi would
put some pixels into the neighbouring unit at 125% and 175%).

```c
static inline gates_i32 gates_logical(gates_i32 px, gates_u32 dpi);
```

A logical rect in device pixels: its edges scale, so adjacent rects stay
adjacent at any dpi.

```c
static inline gates_rect_t gates_rect_px(gates_rect_t r, gates_u32 dpi);
```

A line or border thickness: scaled, but never thinner than one pixel.

```c
static inline gates_i32 gates_thickness_px(gates_i32 t, gates_u32 dpi);
```

## gates/image.h

Images and icons (0.5.0).

A tree owns images by id (0 = none): RGBA8 pixels copied in, or decoded from
a file or memory by the decoder the platform installs (the Win32 window
installs Windows Imaging Component: PNG, JPEG, BMP, GIF, ICO, TIFF). An
image's natural size is its pixel size in logical units, so it scales with
the window like everything else (bilinear). Removing an image leaves the
nodes that showed it empty; ids are never reused within a tree.

An image node shows one image, at its natural size or a size the program
sets (then the picture keeps its aspect ratio, centred). With an accessible
name (gates_node_set_access_name) it is an Image to assistive technology;
without one it is decoration and left out. Icons are drawn at 16 x 16 units:
on buttons (left of the label; a button with an icon and no text needs an
access name), on commands (menus draw it in the gutter, toolbars left of the
label or alone with gates_toolbar_set_icons_only).

Variants (0.10.0). An image may hold more pixel sets of the same picture -
a 16-unit icon drawn at 16, 24 and 32 pixels, say. Its natural size stays
the first set's; whenever it is drawn, the renderer takes the smallest set
that covers the device pixels it fills, else the largest, so icons stay
crisp at 150 % and 200 % and any image drawn larger than its natural size
gains too. Platform-free.

Copies w x h straight-alpha RGBA8 pixels (rows `stride` bytes apart, 0 =
w \* 4). INVALID_ARG for a size of 0 or over 16384 on a side.

```c
[[nodiscard]] gates_err_t gates_image_add_rgba(gates_tree_t *tree, gates_i32 w, gates_i32 h, const gates_u8 *rgba, gates_u32 stride, gates_image_id_t *out_id);
```

NOT_FOUND for an unknown id.

```c
[[nodiscard]] gates_err_t gates_image_remove(gates_tree_t *tree, gates_image_id_t id);
```

The natural size ({0, 0} for an unknown id).

```c
gates_size_t gates_image_size(const gates_tree_t *tree, gates_image_id_t id);
```

For custom painting with gates_draw_image (valid until the image is removed).

```c
const gates_image_t *gates_tree_image(const gates_tree_t *tree, gates_image_id_t id);
```

The decoder seam: turns a file (a UTF-8 path) or bytes into RGBA8 pixels
allocated from `alloc` (the tree frees them after copying).
Copied; null removes it.

```c
void gates_tree_set_image_decoder(gates_tree_t *tree, const gates_image_decoder_t *decoder);
```

UNSUPPORTED without a decoder; the decoder's error when it cannot decode.

```c
[[nodiscard]] gates_err_t gates_image_load_file(gates_tree_t *tree, gates_str_t path, gates_image_id_t *out_id);
[[nodiscard]] gates_err_t gates_image_load_memory(gates_tree_t *tree, const void *bytes, gates_usize_t size, gates_image_id_t *out_id);
```

Adds a pixel set to image `id` (0.10.0): the same picture at another size,
copied like gates_image_add_rgba. Its h must be within one pixel of
w \* (natural h) / (natural w), and its width not the natural one
(INVALID_ARG otherwise); a set as wide as one added before replaces it.
NOT_FOUND for an unknown id. The load forms decode like the ones above.

```c
[[nodiscard]] gates_err_t gates_image_add_variant_rgba(gates_tree_t *tree, gates_image_id_t id, gates_i32 w, gates_i32 h, const gates_u8 *rgba, gates_u32 stride);
[[nodiscard]] gates_err_t gates_image_load_variant_file(gates_tree_t *tree, gates_image_id_t id, gates_str_t path);
[[nodiscard]] gates_err_t gates_image_load_variant_memory(gates_tree_t *tree, gates_image_id_t id, const void *bytes, gates_usize_t size);
```

Pixel sets, the natural one first then the others by width (0 for an
unknown id), and the size of one ({0, 0} past the end).

```c
gates_u32 gates_image_variant_count(const gates_tree_t *tree, gates_image_id_t id);
gates_size_t gates_image_variant_size(const gates_tree_t *tree, gates_image_id_t id, gates_u32 index);
```

The pixel size of the set drawn into `device` pixels ({0, 0} for an unknown
id): what a renderer of its own should use too.

```c
gates_size_t gates_image_pick_size(const gates_tree_t *tree, gates_image_id_t id, gates_size_t device);
```

### the image node and icons

An image node showing `image` (0 = nothing yet); gates_image_node_set changes it.

```c
[[nodiscard]] gates_err_t gates_image_create(gates_tree_t *tree, gates_node_t parent, gates_image_id_t image, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_image_node_set(gates_tree_t *tree, gates_node_t node, gates_image_id_t image);
```

{0, 0} = the natural size.

```c
[[nodiscard]] gates_err_t gates_image_node_set_size(gates_tree_t *tree, gates_node_t node, gates_size_t size);
gates_image_id_t gates_image_node_image(const gates_tree_t *tree, gates_node_t node);
```

0 removes it.

```c
[[nodiscard]] gates_err_t gates_button_set_icon(gates_tree_t *tree, gates_node_t button, gates_image_id_t icon);
```

Menus and toolbars show the command's icon.

```c
[[nodiscard]] gates_err_t gates_command_set_icon(gates_tree_t *tree, gates_node_t scope, gates_command_id_t id, gates_image_id_t icon);
gates_image_id_t gates_command_icon(const gates_tree_t *tree, gates_node_t scope, gates_command_id_t id);
```

Toolbar buttons with an icon show only it (the label goes to the tooltip).

```c
[[nodiscard]] gates_err_t gates_toolbar_set_icons_only(gates_tree_t *tree, gates_node_t bar, bool icons_only);
```

## gates/input.h

Unified input events.
Platform-free. The Win32 backend fills the mouse fields; pen, touch and
gestures are for later backends.

## gates/inputs.h

Number input: spin box and slider (0.4.0).

Both hold an integer value in a range with a step and a page step. A
decimal quantity uses a scale: value 125 with scale 100 is shown as 1.25.
A person's change queues GATES_EVENT_VALUE_CHANGED with ev->value = the new
value (the latest when changes coalesce); gates_range_set_value is silent.

Spin box: a text box with up/down arrows. Up/Down step, PgUp/PgDn page (the
text box keeps Left/Right/Home/End for its caret). Typing marks the box
invalid until the text is a number in range; Enter or leaving the box
commits it - text that is not a number in range goes back to the value.
A click on an arrow steps (the focus goes to the box); held, it repeats
after a pause while the pointer stays on it (a tree with a clock). The
wheel steps a focused spin box or slider (unfocused, it scrolls the page).

Slider: a track with a thumb, horizontal or vertical. Left/Down step down,
Right/Up step up, PgUp/PgDn page, Home/End go to the ends; dragging the
thumb follows the pointer in steps; a press on the track pages toward it.
Optional tick marks. Platform-free.

INVALID_ARG for min > max, a negative step or page, or another scale.

```c
[[nodiscard]] gates_err_t gates_spin_create(gates_tree_t *tree, gates_node_t parent, const gates_range_t *range, gates_node_t *out_spin);
[[nodiscard]] gates_err_t gates_slider_create(gates_tree_t *tree, gates_node_t parent, const gates_range_t *range, bool vertical, gates_node_t *out_slider);
```

Silent; clamped into the range. INVALID_ARG for other nodes.

```c
[[nodiscard]] gates_err_t gates_range_set_value(gates_tree_t *tree, gates_node_t node, gates_i64 value);
gates_i64 gates_range_value(const gates_tree_t *tree, gates_node_t node);
```

New limits and steps (the value is clamped into them, silently).

```c
[[nodiscard]] gates_err_t gates_range_set(gates_tree_t *tree, gates_node_t node, const gates_range_t *range);
gates_range_t gates_range_get(const gates_tree_t *tree, gates_node_t node);
```

Slider tick marks every `steps` steps (0 = none).

```c
[[nodiscard]] gates_err_t gates_slider_set_ticks(gates_tree_t *tree, gates_node_t slider, gates_u32 steps);
```

The spin box's text box (for a label target, an access name, a width).

```c
gates_node_t gates_spin_box(const gates_tree_t *tree, gates_node_t spin);
```

Formats `value` with the scale's decimals ("-1.25"); returns the length it
needs, NUL-terminated when cap > 0. Parses the same form (a leading '+' or
'-', digits, at most the scale's decimals; ',' is read as '.'); false for
anything else, including overflow.

```c
gates_usize_t gates_range_format(gates_i64 value, gates_u32 scale, char *buf, gates_usize_t cap);
bool gates_range_parse(gates_str_t text, gates_u32 scale, gates_i64 *out);
```

## gates/layout.h

Intrinsic layout: absolute, row, column, stack, split,
scroll, form, grid and wrap; padding/gap/grow/align; measure/arrange.
Platform-free.

Invariant: after arrange, sibling layout rects in normal-flow
containers (row/column) do not overlap. Stack children intentionally share
their parent's content rect; only the active child is painted/hit.

GRID (0.4.0): columns 1..GATES_GRID_MAX_COLUMNS; a column's grow weight;
a child's span in columns (1 by default; wider than the grid is cut to fit).

```c
[[nodiscard]] gates_err_t gates_layout_set_grid(gates_tree_t *tree, gates_node_t node, gates_u32 columns);
[[nodiscard]] gates_err_t gates_layout_set_grid_column_grow(gates_tree_t *tree, gates_node_t node, gates_u32 column, gates_u8 weight);
```

A row's grow weight (0.8.0): row < GATES_GRID_MAX_GROW_ROWS, counted over shown rows.

```c
[[nodiscard]] gates_err_t gates_layout_set_grid_row_grow(gates_tree_t *tree, gates_node_t node, gates_u32 row, gates_u8 weight);
[[nodiscard]] gates_err_t gates_layout_set_child_span(gates_tree_t *tree, gates_node_t child, gates_u32 columns);
```

Container properties.

```c
[[nodiscard]] gates_err_t gates_layout_set(gates_tree_t *tree, gates_node_t node, gates_layout_t kind);
[[nodiscard]] gates_err_t gates_layout_set_padding(gates_tree_t *tree, gates_node_t node, gates_i32 padding);
[[nodiscard]] gates_err_t gates_layout_set_gap(gates_tree_t *tree, gates_node_t node, gates_i32 gap);
```

STACK: which child (by sibling order, 0-based) is visible/interactive.

```c
[[nodiscard]] gates_err_t gates_layout_set_stack_active(gates_tree_t *tree, gates_node_t node, gates_u32 child_index);
gates_u32 gates_layout_stack_active(const gates_tree_t *tree, gates_node_t node);
```

SPLIT: direction plus the first pane's share in per-mille (1..999).
Panes never shrink below GATES_SPLIT_MIN_PANE_PX; dragging the handle
updates the ratio.

```c
[[nodiscard]] gates_err_t gates_layout_set_split(gates_tree_t *tree, gates_node_t node, gates_split_dir_t dir, gates_i32 ratio_permille);
gates_i32 gates_layout_split_ratio(const gates_tree_t *tree, gates_node_t node);
```

SCROLL: vertical offset in px, clamped to [0, content_h - viewport_h].

```c
[[nodiscard]] gates_err_t gates_layout_set_scroll_offset(gates_tree_t *tree, gates_node_t node, gates_i32 offset_y);
gates_i32 gates_layout_scroll_offset(const gates_tree_t *tree, gates_node_t node);
```

SCROLL sideways (0.10.0, off by default): children are laid out as wide as
the widest of them (at least the viewport), and when that is wider than the
viewport a horizontal bar appears along the bottom. The wheel's sideways
motion (Shift+wheel on Windows) scrolls it, focus moves bring the focused
control into view across too, and assistive technology can scroll it
(gates_access_hscroll_info). Off, children are exactly the viewport's width
as before. The offset is in px, clamped to [0, content_w - viewport_w].

```c
[[nodiscard]] gates_err_t gates_layout_set_scroll_sideways(gates_tree_t *tree, gates_node_t node, bool on);
bool gates_layout_scroll_sideways(const gates_tree_t *tree, gates_node_t node);
[[nodiscard]] gates_err_t gates_layout_set_scroll_x(gates_tree_t *tree, gates_node_t node, gates_i32 offset_x);
gates_i32 gates_layout_scroll_x(const gates_tree_t *tree, gates_node_t node);
```

Measured content size (valid after gates_layout_run).

```c
gates_size_t gates_layout_scroll_content(const gates_tree_t *tree, gates_node_t node);
```

Child properties.

```c
[[nodiscard]] gates_err_t gates_layout_set_child_grow(gates_tree_t *tree, gates_node_t node, gates_u8 weight);
[[nodiscard]] gates_err_t gates_layout_set_child_align(gates_tree_t *tree, gates_node_t node, gates_align_t align);
```

ABSOLUTE child: requested rect relative to the parent's content box.

```c
[[nodiscard]] gates_err_t gates_layout_set_abs_rect(gates_tree_t *tree, gates_node_t node, gates_rect_t rect);
```

Runs measure + arrange over the whole tree; root gets {0,0,viewport}.
Clears the tree's layout-dirty bit.

```c
[[nodiscard]] gates_err_t gates_layout_run(gates_tree_t *tree, gates_size_t viewport, const gates_text_backend_t *text);
```

Results (valid after gates_layout_run).

```c
gates_rect_t gates_node_layout_rect(const gates_tree_t *tree, gates_node_t node);
gates_size_t gates_node_preferred_size(const gates_tree_t *tree, gates_node_t node);
```

Invariant checker: recursively verifies that sibling layout rects in row/column
containers do not overlap. Used by tests and debug builds.

```c
bool gates_layout_validate(const gates_tree_t *tree, gates_node_t node);
```

## gates/overlay.h

Transient surfaces: modal dialogs and context menus as
overlays inside the window.

Overlays sit above the window's content: they are laid out after it (a
dialog centred, a menu at its anchor, both kept inside the window), painted
on top of it and hit first. One set of rules dismisses them: a menu closes
on Escape, on an outside click (the click goes no further), when the window
loses focus, or when its commands' scope is destroyed. A dialog is modal:
input below it is blocked, it is the focus scope (Tab cycles inside, only
its commands' shortcuts apply, Enter runs its default command, Escape its
cancel command or else cancels it). Every dialog and menu reports its end
once, through its handler (GATES_EVENT_DIALOG_CLOSED / _MENU_CLOSED), and
its node is destroyed after that delivery: during the CLOSED handler the
dialog's content is still valid, so read what you need there. Opening a
dialog cancels any press or drag in progress below it. There is no nested event loop:
opening returns at once. At most 8 overlays are open at a time.
Platform-free.

Opens a modal dialog. \*out_dialog is the dialog (register its commands with
it as scope; set a handler on it for DIALOG_CLOSED); \*out_content is a
column panel for the application's controls. The first focusable control
gets focus at the next layout run. OUT_OF_BOUNDS when 8 overlays are open.

```c
[[nodiscard]] gates_err_t gates_dialog_open(gates_tree_t *tree, const gates_dialog_desc_t *desc, gates_node_t *out_dialog, gates_node_t *out_content);
```

Ends the dialog once with this result: removes it, restores the focus that
was current when it opened (or the next control), and queues DIALOG_CLOSED.
INVALID_STATE when it is not an open dialog. On failure to queue the event
the dialog stays open (nothing is lost).

```c
[[nodiscard]] gates_err_t gates_dialog_close(gates_tree_t *tree, gates_node_t dialog, gates_dialog_result_t result);
```

Opens a context menu at `at` (window coordinates) listing the commands of
`scope` in order; id 0 draws a separator. Labels, checked marks, shortcuts
and enabled states come from the commands. Choosing an entry invokes its
command through the same queued, re-checked path as a shortcut.
Submenus (0.10.0, gates_command_set_submenu): an entry that has one shows an
arrow and opens it beside itself - Right, Enter, Space, a click, or the
pointer resting on it for GATES_MENU_SUB_DELAY_MS; Left or Escape close it;
another row of the parent closes it. Choosing in a submenu closes the whole
chain, and the menu the program opened reports MENU_CLOSED with the id. A
press outside every menu of the chain closes them all. Submenus count
against the 8 overlays.

```c
[[nodiscard]] gates_err_t gates_menu_open(gates_tree_t *tree, gates_point_t at, gates_node_t scope, const gates_command_id_t *ids, gates_u32 count, gates_node_t *out_menu);
```

Closes a menu without invoking anything (reported with result 0).

```c
[[nodiscard]] gates_err_t gates_menu_close(gates_tree_t *tree, gates_node_t menu);
```

Closes every open menu (the platform calls this when the window loses
focus); dialogs stay.

```c
void gates_tree_dismiss_menus(gates_tree_t *tree);
```

Open overlays, topmost last.

```c
gates_u32 gates_tree_overlay_count(const gates_tree_t *tree);
bool gates_overlay_is_open(const gates_tree_t *tree, gates_node_t overlay);
```

## gates/post.h

Posting from worker threads to the UI.

A worker never touches the tree. It holds a sender (from gates_app_sender
on the UI thread, before the worker starts) and posts messages: a target
(a tree and a node in it), a kind, and a payload whose ownership passes to
gates when the post succeeds. The UI thread delivers messages at a safe
point, a bounded number per turn, to the target node's message handler, then
releases the payload. Every payload is released exactly once: after delivery,
when its target is gone (stale), when it is replaced, or when the app shuts
down. A post that fails (FULL, CLOSED, too large) leaves the payload with the
caller.

Posting never blocks on the UI and never allocates. The queue has a limit
in messages and in payload bytes. It starts with GATES_POST_INITIAL_MESSAGES
slots (or the limit, when smaller) and grows on the UI thread - doubling at
the next delivery turn after posts filled three quarters of it - up to the
limit, so a program pays for the room it used, never for more (0.9.0). A
post that finds it full is refused, not held. A full queue answers GATES_POST_FULL
(try again later, drop, or coalesce); a closed one GATES_POST_CLOSED (stop
producing and release the sender). A sender stays valid on every thread
until its last reference is released, even after the app is destroyed.

Release functions may run on a posting thread or on the thread that shuts
the app down: they must only free the payload, never touch UI state.
gates_sender_retain / _release / _post may be called from any thread; every
other function here is for the UI thread. The allocator given to the app
must be thread-safe (the default one is). Platform-free.

The tree's serial number, and a target for a node of it (UI thread).

```c
gates_u64 gates_tree_serial(const gates_tree_t *tree);
gates_target_t gates_target(const gates_tree_t *tree, gates_node_t node);
```

The app's sender, retained for the caller (release it when done).

```c
[[nodiscard]] gates_err_t gates_app_sender(gates_app_t *app, gates_sender_t **out_sender);
void gates_sender_retain(gates_sender_t *sender);
void gates_sender_release(gates_sender_t *sender);
```

OK: gates owns the payload now. GATES_POST_FULL / GATES_POST_CLOSED, or
OUT_OF_BOUNDS for a message larger than the byte limit: the caller keeps it.

```c
[[nodiscard]] gates_err_t gates_sender_post(gates_sender_t *sender, const gates_message_t *msg);
```

The node's message handler (null removes it). The payload is borrowed for
the call and released right after it. Messages for a node without a handler
are released undelivered. A handler may post again: that message is
delivered in a later turn, never recursively.

```c
[[nodiscard]] gates_err_t gates_node_set_message_handler(gates_tree_t *tree, gates_node_t node, gates_message_fn fn, void *user);
```

### for platform backends and tests

The core has no threads of its own: the platform supplies a lock (kept
inside the sender, so it lives as long as the sender) and a wake-up that
makes the UI thread call gates_sender_dispatch soon. Wake is called with the
lock held, only when the queue goes from empty to non-empty, and never
after gates_sender_close returns.

A new sender with one reference (the creator's).

```c
[[nodiscard]] gates_err_t gates_sender_create(const gates_sender_desc_t *desc, gates_sender_t **out_sender);
```

Trees that may receive messages (a tree is attached to at most one sender;
gates_tree_destroy detaches it, and its queued messages become stale).

```c
[[nodiscard]] gates_err_t gates_sender_attach(gates_sender_t *sender, gates_tree_t *tree);
void gates_sender_detach(gates_sender_t *sender, gates_tree_t *tree);
```

Delivers at most `max` messages (0 -> GATES_POST_PER_TURN); returns how many
remain queued (the platform schedules another turn when that is not 0).

```c
gates_u32 gates_sender_dispatch(gates_sender_t *sender, gates_u32 max);
```

Stops accepting posts (CLOSED from now on), releases every queued payload
on this thread, detaches all trees. The sender memory stays until the last
reference is released.

```c
void gates_sender_close(gates_sender_t *sender);
gates_u32 gates_sender_pending(gates_sender_t *sender);
```

Slots the queue has now: grows toward max_messages as it is used (0.9.0).

```c
gates_u32 gates_sender_capacity(gates_sender_t *sender);
gates_usize_t gates_sender_pending_bytes(gates_sender_t *sender);
```

## gates/propgrid.h

Property grid (0.6.0): a record's typed
fields as rows of a name and an editor, grouped by category.

A property grid is a panel built from the ordinary controls: each category
is a collapsible group box (its title a Tab stop), holding a two-column
grid of labels and editors - a text box, a check box, a choice or a spin
box. Properties without a category come first, outside any group. Each
editor is named by its label for assistive technology, and Tab walks the
editors in order. A property is known by a stable id (nonzero, unique in
the grid); gates_propgrid_editor gives its editor, whose own functions set
and read the value (gates_textbox_set_text, gates_checkbox_set_checked,
gates_options_set_selected, gates_range_set_value, ...).

Changes a person makes are reported to the grid's handler as one kind of
event: GATES_EVENT_VALUE_CHANGED with ev->source = the grid, ev->result =
the property id, and the value in ev->text (text), ev->checked (bool) or
ev->value (a number, or the chosen option id). Text reports every committed
change, as a text box does. Program changes through the editors are
silent. Platform-free.

An empty property grid; add properties below.

```c
[[nodiscard]] gates_err_t gates_propgrid_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_grid);
```

Adds a property at the end of its category (made, as a collapsible group
after the others, the first time it is named; empty = no category). The
name and category are copied. INVALID_ARG for id 0, an id already in the
grid, or what the editor itself refuses (a bad option list or range);
nothing is left behind on failure.

```c
[[nodiscard]] gates_err_t gates_propgrid_add_text(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id, gates_str_t name, gates_str_t value);
[[nodiscard]] gates_err_t gates_propgrid_add_bool(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id, gates_str_t name, bool value);
[[nodiscard]] gates_err_t gates_propgrid_add_choice(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id, gates_str_t name, const gates_option_t *options, gates_u32 count, gates_u32 selected_id);
[[nodiscard]] gates_err_t gates_propgrid_add_number(gates_tree_t *tree, gates_node_t grid, gates_str_t category, gates_prop_id_t id, gates_str_t name, const gates_range_t *range);
```

The property's editor, or GATES_NODE_NULL for an unknown id.

```c
gates_node_t gates_propgrid_editor(const gates_tree_t *tree, gates_node_t grid, gates_prop_id_t id);
```

The category's group box, or GATES_NODE_NULL (to fold it, or save its state).

```c
gates_node_t gates_propgrid_category(const gates_tree_t *tree, gates_node_t grid, gates_str_t category);
gates_u32 gates_propgrid_count(const gates_tree_t *tree, gates_node_t grid);
```

The handler for property changes (null removes it). `user` is borrowed
while registered. The grid hears its editors through a bubble handler of
its own: do not set another on the grid node itself.

```c
[[nodiscard]] gates_err_t gates_propgrid_set_handler(gates_tree_t *tree, gates_node_t grid, gates_event_fn fn, void *user);
```

## gates/render.h

Software renderer, the reference renderer.

Consumes a balanced gates_draw_list_t and rasterizes into a caller-provided
32-bit BGRA8 pixel buffer (byte order B,G,R,A - GDI-DIB native, the one
format). Deterministic, platform-free, testable on plain memory.

Blending is straight-alpha src-over with integer rounding:
  out = (src \* a + dst \* (255 - a) + 127) / 255
IMAGE commands draw the image scaled into their rect (bilinear over the image's
pixel centres, edges clamped, straight-alpha src-over); TEXT requires a text backend. An unbalanced draw list returns PROVEN_ERR_INVALID_STATE.

```c
[[nodiscard]] gates_err_t gates_render_soft(const gates_draw_list_t *dl, gates_pixels_t target, const gates_text_backend_t *text_backend);
```

The same for a draw list in logical units rendered at `dpi`:
rects and clips scale by their edges (gates_rect_px), thicknesses never drop
below one pixel, text goes to the backend's draw_scaled (or to draw, at the
scaled rect, when it has none). gates_render_soft is this at 96 dpi.

```c
[[nodiscard]] gates_err_t gates_render_soft_scaled(const gates_draw_list_t *dl, gates_pixels_t target, const gates_text_backend_t *text_backend, gates_u32 dpi);
```

BGRA8 pixel pack/unpack helpers (shared by renderer, present, and tests).

```c
static inline gates_u32 gates_pixel_pack(gates_color_t c);
```

The inverse of gates_pixel_pack.

```c
static inline gates_color_t gates_pixel_unpack(gates_u32 px);
```

## gates/state.h

Persisted UI state (0.3.0).

What a person arranges - split positions, the selected tab, column widths,
scroll positions - can outlive the program: gates_state_save writes it as
text, the program keeps the text where it likes (a file beside its
settings), and gates_state_load applies it at the next start. Only nodes with
an automation id (gates_node_set_automation_id) take part, and the id is the
key, so ids must stay the same between versions of the program.

Format: a first line "# gates state 1", then one line per node,
"&lt;kind> &lt;value> &lt;id>" - kind is split (ratio per mille), tabs (selected
index), columns ("&lt;column id>:&lt;width>" per column in display order, comma
separated, "h" after a hidden one; the older widths-only form still loads)
or scroll (offset; "y,x" when it is also scrolled sideways, 0.10.0);
the value has no spaces; the id runs to the end of the line, so any id text is
safe except a line break. Loading skips lines it does not understand, ids it
does not find and kinds that do not match the node - old files never break a
newer program. Platform-free; the window's placement is in gates/window.h.

Writes the state into buf. \*needed always receives the text's length; with
buf == null only the length is reported (OK); a too small cap returns
OVERFLOW and writes nothing.

```c
[[nodiscard]] gates_err_t gates_state_save(const gates_tree_t *tree, gates_u8 *buf, gates_usize_t cap, gates_usize_t *needed);
```

Applies the lines that match; \*applied (optional) receives how many did.
Never fails on content: only a bad argument is an error. Run it after the
tree is built (a scroll offset is clamped at the next layout).

```c
[[nodiscard]] gates_err_t gates_state_load(gates_tree_t *tree, gates_str_t text, gates_u32 *applied);
```

## gates/task.h

Background tasks (0.6.0): work on a
thread of its own, with progress, a result and cancellation reported on
the UI thread.

gates_task_start runs `work` on a new thread. The work function never
touches the tree: it reports progress with gates_task_report (the latest
report replaces one not yet shown), polls gates_task_cancelled, and returns
its result. On the UI thread, in the tree's normal event turn, gates calls
on_progress for reports and on_done once when the work has returned (then
the thread is joined and the task handle ends after on_done). Data the work
produced can be left in the program's own structure (`user`): everything
the work wrote before returning is visible to on_done.

gates_task_cancel only asks: the work decides when to stop, and on_done
still comes, with `cancelled` true. Destroying the tree (closing its window)
cancels every task and waits for each work function to return; on_done is
not called then. A work function that never looks at gates_task_cancelled
keeps its window from closing until it ends.

The tree needs a sender (gates/post.h: a window has one) and threads from
the platform (the Win32 window installs them); otherwise UNSUPPORTED.
Message kinds from 0xFFFF0000 up are gates' own for this: a program's
messages use lower kinds.

Worker side (any thread).
permille 0..1000 (larger is reported as 1000); text is copied (may be empty).
OK, or the queue's answer (GATES_POST_FULL / GATES_POST_CLOSED: that report
is dropped; the work may go on).

```c
[[nodiscard]] gates_err_t gates_task_report(gates_task_t *task, gates_u32 permille, gates_str_t text);
bool gates_task_cancelled(const gates_task_t *task);
```

\*out_task (may be null) is valid until on_done returns or the tree is
destroyed. UNSUPPORTED without a sender or threads; the platform's error
when the thread cannot start (nothing is left behind).

```c
[[nodiscard]] gates_err_t gates_task_start(gates_tree_t *tree, const gates_task_desc_t *desc, gates_task_t **out_task);
```

Asks the work to stop (it sees gates_task_cancelled).

```c
void gates_task_cancel(gates_task_t *task);
```

Tasks started and not yet done (their on_done not yet called).

```c
gates_u32 gates_tree_task_count(const gates_tree_t *tree);
```

### for platform backends and tests

Installs (copies) the platform's threads; null removes them (tasks already
running are unaffected).

```c
void gates_tree_set_threads(gates_tree_t *tree, const gates_threads_t *threads);
```

## gates/text.h

Text metrics contract and backend seam (0.2.0).

The core depends only on this contract. Per font a backend reports ascent,
descent, line height, an average character width (a sizing hint), and the
advance of every code point; a string's width is exactly the sum of its code
points' advances (no kerning, ligatures or shaping), so the core computes
caret positions, selections and hit tests itself. Rendering happens freely
inside the rect the core assigns, with code point k at the sum of the
advances before it. Proportional and fixed-pitch faces both satisfy it.
Platform-free header.

A face at `percent` (100 gives the face alone).

```c
static inline gates_font_t gates_font_sized(gates_font_t face, gates_u32 percent);
static inline gates_font_t gates_font_face(gates_font_t font);
static inline gates_u32 gates_font_percent(gates_font_t font);
```

A length at the font's size: round-half-up(v \* percent / 100).

```c
static inline gates_i32 gates_font_scale(gates_font_t font, gates_i32 v);
```

Named faces (0.12.0): a face chosen by name - "Malgun Gothic", "Arial" - for
one node and what is under it (gates_node_set_font), or for a whole window
by setting it on the root. gates_font_named registers a name once per process
and returns its face (GATES_FONT_NAMED_FIRST and up; the same name, compared
byte for byte, gives the same face); it is a face like GATES_FONT_UI, so it
takes sizes and is inherited the same way. A backend draws it from
gates_font_face_name; a face the system does not have is drawn in the UI face,
and a backend with one face of its own (the builtin one) ignores names.
Register on the UI thread, before or between frames. Names are UTF-8, 1 to
GATES_FONT_NAME_MAX bytes: INVALID_ARG otherwise, OVERFLOW past
GATES_FONT_NAMED_MAX names.

```c
[[nodiscard]] gates_err_t gates_font_named(gates_str_t name, gates_font_t *out_face);
```

The name of a named face (any size of it); empty for GATES_FONT_UI,
GATES_FONT_MONO and faces never registered. Valid for the process.

```c
gates_str_t gates_font_face_name(gates_font_t font);
```

### widths (src/text/gates_text_width.c, 0.2.0)

The width of a single-line string: the sum of its code points' advances
(0 for a null backend).

```c
gates_i32 gates_text_width(const gates_text_backend_t *backend, gates_font_t font, gates_str_t text);
```

The byte offset of the code-point boundary nearest to x (x measured from the
start of the text; before the start -> 0, past the end -> text.size).

```c
gates_u32 gates_text_offset_at_x(const gates_text_backend_t *backend, gates_font_t font, gates_str_t text, gates_i32 x);
```

### shared UTF-8 and cell rules (src/text/gates_text_utf8.c)

Decoding is shared by every backend and the edit core. Cells (1 narrow, 2
wide) are a helper for fixed-pitch backends; layout no longer uses them.

Decodes the codepoint starting at `at`; returns bytes consumed (>= 1).
Malformed input consumes one byte and yields U+FFFD.

```c
gates_u32 gates_text_decode(gates_str_t text, gates_u32 at, gates_u32 *out_cp);
```

Display cells for one codepoint: 2 for Hangul/CJK/fullwidth, else 1.

```c
gates_u32 gates_text_cell_width(gates_u32 codepoint);
```

Cells occupied by a whole string.

```c
gates_u32 gates_text_cells(gates_str_t text);
```

Byte offset of the codepoint boundary before/after `at` (clamped).

```c
gates_u32 gates_text_prev_offset(gates_str_t text, gates_u32 at);
gates_u32 gates_text_next_offset(gates_str_t text, gates_u32 at);
```

The builtin reference backend: embedded 8x16 monospace
bitmap font (vendored public-domain font8x8, rows doubled). Deterministic
on every platform; ASCII glyphs, replacement box otherwise; the same face
for every font (advances by the cell rule: 8 per narrow, 16 per wide code
point). The explicit opt-in for pixel-exact output.

```c
const gates_text_backend_t *gates_text_backend_builtin(void);
```

The Win32 GDI backend: real system glyphs including Hangul. GATES_FONT_UI is
the system message font (proportional), GATES_FONT_MONO a fixed-pitch face.
Only available in builds that include src/platform/win32; it satisfies the
contract by placing every glyph at its logical offset itself.

```c
const gates_text_backend_t *gates_text_backend_win32_gdi(void);
```

## gates/text_buffer.h

Text buffer (0.7.0): UTF-8 text with lines, styles and
marks, for the multi-line editor and for programs handling large texts.

Bytes live in a gap buffer: an edit near the last one costs little, and a
read never moves the gap - it returns the text as at most two spans (before
and after the gap). Only gates_text_buffer_contiguous moves it, when a
program asks for one span. Offsets are bytes; the buffer does not check
UTF-8 (the editor does, and edits only at code point boundaries).

Lines end after each "\n" (a "\r" before it belongs to the line end; a lone
"\r" is text). There is always at least one line; a text ending in "\n" has
an empty last line. Line starts are kept in an index updated per edit with a
pending shift (Scintilla's partitioning), so an edit near the end of a long
text does not rewrite every start after it.

Every byte has a style byte (0 = plain); inserted text takes style 0. Marks
are positions that move with edits: an insertion exactly at a LEFT mark
goes after it (the mark stays before the new text), at a RIGHT mark before
it (the mark moves past the new text); a deleted range containing a mark
moves it to where the range was. A replace is a delete then an insert.
Mark ids are never reused. Platform-free; not thread-safe.

The allocator: zero -> the proven heap.

```c
[[nodiscard]] gates_err_t gates_text_buffer_create(gates_allocator_t alloc, gates_text_buffer_t **out);
void gates_text_buffer_destroy(gates_text_buffer_t *b);
```

Bytes of text.

```c
gates_u32 gates_text_buffer_length(const gates_text_buffer_t *b);
```

Replaces [begin, end) with text (copied). INVALID_ARG for a bad range,
OUT_OF_BOUNDS past GATES_TEXT_BUFFER_MAX, NOMEM; on failure nothing changes.
Read what a replace removes (for undo) before calling it.

```c
[[nodiscard]] gates_err_t gates_text_buffer_replace(gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_str_t text);
[[nodiscard]] gates_err_t gates_text_buffer_set_text(gates_text_buffer_t *b, gates_str_t text);
```

Reading: [begin, end) (clamped) as two spans (the second empty unless the
range crosses the gap); a copy (returns the bytes copied, at most cap); one
byte (0 past the end); the whole text as one span (moves the gap to the end:
valid until the next edit).

```c
void gates_text_buffer_span(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_str_t *first, gates_str_t *second);
gates_u32 gates_text_buffer_copy(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_u8 *out, gates_u32 cap);
gates_u8 gates_text_buffer_byte(const gates_text_buffer_t *b, gates_u32 offset);
gates_str_t gates_text_buffer_contiguous(gates_text_buffer_t *b);
```

Lines, from 0. line_start of a line past the last is the length; line_end is
where the line's text ends (before "\n" or "\r\n"); line_of is the line
containing an offset (the last line for the length and beyond).

```c
gates_u32 gates_text_buffer_line_count(const gates_text_buffer_t *b);
gates_u32 gates_text_buffer_line_start(const gates_text_buffer_t *b, gates_u32 line);
gates_u32 gates_text_buffer_line_end(const gates_text_buffer_t *b, gates_u32 line);
gates_u32 gates_text_buffer_line_of(const gates_text_buffer_t *b, gates_u32 offset);
```

Styles: one byte per text byte.

```c
gates_u8 gates_text_buffer_style(const gates_text_buffer_t *b, gates_u32 offset);
void gates_text_buffer_set_style(gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_u8 style);
```

The styles of [begin, end) as two spans, like gates_text_buffer_span.

```c
void gates_text_buffer_style_span(const gates_text_buffer_t *b, gates_u32 begin, gates_u32 end, gates_str_t *first, gates_str_t *second);
```

Marks.

```c
[[nodiscard]] gates_err_t gates_text_buffer_mark_add(gates_text_buffer_t *b, gates_u32 offset, gates_mark_gravity_t gravity, gates_mark_id_t *out);
```

NOT_FOUND for an unknown id.

```c
[[nodiscard]] gates_err_t gates_text_buffer_mark_remove(gates_text_buffer_t *b, gates_mark_id_t id);
[[nodiscard]] gates_err_t gates_text_buffer_mark_set(gates_text_buffer_t *b, gates_mark_id_t id, gates_u32 offset);
```

The mark's offset, or false for an unknown id.

```c
bool gates_text_buffer_mark_offset(const gates_text_buffer_t *b, gates_mark_id_t id, gates_u32 *offset);
```

Find the needle from `from`: forward, the first match starting at or after
it; backward, the last match ending at or before it. Case-insensitive
compares ASCII letters only.

```c
bool gates_text_buffer_find(const gates_text_buffer_t *b, gates_u32 from, gates_str_t needle, gates_u32 flags, gates_u32 *match_begin);
```

## gates/text_edit.h

UTF-8 text edit core.

Owns a UTF-8 buffer, a caret, a selection and an uncommitted preedit range
(filled by an IME adapter). Platform-free and allocator-injected; the
textbox widget and any future editor build on it.

Editing unit: one codepoint. Korean NFC syllables are single codepoints, so
caret motion and backspace are correct for Hangul. Grapheme clusters
(combining marks, emoji ZWJ sequences) are a documented v0.x deferral.

`alloc` {0} = the heap; `initial` is copied, the caret after it.

```c
[[nodiscard]] gates_err_t gates_text_edit_init(gates_text_edit_t *ed, gates_allocator_t alloc, gates_str_t initial);
void gates_text_edit_deinit(gates_text_edit_t *ed);
```

The committed text (borrowed until the next change), the caret, and the
selection [begin, end) - empty when the anchor is at the caret.

```c
gates_str_t gates_text_edit_text(const gates_text_edit_t *ed);
gates_u32 gates_text_edit_caret(const gates_text_edit_t *ed);
bool gates_text_edit_has_selection(const gates_text_edit_t *ed);
gates_u32 gates_text_edit_sel_begin(const gates_text_edit_t *ed);
gates_u32 gates_text_edit_sel_end(const gates_text_edit_t *ed);
```

Replaces all text (copied); caret and anchor at the end.

```c
[[nodiscard]] gates_err_t gates_text_edit_set_text(gates_text_edit_t *ed, gates_str_t text);
```

Ensures room for `total_bytes` of committed text, so a following insert up
to that size cannot fail. Nothing else changes.

```c
[[nodiscard]] gates_err_t gates_text_edit_reserve(gates_text_edit_t *ed, gates_u32 total_bytes);
```

Replaces the selection (if any) with `text` and leaves the caret after it.

```c
[[nodiscard]] gates_err_t gates_text_edit_insert(gates_text_edit_t *ed, gates_str_t text);
```

Delete the selection, else one codepoint before/after the caret.

```c
[[nodiscard]] gates_err_t gates_text_edit_backspace(gates_text_edit_t *ed);
[[nodiscard]] gates_err_t gates_text_edit_delete(gates_text_edit_t *ed);
```

Left/Right by a code point, Home/End to the ends; `extend` keeps the
anchor. An unextended Left/Right with a selection collapses it to that edge.

```c
void gates_text_edit_move(gates_text_edit_t *ed, gates_caret_move_t how, bool extend);
void gates_text_edit_select_all(gates_text_edit_t *ed);
```

Clamped to a codepoint boundary.

```c
void gates_text_edit_set_caret(gates_text_edit_t *ed, gates_u32 byte_offset, bool extend);
```

Preedit (IME composition). The committed buffer is untouched;
the preedit is displayed at the selection end (the caret when nothing is
selected). A failed set keeps the previous preedit.

```c
[[nodiscard]] gates_err_t gates_text_edit_set_preedit(gates_text_edit_t *ed, gates_str_t text);
void gates_text_edit_clear_preedit(gates_text_edit_t *ed);
gates_str_t gates_text_edit_preedit(const gates_text_edit_t *ed);
```

Cell geometry for rendering and hit testing (the cell rules).

```c
gates_u32 gates_text_edit_cells_before(const gates_text_edit_t *ed, gates_u32 byte_offset);
```

Byte offset of the codepoint boundary at or before `cell`.

```c
gates_u32 gates_text_edit_offset_at_cell(const gates_text_edit_t *ed, gates_u32 cell);
```

## gates/theme.h

Semantic color tokens.
Ordinary controls never use hard-coded RGB; they resolve tokens through the
active theme (light, dark and high contrast; the window follows the system).
Platform-free.

Built-in profiles. Every token is defined in each; text tokens
reach 4.5:1 contrast on their backgrounds, borders and cues 3:1. Windows
follow the system (dark mode, high contrast) unless the application picks
a theme (gates/window.h).

```c
const gates_theme_t *gates_theme_light(void);
const gates_theme_t *gates_theme_dark(void);
```

Fills \*out with a high-contrast theme from the system's colours (null ->
the classic black high-contrast set). Borders and cues are wider.

```c
void gates_theme_high_contrast(const gates_system_colors_t *sys, gates_theme_t *out);
```

Readers with defaults: a null theme or 0 width gives 2; an unknown token
gives a loud magenta.

```c
static inline gates_i32 gates_theme_focus_width(const gates_theme_t *theme);
static inline gates_i32 gates_theme_error_width(const gates_theme_t *theme);
static inline gates_color_t gates_theme_color(const gates_theme_t *theme, gates_color_token_t token);
```

## gates/timer.h

UI timers.

A timer belongs to a node of a tree and runs its callback on the UI thread
after `interval_ms`, once or repeatedly. Cancelling is immediate: a
cancelled timer never fires afterwards, and destroying its node or tree
cancels it. A repeating timer that is late fires once and is rescheduled
from now (no burst of missed ticks). A callback may start and cancel timers,
its own included; a timer started in a callback fires on a later run, never
in the same one. Resolution is the platform's (Win32: about 10-16 ms).
Idle windows have no platform timer running. Platform-free.

interval_ms >= 1. INVALID_STATE when the tree has no clock (a window's tree
has one).

```c
[[nodiscard]] gates_err_t gates_timer_start(gates_tree_t *tree, gates_node_t node, gates_u32 interval_ms, bool repeat, gates_timer_fn fn, void *user, gates_timer_id_t *out_id);
```

INVALID_ARG when the id is not an active timer of this tree.

```c
[[nodiscard]] gates_err_t gates_timer_cancel(gates_tree_t *tree, gates_timer_id_t id);
bool gates_timer_active(const gates_tree_t *tree, gates_timer_id_t id);
```

### for platform backends and tests

The clock, and `changed`, called whenever the next due time may have moved
(a timer started or cancelled) so the platform can re-arm its own timer.

```c
void gates_tree_set_clock(gates_tree_t *tree, gates_clock_fn now, void (*changed)(void *ctx), void *ctx);
```

Fires every timer that is due now (each at most once) and returns the
milliseconds until the next one is due, or GATES_TIMER_NONE.

```c
gates_u32 gates_tree_run_timers(gates_tree_t *tree);
```

Milliseconds until the next timer is due (0 = overdue), or GATES_TIMER_NONE.

```c
gates_u32 gates_tree_next_timer(const gates_tree_t *tree);
gates_u32 gates_tree_timer_count(const gates_tree_t *tree);
```

## gates/tree.h

Retained node tree core.

A node pool with generation handles, O(1) tree links, subtree destroy with
deferred free at explicit safe points. A window (gates_window_t) owns one
tree; a tree can also live without a window (tests, headless programs).

Threading: a gates_tree_t is single-thread-owned (UI thread).

### lifecycle

Creates a tree with a live root node (kind GATES_NODE_CUSTOM).

```c
[[nodiscard]] gates_err_t gates_tree_create(const gates_tree_desc_t *desc, gates_tree_t **out_tree);
```

Frees every slot and the tree itself. Handles become meaningless.

```c
void gates_tree_destroy(gates_tree_t *tree);
```

The root node: everything in the window hangs from it; never destroyed.

```c
gates_node_t gates_tree_root(const gates_tree_t *tree);
```

True only for a live, non-destroy-pending node whose generation matches
(a destroy_pending node is no longer a valid target).

```c
bool gates_node_is_valid(const gates_tree_t *tree, gates_node_t node);
```

### node ops

parent == GATES_NODE_NULL creates a detached node (attach with append).

```c
[[nodiscard]] gates_err_t gates_node_create(gates_tree_t *tree, gates_node_t parent, const gates_node_desc_t *desc, gates_node_t *out_node);
```

Subtree destroy: marks node + descendants destroy_pending and unlinks
from the parent. Slots are freed at the next gates_tree_flush_destroys().
The root cannot be destroyed.

```c
[[nodiscard]] gates_err_t gates_node_destroy(gates_tree_t *tree, gates_node_t node);
```

Detaches node from its parent; the subtree below it stays intact.

```c
[[nodiscard]] gates_err_t gates_node_remove(gates_tree_t *tree, gates_node_t node);
```

Appends a detached node as parent's last child.

```c
[[nodiscard]] gates_err_t gates_node_append(gates_tree_t *tree, gates_node_t parent, gates_node_t child);
```

Inserts a detached node before `before` (a child of parent).
before == GATES_NODE_NULL appends (DOM insertBefore semantics).

```c
[[nodiscard]] gates_err_t gates_node_insert_before(gates_tree_t *tree, gates_node_t parent, gates_node_t child, gates_node_t before);
```

Moves node (attached or detached) under new_parent (append position).
Rejects making a node a descendant of itself (cycle prevention);
link updates are O(1), the cycle check walks new_parent's ancestors.

```c
[[nodiscard]] gates_err_t gates_node_reparent(gates_tree_t *tree, gates_node_t node, gates_node_t new_parent);
```

Safe point: frees all destroy_pending slots (generation bump + free list).
The window calls it after event dispatch, before layout, at frame end.

```c
[[nodiscard]] gates_err_t gates_tree_flush_destroys(gates_tree_t *tree);
```

### introspection

Links, counts, kind and the desc's user_data; GATES_NODE_NULL, 0,
GATES_NODE_CUSTOM or null for a node that is gone.

```c
gates_node_t gates_node_parent(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_node_first_child(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_node_last_child(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_node_prev_sibling(const gates_tree_t *tree, gates_node_t node);
gates_node_t gates_node_next_sibling(const gates_tree_t *tree, gates_node_t node);
gates_u32 gates_node_child_count(const gates_tree_t *tree, gates_node_t node);
gates_node_kind_t gates_node_kind(const gates_tree_t *tree, gates_node_t node);
void *gates_node_user_data(const gates_tree_t *tree, gates_node_t node);
```

Hidden: a hidden node and everything below it take no space in
layout, are not painted, not hit and not focusable; focus, a press or a drag
inside it are let go, and a choice's open list inside it closes. The flag is
the node's own; a child of a hidden node keeps its flag but is not shown.
A hidden page of a stack still counts as a page. The root and overlays
(dialogs, menus) cannot be hidden (INVALID_ARG).

```c
[[nodiscard]] gates_err_t gates_node_set_hidden(gates_tree_t *tree, gates_node_t node, bool hidden);
bool gates_node_hidden(const gates_tree_t *tree, gates_node_t node);
```

Font (0.2.0): a face from gates/text.h - GATES_FONT_UI (the platform's
proportional UI face, the default), GATES_FONT_MONO (fixed pitch), a named
face from gates_font_named (0.12.0), or GATES_FONT_INHERIT (-1, take the
parent's). Like CSS font-family, a node's font applies to everything under
it that does not choose its own: set on the root, a face is the whole
window's. Dialogs and menus are not under the root: they start from
GATES_FONT_UI unless set on them (the program's UI face for everything:
gates_app_desc_t.ui_font). Sizes are separate (gates_node_set_font_size).
INVALID_ARG for other values, a sized font, and names never registered.

```c
[[nodiscard]] gates_err_t gates_node_set_font(gates_tree_t *tree, gates_node_t node, gates_i32 font);
```

The effective font: the node's own face, else its nearest ancestor's, else
GATES_FONT_UI - at the effective size (gates_font_sized; 0.10.0).

```c
gates_i32 gates_node_font(const gates_tree_t *tree, gates_node_t node);
```

Size (0.10.0), in percent of the platform's text size: GATES_FONT_SIZE_MIN
to _MAX (GATES_FONT_SIZE_SMALL, _LARGE, _HEADING are usual ones), or 0 to
take the parent's. Inherited like the face, and independent of it: a MONO
node under a HEADING panel is mono at 150 %. Everything the node measures
and draws follows it (text, line heights, rows, carets). INVALID_ARG out of
range.

```c
[[nodiscard]] gates_err_t gates_node_set_font_size(gates_tree_t *tree, gates_node_t node, gates_u32 percent);
```

The effective size in percent (100 when neither the node nor an ancestor sets one).

```c
gates_u32 gates_node_font_size(const gates_tree_t *tree, gates_node_t node);
```

Counters for tests and diagnostics.

```c
gates_u32 gates_tree_live_count(const gates_tree_t *tree);
gates_u32 gates_tree_pending_count(const gates_tree_t *tree);
gates_u32 gates_tree_capacity(const gates_tree_t *tree);
```

### dirty tracking

GATES_TREE_DIRTY_\* bits set since they were last cleared (a layout run
clears the layout bit). Any layout bit relayouts from the root; any paint
bit repaints the window.

```c
gates_u32 gates_tree_dirty(const gates_tree_t *tree);
void gates_tree_clear_dirty(gates_tree_t *tree, gates_u32 bits);
```

### keyboard focus (traversal and scopes: gates/ui.h, gates/overlay.h)

The focused node, or GATES_NODE_NULL.

```c
gates_node_t gates_tree_focus(const gates_tree_t *tree);
```

GATES_NODE_NULL clears focus. Marks paint dirty when the focus changes.

```c
void gates_tree_set_focus(gates_tree_t *tree, gates_node_t node);
```

## gates/types.h

Foundation types.
Thin, zero-cost aliases over proven_c_lib. Core code includes no platform headers.

Every gates call returns GATES_OK or a proven error.

```c
static inline bool gates_is_ok(gates_err_t err);
```

gates_str_t is a proven string view: these convert for free.

```c
static inline gates_str_t gates_str_from_proven(proven_u8str_view_t v);
static inline proven_u8str_view_t gates_str_to_proven(gates_str_t s);
```

The null handle (not whether a node is alive: gates_node_is_valid), and
handle equality (index and generation).

```c
static inline bool gates_node_is_null(gates_node_t n);
static inline bool gates_node_eq(gates_node_t a, gates_node_t b);
```

## gates/ui.h

Tree paint walk and pointer routing.
The window drives these each frame: layout (gates/layout.h) -> paint walk
emits theme-token draw commands -> renderer; pointer events route to the
deepest hit widget (hover/press/click/toggle). Platform-free.

Emits draw commands for the tree (visible nodes only: inactive stack pages
are skipped) into dl. Run gates_layout_run first. Clears the tree's
paint-dirty bit.

```c
[[nodiscard]] gates_err_t gates_paint_tree(gates_tree_t *tree, gates_draw_list_t *dl, const gates_theme_t *theme, const gates_text_backend_t *text);
```

Deepest visible node whose layout rect contains p (normal-flow non-overlap makes
this unambiguous in normal flow; stack considers the active page only).

```c
gates_node_t gates_hit_test(const gates_tree_t *tree, gates_point_t p);
```

The pointer's shape over p (0.13.0), so a person sees what a press there
does before pressing: RESIZE_EW over a table's column edge and a
side-by-side split's handle, RESIZE_NS over a stacked split's handle, TEXT
over a text box and an editor's text, ARROW elsewhere (also over disabled
controls and what a modal dialog covers). While a drag runs, the drag's
shape wherever the pointer is. The platform asks before it draws the pointer
(Win32: WM_SETCURSOR); run gates_layout_run first.

```c
gates_cursor_t gates_cursor_at(gates_tree_t *tree, gates_point_t p);
```

Routes a pointer event: updates hover/pressed, fires button on_click and
checkbox on_toggle on release-inside, marks paint dirty on state changes.
Returns the node that consumed the event (or GATES_NODE_NULL).

```c
gates_node_t gates_input_pointer(gates_tree_t *tree, const gates_pointer_event_t *ev);
```

Moves keyboard focus to the next (or previous) focusable control in tree
order, wrapping inside the focus scope, and scrolls it into view. Returns
false when there is none. Tab / Shift+Tab do the same.

```c
bool gates_tree_focus_next(gates_tree_t *tree, bool backward);
```

Routes a key: the focused control first (textbox editing; Space/Enter on a
button or checkbox), then Tab traversal, Escape (press cancel, scope cancel
command), Enter (scope default command), then command shortcuts. Returns
true when the key was consumed; false leaves it to the application. Key-up
events matter only for Space (activation happens on release).

```c
bool gates_input_key(gates_tree_t *tree, const gates_key_event_t *ev);
```

Routes a committed character (Unicode codepoint) to the focused node.
Control codepoints below U+0020 (and U+007F) are ignored.

```c
gates_input_result_t gates_input_char(gates_tree_t *tree, gates_u32 codepoint);
```

IME composition, called by the platform IME adapter.

preedit: sets or replaces the in-progress text shown at the caret (after
  the selection); cursor is the IME cursor in bytes into the text, snapped
  back to a codepoint start. An empty text clears the composition without
  committing anything.
commit: the IME's result string. Replaces the selection exactly once and
  ends the composition. This is the only path that commits composed text.
preedit_cancel: drops the composition without committing; IGNORED when none
  is open.

```c
gates_input_result_t gates_input_preedit(gates_tree_t *tree, gates_str_t text, gates_u32 cursor);
gates_input_result_t gates_input_commit(gates_tree_t *tree, gates_str_t text);
gates_input_result_t gates_input_preedit_cancel(gates_tree_t *tree);
```

The last input-path failure since the previous call (OK when none), then
cleared. Input functions that cannot return an error (keys, pointer) record
failures here: for example an edit, toggle or activation that was skipped
because its notification could not be reserved.

```c
gates_err_t gates_input_take_error(gates_tree_t *tree);
```

Pointer capture was lost (another window took it, or the platform cancelled
the mode): drops the pressed widget and any handle, thumb or selection drag,
so a later release activates nothing.

```c
void gates_input_cancel_pointer(gates_tree_t *tree);
```

True while the focused textbox holds a preedit.

```c
bool gates_input_composing(const gates_tree_t *tree);
```

The focused textbox's caret (inside the preedit while composing) as of the
last paint, in window coordinates; false when there is none. The IME
adapter positions the composition and candidate windows from it.

```c
bool gates_input_caret_rect(const gates_tree_t *tree, gates_rect_t *out);
```

## gates/undo.h

Undo stack (0.6.0): undo and redo for
the program's own data, with labels for menus and a "saved" mark.

The program makes a change, then pushes an entry that can take it back
(undo) and make it again (redo); each has its own data pointer (often the
same one) and an optional drop function that frees the data when the entry
leaves the stack. Entries with the same nonzero merge key, pushed one after
another, merge into one (typing into one field): the first entry's undo
and label with the latest entry's redo; the data between is dropped.
Undoing, redoing, gates_undo_break_merge and gates_undo_mark_clean end a
merge run.
Pushing after an undo drops the entries that could have been redone. At
most `max_entries` are kept; the oldest go first.

The clean mark remembers the state the program saved: gates_undo_is_clean
is true while undo and redo have brought the data back to it, and false for
good once that state can no longer be reached. Undo and redo functions run
on the calling thread and must not call into the same stack; an error from
them leaves the stack where it was.

gates_undo_bind keeps two commands of a tree in step: enabled while there
is something to undo or redo, labelled with the word and the entry's label
("Undo Rename"). Text boxes keep their own undo. Not a node; platform-free.

max_entries 0 -> 100. The allocator: zero -> the proven heap.

```c
[[nodiscard]] gates_err_t gates_undo_create(gates_allocator_t alloc, gates_u32 max_entries, gates_undo_t **out);
```

Drops every entry and unbinds; null is ignored.

```c
void gates_undo_destroy(gates_undo_t *u);
```

After the change was made. INVALID_ARG without undo or redo; on any error
the entry is not kept and its data is dropped (the change stays made).

```c
[[nodiscard]] gates_err_t gates_undo_push(gates_undo_t *u, const gates_undo_entry_t *entry);
```

INVALID_STATE with nothing to undo or redo; BUSY when called from inside an
undo or redo function; otherwise the function's own result.

```c
[[nodiscard]] gates_err_t gates_undo_undo(gates_undo_t *u);
[[nodiscard]] gates_err_t gates_undo_redo(gates_undo_t *u);
bool gates_undo_can_undo(const gates_undo_t *u);
bool gates_undo_can_redo(const gates_undo_t *u);
```

The label of what would be undone or redone next (empty when nothing).

```c
gates_str_t gates_undo_undo_label(const gates_undo_t *u);
gates_str_t gates_undo_redo_label(const gates_undo_t *u);
void gates_undo_break_merge(gates_undo_t *u);
void gates_undo_mark_clean(gates_undo_t *u);
bool gates_undo_is_clean(const gates_undo_t *u);
```

Drops every entry; the present state becomes the clean one.

```c
void gates_undo_clear(gates_undo_t *u);
```

Keeps commands undo_cmd and redo_cmd of `scope` in step from now on
(NOT_FOUND unless both exist). Words are copied ("Undo", "Redo", or the
program's language). A null tree unbinds; unbind (or destroy the stack)
before destroying the tree. The program's commands call gates_undo_undo /
_redo; a scope that goes away just stops the updates.

```c
[[nodiscard]] gates_err_t gates_undo_bind(gates_undo_t *u, gates_tree_t *tree, gates_node_t scope, gates_command_id_t undo_cmd, gates_command_id_t redo_cmd, gates_str_t undo_word, gates_str_t redo_word);
```

## gates/version.h

Version.

Compatibility: source compatible within a minor version (0.2.x); rebuild the
program on every update. No binary ABI is promised yet - public structs may
change size between versions. A program can check at run time that the
library it links is the one its headers describe:
  if (gates_version() != GATES_VERSION_NUMBER) { ... }

The version the library was built as (GATES_VERSION_NUMBER of its headers).

```c
gates_u32 gates_version(void);
```

The same as text, e.g. "0.1.0" (static storage).

```c
const char *gates_version_string(void);
```

## gates/view.h

Virtual views: a list or a table over a model the
application owns, of any size.

A view is one retained node. It never creates a node per row: it asks the
model only for the rows it paints (at most GATES_VIEW_MAX_ROWS at a time)
and keeps the selection as a stable item id, never as a row number, so the
selected item stays selected when rows are inserted, removed or reordered.
Row counts and offsets are 64-bit; the vertical position is kept in rows.

Model rules (UI thread only): callbacks read, they never block, do I/O or
change the model; the model changes only between callbacks, after which
the application calls gates_view_model_changed. A cell's text must stay
valid until the next model callback; the view copies it before asking for
anything else. Callbacks are never made during event dispatch or from inside
another callback, and never after gates_view_set_model(view, null) returns
(which is safe inside a handler).

Interaction: one selected item (0 = none). Up/Down, PageUp/PageDown,
Home/End move the selection and keep it in view; Enter or a double click on
the selected row activates it; Left/Right and Shift+wheel scroll sideways.
A click selects the row under it. Clicking a header cell asks for sorting
(the model sorts); dragging a header cell's right edge resizes the column,
never below its minimum. The view is one Tab stop. Typing selects the next
row whose cell in the current column (0.10.0; else the first shown column)
starts with the letters typed (ASCII letters in
any case; a pause of a second starts over, the same letter again steps
through such rows; each character looks at up to 4096 rows). Ctrl+C copies
the selected row's shown cells, separated by tabs.
Events (a tree adds GATES_EVENT_EXPAND_REQUESTED, see gates_view_desc_t):
GATES_EVENT_SELECTION_CHANGED (ev->item = the selected id; a change
made by gates_view_model_changed because the selected item went away is
announced with origin PROGRAM), GATES_EVENT_ACTIVATED (ev->item = the row's
id), GATES_EVENT_SORT_REQUESTED (ev->result = the column id),
GATES_EVENT_CELL_EDITED (ev->item = the row, ev->result = the column).

Cells (0.6.0): a column shows text, a check box, a progress bar or an
icon and text, or paints itself. An editable column lets a person change
the selected row's cell. A table has a current column (Ctrl+Left/Right, or
the cell pressed; outlined in the selected row): F2 edits it when it is an
editable text column, else the first such column; a double click edits the
text cell under it; a text box opens over the cell with the model's text
selected. Enter commits, Escape cancels, focus leaving commits. Space
toggles the current column when it is an editable check column, else the
first such column; a click on the box toggles it. A commit calls the model's set_cell; an error keeps the editor open and
marks it invalid (when focus left, the edit is dropped instead). After a
commit the view re-reads the model (as gates_view_model_changed) and reports
CELL_EDITED. Scrolling or resizing a column first commits an open edit; the
view does not move while the model refuses it. Platform-free.

### a selection store (0.10.0)

Rows kept as sorted, separate ranges, for a multi-select view's program:
answer the model's next_selected with gates_selection_next and hand every
SELECT_REQUESTED to gates_view_apply_selection. Rows are model positions,
so tell the store when rows come or go (rows_inserted / rows_removed).
A program may keep its own selection instead; the view never reads this.

`alloc` {0} = the heap. Every change below either happens whole or, on
NOMEM, leaves the selection as it was.

```c
[[nodiscard]] gates_err_t gates_selection_create(gates_allocator_t alloc, gates_selection_t **out);
void gates_selection_destroy(gates_selection_t *sel);
void gates_selection_clear(gates_selection_t *sel);
```

Rows lo..hi (inclusive; INVALID_ARG when lo > hi or hi is GATES_ROW_NONE).

```c
[[nodiscard]] gates_err_t gates_selection_add(gates_selection_t *sel, gates_u64 lo, gates_u64 hi);
[[nodiscard]] gates_err_t gates_selection_remove(gates_selection_t *sel, gates_u64 lo, gates_u64 hi);
[[nodiscard]] gates_err_t gates_selection_toggle(gates_selection_t *sel, gates_u64 row);
bool gates_selection_contains(const gates_selection_t *sel, gates_u64 row);
```

The first selected row at or after `row`, or GATES_ROW_NONE: what the
model's next_selected answers.

```c
gates_u64 gates_selection_next(const gates_selection_t *sel, gates_u64 row);
```

Selected rows in all, and the ranges one by one (in row order).

```c
gates_u64 gates_selection_count(const gates_selection_t *sel);
gates_u32 gates_selection_range_count(const gates_selection_t *sel);
bool gates_selection_range(const gates_selection_t *sel, gates_u32 index, gates_u64 *lo, gates_u64 *hi);
```

A request in rows: ONE and RANGE replace the selection, TOGGLE flips the
target, ADD_RANGE adds anchor..target, ALL selects rows 0..row_count - 1.
An anchor at or past row_count (GATES_ROW_NONE: none) makes a range the
target alone; a target past the end is OUT_OF_BOUNDS.

```c
[[nodiscard]] gates_err_t gates_selection_apply(gates_selection_t *sel, gates_select_request_t what, gates_u64 target, gates_u64 anchor, gates_u64 row_count);
```

`count` rows were inserted before row `at`, or rows at..at + count - 1
removed: later rows move. Inserted rows are not selected (a range they land
in is split); ranges that meet once rows are removed join.

```c
[[nodiscard]] gates_err_t gates_selection_rows_inserted(gates_selection_t *sel, gates_u64 at, gates_u64 count);
[[nodiscard]] gates_err_t gates_selection_rows_removed(gates_selection_t *sel, gates_u64 at, gates_u64 count);
```

Applies a view's SELECT_REQUESTED event (its ids turned into rows through
the view's model) and calls gates_view_model_changed. INVALID_ARG for
another event; NOT_FOUND when the view or the target row is gone (nothing
changes).

```c
[[nodiscard]] gates_err_t gates_view_apply_selection(gates_tree_t *tree, const gates_event_t *ev, gates_selection_t *sel);
```

INVALID_ARG for bad columns (id 0, duplicates); nothing is left on failure.
A multi-select view refuses a model without next_selected (set_model).

```c
[[nodiscard]] gates_err_t gates_view_create(gates_tree_t *tree, gates_node_t parent, const gates_view_desc_t *desc, gates_node_t *out_view);
```

Binds (copies the struct; `user` and callbacks are borrowed) or, with null,
detaches. Binding resets scrolling and selection.

```c
[[nodiscard]] gates_err_t gates_view_set_model(gates_tree_t *tree, gates_node_t view, const gates_rows_model_t *model);
```

After the model changed: keeps the selected item if it is still there,
otherwise selects the item now nearest to its old row (or none), clamps the
scroll position and repaints.

```c
[[nodiscard]] gates_err_t gates_view_model_changed(gates_tree_t *tree, gates_node_t view);
```

The selected row's id, or 0 (multi-select: the focus row).

```c
gates_item_id_t gates_view_selected(const gates_tree_t *tree, gates_node_t view);
```

Silent. 0 clears; an id the model does not have is INVALID_ARG.

```c
[[nodiscard]] gates_err_t gates_view_set_selected(gates_tree_t *tree, gates_node_t view, gates_item_id_t id);
```

Scrolls so the item's row is visible (INVALID_ARG when it is not in the model).

```c
[[nodiscard]] gates_err_t gates_view_scroll_to(gates_tree_t *tree, gates_node_t view, gates_item_id_t id);
```

The first row shown, and how many whole rows fit (valid after layout).

```c
gates_u64 gates_view_first_row(const gates_tree_t *tree, gates_node_t view);
gates_u32 gates_view_visible_rows(const gates_tree_t *tree, gates_node_t view);
```

Horizontal scroll position in px.

```c
gates_i32 gates_view_scroll_x(const gates_tree_t *tree, gates_node_t view);
gates_i32 gates_view_column_width(const gates_tree_t *tree, gates_node_t view, gates_column_id_t column);
[[nodiscard]] gates_err_t gates_view_set_column_width(gates_tree_t *tree, gates_node_t view, gates_column_id_t column, gates_i32 width);
```

### choosing and ordering columns (0.6.0)

A hidden column takes no space, is not painted or asked for, and is left
out of assistive technology's columns; hiding the column being edited
cancels the edit. INVALID_STATE when the call would hide the last shown
column. Positions count every column, hidden or not, from 0. Widths, order
and hidden marks are saved and loaded by gates_state (gates/state.h).

```c
[[nodiscard]] gates_err_t gates_view_set_column_hidden(gates_tree_t *tree, gates_node_t view, gates_column_id_t column, bool hidden);
bool gates_view_column_hidden(const gates_tree_t *tree, gates_node_t view, gates_column_id_t column);
```

Moves the column to `position` (the others keep their order).

```c
[[nodiscard]] gates_err_t gates_view_move_column(gates_tree_t *tree, gates_node_t view, gates_column_id_t column, gates_u32 position);
```

The column at `position`, or 0 past the end.

```c
gates_column_id_t gates_view_column_at(const gates_tree_t *tree, gates_node_t view, gates_u32 position);
```

Opens the header menu at `at` (window coordinates), as a right press on the
header would; INVALID_ARG without column_menu. For a "Columns" command.

```c
[[nodiscard]] gates_err_t gates_view_open_column_menu(gates_tree_t *tree, gates_node_t view, gates_point_t at, gates_node_t *out_menu);
```

### editing cells (0.6.0)

gates_view_edit opens the editor on a cell as F2 would (selecting the row,
scrolling it into view, focusing the editor): INVALID_ARG when the column is
not an editable text or icon column, the model has no set_cell or the item
is not in the model; an open edit is committed first (its error is
returned and nothing else happens). gates_view_end_edit commits (true) or
cancels (false) an open edit and gives focus back to the view when the
editor had it; a refused commit returns the model's error and keeps the
editor open. OK when nothing was open.

```c
[[nodiscard]] gates_err_t gates_view_edit(gates_tree_t *tree, gates_node_t view, gates_item_id_t id, gates_column_id_t column);
[[nodiscard]] gates_err_t gates_view_end_edit(gates_tree_t *tree, gates_node_t view, bool commit);
```

true while an edit is open; the row and column through the pointers (may be null).

```c
bool gates_view_editing(const gates_tree_t *tree, gates_node_t view, gates_item_id_t *id, gates_column_id_t *column);
```

The editor text box (GATES_NODE_NULL for a view without editable text
columns): for its text, a maximum length or validation while it is open.

```c
gates_node_t gates_view_editor(const gates_tree_t *tree, gates_node_t view);
```

Where parts of the view are now (window coordinates, valid after layout;
empty when the part is not shown): ROW = row `index` if it is visible,
HEADER = the header cell of the column at position `index`, BODY = the rows
area, VTHUMB / HTHUMB = the scrollbar thumbs. For hit tests and tooltips.

```c
gates_rect_t gates_view_part_rect(const gates_tree_t *tree, gates_node_t view, gates_view_part_t part, gates_u64 index);
```

### log view

A view whose model is a bounded ring owned by gates: lines are appended on
the UI thread and copied; when a limit is passed the oldest lines are
dropped and counted. Line ids grow from 1 and never repeat, so a selected
line stays selected until it is dropped (then the oldest remaining line is
selected, announced with origin PROGRAM). While the view shows the last
line it follows new lines; scrolling up (wheel, keys, thumb) stops
following; reaching the end again (End, wheel, thumb) resumes it, and
both report GATES_EVENT_FOLLOW_CHANGED (the program's set_following is
silent). The
worker-thread producer uses gates/post.h through the same
append path. gates_view_set_model is refused on a log view.

A read-only view of lines that follows the end until the person scrolls away.

```c
[[nodiscard]] gates_err_t gates_log_create(gates_tree_t *tree, gates_node_t parent, const gates_log_desc_t *desc, gates_node_t *out_log);
```

Appends one line (copied; CR, LF and TAB become spaces). OUT_OF_BOUNDS for
a line longer than max_bytes; on failure nothing changes.

```c
[[nodiscard]] gates_err_t gates_log_append(gates_tree_t *tree, gates_node_t log, gates_str_t line);
void gates_log_clear(gates_tree_t *tree, gates_node_t log);
gates_u64 gates_log_count(const gates_tree_t *tree, gates_node_t log);
```

Lines dropped to stay within the limits since creation or the last clear.

```c
gates_u64 gates_log_dropped(const gates_tree_t *tree, gates_node_t log);
bool gates_log_following(const gates_tree_t *tree, gates_node_t log);
```

A kept line's text (borrowed until the next append or clear), empty when the
id was dropped or never existed. For showing or copying the selected line.

```c
gates_str_t gates_log_line(const gates_tree_t *tree, gates_node_t log, gates_item_id_t id);
```

true jumps to the end and follows; false stops following.

```c
[[nodiscard]] gates_err_t gates_log_set_following(gates_tree_t *tree, gates_node_t log, bool follow);
```

## gates/widget.h

Primitive widgets:
panel, label, button, checkbox. Widgets are tree nodes with semantic
state; they emit theme-token draw commands during the paint walk and
receive interaction through the pointer routing (gates/hit via window).
Platform-free.

Creation: parent may be GATES_NODE_NULL (attach later). Text is copied.

```c
[[nodiscard]] gates_err_t gates_panel_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_label_create(gates_tree_t *tree, gates_node_t parent, gates_str_t text, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_button_create(gates_tree_t *tree, gates_node_t parent, gates_str_t text, gates_click_fn on_click, void *user, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_checkbox_create(gates_tree_t *tree, gates_node_t parent, gates_str_t text, bool checked, gates_toggle_fn on_toggle, void *user, gates_node_t *out_node);
```

Single-line textbox. `cols` is the intrinsic width in average character widths (0 -> 16).
Editing state lives in a gates_text_edit_t owned by the widget.

```c
[[nodiscard]] gates_err_t gates_textbox_create(gates_tree_t *tree, gates_node_t parent, gates_str_t text, gates_u32 cols, gates_node_t *out_node);
```

Replaces the whole text (copied) and puts the caret at the end. Returns
PROVEN_ERR_BUSY while an IME composition is open in this box (commit or
cancel it first); on failure the old text is kept. gates_widget_set_text
on a textbox does the same.

```c
[[nodiscard]] gates_err_t gates_textbox_set_text(gates_tree_t *tree, gates_node_t node, gates_str_t text);
```

Borrowed committed text (invalid after the next edit).

```c
gates_str_t gates_textbox_text(const gates_tree_t *tree, gates_node_t node);
```

Copies the text into buf. \*needed always receives the text's size; with
buf == null only the size is reported (OK), a too small cap returns
OVERFLOW and copies nothing. Works for password boxes (explicit getter).

```c
[[nodiscard]] gates_err_t gates_textbox_copy_text(const gates_tree_t *tree, gates_node_t node, gates_u8 *buf, gates_usize_t cap, gates_usize_t *needed);
```

Selection in UTF-8 byte offsets: text between anchor and caret. Offsets
inside a multibyte sequence or past the end are rejected (INVALID_ARG),
never clamped. BUSY while a composition is open. Editing is per codepoint,
not per grapheme cluster.

```c
[[nodiscard]] gates_err_t gates_textbox_selection(const gates_tree_t *tree, gates_node_t node, gates_u32 *anchor, gates_u32 *caret);
[[nodiscard]] gates_err_t gates_textbox_set_selection(gates_tree_t *tree, gates_node_t node, gates_u32 anchor, gates_u32 caret);
```

Replaces the selection with text (copied): silent (no event), one undo unit,
BUSY while composing, OUT_OF_BOUNDS over the maximum length.

```c
[[nodiscard]] gates_err_t gates_textbox_replace_selection(gates_tree_t *tree, gates_node_t node, gates_str_t text);
```

Read-only: focus, selection and copy work; editing, paste, cut, undo and IME
composition are refused.

```c
[[nodiscard]] gates_err_t gates_textbox_set_read_only(gates_tree_t *tree, gates_node_t node, bool read_only);
bool gates_textbox_read_only(const gates_tree_t *tree, gates_node_t node);
```

Maximum length in UTF-8 bytes (0 = unlimited). OUT_OF_BOUNDS when the
current text is already longer. User edits past it are refused whole and
reported with GATES_EVENT_LIMIT_EXCEEDED.

```c
[[nodiscard]] gates_err_t gates_textbox_set_max_bytes(gates_tree_t *tree, gates_node_t node, gates_u32 max_bytes);
gates_u32 gates_textbox_max_bytes(const gates_tree_t *tree, gates_node_t node);
```

Password: one '\*' per codepoint, no copy or cut, events carry no text, no
undo history, no IME. The widget does not promise secure erasure of copies
held by the process or the operating system.

```c
[[nodiscard]] gates_err_t gates_textbox_set_password(gates_tree_t *tree, gates_node_t node, bool password);
bool gates_textbox_password(const gates_tree_t *tree, gates_node_t node);
```

Undo history bounds (defaults 64 entries, 16384 bytes of edit content;
allocation overhead is not counted). Oldest entries go first; when memory is
short the oldest are dropped and the edit still happens. set_text clears the
history. A read-only box keeps its history but reports nothing to undo or
redo until it is editable again.

```c
[[nodiscard]] gates_err_t gates_textbox_set_undo_limits(gates_tree_t *tree, gates_node_t node, gates_u32 max_entries, gates_u32 max_bytes);
bool gates_textbox_can_undo(const gates_tree_t *tree, gates_node_t node);
bool gates_textbox_can_redo(const gates_tree_t *tree, gates_node_t node);
```

The offer kept after GATES_EVENT_LIMIT_EXCEEDED. accept_fit inserts the part
that fits (codepoint boundary) where the refused edit would have gone, as a
user edit with its own event and undo unit; INVALID_STATE when there is no
offer or the text changed since. discard drops the offer.

```c
[[nodiscard]] gates_err_t gates_textbox_accept_fit(gates_tree_t *tree, gates_node_t node);
void gates_textbox_discard_rejected(gates_tree_t *tree, gates_node_t node);
```

DEPRECATED: the mutable edit core, for the IME adapter's
history and advanced use. Changes made through it bypass events, limits and
undo; call gates_textbox_edit_commit afterwards. New code uses the safe
operations above. Null for non-textboxes.

```c
gates_text_edit_t *gates_textbox_edit(gates_tree_t *tree, gates_node_t node);
```

Re-validates caret and selection after raw edits, clears the undo history
and pending offer, bumps the revision and repaints. Cannot repair text that
was made invalid UTF-8.

```c
void gates_textbox_edit_commit(gates_tree_t *tree, gates_node_t node);
```

Error state (validation display): the border is
drawn with GATES_COLOR_ERROR (a focused box also shows its focus ring inside
it). The text is untouched; validating is the application's job.

```c
[[nodiscard]] gates_err_t gates_textbox_set_invalid(gates_tree_t *tree, gates_node_t node, bool invalid);
bool gates_textbox_invalid(const gates_tree_t *tree, gates_node_t node);
```

### options: radio group and choice

Both are one node holding a list of options with stable ids; the
application reads the selected id, never a row number. Ids are nonzero and
unique within the node; labels are copied. 0 means "nothing selected".
A person's change queues GATES_EVENT_VALUE_CHANGED with ev->result = the
newly selected id (the change is not made when the event cannot be queued);
the setters below are silent, and every selection change bumps the revision.
The node is a single Tab stop, focusable while it has an enabled option.

Radio group: every option is a row. Up/Left and Down/Right move to the
previous/next enabled option and select it (wrapping), Home/End go to the
first/last, Space selects the first enabled option when none is selected; a
click on a row (press and release on the same row) selects it.
Choice: shows the selected option's label; Space, Enter, Alt+Down or a click
open its option list, an overlay with the menu's rules (Up/Down, Enter or
Space chooses, Escape or an outside click only closes). The list also
closes when the window loses focus, or the choice is disabled, hidden, given
new options or destroyed.

selected_id: 0 or one of the ids. INVALID_ARG for a bad list (id 0,
duplicates) or an unknown selected_id; nothing is left behind on failure.

```c
[[nodiscard]] gates_err_t gates_radio_create(gates_tree_t *tree, gates_node_t parent, const gates_option_t *options, gates_u32 count, gates_u32 selected_id, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_choice_create(gates_tree_t *tree, gates_node_t parent, const gates_option_t *options, gates_u32 count, gates_u32 selected_id, gates_node_t *out_node);
```

Replaces the options (count may be 0). The selection stays when its id is
still there, otherwise it is cleared, silently. On failure the old list stays.

```c
[[nodiscard]] gates_err_t gates_options_set(gates_tree_t *tree, gates_node_t node, const gates_option_t *options, gates_u32 count);
```

Silent. 0 clears; an id that is not an option is INVALID_ARG. A disabled
option can be selected by the program.

```c
[[nodiscard]] gates_err_t gates_options_set_selected(gates_tree_t *tree, gates_node_t node, gates_u32 id);
gates_u32 gates_options_selected(const gates_tree_t *tree, gates_node_t node);
[[nodiscard]] gates_err_t gates_options_set_enabled(gates_tree_t *tree, gates_node_t node, gates_u32 id, bool enabled);
gates_u32 gates_options_count(const gates_tree_t *tree, gates_node_t node);
```

The choice's open option list (an overlay node), or GATES_NODE_NULL.

```c
gates_node_t gates_choice_list(const gates_tree_t *tree, gates_node_t choice);
bool gates_choice_list_open(const gates_tree_t *tree, gates_node_t choice);
```

### separator and progress

A one-pixel line (CONTROL_BORDER) with space around it: horizontal in a
column, vertical in a row. Not focusable.

```c
[[nodiscard]] gates_err_t gates_separator_create(gates_tree_t *tree, gates_node_t parent, gates_node_t *out_node);
```

A track and a fill for a value in per-mille (clamped to 0..1000). Not
focusable; put a label beside it for the words.

```c
[[nodiscard]] gates_err_t gates_progress_create(gates_tree_t *tree, gates_node_t parent, gates_i32 permille, gates_node_t *out_node);
[[nodiscard]] gates_err_t gates_progress_set_value(gates_tree_t *tree, gates_node_t node, gates_i32 permille);
gates_i32 gates_progress_value(const gates_tree_t *tree, gates_node_t node);
```

### group box (0.4.0)

A titled frame around a column panel (\*out_content) for related controls.
The title takes mnemonic markup: Alt+x focuses the first control inside, or
for a collapsible group toggles it. A collapsible group's title is a Tab stop
with an open/closed mark; Space, Enter or a click shows or hides the content,
and a person's toggle queues VALUE_CHANGED on the group (ev->checked =
expanded). gates_group_set_expanded is silent.

```c
[[nodiscard]] gates_err_t gates_group_create(gates_tree_t *tree, gates_node_t parent, gates_str_t title, bool collapsible, gates_node_t *out_group, gates_node_t *out_content);
[[nodiscard]] gates_err_t gates_group_set_expanded(gates_tree_t *tree, gates_node_t group, bool expanded);
bool gates_group_expanded(const gates_tree_t *tree, gates_node_t group);
```

Properties (setters mark the node layout/paint dirty as appropriate).

```c
[[nodiscard]] gates_err_t gates_widget_set_text(gates_tree_t *tree, gates_node_t node, gates_str_t text);
gates_str_t gates_widget_text(const gates_tree_t *tree, gates_node_t node);
```

A disabled widget is drawn dimmed and takes no input or focus.

```c
[[nodiscard]] gates_err_t gates_widget_set_disabled(gates_tree_t *tree, gates_node_t node, bool disabled);
bool gates_widget_disabled(const gates_tree_t *tree, gates_node_t node);
```

Silent: no on_toggle for the program's own change.

```c
[[nodiscard]] gates_err_t gates_checkbox_set_checked(gates_tree_t *tree, gates_node_t node, bool checked);
bool gates_checkbox_checked(const gates_tree_t *tree, gates_node_t node);
```

Keyboard focus: buttons, checkboxes and textboxes are focusable
while enabled and reachable (not on an inactive stack page, inside the
current focus scope). false takes a control out of the Tab order. Clicking a
button or checkbox focuses it.

```c
[[nodiscard]] gates_err_t gates_widget_set_focusable(gates_tree_t *tree, gates_node_t node, bool focusable);
bool gates_widget_focusable(const gates_tree_t *tree, gates_node_t node);
```

Interaction state (driven by pointer routing; read-only for apps).

```c
bool gates_widget_hovered(const gates_tree_t *tree, gates_node_t node);
bool gates_widget_pressed(const gates_tree_t *tree, gates_node_t node);
```

## gates/window.h

Window/surface boundary.
Opaque, platform-free header. A window owns its retained tree; the tree lays
out and paints its widgets, and the paint callback may add draw commands.

A top-level window with its tree; shown when gates_app_run starts (or at
once while it runs). Destroying it cancels its running jobs and waits.

```c
[[nodiscard]] gates_err_t gates_window_create(gates_app_t *app, const gates_window_desc_t *desc, const gates_window_callbacks_t *callbacks, gates_window_t **out_window);
void gates_window_destroy(gates_window_t *win);
```

The window's retained node tree (owned by the window).

```c
gates_tree_t *gates_window_tree(gates_window_t *win);
```

Theme. SYSTEM (the default) follows the desktop: high contrast
when it is on, else dark or light as the Windows app mode says, switching
live when the user changes it; the title bar follows too.

```c
void gates_window_set_theme_mode(gates_window_t *win, gates_theme_mode_t mode);
gates_theme_mode_t gates_window_theme_mode(const gates_window_t *win);
```

An application theme (copied); it stays until set_theme_mode is called.

```c
[[nodiscard]] gates_err_t gates_window_set_theme(gates_window_t *win, const gates_theme_t *theme);
```

The theme in use now.

```c
const gates_theme_t *gates_window_theme(const gates_window_t *win);
```

Logical units: the window scales its drawing to the monitor's DPI
and converts pointer positions back, so the tree never sees device pixels.
GATES_FORCE_DPI=&lt;dpi> in the environment forces a DPI (diagnostics).

```c
gates_size_t gates_window_client_size(const gates_window_t *win);
void gates_window_request_repaint(gates_window_t *win);
```

Accessibility. The window is a UI Automation provider: screen
readers and automation tools see the tree as access.h describes it.
Zoom (percent, 25..400, 100 = none) scales the whole interface on top of the
monitor's DPI and the Windows "Text size" setting, which it also follows.

```c
void gates_window_set_zoom(gates_window_t *win, gates_u32 percent);
gates_u32 gates_window_zoom(const gates_window_t *win);
```

The user turned animation effects off: show end states, skip motion.

```c
bool gates_window_reduced_motion(const gates_window_t *win);
```

Spoken by screen readers (a UI Automation notification); assertive
interrupts what is being read. The text is copied.

```c
[[nodiscard]] gates_err_t gates_window_announce(gates_window_t *win, gates_str_t text, bool assertive);
```

Placement (0.3.0): where the window is, as text to keep in a
file - "x,y,w,h,state" with the normal (restored) rectangle in screen pixels
and state "normal" or "maximized". \*needed always receives its length; with
a buffer too small nothing is written (OVERFLOW). set_placement takes that
text back; a rectangle that no longer meets any monitor moves onto the
nearest one; malformed text is INVALID_ARG and changes nothing.

```c
[[nodiscard]] gates_err_t gates_window_placement(const gates_window_t *win, gates_u8 *buf, gates_usize_t cap, gates_usize_t *needed);
[[nodiscard]] gates_err_t gates_window_set_placement(gates_window_t *win, gates_str_t text);
```

Native dialogs (0.5.0): the platform's own modal dialogs. The call
returns when the person answers; the window's menus close first. A cancel is
GATES_OK with \*len = 0 (or chosen = false). Paths are UTF-8; \*len receives the
path's length, and a too small cap returns OVERFLOW and writes nothing.
filters: "Text files|\*.txt|All files|\*.\*" (name|patterns pairs, patterns
separated by ';'). folder: where it starts (empty: the platform's choice);
name: a save dialog's suggested file name. A save dialog asks before
overwriting. Run them from a command or event handler, never while painting.

```c
[[nodiscard]] gates_err_t gates_window_open_file(gates_window_t *win, const gates_file_dialog_t *desc, gates_u8 *buf, gates_usize_t cap, gates_usize_t *len);
```

Several files at once (0.8.0): each path is followed by a NUL byte in buf,
\*len receives all of their bytes (OVERFLOW writes nothing), \*count how many
paths; a cancel gives \*len = 0 and \*count = 0.

```c
[[nodiscard]] gates_err_t gates_window_open_files(gates_window_t *win, const gates_file_dialog_t *desc, gates_u8 *buf, gates_usize_t cap, gates_usize_t *len, gates_u32 *count);
[[nodiscard]] gates_err_t gates_window_save_file(gates_window_t *win, const gates_file_dialog_t *desc, gates_u8 *buf, gates_usize_t cap, gates_usize_t *len);
[[nodiscard]] gates_err_t gates_window_choose_folder(gates_window_t *win, const gates_file_dialog_t *desc, gates_u8 *buf, gates_usize_t cap, gates_usize_t *len);
```

\*color is the starting colour and, when chosen, the answer (alpha kept).

```c
[[nodiscard]] gates_err_t gates_window_choose_color(gates_window_t *win, gates_color_t *color, bool *chosen);
```

A message box, modal to the window: the button pressed, or
GATES_ANSWER_NONE when it could not be shown.

```c
gates_answer_t gates_window_message(gates_window_t *win, gates_str_t title, gates_str_t text, gates_message_buttons_t buttons, gates_message_icon_t icon);
```
