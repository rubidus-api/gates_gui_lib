# Changelog

All notable changes to this project will be documented in this file.

This project follows Keep a Changelog.

## [0.1.0] - 2026-09-27

The first package (plan-0015): headers and static libraries a program builds against.
Source compatible within 0.1.x; rebuild on every update; no binary ABI is promised yet.

### Added (package and manual)

- The package (plan-0015): `make dist` builds `dist/gates-0.1.0/` - public headers, the proven headers they need, static libraries (`lib/win64/libgates.a`, `lib/host/libgates_core.a`, and `libproven.a` for each, separate from gates), licenses, the manual and `examples/consumer/`, a program built from the package alone; `make install PREFIX=...`. `gates/version.h` (`GATES_VERSION_*`, `gates_version()`, `gates_version_string()`) and `gates/gates.h`, which includes the whole public API. The libraries export only `gates_*` / `proven_*` names.
- Sample applications `app_todo`, `app_calculator`, `app_converter` and `app_files` (examples/), shipped with every other example as Windows 11 x64 binaries in the release.
- A live label no longer re-announces (or re-lays out) when it is given the text it already shows.
- The manual, in English (`manual/`, canonical) and Korean (`manual-ko/`): eleven chapters from the first program to deployment and troubleshooting; every program it prints is compiled against the package, and the headless ones run (`make manual-check`).

### Changed (breaking)

- Pointer targets are at least 24 x 24 units (WCAG 2.2 2.5.8, plan-0014): radio rows, menu and list rows, view rows and checkboxes are taller when the text line is shorter than that.

- Every coordinate and size the API takes or returns is now a logical unit, 1/96 inch (plan-0013): layout values, node rects, preferred sizes, view part rects, window sizes, pointer positions and the viewport given to `gates_layout_run`. A window scales once at its boundary - drawing goes to device pixels, pointer positions come back in units - so on displays above 100% applications grow with the scale; at 100% nothing changes. `gates_pointer_event_t.screen_pos` stays in device pixels.

### Added

- Accessibility (plan-0014 stage 1): `include/gates/access.h`. Every node has a role, a name (explicit `gates_node_set_access_name`, a tied label `gates_node_set_labelled_by`, a form field's label, or its own text), a description (a field's error and help), states, a value, bounds and the actions it accepts, and radio options, choice options and menu entries are virtual items addressed by id; `gates_access_info`, the accessible tree (`gates_access_parent` / `_first_child` / `_last_child` / `_next` / `_prev` / `_at_point` / `_focus_ref`), actions that take the same paths as input (`gates_access_invoke` / `_toggle` / `_select` / `_expand` / `_set_value` / `_focus`), a change log for platform adapters, live regions (`gates_node_set_live`), announcements, stable automation ids (`gates_node_set_automation_id`; forms and commands give `field-<id>` and `cmd-<id>`), and an audit of enforced rules (`gates_access_audit`, `gates_theme_audit`: names, 24 x 24 targets, keyboard reach, duplicate ids, contrast, focus cues).
- Accessibility (plan-0014 stage 2): views expose their shown rows (and the selection) as items - list items, table rows named by their cells, tree items with level, expanded state and loading rows - with select, invoke (as Enter) and expand-as-request through the model; `gates_access_scroll_info` / `_scroll_to` / `_scroll_by` for views and scroll areas; text of edits for text APIs (`gates_access_text_rect`, `_text_offset_at`, `_select_text`); `set_position`, `set_size` and `level` in the info.
- Windows: every window is a UI Automation provider written in C (Invoke, Toggle, Value, RangeValue, Selection, SelectionItem, ExpandCollapse, Grid, GridItem, Scroll, Window for dialogs, Text and Text2 for edits with UTF-16 offsets; focus, property, structure, live-region, menu-opened, text-changed, text-selection-changed and notification events), so Narrator and automation tools see the tree; a hidden system caret follows the text caret for magnifiers; `gates_window_set_zoom` / `gates_window_zoom` and the Windows "Text size" setting scale the whole window; `gates_window_reduced_motion`; `gates_window_announce`; `GATES_ACCESS_STRICT=1` audits after every layout and shows the issue count in the title. Links `uiautomationcore`, `ole32`, `oleaut32`, `uuid`; the UI thread is initialised as a COM single-threaded apartment. `widgets_demo`: F3 / F4 zoom. The examples name every control.

- Themes (plan-0013): `gates_theme_dark()`, `gates_theme_high_contrast()` from the system's colours, focus and error cue widths in `gates_theme_t`; windows follow the system (dark mode, high contrast, live) unless the application picks a mode or theme (`gates_window_set_theme_mode`, `gates_window_set_theme`, `gates_window_theme`); the title bar follows dark mode. The light palette's disabled text is darker (3:1 contrast).
- DPI (plan-0013): per-monitor DPI awareness, `WM_DPICHANGED` handling, logical-unit helpers `gates_px`, `gates_logical`, `gates_rect_px`, `gates_thickness_px`, `gates_render_soft_scaled`, and an optional `draw_scaled` for text backends (the GDI backend draws crisp scaled glyphs on the scaled cell grid). `GATES_FORCE_DPI` forces a DPI for diagnostics. `widgets_demo`: F2 cycles the theme mode.

