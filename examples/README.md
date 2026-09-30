# Examples

Windows programs built by `make win` (mingw-w64) into `build/win/`. Every example uses public headers only and keeps its state through widget events, never by reading widgets during paint. Each file's header says what interaction the control serves and how to check it by hand.

## One example per control

| Example | Control | What the person does | Events used |
|---|---|---|---|
| `ctl_label` | label | reads text (plain, wide characters, disabled, replaced by the program) | ACTIVATED (button) |
| `ctl_button` | button | asks for one action; can be disabled | ACTIVATED |
| `ctl_checkbox` | checkbox | turns options on or off; the program can set and announce them | VALUE_CHANGED (USER and PROGRAM) |
| `ctl_textbox` | textbox | types a line with the keyboard or the IME; copies, pastes, undoes; is asked what to do with input that does not fit; uses password and read-only fields | TEXT_CHANGED, PREEDIT_CHANGED, LIMIT_EXCEEDED |
| `ctl_panel` | panel | sees grouped items; the list grows and shrinks at run time | ACTIVATED |
| `ctl_stack` | stack layout | switches between pages shown in one place | ACTIVATED |
| `ctl_split` | split layout | drags handles to share space (nested) | ACTIVATED |
| `ctl_scroll` | scroll layout | scrolls a long list with the wheel or thumb | VALUE_CHANGED |
| `ctl_keyboard` | focus and activation | fills and submits a form with the keyboard alone (Tab, Space, Enter, Esc) | VALUE_CHANGED, commands |
| `ctl_commands` | commands, context menu | runs actions from buttons, shortcuts or a right-click / Shift+F10 menu | TEXT_CHANGED, commands |
| `ctl_dialog` | modal dialog | answers a confirmation and a rename dialog | DIALOG_CLOSED, TEXT_CHANGED, commands |
| `ctl_radio` | radio group, hidden | picks one of a few options with arrows or clicks; an option is enabled at run time; a group is hidden until asked for | VALUE_CHANGED (result = option id) |
| `ctl_choice` | choice, error state | picks from a dropdown list; "Other" shows a name field marked invalid while empty | VALUE_CHANGED, TEXT_CHANGED |
| `ctl_progress` | progress, separator, timer | reads how far work has come; steps it with buttons or lets a timer fill it | ACTIVATED, timers |
| `ctl_list` | list view | browses a million generated rows by keys, wheel and thumb; activates a row | SELECTION_CHANGED, ACTIVATED |
| `ctl_table` | table view | sorts by clicking headers, resizes columns, scrolls sideways; the selection follows its item | SORT_REQUESTED, SELECTION_CHANGED |
| `ctl_tree` | tree view | opens and closes folders by keys or marks; sees loading and error rows | EXPAND_REQUESTED |
| `ctl_log` | log view | watches a bounded log follow its end, scrolls back to read, sees dropped lines counted | SELECTION_CHANGED, FOLLOW_CHANGED |

## Other programs

| Example | Purpose |
|---|---|
| `app_todo` | sample application: a to-do list - Enter adds, Enter or a double click marks done, Delete removes, a context menu, a live status line, saved in `%LOCALAPPDATA%\gates-todo.txt` |
| `app_calculator` | sample application: a calculator - a grid of buttons with spoken names for the symbols, typed digits and operators, a live result |
| `app_converter` | sample application: a unit converter - a form whose choices change with the quantity, converting as you type, an error instead of a result for text that is not a number |
| `gallery` | every control in one window (0.3.0, Inputs page 0.4.0, Pictures page 0.5.0, Data & jobs page 0.6.0, Text editor page 0.7.0, opening several files 0.8.0): a menu bar, a toolbar, tabs, a status bar with a clock, tooltips and access keys everywhere; theme and zoom commands; the page, split, columns and window placement kept in `%LOCALAPPDATA%\gates-gallery.ini` |
| `app_files` | sample application: a folder browser - a table filled by a worker thread, folders first, sorting by any column, Enter opens a folder, Backspace goes up (files are never opened or changed) |
| `app_inspector` | reference application: a record table with a detail form; add, shuffle and remove records while the selection follows its record; rename a record in place (0.6.0) |
| `app_logview` | reference application: a live log fed by a worker thread - run, pause, flood (the producer skips batches when the queue is full), follow, the selected line in full, a clean exit while flooding |
| `app_settings` | reference application: a settings form with validation on Save, messages under the fields, a revealed row, Save/Cancel as commands, and an applying step that animates unless Windows is set to reduce motion |
| `hello_window` | the smallest window: surface, present, input callbacks |
| `widgets_demo` | several controls and layouts together |
| `text_demo` | text rendering and the textbox with Korean IME composition |
| `text_conformance` | console program running the text backend contract on Windows |

Pattern shared by the control examples:

```c
gates_node_t box = GATES_NODE_NULL;
TRY(gates_textbox_create(tree, root, GATES_STR(""), 30, &box));
TRY(gates_widget_set_handler(tree, box, on_change, app_state)); /* events, not polling */
```

The handler runs after the input message was handled; it may change or destroy any node.
