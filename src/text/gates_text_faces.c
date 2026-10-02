/* gates_gui_lib - named faces (0.12.0): a process-wide list of face names, so a
 * font stays one integer through the tree, the draw list and the text backend
 * contract. Used on the UI thread. Platform-free. */
#include <gates/encoding.h>
#include <gates/text.h>

#include <string.h>

static gates_u8 g_names[GATES_FONT_NAMED_MAX][GATES_FONT_NAME_MAX];
static gates_u8 g_sizes[GATES_FONT_NAMED_MAX];
static gates_u32 g_count;

gates_err_t gates_font_named(gates_str_t name, gates_font_t *out_face) {
    if (out_face == nullptr || name.ptr == nullptr || name.size == 0 || name.size > GATES_FONT_NAME_MAX ||
        !gates_utf8_valid(name, nullptr) || memchr(name.ptr, 0, name.size) != nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    for (gates_u32 i = 0; i < g_count; i++) {
        if (g_sizes[i] == name.size && memcmp(g_names[i], name.ptr, name.size) == 0) {
            *out_face = (gates_font_t)(GATES_FONT_NAMED_FIRST + i);
            return GATES_OK;
        }
    }
    if (g_count >= GATES_FONT_NAMED_MAX) return PROVEN_ERR_OVERFLOW;
    memcpy(g_names[g_count], name.ptr, name.size);
    g_sizes[g_count] = (gates_u8)name.size;
    *out_face = (gates_font_t)(GATES_FONT_NAMED_FIRST + g_count);
    g_count++;
    return GATES_OK;
}

gates_str_t gates_font_face_name(gates_font_t font) {
    gates_i32 i = gates_font_face(font) - GATES_FONT_NAMED_FIRST;
    if (i < 0 || (gates_u32)i >= g_count) return (gates_str_t){ .ptr = nullptr, .size = 0 };
    return (gates_str_t){ .ptr = g_names[i], .size = g_sizes[i] };
}