- Posting from worker threads (plan-0012): `include/gates/post.h`. A reference-counted sender (`gates_app_sender`, `gates_sender_retain` / `_release`) outlives the app; `gates_sender_post` never blocks or allocates and answers `GATES_POST_FULL` or `GATES_POST_CLOSED` (the caller keeps the payload), otherwise gates owns the payload and releases it exactly once - after delivery to the target node's handler (`gates_node_set_message_handler`), when the target is gone, when a replaceable snapshot replaces it, or at shutdown. Targets name a tree by its process-unique serial (`gates_tree_serial`, `gates_target`). Delivery is bounded per turn (64). Queue limits in `gates_app_desc_t` (defaults 1024 messages / 1 MiB). The core stays thread-free: the platform supplies the lock and the wake-up (`gates_sync_t`, `gates_sender_create`).
- Timers (plan-0012): `include/gates/timer.h` - `gates_timer_start` / `_cancel` / `_active` on a node; cancel is immediate, node or tree destruction cancels, a late repeating timer fires once. Win32 keeps one SetTimer per window for the next due timer (none when idle).
- `app_logview` now reads from a real worker thread (skips batches on FULL, ends on CLOSED, keeps input responsive while flooding); `ctl_progress` has a timer-driven "run".

- Virtual views (plan-0011): `include/gates/view.h`. A list or table is one node over a model the application owns (`gates_rows_model_t`: count, id_at, index_of, cell, optional revision and row_info) and asks only for the rows it paints (at most `GATES_VIEW_MAX_ROWS`); rows are 64-bit and the vertical position is kept in rows; the single selection is a stable item id that survives inserts, removals and reordering (`gates_view_model_changed` moves a vanished selection to its nearest neighbour and announces it with origin PROGRAM). Keyboard (arrows, PageUp/PageDown, Home/End, Enter), click and double click, wheel and Shift+wheel, both scrollbars, header clicks as sort requests, column resizing with minima. Trees: flattened visible rows with depth and open state, open/close as requests, Left/Right navigation, loading and error rows. Logs: `gates_log_create` / `_append` / `_clear` / `_line` / `_following` with line and byte limits, counted drops and follow-at-end. Events SELECTION_CHANGED, SORT_REQUESTED, EXPAND_REQUESTED and ACTIVATED with `ev->item`. Examples `ctl_list`, `ctl_table`, `ctl_tree`, `ctl_log` and reference applications `app_inspector` and `app_logview`.
- Input: `GATES_KEY_PAGE_UP` / `_PAGE_DOWN` and `gates_pointer_event_t.clicks`; the Win32 window class has CS_DBLCLKS, double clicks arrive with `clicks = 2`, and Shift+wheel scrolls sideways.

- Forms (plan-0010 stage 2): layout kind `GATES_LAYOUT_KIND_FORM` (one label column, labels above editors when the form is narrower than the label column plus 12 cells, hidden rows take no space) and `include/gates/form.h` (`gates_form_create`, `gates_form_add_text` / `_checkbox` / `_choice` / `_radio` with label, help, required marker and read-only, `gates_form_set_error`, lookups by field id, `gates_form_set_row_hidden`); node kind FORM. Rows are built completely before they are attached. Reference application `app_settings`.
- Fields (plan-0010 stage 1): radio group and choice (dropdown) as single nodes with stable option ids (`gates_option_t`, `gates_radio_create`, `gates_choice_create`, `gates_options_set` / `_set_selected` / `_selected` / `_set_enabled` / `_count`, `gates_choice_list` / `_list_open`); VALUE_CHANGED carries the selected id in `ev->result`; the choice's list is an overlay with the menu's rules and opens with Space, Enter, Alt+Down or a click. Separator (`gates_separator_create`) and progress bar (`gates_progress_create` / `_set_value` / `_value`, per-mille). Hidden nodes (`gates_node_set_hidden` / `gates_node_hidden`) for layout, paint, hit testing and focus. Textbox error state (`gates_textbox_set_invalid`) and theme token `GATES_COLOR_ERROR`. `GATES_STR_INIT` for static option tables. Examples `ctl_radio`, `ctl_choice`, `ctl_progress`.

### Fixed

- Win32: Alt+Up and Alt+Down now reach the widget tree (they were handed to Windows with every other Alt combination); unused, they still go to Windows.

