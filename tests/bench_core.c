/* bench_core - performance evidence for the platform-free core (plan-0017,
 * RFC-0003 section 13). Not a test: `make bench` builds it against the -O2
 * libraries and prints a report.
 *
 * Scenarios resemble the reference applications: a settings form, an
 * inspector table over 100 000 rows, a log of 2 000 lines, a window of 150
 * labels. For each: cold start (build + first frame), full layout, the paint
 * walk (draw list), software rendering at 96 and 144 dpi into a 1280 x 800
 * logical window (and at 288 dpi, a 3840 x 2400 pixel surface: the size a full
 * repaint grows with), and interaction cycles (a keystroke in a text box, a one-row
 * scroll) from input to rendered frame. Memory is split into UI-owned bytes
 * (the tree's allocator), model-owned bytes (the application's rows), frame
 * scratch (draw list and its text arena) and queue bytes (the sender under a
 * flood). Visible-row work counts model calls per frame. The post flood
 * measures latency from post to delivery with a real producer thread.
 * Times are wall clock (CLOCK_MONOTONIC), reported as median / p95 / max. */
#include <gates/gates.h>

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WIN_W 1280
#define WIN_H 800

static gates_u64 now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gates_u64)ts.tv_sec * 1000000000ull + (gates_u64)ts.tv_nsec;
}

/* -- a counting allocator: live bytes and peak ------------------------------------------------ */

typedef struct counter_t {
    gates_usize_t live, peak;
    gates_u64 allocs;
} counter_t;

#define HDR 16u

static proven_result_mem_mut_t c_alloc(void *ctx, proven_size_t size, proven_size_t align) {
    (void)align;
    counter_t *c = ctx;
    unsigned char *p = malloc(size + HDR);
    if (p == nullptr) return (proven_result_mem_mut_t){ .err = PROVEN_ERR_NOMEM };
    memcpy(p, &size, sizeof size);
    c->live += size;
    c->allocs++;
    if (c->live > c->peak) c->peak = c->live;
    return (proven_result_mem_mut_t){ .err = GATES_OK, .value = { .ptr = p + HDR, .size = size } };
}

static void c_free(void *ctx, void *ptr) {
    if (ptr == nullptr) return;
    counter_t *c = ctx;
    unsigned char *p = (unsigned char *)ptr - HDR;
    proven_size_t size;
    memcpy(&size, p, sizeof size);
    c->live -= size;
    free(p);
}

static proven_result_mem_mut_t c_realloc(void *ctx, void *old, proven_size_t old_size, proven_size_t new_size,
                                         proven_size_t align) {
    (void)old_size;
    proven_result_mem_mut_t r = c_alloc(ctx, new_size, align);
    if (!gates_is_ok(r.err) || old == nullptr) return r;
    proven_size_t prev;
    memcpy(&prev, (unsigned char *)old - HDR, sizeof prev);
    memcpy(r.value.ptr, old, prev < new_size ? prev : new_size);
    c_free(ctx, old);
    return r;
}

static gates_allocator_t counting(counter_t *c) {
    return (gates_allocator_t){ .ctx = c, .alloc_fn = c_alloc, .realloc_fn = c_realloc, .free_fn = c_free };
}

/* -- samples ----------------------------------------------------------------------------------- */

#define MAX_SAMPLES 4096
typedef struct samples_t {
    gates_u64 v[MAX_SAMPLES];
    int n;
} samples_t;

static int cmp_u64(const void *a, const void *b) {
    gates_u64 x = *(const gates_u64 *)a, y = *(const gates_u64 *)b;
    return x < y ? -1 : x > y;
}

static void add(samples_t *s, gates_u64 ns) {
    if (s->n < MAX_SAMPLES) s->v[s->n++] = ns;
}

static void report(const char *scenario, const char *what, samples_t *s) {
    if (s->n == 0) return;
    qsort(s->v, (size_t)s->n, sizeof s->v[0], cmp_u64);
    gates_u64 med = s->v[s->n / 2], p95 = s->v[(s->n * 95) / 100], max = s->v[s->n - 1];
    printf("%-10s %-22s median %9.3f ms   p95 %9.3f ms   max %9.3f ms   (n=%d)\n", scenario, what, med / 1e6,
           p95 / 1e6, max / 1e6, s->n);
    s->n = 0;
}

/* -- models ---------------------------------------------------------------------------------- */

typedef struct row_t {
    gates_item_id_t id;
    gates_u32 size;
    char name[24];
} row_t;

