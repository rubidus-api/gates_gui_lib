/* gates_gui_lib - version (plan-0015).
 *
 * Compatibility: source compatible within a minor version (0.2.x); rebuild the
 * program on every update. No binary ABI is promised yet - public structs may
 * change size between versions. A program can check at run time that the
 * library it links is the one its headers describe:
 *   if (gates_version() != GATES_VERSION_NUMBER) { ... }                        */
#ifndef GATES_VERSION_H
#define GATES_VERSION_H

#include <gates/types.h>

#define GATES_VERSION_MAJOR 0
#define GATES_VERSION_MINOR 7
#define GATES_VERSION_PATCH 0
#define GATES_VERSION_STRING "0.7.0"
/* major * 10000 + minor * 100 + patch */
#define GATES_VERSION_NUMBER (GATES_VERSION_MAJOR * 10000 + GATES_VERSION_MINOR * 100 + GATES_VERSION_PATCH)

/* The version the library was built as (GATES_VERSION_NUMBER of its headers). */
gates_u32 gates_version(void);
/* The same as text, e.g. "0.1.0" (static storage). */
const char *gates_version_string(void);

#endif /* GATES_VERSION_H */
