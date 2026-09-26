/* gates_gui_lib - version (plan-0015). */
#include <gates/version.h>

gates_u32 gates_version(void) {
    return GATES_VERSION_NUMBER;
}

const char *gates_version_string(void) {
    return GATES_VERSION_STRING;
}
