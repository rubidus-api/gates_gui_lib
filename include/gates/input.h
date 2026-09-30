/* gates_gui_lib - unified input events (RFC-0001 section 15, Phase 1 subset).
 * Platform-free. Phase 1 fills mouse fields only; pen/touch arrive with
 * later backends, gestures in Phase 5. */
#ifndef GATES_INPUT_H
#define GATES_INPUT_H

#include <gates/geometry.h>

typedef enum gates_pointer_type_t {
    GATES_POINTER_MOUSE,
    GATES_POINTER_PEN,
    GATES_POINTER_TOUCH,
    GATES_POINTER_TOUCHPAD,
    GATES_POINTER_UNKNOWN,
} gates_pointer_type_t;

typedef enum gates_pointer_action_t {
    GATES_POINTER_DOWN,
    GATES_POINTER_UP,
    GATES_POINTER_MOVE,
    GATES_POINTER_WHEEL,
} gates_pointer_action_t;

#define GATES_BUTTON_LEFT   0x1u
#define GATES_BUTTON_RIGHT  0x2u
#define GATES_BUTTON_MIDDLE 0x4u

typedef struct gates_pointer_event_t {
    gates_pointer_action_t action;
    gates_u32 pointer_id;            /* mouse = 0 */
    gates_pointer_type_t type;
    gates_point_t pos;               /* client coordinates, logical units (plan-0013) */
    gates_point_t screen_pos;        /* device pixels, as the platform reports them */
    gates_vec2_t delta;              /* movement since the previous event */
    gates_vec2_t wheel;              /* WHEEL: lines/notches (y = vertical) */
    gates_u32 buttons;               /* GATES_BUTTON_* held AFTER this event */
    gates_u32 button;                /* DOWN/UP: the button that changed */
    float pressure;                  /* mouse = 1.0 while any button held */
    float tilt_x;
    float tilt_y;
    bool primary;
    bool canceled;
    /* DOWN: 2 for the second press of a double click, 3 for a third quick
     * press in the same place, else 1 (0 reads as 1). */
    gates_u32 clicks;
    /* Modifier keys held at the time of the event (0.8.0): Shift+press
     * extends a text selection. */
    bool shift;
    bool ctrl;
    bool alt;
} gates_pointer_event_t;

/* Semantic keys the core understands. The platform layer maps its virtual
 * key codes onto these so the core stays platform-free; `vkey` keeps the raw
 * code for applications that need it. */
typedef enum gates_key_t {
    GATES_KEY_NONE = 0,
    GATES_KEY_LEFT,
    GATES_KEY_RIGHT,
    GATES_KEY_UP,
    GATES_KEY_DOWN,
    GATES_KEY_HOME,
    GATES_KEY_END,
    GATES_KEY_BACKSPACE,
    GATES_KEY_DELETE,
    GATES_KEY_ENTER,
    GATES_KEY_TAB,
    GATES_KEY_ESCAPE,
    GATES_KEY_A,                     /* for Ctrl+A select-all */
    GATES_KEY_C,                     /* Ctrl+C copy (plan-0008) */
    GATES_KEY_X,                     /* Ctrl+X cut */
    GATES_KEY_V,                     /* Ctrl+V paste */
    GATES_KEY_Z,                     /* Ctrl+Z undo, Ctrl+Shift+Z redo */
    GATES_KEY_Y,                     /* Ctrl+Y redo */
    GATES_KEY_SPACE,                 /* activates a focused button/checkbox (plan-0009) */
    GATES_KEY_F1, GATES_KEY_F2, GATES_KEY_F3, GATES_KEY_F4, GATES_KEY_F5, GATES_KEY_F6,
    GATES_KEY_F7, GATES_KEY_F8, GATES_KEY_F9, GATES_KEY_F10, GATES_KEY_F11, GATES_KEY_F12,
    GATES_KEY_PAGE_UP,               /* views page through rows (plan-0011) */
    GATES_KEY_PAGE_DOWN,
} gates_key_t;

typedef struct gates_key_event_t {
    gates_u32 vkey;                  /* raw platform virtual key code */
    gates_key_t key;                 /* semantic key, NONE if unmapped */
    bool down;
    bool repeat;
    bool shift;
    bool ctrl;
    /* Ctrl+Alt is AltGr on some layouts: a key with alt held is never read as
     * a Ctrl shortcut. */
    bool alt;
    /* 'A'-'Z' or '0'-'9' for letter and digit keys (unshifted, layout's key
     * label), else 0. Command shortcuts match on it (plan-0009). */
    gates_u8 letter;
} gates_key_event_t;

#endif /* GATES_INPUT_H */