- Overlays (plan-0009 stage 2): `include/gates/overlay.h` with modal dialogs (`gates_dialog_open` / `_close`, DIALOG_CLOSED once, focus scope, dimmed and blocked content behind, focus restored, any press or drag below cancelled) and context menus of commands (`gates_menu_open` / `_close`, MENU_CLOSED, keyboard navigation, outside click only closes, closed on window focus loss); node kinds DIALOG and MENU; theme token OVERLAY_DIM. Examples `ctl_dialog` and the context menu in `ctl_commands` (right-click or Shift+F10).

- Keyboard focus (plan-0009 stage 1): buttons and checkboxes are focusable and draw a focus ring; Tab / Shift+Tab traversal in tree order with wrap (`gates_tree_focus_next`), `gates_widget_set_focusable`, focus repair when the focused control is disabled, hidden or destroyed, scroll-to-focus; Space activates on release, Enter activates a focused button, Escape cancels a press.
- Command model `include/gates/command.h`: registration per scope, shortcuts (Ctrl+key or F1-F12), default/cancel roles for Enter/Escape, enabled/checked/label state, `gates_command_invoke`, `gates_button_set_command`; invocations are queued and re-checked before running.
- Key events carry `letter`; new keys SPACE and F1-F12. Examples `ctl_keyboard` and `ctl_commands`.

- Text editing contract (plan-0008, RFC-0003 phase C): `gates_textbox_copy_text`, `_selection`, `_set_selection`, `_replace_selection`, `_set_read_only`, `_set_max_bytes`, `_set_password`, `_set_undo_limits`, `_can_undo`/`_can_redo`, `_accept_fit`/`_discard_rejected`, `_edit_commit`; `gates_text_edit_reserve`.
- Clipboard boundary `include/gates/clipboard.h` with a Win32 CF_UNICODETEXT provider; Ctrl+C/X/V, Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z; single-line paste policy.
- GATES_EVENT_LIMIT_EXCEEDED with `limit` and `fit_bytes`: an over-limit edit is refused and kept as an offer so the application can ask the person.
- `alt` on `gates_key_event_t`; Ctrl+Alt (AltGr) is never a shortcut.
- ctl_textbox shows a 10-byte field with its question, a password field and a read-only field.

- Typed change notifications (plan-0007, RFC-0003 phase B): `include/gates/event.h` with TEXT_CHANGED, PREEDIT_CHANGED, VALUE_CHANGED and ACTIVATED, `gates_widget_set_handler`, `gates_widget_notify`, `gates_widget_revision`, `gates_tree_dispatch_events`, `gates_tree_pending_events`; events are reserved before an edit commits, coalesced, delivered at a safe point, and safe against handlers that mutate or destroy nodes.
- `gates_input_take_error` and `gates_input_cancel_pointer`; the Win32 window cancels press/drag on WM_CAPTURECHANGED and WM_CANCELMODE, delivers events after every input message, and cancels an IME composition when application code moves focus.
- One example per control: `ctl_label`, `ctl_button`, `ctl_checkbox`, `ctl_textbox`, `ctl_panel`, `ctl_stack`, `ctl_split`, `ctl_scroll` (see `examples/README.md`).

- IME composition (plan-0006, Phase 3B): `gates_input_preedit`, `gates_input_commit`, `gates_input_preedit_cancel`, `gates_input_composing`, `gates_input_caret_rect`; the textbox draws the preedit inline with an underline and keeps the IME cursor in view.
- Win32 IMM32 adapter: WM_IME_STARTCOMPOSITION/COMPOSITION/ENDCOMPOSITION, result string as the only commit, composition completed before a click or focus loss, composition and candidate windows placed at the caret; examples link `imm32`.
- `gates_textbox_set_text` (returns BUSY while a composition is open).
- `gates_input_result_t` (IGNORED / CONSUMED / FAILED).
- Core intent stated in README and RFC-0001 section 1.0: an interaction layer, not a picture (HTML+CSS-like).

### Changed

- Clicking a button or checkbox now gives it keyboard focus; clicking elsewhere leaves focus where it is (previously any click outside a textbox cleared focus).
- Destroying the focused textbox moves focus to the next control instead of leaving none.

- `gates_input_char` returns `gates_input_result_t` instead of `bool` (IGNORED is 0, so boolean use still works).
- A preedit is displayed at the selection end; moving focus away from a textbox drops its uncommitted preedit.
- RFC-0003 accepted as the next milestone (Windows Tool UI 1).

### Deprecated

- `gates_textbox_edit` (mutable edit core): use the safe textbox operations; call `gates_textbox_edit_commit` after raw edits.

### Removed

### Fixed

- Win32 released the mouse capture before routing the button-up event.
- `gates_widget_text` on a textbox returned an unused label field instead of the box's text.
- text_demo no longer updates labels during paint; it follows the textbox through events.

- `gates_input_char` reported an insert that failed on allocation as consumed; it now returns FAILED and leaves the text unchanged.
- `gates_widget_set_text` on a textbox changed a hidden label field instead of the box's text.
- text_demo's mirror label could lag one frame behind the textbox.

### Security