typedef struct rows_t {
    row_t *rows;
    gates_u64 n;
    gates_u64 cells;             /* model calls */
    char buf[48];
} rows_t;

static gates_u64 r_count(void *u) { return ((rows_t *)u)->n; }
static gates_item_id_t r_id_at(void *u, gates_u64 row) { return row < ((rows_t *)u)->n ? ((rows_t *)u)->rows[row].id : 0; }
static bool r_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    rows_t *r = u;
    if (id == 0 || id > r->n) return false;
    *row = id - 1; /* ids are row + 1 here */
    return true;
}
static gates_err_t r_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    rows_t *r = u;
    r->cells++;
    const row_t *w = &r->rows[id - 1];
    int k = col == 2 ? snprintf(r->buf, sizeof r->buf, "%u", w->size)
          : col == 3 ? snprintf(r->buf, sizeof r->buf, "2026-09-%02u", (unsigned)(id % 28 + 1))
          : snprintf(r->buf, sizeof r->buf, "%s", w->name);
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)r->buf, .size = (gates_usize_t)k };
    return GATES_OK;
}

/* -- scenarios ------------------------------------------------------------------------------ */

typedef struct scene_t {
    const char *name;
    gates_tree_t *tree;
    counter_t mem;
    rows_t rows;
    gates_node_t focus_box, view;
} scene_t;

static const gates_text_backend_t *be;

static void build_settings(scene_t *s) {
    gates_tree_t *t = s->tree;
    gates_node_t root = gates_tree_root(t), form, n;
    (void)gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    (void)gates_layout_set_padding(t, root, 12);
    (void)gates_layout_set_gap(t, root, 8);
    (void)gates_label_create(t, root, GATES_STR("Settings"), &n);
    (void)gates_form_create(t, root, &form);
    static const gates_option_t langs[] = { { .id = 1, .label = GATES_STR_INIT("English") },
                                            { .id = 2, .label = GATES_STR_INIT("Korean") } };
    for (gates_u32 f = 1; f <= 6; f++) {
        char label[16];
        int k = snprintf(label, sizeof label, "Field %u", f);
        gates_field_desc_t d = { .label = { .ptr = (const gates_u8 *)label, .size = (gates_usize_t)k },
                                 .help = GATES_STR("help text for this field"), .text = GATES_STR("value") };
        if (f == 5) {
            d = (gates_field_desc_t){ .label = GATES_STR("Language"), .options = langs, .option_count = 2, .selected_id = 1 };
            (void)gates_form_add_choice(t, form, f, &d, nullptr);
        } else if (f == 6) {
            d = (gates_field_desc_t){ .label = GATES_STR("News"), .text = GATES_STR("send me news") };
            (void)gates_form_add_checkbox(t, form, f, &d, nullptr);
        } else {
            (void)gates_form_add_text(t, form, f, &d, f == 1 ? &s->focus_box : nullptr);
        }
    }
    gates_node_t bar;
    (void)gates_panel_create(t, root, &bar);
    (void)gates_layout_set(t, bar, GATES_LAYOUT_KIND_ROW);
    (void)gates_button_create(t, bar, GATES_STR("Save"), nullptr, nullptr, &n);
    (void)gates_button_create(t, bar, GATES_STR("Cancel"), nullptr, nullptr, &n);
    (void)gates_label_create(t, root, GATES_STR("nothing saved yet"), &n);
}

static void build_inspector(scene_t *s) {
    gates_tree_t *t = s->tree;
    gates_node_t root = gates_tree_root(t), n;
    (void)gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    static const gates_column_desc_t cols[] = {
        { .id = 1, .label = GATES_STR_INIT("Id"), .width = 60 }, { .id = 2, .label = GATES_STR_INIT("Size"), .width = 90 },
        { .id = 3, .label = GATES_STR_INIT("Date"), .width = 110 }, { .id = 4, .label = GATES_STR_INIT("Name"), .width = 300 },
    };
    (void)gates_view_create(t, root, &(gates_view_desc_t){ .columns = cols, .column_count = 4, .header = true }, &s->view);
    (void)gates_layout_set_child_grow(t, s->view, 1);
    gates_rows_model_t m = { .user = &s->rows, .count = r_count, .id_at = r_id_at, .index_of = r_index_of, .cell = r_cell };
    (void)gates_view_set_model(t, s->view, &m);
    (void)gates_label_create(t, root, GATES_STR("100000 records"), &n);
}

