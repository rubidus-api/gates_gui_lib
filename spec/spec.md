# gates Specification (v0.14.0)

This document states what gates guarantees: the contract a program may rely on. The manual
(`manual/`) teaches how to use gates; the public headers (`include/gates/*.h`) give every
function's exact terms. When this document and a header disagree, one of them has a bug:
please report it. Words: **must** is a requirement on the program (or a backend), **does** and
**is** describe what gates guarantees, **may** is a freedom.

Profile: "Windows Tool UI 1" - C23, Windows 10/11 through Win32, one window per top-level
surface, software rendering. The core is platform-free and builds on any C23 host.

## 1. What gates is

gates is an interaction layer, not a picture. A program declares what it needs from a person
as semantic nodes - labelled fields, buttons, lists, commands - and gates carries that to the
person through the platform (pointer, keyboard, input methods, screen readers) and returns
the person's answers as events. Meaning and behaviour are the contract; appearance belongs to
the backend and the theme, as in HTML and CSS.

Consequences a program can rely on:

- A control is complete when it works for everyone: reachable and operable by pointer and by
  keyboard, usable with the installed input method where it takes text, described to
  assistive technology, and correct in its states and failures.
- Programs never place controls by coordinates in ordinary layouts and never choose colours
  for ordinary controls; they choose layouts and theme tokens.
- Pixel-exact output is guaranteed only for the reference text backend and the software
  renderer (both deterministic, used by tests).

## 2. The tree

- **Nodes and handles.** Every control is one node of a tree (`gates/tree.h`). A handle
  (`gates_node_t`) is an index and a generation; a handle to a destroyed node is stale and
  every function refuses it (INVALID_ARG) instead of reaching another node. Compare handles
  with `gates_node_eq`. `GATES_NODE_NULL` is no node.
- **Ownership.** A window owns one tree; a tree may also exist without a window (tests,
  headless programs). The tree owns its nodes, their state and their text (copied in). Strings
  passed in are borrowed for the call only unless a header says otherwise.
- **Destroying.** Destroying a node marks it and its subtree at once - they are invalid as
  targets immediately - and frees them at the next safe point
  (`gates_tree_flush_destroys`: after event dispatch, before layout, at frame end).
- **Threads.** A tree, its nodes and everything reached through them belong to the UI thread.
  Other threads talk to it only by posting (section 10).
- **Registered callbacks and `user` pointers** (handlers, models, command functions) are
  borrowed while registered: the program keeps them alive or unregisters first.

## 3. Errors

- Functions that can fail return `gates_err_t` and are `[[nodiscard]]`. `GATES_OK` means the
  whole change happened; any other value means none of it did (failure-atomic): a node that
  could not be created leaves nothing behind, a text that could not be set leaves the old one.
- Codes (from proven): INVALID_ARG (a bad or stale argument), INVALID_STATE (not now: a
  disabled control, a closed dialog), NOMEM, OUT_OF_BOUNDS (a limit), OVERFLOW (a buffer too
  small; nothing written, the needed length reported), UNSUPPORTED, BUSY.
- Out of memory is an ordinary answer, never a crash, and never leaves a tree inconsistent.
- Input functions that cannot return an error (keys, pointer) record their failure - an edit,
  toggle or activation skipped because its notification could not be reserved - for
  `gates_input_take_error`; a window's `on_input_error` callback receives it after each input
  turn when the program sets one.

## 4. Units, scaling and themes

- All sizes and positions in the API are logical units (1/96 inch). The window scales at its
  boundary to the monitor's DPI, a program zoom (`gates_window_set_zoom`) and the Windows "Text
  size" setting; the core never sees device pixels.
- An image's natural size is its pixel size in logical units. An image may hold more pixel sets
  of the same picture (0.10.0); the renderer draws the smallest set that covers the device
  pixels, else the largest.
- Colours come from semantic tokens (`gates/theme.h`) through the active theme: light, dark or
  high contrast, following the system by default. Theme text reaches 4.5:1 contrast on its
  background; borders and cues 3:1; focus and error cues are at least 2 units thick.
- `gates_window_reduced_motion` says whether the person turned animation effects off; a program
  that animates must then show the end state.

