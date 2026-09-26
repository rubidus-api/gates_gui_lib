/* gates_gui_lib — semantic color tokens (RFC-0001 §14, Phase 2 subset).
 * Ordinary controls never use hard-coded RGB (§30); they resolve tokens
 * through the active theme. Full theme system (dark/high-contrast/system
 * notifications) arrives in Phase 5; this subset plus one built-in light
 * palette unblocks widgets. Platform-free. */
#ifndef GATES_THEME_H
#define GATES_THEME_H

#include <gates/draw.h>

typedef enum gates_color_token_t {
    GATES_COLOR_WINDOW_BG,
    GATES_COLOR_WINDOW_FG,
    GATES_COLOR_PANEL_BG,
    GATES_COLOR_PANEL_FG,
    GATES_COLOR_CONTROL_BG,
    GATES_COLOR_CONTROL_FG,
    GATES_COLOR_CONTROL_BORDER,
    GATES_COLOR_CONTROL_HOVER_BG,
    GATES_COLOR_CONTROL_PRESSED_BG,
    GATES_COLOR_CONTROL_DISABLED_FG,
    GATES_COLOR_SELECTION_BG,
    GATES_COLOR_SELECTION_FG,
    GATES_COLOR_FOCUS_RING,
    GATES_COLOR_OVERLAY_DIM,       /* translucent layer behind a modal dialog */
    GATES_COLOR_ERROR,             /* a field whose value was refused (plan-0010) */
    GATES_COLOR_TOKEN_COUNT,
} gates_color_token_t;

typedef struct gates_theme_t {
    gates_color_t colors[GATES_COLOR_TOKEN_COUNT];
    /* Cues that do not rely on colour alone (plan-0013): the focus ring's and an
     * invalid field's border thickness. 0 -> 2. */
    gates_i32 focus_width;
    gates_i32 error_width;
} gates_theme_t;

/* Built-in profiles (plan-0013). Every token is defined in each; text tokens
 * reach 4.5:1 contrast on their backgrounds, borders and cues 3:1. Windows
 * follow the system (dark mode, high contrast) unless the application picks
 * a theme (gates/window.h). */
const gates_theme_t *gates_theme_light(void);
const gates_theme_t *gates_theme_dark(void);

/* The system's high-contrast colours (Win32: GetSysColor of the same names). */
typedef struct gates_system_colors_t {
    gates_color_t window;        /* background */
    gates_color_t window_text;
    gates_color_t highlight;     /* selection background */
    gates_color_t highlight_text;
    gates_color_t button_face;
    gates_color_t button_text;
    gates_color_t gray_text;     /* disabled text */
    gates_color_t hotlight;      /* links, focus */
} gates_system_colors_t;

/* Fills *out with a high-contrast theme from the system's colours (null ->
 * the classic black high-contrast set). Borders and cues are wider. */
void gates_theme_high_contrast(const gates_system_colors_t *sys, gates_theme_t *out);

static inline gates_i32 gates_theme_focus_width(const gates_theme_t *theme) {
    return theme != nullptr && theme->focus_width > 0 ? theme->focus_width : 2;
}
static inline gates_i32 gates_theme_error_width(const gates_theme_t *theme) {
    return theme != nullptr && theme->error_width > 0 ? theme->error_width : 2;
}

static inline gates_color_t gates_theme_color(const gates_theme_t *theme,
                                              gates_color_token_t token) {
    if (theme == nullptr || token >= GATES_COLOR_TOKEN_COUNT) {
        return (gates_color_t){ 255, 0, 255, 255 }; /* loud fallback */
    }
    return theme->colors[token];
}

#endif /* GATES_THEME_H */