static void build_log(scene_t *s) {
    gates_tree_t *t = s->tree;
    gates_node_t root = gates_tree_root(t);
    (void)gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    (void)gates_log_create(t, root, &(gates_log_desc_t){ .max_lines = 2000 }, &s->view);
    (void)gates_layout_set_child_grow(t, s->view, 1);
    for (int i = 0; i < 2000; i++) {
        char line[80];
        int k = snprintf(line, sizeof line, "12:00:%02d.%03d worker %d: processed batch %d of records", i % 60, i % 1000,
                         i % 7, i);
        (void)gates_log_append(t, s->view, (gates_str_t){ .ptr = (const gates_u8 *)line, .size = (gates_usize_t)k });
    }
}

static void build_text(scene_t *s) {
    gates_tree_t *t = s->tree;
    gates_node_t root = gates_tree_root(t), sc, n;
    (void)gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    (void)gates_panel_create(t, root, &sc);
    (void)gates_layout_set(t, sc, GATES_LAYOUT_KIND_SCROLL);
    (void)gates_layout_set_child_grow(t, sc, 1);
    for (int i = 0; i < 150; i++) {
        char line[96];
        int k = snprintf(line, sizeof line, "Label %03d - a line of ordinary interface text with some words", i);
        (void)gates_label_create(t, sc, (gates_str_t){ .ptr = (const gates_u8 *)line, .size = (gates_usize_t)k }, &n);
    }
}

static void scene_new(scene_t *s, const char *name, void (*build)(scene_t *)) {
    memset(&s->mem, 0, sizeof s->mem);
    s->name = name;
    s->focus_box = GATES_NODE_NULL;
    s->view = GATES_NODE_NULL;
    gates_tree_desc_t d = { .allocator = counting(&s->mem) };
    if (!gates_is_ok(gates_tree_create(&d, &s->tree))) exit(2);
    build(s);
}

/* -- frame pieces ---------------------------------------------------------------------------- */

typedef struct frame_t {
    gates_draw_list_t dl;
    gates_u32 *px96, *px144, *px288;
} frame_t;

static gates_pixels_t pixels(gates_u32 *buf, gates_u32 dpi) {
    gates_i32 w = gates_px(WIN_W, dpi), h = gates_px(WIN_H, dpi);
    return (gates_pixels_t){ .ptr = buf, .w = w, .h = h, .stride_bytes = (gates_u32)w * 4u };
}

static void layout(scene_t *s) {
    (void)gates_layout_run(s->tree, (gates_size_t){ WIN_W, WIN_H }, be);
}

static void paint(scene_t *s, frame_t *f) {
    gates_draw_list_reset(&f->dl);
    (void)gates_paint_tree(s->tree, &f->dl, gates_theme_light(), be);
}

static void render(frame_t *f, gates_u32 dpi) {
    (void)gates_render_soft_scaled(&f->dl, pixels(dpi == 96 ? f->px96 : dpi == 144 ? f->px144 : f->px288, dpi), be, dpi);
}

/* One interaction: input already applied; deliver, lay out if needed, paint, render. */
static void frame_after_input(scene_t *s, frame_t *f) {
    (void)gates_tree_dispatch_events(s->tree, 64);
    (void)gates_tree_flush_destroys(s->tree);
    if ((gates_tree_dirty(s->tree) & GATES_TREE_DIRTY_LAYOUT) != 0) layout(s);
    paint(s, f);
    render(f, 96);
    gates_tree_clear_dirty(s->tree, GATES_TREE_DIRTY_LAYOUT | GATES_TREE_DIRTY_PAINT);
}