## 5. Layout

- Layout is measure (bottom-up, preferred sizes) then arrange (top-down rects), run by the
  window before painting (`gates_layout_run`).
- Kinds (`gates/layout.h`): absolute, row, column, stack (one page shown), split (two panes and
  a handle), scroll (a viewport with a scroll bar; sideways too when the program turns it on,
  0.10.0), form (label column and editors; stacks
  labels above editors when narrow), grid (columns with grow weights and spans; rows with grow
  weights) and wrap (lines of children at their own size).
- **Invariant:** after arrange, the layout rects of siblings in normal flow never overlap. The
  only overlaps are the stack's pages (one shown), scrolled content inside its viewport, and
  overlays above the content. `gates_layout_validate` checks it.
- A hidden node (`gates_node_set_hidden`) and its subtree take no space and are not painted,
  hit or focused; assistive technology sees them as offscreen.
- Containers whose height depends on their width (wrap, a form that stacks) get a second
  arrange pass when that width changes, so the measured size matches the arranged one.

## 6. Input, focus and events

- **Pointer.** One pointer model for mouse, pen and touch (`gates/input.h`); events carry the
  position, buttons, click count (1, 2 or 3) and modifier keys. A press goes to the deepest hit
  node; a drag on a handle, thumb or selection owns the pointer until release or
  `gates_input_cancel_pointer`. The pointer's shape tells what a press does (`gates_cursor_at`,
  0.13.0): resize over split handles and column edges, the text beam over text, kept for a drag.
- **Keyboard.** Keys go to the focused node, then to commands (shortcuts), then to the window.
  Tab and Shift+Tab move focus in document order within the focus scope (a modal dialog, else
  the window); Space and Enter activate; Escape cancels. The keyboard is never trapped: a
  control that uses Tab for itself (an editor that types tabs) leaves with Ctrl+Tab.
- **Text and input methods.** Characters arrive as code points; compositions as preedit text
  with a cursor (`gates_input_preedit`, `_commit`, `_preedit_cancel`), drawn at the caret and
  committed as one edit. A composition never follows focus to another node.
- **Events.** A person's change queues a typed event (`gates/event.h`) after the change
  succeeded; the window delivers the queue at a safe point after the input message
  (`gates_tree_dispatch_events`). Nothing is delivered in the middle of an input operation, so a
  handler may change or destroy anything, its source included; events for nodes that are gone
  are skipped. Changes a handler makes queue further events for the next dispatch, never a
  recursive one. Changes the program makes itself are silent (`gates_widget_notify` announces
  one). Events whose state can only be read at delivery (text, selection, value, follow) are
  coalesced and carry the latest state.
- **Commands** (`gates/command.h`). An action with a label, a shortcut and enabled/checked
  state, in a scope (the window, or a dialog). Buttons, menus, toolbars and shortcuts invoke it;
  an invocation is queued, looked up again at delivery and runs only if still enabled, at most
  once.
- **Overlays** (`gates/overlay.h`). Modal dialogs and context menus are overlays above the
  content, hit first. A menu closes on Escape, an outside click (which goes no further), focus
  loss or its scope's destruction. A dialog blocks input below it and is the focus scope. Each
  reports its end once; there is no nested event loop. At most 8 are open at a time.
- **Mnemonics** (`gates/frame.h`). "&x" in a control's text makes Alt+x reach it; menu bar
  titles come first; shared letters cycle focus without activating.

## 7. Controls

The set is finite; manual chapter 2 lists every control with what a person does with it, its
keys, its events and its accessibility role. Each control is one node, including views of any
size and multi-line editors; controls that draw their own parts (radio options, menu entries,
view rows, tabs, toolbar buttons) expose those parts as virtual items (section 11), never as a
node per part.

## 8. Text

- **Encoding.** All text is UTF-8 at the API; gates validates what it receives and refuses
  invalid UTF-8 where a header says so. Offsets are UTF-8 bytes; edits and carets stay on code
  point boundaries.
