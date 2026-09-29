# Chapter 8 - Themes, units and scaling

Headers: `gates/theme.h`, `gates/geometry.h`, `gates/draw.h`, `gates/render.h`, `gates/text.h`.

## Logical units

Every coordinate and size in the API is a logical unit, 1/96 inch: layout values, node
rectangles, preferred sizes, window sizes, pointer positions. A window converts once, at its
edge - to device pixels when it draws, back to units when it reports the pointer - so the same
program is the same physical size on a 100 % and a 200 % display. `gates_px` and
`gates_logical` are the conversions (exact inverses for whole units).

<!-- example: manual/examples/ex_09_units.c -->
```c
/* manual example (host): logical units and themes.
 * expect: 100 units = 150 px at 144 dpi; 150 px = 100 units; light theme: 0 issues */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    /* Every coordinate in the API is a logical unit, 1/96 inch. A window
     * scales once, at its edge: to pixels when drawing, back when reading
     * pointer positions. */
    gates_u32 dpi = 144; /* 150 % */
    gates_i32 px = gates_px(100, dpi);
    gates_i32 back = gates_logical(px, dpi);
    /* A theme is checked against the enforced rules: contrast and cues. */
    gates_access_issue_t issues[8];
    gates_u32 n = gates_theme_audit(gates_theme_light(), issues, 8);
    printf("100 units = %d px at %u dpi; %d px = %d units; light theme: %u issues\n", px, dpi, px, back, n);
    return n == 0 ? 0 : 1;
}
```

The window follows its monitor (per-monitor DPI, and moving between monitors), the Windows
"Text size" setting, and an application zoom (`gates_window_set_zoom`, 25 % to 400 %). All of
them scale the whole interface, as a magnified page does.

## Themes

Colours are tokens (`gates_color_token_t`), not values: the program says "text", "focus",
"error", and the theme says which colour that is. A window follows the system - light or dark
mode, and high contrast from the system's own colours - and switches live when the person
changes it; the title bar follows too. `gates_window_set_theme_mode` fixes a mode,
`gates_window_set_theme` sets an application theme. Every theme is held to the contrast and cue
rules of chapter 9: `gates_theme_audit` checks one.

Focus and errors are never shown by colour alone: the focus ring and the error border have a
width (at least 2 units), so they read in any theme.

## Fonts

Text uses one of two fonts (`gates_font_t` in `gates/text.h`):

| Font | Is | On Windows |
|---|---|---|
| `GATES_FONT_UI` (the default) | the platform's UI face, proportional | the system message font: Segoe UI, Malgun Gothic on Korean Windows |
| `GATES_FONT_MONO` | fixed pitch, for code, logs and aligned columns | Consolas |

`gates_node_set_font` chooses the font of a node and, like CSS `font-family`, of everything
under it that does not choose its own; `GATES_FONT_INHERIT` goes back to the parent's, and
`gates_node_font` reads the effective one. Set it on a panel to switch a whole area, or on one
view for a log. Dialogs and menus are not under the window's root: they start from the UI font
unless you set one on them. Sizes follow the system and are not chosen per node in 0.4.0.

```c
(void)gates_node_set_font(tree, log_view, GATES_FONT_MONO); /* aligned log lines */
```

## Drawing and text backends

A window paints its tree into a draw list (`gates/draw.h`) and the software renderer
(`gates/render.h`) turns it into pixels. Text goes through a text backend (`gates/text.h`),
which reports per font a line height, an average width (a sizing hint: a text box's `cols`)
and the advance of every character; a string's width is exactly the sum of its advances, and
the backend draws each character at that offset. So layout, drawing, the caret and hit testing
agree on every backend and at every scale. The builtin backend is fixed-pitch for both fonts,
which keeps tests deterministic. An application can draw its own things in the window's paint
callback; it never reads widget state there.