static void run_scene(const char *name, void (*build)(scene_t *), frame_t *f, rows_t *rows, int iters) {
    samples_t s = {0};
    scene_t sc = {0};
    /* cold start: build, first layout, paint, render */
    for (int i = 0; i < 20; i++) {
        gates_u64 t0 = now_ns();
        scene_new(&sc, name, build);
        if (rows != nullptr) sc.rows = *rows;
        layout(&sc);
        paint(&sc, f);
        render(f, 96);
        add(&s, now_ns() - t0);
        if (i < 19) gates_tree_destroy(sc.tree);
    }
    if (rows != nullptr) { /* the model was bound before rows were set on the first pass: rebind */
        gates_tree_destroy(sc.tree);
        scene_new(&sc, name, build);
        sc.rows = *rows;
        layout(&sc);
    }
    report(name, "cold start (build+frame)", &s);
    for (int i = 0; i < iters; i++) {
        gates_u64 t0 = now_ns();
        (void)gates_layout_run(sc.tree, (gates_size_t){ WIN_W - (i & 1), WIN_H }, be);
        add(&s, now_ns() - t0);
    }
    report(name, "layout (full)", &s);
    layout(&sc);
    for (int i = 0; i < iters; i++) {
        gates_u64 t0 = now_ns();
        paint(&sc, f);
        add(&s, now_ns() - t0);
    }
    report(name, "paint walk", &s);
    for (int i = 0; i < iters; i++) {
        gates_u64 t0 = now_ns();
        render(f, 96);
        add(&s, now_ns() - t0);
    }
    report(name, "render 96 dpi", &s);
    for (int i = 0; i < iters; i++) {
        gates_u64 t0 = now_ns();
        render(f, 144);
        add(&s, now_ns() - t0);
    }
    report(name, "render 144 dpi", &s);
    for (int i = 0; i < iters; i++) {
        gates_u64 t0 = now_ns();
        render(f, 288);
        add(&s, now_ns() - t0);
    }
    report(name, "render 288 dpi (4K)", &s);
    printf("%-10s %-22s commands %u, draw list %zu bytes, text arena %u bytes\n", name, "frame scratch",
           gates_draw_list_len(&f->dl), (size_t)f->dl.cap * sizeof(gates_draw_cmd_t), f->dl.text_cap);
    printf("%-10s %-22s live %zu bytes, peak %zu bytes, %llu allocations\n", name, "UI-owned (tree)",
           (size_t)sc.mem.live, (size_t)sc.mem.peak, (unsigned long long)sc.mem.allocs);

    if (!gates_node_eq(sc.focus_box, GATES_NODE_NULL)) {
        gates_tree_set_focus(sc.tree, sc.focus_box);
        frame_after_input(&sc, f);
        for (int i = 0; i < iters; i++) {
            gates_u64 t0 = now_ns();
            (void)gates_input_char(sc.tree, (i & 1) ? 'b' : 'a');
            frame_after_input(&sc, f);
            add(&s, now_ns() - t0);
            gates_key_event_t bs = { .key = GATES_KEY_BACKSPACE, .down = true };
            if ((i % 16) == 15) {
                for (int k = 0; k < 16; k++) (void)gates_input_key(sc.tree, &bs);
                frame_after_input(&sc, f);
            }
        }
        report(name, "keystroke to frame", &s);
    }
    if (!gates_node_eq(sc.view, GATES_NODE_NULL) && rows != nullptr) {
        sc.rows.cells = 0;
        gates_u64 frames = 0;
        for (int i = 0; i < iters; i++) {
            gates_u64 t0 = now_ns();
            (void)gates_access_scroll_by(sc.tree, sc.view, (i & 64) ? -1 : 1, false);
            frame_after_input(&sc, f);
            add(&s, now_ns() - t0);
            frames++;
        }
        report(name, "scroll row to frame", &s);
        printf("%-10s %-22s %.1f model cell calls per frame (visible rows x columns)\n", name, "visible-row work",
               (double)sc.rows.cells / (double)frames);
    }
    gates_tree_destroy(sc.tree);
}

/* -- post flood ------------------------------------------------------------------------------ */

static gates_err_t px_init(void **lock) {
    pthread_mutex_t *m = malloc(sizeof *m);
    if (m == nullptr) return PROVEN_ERR_NOMEM;
    pthread_mutex_init(m, nullptr);
    *lock = m;
    return GATES_OK;
}
static void px_fini(void *lock) { pthread_mutex_destroy(lock); free(lock); }
static void px_lock(void *lock) { pthread_mutex_lock(lock); }
static void px_unlock(void *lock) { pthread_mutex_unlock(lock); }

#define FLOOD 200000
typedef struct flood_t {
    gates_sender_t *sender;
    gates_target_t target;
    atomic_int done;
    gates_u64 fulls;
} flood_t;

static gates_u64 g_stamp[FLOOD];

static void *producer(void *arg) {
    flood_t *fl = arg;
    for (int i = 0; i < FLOOD; i++) {
        gates_message_t m = { .target = fl->target, .kind = 1, .payload = (void *)(gates_usize_t)i, .bytes = 64 };
        g_stamp[i] = now_ns();
        while (gates_sender_post(fl->sender, &m) == GATES_POST_FULL) {
            fl->fulls++;
            sched_yield();
            g_stamp[i] = now_ns();
        }
    }
    atomic_store(&fl->done, 1);
    return nullptr;
}

