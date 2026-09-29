# Chapter 10 - Deployment and troubleshooting

Headers: `gates/app.h`, `gates/window.h`, `gates/version.h`.

## Linking

| Target | Link line |
|---|---|
| Windows program | `-lgates -lproven -lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 -luiautomationcore -lole32 -loleaut32 -luuid`, plus `-mwindows` for a program without a console |
| headless (any system) | `-lgates_core -lproven -lm` |

Compile with a C23 compiler (`-std=c23`; GCC 14+, Clang 18+, or mingw-w64 with GCC 14+) and
`-I<package>/include`. `examples/consumer/` in the package is a complete program with a
Makefile that builds against the package alone. `make install PREFIX=/some/dir` in the
source tree copies the headers and libraries to `PREFIX/include` and `PREFIX/lib`.

The result is one executable: the libraries are static, and gates needs nothing on the
target machine beyond Windows 10 or 11 itself (UI Automation, the input method and DPI
functions it uses are part of Windows; newer ones are looked up at run time).

## Versions and compatibility

`gates/version.h` gives `GATES_VERSION_STRING` and `GATES_VERSION_NUMBER`; `gates_version()`
says what the linked library is. Releases with the same minor version are source compatible
with each other: a program that built against 0.2.0 builds against 0.2.1. 0.2.0 changed the
text backend contract (a custom backend adds `glyph_advance` and takes a font instead of a
size); programs that use gates' own backends only rebuild. 0.3.0 adds the application frame
and changes one thing that existed: in button, check box and command labels, `&` before a letter
or digit now marks a mnemonic and is not shown (write `&&` for a literal one). Releases are not promised to be binary
compatible - public structs may change size - so rebuild the program with every update, and
check at start-up:

```c
if (gates_version() != GATES_VERSION_NUMBER) { /* headers and library differ */ }
```

## The application and its windows

`gates_app_create` makes the application (normally one per program, on the UI thread, which it
initialises as a COM single-threaded apartment for UI Automation); `gates_window_create` opens a
window with its own tree; `gates_app_run` runs the loop until the last window closes or
`gates_app_quit` is called; destroy windows, then the app. The allocator in
`gates_app_desc_t` is used for everything gates allocates (the default is thread-safe).

## Troubleshooting

| Symptom | Cause |
|---|---|
| a handler never runs | no handler set, or the change was made by the program (setters are silent: use `gates_widget_notify`) |
| a call returns `PROVEN_ERR_INVALID_ARG` for a node you just used | the handle is stale: the node was destroyed (and maybe its slot reused) |
| `PROVEN_ERR_INVALID_STATE` from an action | the control is disabled, hidden, or behind a modal dialog |
| typed text does not appear | the text box is read-only, or the input passed its maximum length (LIMIT_EXCEEDED is waiting for an answer) |
| the IME does not open | the box is read-only or a password box |
| the interface is tiny or huge | sizes given in pixels instead of logical units (chapter 8) |
| worker results stop arriving | the worker ignored `GATES_POST_FULL` or kept posting after `GATES_POST_CLOSED` |
| Narrator reads "edit" with no name | the label beside the field is not tied to it (chapter 9) |
| duplicate symbols `proven_*` at link time | the program links two copies of proven: keep one |
