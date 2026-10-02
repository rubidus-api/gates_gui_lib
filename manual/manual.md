# gates Manual (v0.13.0)

The manual of `gates`, a small retained GUI library in C23 for tool-style Windows programs:
settings panels, inspectors, log viewers, build tools. Every chapter is one file under
`manual/`; the Korean edition mirrors it under [`manual-ko/`](../manual-ko/manual-ko.md).

## Contents

- [Chapter 0 - Start here](manual-00-start-here.md): what gates is, the package, the first program
- [Chapter 1 - Concepts](manual-01-concepts.md): the node tree, handles, ownership, errors, events
- [Chapter 2 - Controls and layout](manual-02-controls-layout.md): the control matrix, layouts
- [Chapter 3 - Forms](manual-03-forms.md): labelled fields, the draft, validation
- [Chapter 4 - Commands, focus, dialogs and menus](manual-04-commands-focus.md): also undo and redo for the program's data
- [Chapter 5 - Views over your data](manual-05-views.md): lists, tables, trees, logs; cells people edit; the property grid
- [Chapter 6 - Text entry](manual-06-text.md): the text box, the installed IME, limits, Unicode; the multi-line editor and large texts
- [Chapter 7 - Workers and timers](manual-07-workers-timers.md): also background tasks with progress and Cancel
- [Chapter 8 - Themes, units and scaling](manual-08-themes-units.md)
- [Chapter 9 - Accessibility](manual-09-accessibility.md): the model, the rules, UI Automation
- [Chapter 10 - Deployment and troubleshooting](manual-10-deployment.md)
- [Chapter 11 - The application frame](manual-11-application-frame.md): mnemonics, the menu bar, the keymap, toolbar, status bar, tooltips, tabs, saved state
- [Chapter 12 - Numbers, groups and layouts](manual-12-numbers-layouts.md): bubbling, deferred calls, spin box, slider, group box, grid and wrap layouts
- [Chapter 13 - Images and native dialogs](manual-13-images-dialogs.md): images, icons, file/folder/colour/message dialogs
- [Chapter 14 - API reference](manual-14-api-reference.md): every public function with its header's comment, header by header (made from the headers)

## How to read it

Chapters 0 and 1 first; after that, read the chapter of the control or task in front of you.
Every program printed here is a file under `manual/examples/`, and the build compiles it
against the package - the programs marked "host" also run and must print what the text says
they print (`make manual-check`). When the text and a program disagree, the program is right
and the text is a bug.

## Edition

- Library and manual version: 0.13.0. Chapters note the headers they need; everything here is
  in the 0.13.0 profile ("Windows Tool UI 1": Win32, one window per top-level surface, software
  rendering).
- Editions: these Markdown files; a PDF of the guide (chapters 0 to 13) and a web edition of the
  whole manual, chapter 14 included, attached to every release (`make manual-book` makes them).
- Author: rubidus. License: MIT - Copyright (c) 2026 rubidus-api; the library and this manual
  are under the same license (`LICENSE` in the package).
