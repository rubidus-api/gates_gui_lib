/* T004: draw list (docs/tests/cases/T004-draw-list.md). */
#include <gates/draw.h>
#include <proven/heap.h>
#include "gates_test.h"

/* Failing allocator: passes through until `fail_after` calls, then fails —
 * proves push/growth failure-atomicity. */
typedef struct {
    gates_allocator_t inner;
    int calls;
    int fail_after; /* fail when calls >= fail_after; -1 = never */
} flaky_alloc_t;

static bool flaky_should_fail(flaky_alloc_t *f) {
    f->calls++;
    return f->fail_after >= 0 && f->calls > f->fail_after;
}

static proven_result_mem_mut_t flaky_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    flaky_alloc_t *f = ctx;
    if (flaky_should_fail(f)) {
        return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    }
    return f->inner.alloc_fn(f->inner.ctx, size, align);
}

static proven_result_mem_mut_t flaky_realloc(void *ctx, void *old_ptr, proven_size_t old_size,
                                             proven_size_t new_size, proven_size_t align) {
    flaky_alloc_t *f = ctx;
    if (flaky_should_fail(f)) {
        return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    }
    return f->inner.realloc_fn(f->inner.ctx, old_ptr, old_size, new_size, align);
}

static void flaky_free(void *ctx, void *ptr) {
    flaky_alloc_t *f = ctx;
    f->inner.free_fn(f->inner.ctx, ptr);
}

static gates_allocator_t flaky_as_allocator(flaky_alloc_t *f) {
    return (gates_allocator_t){ .ctx = f, .alloc_fn = flaky_alloc,
                                .realloc_fn = flaky_realloc, .free_fn = flaky_free };
}

static void test_encode_each_command(void) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));

    gates_rect_t r = { 1, 2, 30, 40 };
    gates_color_t c = GATES_RGBA(10, 20, 30, 200);
    GT_ASSERT_OK(gates_draw_rect(&dl, r, c));
    GT_ASSERT_OK(gates_draw_border(&dl, r, 3, c));
    GT_ASSERT_OK(gates_draw_line(&dl, (gates_point_t){ 5, 6 }, (gates_point_t){ 7, 8 }, c));
    GT_ASSERT_OK(gates_draw_clip_push(&dl, r));
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT(gates_draw_list_len(&dl) == 5);

    const gates_draw_cmd_t *cmd = gates_draw_list_at(&dl, 0);
    GT_ASSERT(cmd != nullptr && cmd->kind == GATES_DRAW_RECT);
    GT_ASSERT(cmd->rect.x == 1 && cmd->rect.w == 30);
    GT_ASSERT(cmd->color.r == 10 && cmd->color.a == 200);

    cmd = gates_draw_list_at(&dl, 1);
    GT_ASSERT(cmd != nullptr && cmd->kind == GATES_DRAW_BORDER && cmd->thickness == 3);

    cmd = gates_draw_list_at(&dl, 2);
    GT_ASSERT(cmd != nullptr && cmd->kind == GATES_DRAW_LINE);
    GT_ASSERT(cmd->p0.x == 5 && cmd->p1.y == 8);

    GT_ASSERT(gates_draw_list_at(&dl, 2)->kind == GATES_DRAW_LINE);
    GT_ASSERT(gates_draw_list_at(&dl, 3)->kind == GATES_DRAW_CLIP_PUSH);
    GT_ASSERT(gates_draw_list_at(&dl, 4)->kind == GATES_DRAW_CLIP_POP);
    GT_ASSERT(gates_draw_list_at(&dl, 5) == nullptr); /* out of range */

    gates_draw_list_deinit(&dl);
}

static void test_clip_balance(void) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_rect_t r = { 0, 0, 10, 10 };

    GT_ASSERT(gates_draw_list_balanced(&dl));
    /* Pop with empty stack is rejected and encodes nothing. */
    GT_ASSERT_ERR(gates_draw_clip_pop(&dl));
    GT_ASSERT(gates_draw_list_len(&dl) == 0);

    GT_ASSERT_OK(gates_draw_clip_push(&dl, r));
    GT_ASSERT_OK(gates_draw_clip_push(&dl, r));
    GT_ASSERT(!gates_draw_list_balanced(&dl));
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT_OK(gates_draw_clip_pop(&dl));
    GT_ASSERT(gates_draw_list_balanced(&dl));
    GT_ASSERT_ERR(gates_draw_clip_pop(&dl));

    gates_draw_list_deinit(&dl);
}

static void test_growth_beyond_initial_capacity(void) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 2));
    gates_rect_t r = { 0, 0, 1, 1 };
    for (int i = 0; i < 500; i++) {
        GT_ASSERT_OK(gates_draw_rect(&dl, r, GATES_RGB(1, 2, 3)));
    }
    GT_ASSERT(gates_draw_list_len(&dl) == 500);
    GT_ASSERT(dl.cap >= 500);
    /* Every command intact after multiple grows. */
    for (gates_u32 i = 0; i < 500; i++) {
        GT_ASSERT(gates_draw_list_at(&dl, i)->kind == GATES_DRAW_RECT);
    }
    gates_draw_list_deinit(&dl);
}