- **Other encodings** (`gates/encoding.h`, 0.10.0). Text from or to the outside is converted at
  the edge: UTF-8, UTF-16 and UTF-32 in both byte orders on every platform, Windows code pages
  (949, 51949, ... and the system's) through the platform's converter (Win32: installed with the
  app; elsewhere UNSUPPORTED unless a program installs one). Malformed or unmappable text is
  replaced (U+FFFD, or the code page's `?`) or, in strict mode, refused with its byte offset;
  nothing is allocated on an error. The UTF forms also convert into a caller's buffer and in
  pieces without allocating; Win32 programs get UTF-8 arguments and console output. A built-in
  converter for 949 and 51949, from tables measured on Windows 11, serves targets without an OS
  (0.11.0); it refuses Windows' one-way quirks, and 51949's B4D3 follows KS X 1001.
- **Metrics contract** (`gates/text.h`). A text backend reports per font the ascent, descent,
  line height, an average width (a sizing hint) and every code point's advance. A string's width
  is exactly the sum of its advances - no kerning, ligatures or shaping - so the core computes
  carets, selections and hit tests itself, identically on every backend. Rendering inside the
  assigned rect is the backend's.
- **Fonts.** Per node: UI (proportional, the default), MONO (fixed pitch), or a named face
  (`gates_font_named`, 0.12.0; drawn in the UI face where the system lacks it), inherited by
  descendants - set on the root, the whole window's; and a size in percent of the system's text size (50 to 400, 0.10.0),
  inherited separately. A font value carries both (`gates_font_face`, `gates_font_percent`).
- **Text box.** One line; selection, clipboard, undo (bounded: 64 entries and 16384 bytes by
  default), read-only, password (no copy, no undo, no input method, events carry no text), and a
  maximum length that asks: an edit past it is refused and kept as an offer
  (LIMIT_EXCEEDED) for the program to accept in part or discard.
- **Editor** (`gates/editor.h`). Many lines over a text buffer (`gates/text_buffer.h`, at most
  2^30 bytes); it lays out and paints only the lines it shows, so its cost follows the view,
  not the length. Soft wrap, a line number gutter, highlighting through the program's styler
  (asked only for stale lines about to show), marks, find, indenting, its own undo history.
  TEXT_CHANGED carries no text.
- Unicode limits in this profile: no kerning, ligatures or shaping (scripts that need shaping
  show their characters one by one), right-to-left text is not reordered, and editing moves by
  code point, not by grapheme cluster.

## 9. Views over the program's data

- A list, table, tree or log is one node over a model the program owns (`gates/view.h`). The
  view asks the model only for the rows it paints - at most 256 at a time - so a model of any
  size (64-bit row counts) costs what the window costs. The model's callbacks read, never block
  and never change the model; after the model changes the program calls
  `gates_view_model_changed`.
- The selection is an item id, never a row number: it survives inserts, removals and sorting.
  When the selected item goes away the view announces the new selection with origin PROGRAM.
- A multi-select view leaves the selection to the model: it asks `next_selected(row)` only for
  rows it paints, describes or copies, and turns every gesture into a request
  (SELECT_REQUESTED: ONE, TOGGLE, RANGE, ADD_RANGE, ALL) that the program applies. Nothing gates
  holds or walks grows with the number of rows or of selected rows; copying stops at 10000 rows
  and asks the program (COPY_REQUESTED). Dragging over rows asks for anchor..row once per new row
  (0.10.0). A selection store (`gates_selection_t`, 0.10.0) keeps rows as sorted ranges for a
  program that wants one; its size grows with the number of ranges, not of rows.
- Sorting and opening tree rows are requests (SORT_REQUESTED, EXPAND_REQUESTED): the program
  reorders or changes its rows. gates never walks rows it does not show.
- Editable cells change through the model's `set_cell`, the one callback that may change the
  model; a refusal keeps the editor open and invalid.
- A log owns its lines within a line and byte limit (defaults 1000 lines, 1 MiB), dropping and
  counting the oldest; it follows the end until the person scrolls away and reports
  FOLLOW_CHANGED when that changes.

## 10. Threads, posting, timers and tasks

- **Posting** (`gates/post.h`). A worker holds a sender and posts messages (a target node, a
  kind, a payload). Posting never blocks and never allocates. The queue has a limit in messages
  and in payload bytes (defaults 1024 and 1 MiB, set per app); it starts with 256 slots and grows
  on the UI thread, never in a post, up to the limit as it fills; a full queue answers
  GATES_POST_FULL and the payload stays with the caller (backpressure); a closed one
  GATES_POST_CLOSED. The UI thread delivers a bounded number per turn (64) at a safe point.
  Every payload is released exactly once: after delivery, when its target is gone, or at shutdown.
- **Timers** (`gates/timer.h`). Belong to a node; run on the UI thread; cancelling is immediate
  and destroying the node cancels them; a late repeating timer fires once (no burst). Resolution
  is the platform's (Win32: about 10-16 ms). An idle window runs no platform timer.
- **Tasks** (`gates/task.h`). Work on a thread of its own with progress (the latest report
  wins), a result and cancellation, reported on the UI thread; closing the window cancels every
  task and waits for it. Message kinds from 0xFFFF0000 up are reserved for tasks.

## 11. Accessibility

- Every node is described in platform-free terms (`gates/access.h`): a role, a name, a
  description, states, a value, actions, and virtual items addressed by (node, item id). The
  Windows adapter is UI Automation.
- The name is the first of: an explicit name, a tied label, a form field's label, the node's
  own text, an item's label or first cell. A form field's error and help are its description.
  Automation ids are explicit or derived and meant to be stable across runs.
- Actions go through the input paths: the same events, commands, limits and refusals as input.
- Views expose the rows shown now plus the selected row as items, and a table row's cells below
  them (text, column label, check state, progress, and toggling or setting a value through the
  model as a person's edit would); editors expose text by
  character, word, line (a shown row) and paragraph (a text line); ranges and values are read in
  the units shown.
- **Enforced rules** (`gates_access_audit`; `GATES_ACCESS_STRICT=1` makes the Windows window
  report them in its title): every interactive node has a name, is reachable by keyboard and is
  at least 24 x 24 units; automation ids are unique; theme contrast and cue thickness as in
  section 4.

## 12. Persisted UI state

`gates/state.h` saves what a person arranged - split positions, selected tabs, table columns
(order, widths, hidden), scroll offsets (down and, for an area that scrolls sideways, across) - as text keyed by automation id, and loads it back.
Loading skips what it does not understand, so an older file never breaks a newer program. The
window's placement is kept the same way (`gates_window_placement`).

## 13. Limits

| Limit | Value |
|---|---|
| open overlays | 8 |
| view rows painted at a time | 256 |
| grid columns / rows with a grow weight | 16 / 16 |
| text box undo | 64 entries, 16384 bytes (settable) |
| text buffer | 2^30 bytes |
| image side | 16384 pixels |
| post queue | 1024 messages, 1 MiB payload (settable per app; starts at 256 slots and grows); 64 delivered per turn |
| log | 1000 lines, 1 MiB (settable per log) |
| pointer target | at least 24 x 24 units |
| type-ahead | 4096 rows looked at per character |
| multi-select copy | 10000 rows, then the program is asked |

## 14. The Windows backend

- One window per top-level surface; a window is shown when `gates_app_run` starts (or at once
  while it runs), so its first frame already shows the program's UI.
- Rendering: the software renderer into a DIB, presented with GDI; text through GDI with the
  system message font (UI; the program may name another, `ui_font` / `gates_app_set_ui_font`,
  0.12.0), Consolas (MONO) and named faces at the UI size, missing characters from the system's
  fallback fonts.
- Input methods through IMM32 (composition drawn inline at the caret); the clipboard, images
  (Windows Imaging Component), native file, folder, colour and message dialogs, per-monitor DPI,
  system themes and UI Automation are provided by the backend.
- Build: mingw-w64 (UCRT), static libraries `libgates.a` and `libproven.a`.

## 15. Versions and compatibility

- The version is in `gates/version.h`; `gates_version()` reports the one the library was built
  as.
- Before 1.0 a minor version may change the API; every change is listed in `CHANGELOG.md`.
  Structures the program fills gain fields only at their end, so designated initializers keep
  working across versions.
- The package (`gates-<version>-sdk.zip`) contains the public headers, the gates and proven
  static libraries, the manual in English and Korean, and a consumer example built from the
  package alone.
