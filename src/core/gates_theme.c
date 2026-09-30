/* gates_gui_lib - built-in theme profiles: light, dark and high contrast.
 * The only place in the library where widget RGB
 * values live; everything else speaks tokens. Platform-free. */
#include <gates/theme.h>

const gates_theme_t *gates_theme_light(void) {
    static const gates_theme_t light = {
        .colors = {
            [GATES_COLOR_WINDOW_BG]           = { 245, 245, 242, 255 },
            [GATES_COLOR_WINDOW_FG]           = {  28,  30,  34, 255 },
            [GATES_COLOR_PANEL_BG]            = { 236, 236, 232, 255 },
            [GATES_COLOR_PANEL_FG]            = {  28,  30,  34, 255 },
            [GATES_COLOR_CONTROL_BG]          = { 252, 252, 250, 255 },
            [GATES_COLOR_CONTROL_FG]          = {  20,  22,  26, 255 },
            [GATES_COLOR_CONTROL_BORDER]      = { 120, 124, 132, 255 },
            [GATES_COLOR_CONTROL_HOVER_BG]    = { 226, 232, 244, 255 },
            [GATES_COLOR_CONTROL_PRESSED_BG]  = { 198, 210, 235, 255 },
            [GATES_COLOR_CONTROL_DISABLED_FG] = { 118, 120, 126, 255 },
            [GATES_COLOR_SELECTION_BG]        = {  52,  98, 205, 255 },
            [GATES_COLOR_SELECTION_FG]        = { 252, 252, 252, 255 },
            [GATES_COLOR_FOCUS_RING]          = {  52,  98, 205, 255 },
            [GATES_COLOR_OVERLAY_DIM]         = {  20,  22,  26,  72 },
            [GATES_COLOR_ERROR]               = { 196,  43,  28, 255 },
        },
        .focus_width = 2,
        .error_width = 2,
    };
    return &light;
}

const gates_theme_t *gates_theme_dark(void) {
    static const gates_theme_t dark = {
        .colors = {
            [GATES_COLOR_WINDOW_BG]           = {  32,  32,  34, 255 },
            [GATES_COLOR_WINDOW_FG]           = { 236, 236, 236, 255 },
            [GATES_COLOR_PANEL_BG]            = {  43,  43,  46, 255 },
            [GATES_COLOR_PANEL_FG]            = { 236, 236, 236, 255 },
            [GATES_COLOR_CONTROL_BG]          = {  24,  24,  26, 255 },
            [GATES_COLOR_CONTROL_FG]          = { 240, 240, 240, 255 },
            [GATES_COLOR_CONTROL_BORDER]      = { 138, 138, 144, 255 },
            [GATES_COLOR_CONTROL_HOVER_BG]    = {  58,  60,  66, 255 },
            [GATES_COLOR_CONTROL_PRESSED_BG]  = {  74,  78,  90, 255 },
            [GATES_COLOR_CONTROL_DISABLED_FG] = { 140, 140, 146, 255 },
            [GATES_COLOR_SELECTION_BG]        = {  30,  96, 176, 255 },
            [GATES_COLOR_SELECTION_FG]        = { 255, 255, 255, 255 },
            [GATES_COLOR_FOCUS_RING]          = { 110, 180, 255, 255 },
            [GATES_COLOR_OVERLAY_DIM]         = {   0,   0,   0, 120 },
            [GATES_COLOR_ERROR]               = { 255, 110, 100, 255 },
        },
        .focus_width = 2,
        .error_width = 2,
    };
    return &dark;
}

/* Relative brightness on a 0..255 scale (integer, good enough to pick a side). */
static int brightness(gates_color_t c) {
    return (c.r * 299 + c.g * 587 + c.b * 114) / 1000;
}

void gates_theme_high_contrast(const gates_system_colors_t *sys, gates_theme_t *out) {
    if (out == nullptr) {
        return;
    }
    /* Classic "High Contrast Black" when the system gives nothing. */
    static const gates_system_colors_t black = {
        .window = { 0, 0, 0, 255 },
        .window_text = { 255, 255, 255, 255 },
        .highlight = { 26, 235, 255, 255 },
        .highlight_text = { 0, 0, 0, 255 },
        .button_face = { 0, 0, 0, 255 },
        .button_text = { 255, 255, 255, 255 },
        .gray_text = { 63, 242, 63, 255 },
        .hotlight = { 255, 255, 0, 255 },
    };
    const gates_system_colors_t *s = sys != nullptr ? sys : &black;
    bool light_bg = brightness(s->window) > 128;
    gates_color_t error = light_bg ? (gates_color_t){ 176, 0, 0, 255 }
                                   : (gates_color_t){ 255, 120, 120, 255 };
    *out = (gates_theme_t){
        .colors = {
            [GATES_COLOR_WINDOW_BG]           = s->window,
            [GATES_COLOR_WINDOW_FG]           = s->window_text,
            [GATES_COLOR_PANEL_BG]            = s->window,
            [GATES_COLOR_PANEL_FG]            = s->window_text,
            [GATES_COLOR_CONTROL_BG]          = s->button_face,
            [GATES_COLOR_CONTROL_FG]          = s->button_text,
            [GATES_COLOR_CONTROL_BORDER]      = s->button_text,
            /* Hover and press keep the colours; the border and focus ring say it. */
            [GATES_COLOR_CONTROL_HOVER_BG]    = s->button_face,
            [GATES_COLOR_CONTROL_PRESSED_BG]  = s->button_face,
            [GATES_COLOR_CONTROL_DISABLED_FG] = s->gray_text,
            [GATES_COLOR_SELECTION_BG]        = s->highlight,
            [GATES_COLOR_SELECTION_FG]        = s->highlight_text,
            [GATES_COLOR_FOCUS_RING]          = s->hotlight,
            [GATES_COLOR_OVERLAY_DIM]         = { 0, 0, 0, 0 }, /* no dimming: keep contrast */
            [GATES_COLOR_ERROR]               = error,
        },
        .focus_width = 3,
        .error_width = 3,
    };
}