static void test_growth_failure_atomic(void) {
    flaky_alloc_t f = { .inner = proven_heap_allocator(), .fail_after = 1 };
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, flaky_as_allocator(&f), 2)); /* call 1 ok */

    gates_rect_t r = { 0, 0, 1, 1 };
    GT_ASSERT_OK(gates_draw_rect(&dl, r, GATES_RGB(1, 1, 1)));
    GT_ASSERT_OK(gates_draw_rect(&dl, r, GATES_RGB(2, 2, 2)));
    /* Third push needs growth -> allocator fails -> list unchanged. */
    GT_ASSERT_ERR(gates_draw_rect(&dl, r, GATES_RGB(3, 3, 3)));
    GT_ASSERT(gates_draw_list_len(&dl) == 2);
    GT_ASSERT(gates_draw_list_at(&dl, 1)->color.r == 2);

    /* Failed clip push must not change the depth. */
    GT_ASSERT_ERR(gates_draw_clip_push(&dl, r));
    GT_ASSERT(gates_draw_list_balanced(&dl));

    gates_draw_list_deinit(&dl);
}

static void test_reset_reuse(void) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    gates_rect_t r = { 0, 0, 5, 5 };
    GT_ASSERT_OK(gates_draw_clip_push(&dl, r));
    GT_ASSERT_OK(gates_draw_rect(&dl, r, GATES_RGB(9, 9, 9)));

    gates_draw_list_reset(&dl);
    GT_ASSERT(gates_draw_list_len(&dl) == 0);
    GT_ASSERT(gates_draw_list_balanced(&dl)); /* dangling depth cleared */
    GT_ASSERT_OK(gates_draw_rect(&dl, r, GATES_RGB(1, 1, 1)));
    GT_ASSERT(gates_draw_list_len(&dl) == 1);

    gates_draw_list_deinit(&dl);
}

static void test_bad_args(void) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));
    GT_ASSERT_ERR(gates_draw_border(&dl, (gates_rect_t){ 0, 0, 5, 5 }, 0, GATES_RGB(0, 0, 0)));
    GT_ASSERT_ERR(gates_draw_list_init(nullptr, (gates_allocator_t){0}, 0));
    gates_draw_list_deinit(&dl);
}

static void test_text_command_copies(void) {
    gates_draw_list_t dl;
    GT_ASSERT_OK(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0));

    char mutable_text[] = "hello";
    gates_str_t s = { .ptr = (const proven_byte_t *)mutable_text, .size = 5 };
    GT_ASSERT_OK(gates_draw_text(&dl, (gates_rect_t){ 1, 2, 40, 16 }, s, 0,
                                 GATES_RGB(1, 2, 3)));
    /* The list owns a copy: mutating the source must not affect the command. */
    mutable_text[0] = 'X';

    const gates_draw_cmd_t *cmd = gates_draw_list_at(&dl, 0);
    GT_ASSERT(cmd != nullptr && cmd->kind == GATES_DRAW_TEXT);
    gates_str_t back = gates_draw_cmd_text(&dl, cmd);
    GT_ASSERT(back.size == 5);
    GT_ASSERT(back.ptr[0] == 'h');

    /* Multiple texts pack into the arena at distinct offsets. */
    GT_ASSERT_OK(gates_draw_text(&dl, (gates_rect_t){0}, GATES_STR("world!"), 0,
                                 GATES_RGB(0, 0, 0)));
    gates_str_t b2 = gates_draw_cmd_text(&dl, gates_draw_list_at(&dl, 1));
    GT_ASSERT(b2.size == 6 && b2.ptr[0] == 'w');
    GT_ASSERT(back.ptr + 5 <= b2.ptr);

    /* reset reclaims the arena. */
    gates_draw_list_reset(&dl);
    GT_ASSERT(dl.text_len == 0);
    GT_ASSERT_OK(gates_draw_text(&dl, (gates_rect_t){0}, GATES_STR("z"), 0,
                                 GATES_RGB(0, 0, 0)));
    GT_ASSERT(gates_draw_list_at(&dl, 0)->text_offset == 0);

    /* Empty text is legal (measures to nothing, renders nothing). */
    GT_ASSERT_OK(gates_draw_text(&dl, (gates_rect_t){0}, (gates_str_t){0}, 0,
                                 GATES_RGB(0, 0, 0)));

    gates_draw_list_deinit(&dl);
}

int main(void) {
    test_encode_each_command();
    test_clip_balance();
    test_growth_beyond_initial_capacity();
    test_growth_failure_atomic();
    test_reset_reuse();
    test_bad_args();
    test_text_command_copies();
    return gt_report("test_draw_list");
}
