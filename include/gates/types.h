/* gates_gui_lib - foundation types.
 * Thin, zero-cost aliases over proven_c_lib. Core code includes no platform headers. */
#ifndef GATES_TYPES_H
#define GATES_TYPES_H

#include <proven/types.h>
#include <proven/error.h>
#include <proven/allocator.h>
#include <proven/u8str.h>

/* Fixed-width integers (identical to proven's; static-asserted in tests). */
typedef proven_i8   gates_i8;
typedef proven_i16  gates_i16;
typedef proven_i32  gates_i32;
typedef proven_i64  gates_i64;
typedef proven_u8   gates_u8;
typedef proven_u32  gates_u32;
typedef proven_u64  gates_u64;
/* Byte/memory sizes. (2D geometric size is gates_size_t in gates/geometry.h,
 * the layout's 2D size.) */
typedef proven_size_t gates_usize_t;

/* Result-style errors: gates errors ARE proven errors (cheap boundary). */
typedef proven_err_t gates_err_t;
#define GATES_OK PROVEN_OK

/* Every gates call returns GATES_OK or a proven error. */
static inline bool gates_is_ok(gates_err_t err) {
    return err == PROVEN_OK;
}

/* Borrowed UTF-8 string view. Borrowed views never imply ownership. */
typedef proven_u8str_view_t gates_str_t;
#define GATES_STR(lit) PROVEN_LIT(lit)
/* The same for static initializers (option tables): { .label = GATES_STR_INIT("x") }. */
#define GATES_STR_INIT(lit) PROVEN_LIT_INIT(lit)

/* gates_str_t is a proven string view: these convert for free. */
static inline gates_str_t gates_str_from_proven(proven_u8str_view_t v) { return v; }
static inline proven_u8str_view_t gates_str_to_proven(gates_str_t s) { return s; }

/* Allocator injection: gates uses the proven allocator trait everywhere. */
typedef proven_allocator_t gates_allocator_t;

/* Generation node handle. Never a raw pointer. */
typedef struct gates_node_t {
    gates_u32 index;
    gates_u32 generation;
} gates_node_t;

#define GATES_NODE_NULL ((gates_node_t){ UINT32_MAX, 0u })

/* The null handle (not whether a node is alive: gates_node_is_valid), and
 * handle equality (index and generation). */
static inline bool gates_node_is_null(gates_node_t n) {
    return n.index == UINT32_MAX;
}
static inline bool gates_node_eq(gates_node_t a, gates_node_t b) {
    return a.index == b.index && a.generation == b.generation;
}

#endif /* GATES_TYPES_H */