static samples_t g_lat;
static int g_delivered;

static void on_msg(gates_tree_t *tree, gates_node_t node, gates_u32 kind, void *payload, gates_usize_t bytes, void *user) {
    (void)tree; (void)node; (void)kind; (void)bytes; (void)user;
    gates_usize_t i = (gates_usize_t)payload;
    if ((i % 50) == 0) add(&g_lat, now_ns() - g_stamp[i]);
    g_delivered++;
}

static void run_flood(void) {
    counter_t qmem = {0};
    gates_sender_desc_t d = { .allocator = counting(&qmem), .sync = { px_init, px_fini, px_lock, px_unlock } };
    flood_t fl = {0};
    if (!gates_is_ok(gates_sender_create(&d, &fl.sender))) exit(3);
    gates_tree_t *t = nullptr;
    (void)gates_tree_create(&(gates_tree_desc_t){0}, &t);
    gates_node_t n;
    (void)gates_label_create(t, gates_tree_root(t), GATES_STR("sink"), &n);
    (void)gates_node_set_message_handler(t, n, on_msg, nullptr);
    (void)gates_sender_attach(fl.sender, t);
    fl.target = gates_target(t, n);
    pthread_t th;
    pthread_create(&th, nullptr, producer, &fl);
    samples_t turn = {0};
    gates_usize_t peak_pending = 0;
    while (!atomic_load(&fl.done) || gates_sender_pending(fl.sender) > 0) {
        gates_usize_t pb = gates_sender_pending_bytes(fl.sender);
        if (pb > peak_pending) peak_pending = pb;
        gates_u64 t0 = now_ns();
        (void)gates_sender_dispatch(fl.sender, 64);
        if (turn.n < MAX_SAMPLES && (g_delivered % 97) == 0) add(&turn, now_ns() - t0);
    }
    pthread_join(th, nullptr);
    report("flood", "post-to-delivery", &g_lat);
    report("flood", "dispatch turn (64)", &turn);
    printf("%-10s %-22s %d delivered, %llu FULL answers, peak payload bytes %zu, queue allocator %zu bytes\n", "flood",
           "queue", g_delivered, (unsigned long long)fl.fulls, (size_t)peak_pending, (size_t)qmem.peak);
    gates_sender_close(fl.sender);
    gates_sender_release(fl.sender);
    gates_tree_destroy(t);
}

int main(int argc, char **argv) {
    int iters = argc > 1 ? atoi(argv[1]) : 200;
    be = gates_text_backend_builtin();
    frame_t f = {0};
    if (!gates_is_ok(gates_draw_list_init(&f.dl, (gates_allocator_t){0}, 0))) return 2;
    f.px96 = malloc((size_t)WIN_W * WIN_H * 4);
    f.px144 = malloc((size_t)gates_px(WIN_W, 144) * (size_t)gates_px(WIN_H, 144) * 4);
    f.px288 = malloc((size_t)gates_px(WIN_W, 288) * (size_t)gates_px(WIN_H, 288) * 4);
    if (f.px96 == nullptr || f.px144 == nullptr || f.px288 == nullptr) return 2;
    printf("gates %s core benchmark: %d iterations, window %dx%d logical, builtin text backend\n",
           gates_version_string(), iters, WIN_W, WIN_H);

    rows_t rows = { .n = 100000 };
    rows.rows = malloc((size_t)rows.n * sizeof *rows.rows);
    if (rows.rows == nullptr) return 2;
    for (gates_u64 i = 0; i < rows.n; i++) {
        rows.rows[i].id = i + 1;
        rows.rows[i].size = (gates_u32)(i * 2654435761u % 100000u);
        snprintf(rows.rows[i].name, sizeof rows.rows[i].name, "record-%06llu.dat", (unsigned long long)i);
    }
    run_scene("settings", build_settings, &f, nullptr, iters);
    run_scene("inspector", build_inspector, &f, &rows, iters);
    printf("%-10s %-22s %zu bytes (100000 rows kept by the application)\n", "inspector", "model-owned",
           (size_t)rows.n * sizeof *rows.rows);
    run_scene("log", build_log, &f, nullptr, iters);
    run_scene("text", build_text, &f, nullptr, iters);
    run_flood();
    free(rows.rows);
    free(f.px96);
    free(f.px144);
    free(f.px288);
    gates_draw_list_deinit(&f.dl);
    return 0;
}
