/* app_files - a folder browser (sample application, 0.1.0).
 *
 * Shows: a table over rows the application keeps, filled by a worker thread
 * that reads the folder and posts batches through the sender (the UI never
 * waits on the disk); a newer scan making older batches stale; sorting on a
 * header click; Enter or a double click opening a folder; commands for Up and
 * Refresh; a live status line.
 *
 * Use: type a folder and press Enter (or Go); Enter or a double click on a
 * folder opens it, Backspace (in the table), Alt+Up or Up goes to the parent
 * folder, F5 reads the folder again, a header click sorts (again: reverse).
 * Files are never opened or changed. Escape quits.
 *
 * Check: the window starts in your profile folder; folders come first;
 * clicking "Size" sorts by size; opening C:\Windows\System32 lists thousands
 * of entries while the window stays responsive; Up returns to C:\Windows. */
#include <gates/gates.h>

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) return e_; } while (0)

enum { COL_NAME = 1, COL_SIZE, COL_DATE, COL_TYPE };
enum { CMD_GO = 1, CMD_UP, CMD_REFRESH };
enum { MSG_BATCH = 1 };
enum { BATCH = 64, NAME_MAX_BYTES = 780 };

typedef struct entry_t {
    gates_item_id_t id;
    bool dir;
    unsigned long long size;
    unsigned long long mtime;    /* FILETIME ticks */
    char name[NAME_MAX_BYTES];
} entry_t;

/* One batch from the worker: `scan` says which scan it belongs to. */
typedef struct batch_t {
    gates_u32 scan;
    gates_u32 n;
    bool last;
    bool failed;
    entry_t e[BATCH];
} batch_t;

typedef struct scan_job_t {
    gates_sender_t *sender;
    gates_target_t target;
    gates_u32 scan;
    wchar_t path[MAX_PATH + 4];
} scan_job_t;

static _Atomic gates_u32 g_scan; /* the scan the UI wants; older workers stop */

typedef struct app_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t path, table, status;
    entry_t *rows;
    gates_u32 count, cap;
    gates_item_id_t next_id;
    gates_u64 rev;
    gates_column_id_t sort_col;
    bool descending;
    bool scanning;
    char folder[MAX_PATH * 3];
    char cell[64];
} app_t;

static app_t *g_sort;

/* -- the worker: reads a folder, posts batches ----------------------------------------- */

static void release_batch(void *payload, void *ctx) {
    (void)ctx;
    free(payload);
}

static bool post(scan_job_t *job, batch_t *b) {
    for (;;) {
        gates_message_t m = { .target = job->target, .kind = MSG_BATCH, .payload = b, .bytes = sizeof *b,
                              .release = release_batch };
        gates_err_t err = gates_sender_post(job->sender, &m);
        if (gates_is_ok(err)) return true;
        if (err != GATES_POST_FULL || atomic_load(&g_scan) != job->scan) {
            free(b); /* closed, or a newer scan: the batch is ours to drop */
            return false;
        }
        Sleep(2);
    }
}

static DWORD WINAPI scan_main(void *arg) {
    scan_job_t *job = arg;
    wchar_t pattern[MAX_PATH + 8];
    swprintf(pattern, MAX_PATH + 8, L"%ls\\*", job->path);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    batch_t *b = calloc(1, sizeof *b);
    bool alive = b != nullptr;
    if (alive) {
        b->scan = job->scan;
        b->failed = h == INVALID_HANDLE_VALUE;
    }
    if (alive && h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            if (atomic_load(&g_scan) != job->scan) {
                alive = false;
                break;
            }
            entry_t *e = &b->e[b->n++];
            e->dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e->size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            e->mtime = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            int n = WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, e->name, NAME_MAX_BYTES, nullptr, nullptr);
            if (n <= 0) snprintf(e->name, NAME_MAX_BYTES, "?");
            if (b->n == BATCH) {
                if (!post(job, b)) {
                    alive = false;
                    b = nullptr;
                    break;
                }
                b = calloc(1, sizeof *b);
                if (b == nullptr) {
                    alive = false;
                    break;
                }
                b->scan = job->scan;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    } else if (h != INVALID_HANDLE_VALUE) {
        FindClose(h);
    }
    if (alive && b != nullptr) {
        b->last = true;
        (void)post(job, b);
    } else {
        free(b);
    }
    gates_sender_release(job->sender);
    free(job);
    return 0;
}

/* -- the model -------------------------------------------------------------------------- */

static gates_u64 m_revision(void *u) { return ((app_t *)u)->rev; }
static gates_u64 m_count(void *u) { return ((app_t *)u)->count; }
static gates_item_id_t m_id_at(void *u, gates_u64 row) {
    app_t *a = u;
    return row < a->count ? a->rows[row].id : 0;
}
static bool m_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    app_t *a = u;
    for (gates_u32 i = 0; i < a->count; i++) {
        if (a->rows[i].id == id) {
            *row = i;
            return true;
        }
    }
    return false;
}

static void format_size(char *out, size_t cap, unsigned long long v) {
    static const char *const unit[] = { "bytes", "KB", "MB", "GB", "TB" };
    double d = (double)v;
    int u = 0;
    while (d >= 1024.0 && u < 4) {
        d /= 1024.0;
        u++;
    }
    if (u == 0) snprintf(out, cap, "%llu bytes", v);
    else snprintf(out, cap, "%.1f %s", d, unit[u]);
}

static gates_err_t m_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    app_t *a = u;
    gates_u64 row = 0;
    if (!m_index_of(a, id, &row)) return PROVEN_ERR_INVALID_ARG;
    const entry_t *e = &a->rows[row];
    const char *s = a->cell;
    if (col == COL_NAME) {
        s = e->name;
    } else if (col == COL_SIZE) {
        if (e->dir) a->cell[0] = '\0';
        else format_size(a->cell, sizeof a->cell, e->size);
    } else if (col == COL_DATE) {
        FILETIME ft = { (DWORD)e->mtime, (DWORD)(e->mtime >> 32) }, lt;
        SYSTEMTIME st;
        if (FileTimeToLocalFileTime(&ft, &lt) && FileTimeToSystemTime(&lt, &st)) {
            snprintf(a->cell, sizeof a->cell, "%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        } else {
            a->cell[0] = '\0';
        }
    } else {
        const char *dot = strrchr(e->name, '.');
        if (e->dir) snprintf(a->cell, sizeof a->cell, "folder");
        else if (dot != nullptr && dot != e->name && strlen(dot + 1) < 16) snprintf(a->cell, sizeof a->cell, "%s file", dot + 1);
        else snprintf(a->cell, sizeof a->cell, "file");
    }
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) };
    return GATES_OK;
}

/* Folders first; then the chosen column. */
static int compare(const void *x, const void *y) {
    const entry_t *p = x, *q = y;
    if (p->dir != q->dir) return p->dir ? -1 : 1;
    int r = 0;
    if (g_sort->sort_col == COL_SIZE) r = p->size < q->size ? -1 : p->size > q->size ? 1 : 0;
    else if (g_sort->sort_col == COL_DATE) r = p->mtime < q->mtime ? -1 : p->mtime > q->mtime ? 1 : 0;
    if (r == 0) r = _stricmp(p->name, q->name);
    return g_sort->descending ? -r : r;
}

static void sort_rows(app_t *a) {
    g_sort = a;
    qsort(a->rows, a->count, sizeof *a->rows, compare);
}

/* -- scans and status ------------------------------------------------------------------- */

static void set_status(app_t *a, const char *s) {
    (void)gates_widget_set_text(a->tree, a->status, (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) });
}

static void show_summary(app_t *a) {
    gates_u32 dirs = 0;
    unsigned long long bytes = 0;
    for (gates_u32 i = 0; i < a->count; i++) {
        if (a->rows[i].dir) dirs++;
        else bytes += a->rows[i].size;
    }
    char size[32], buf[128];
    format_size(size, sizeof size, bytes);
    snprintf(buf, sizeof buf, "%u folder%s, %u file%s, %s%s", dirs, dirs == 1 ? "" : "s", a->count - dirs,
             a->count - dirs == 1 ? "" : "s", size, a->scanning ? " - reading..." : "");
    set_status(a, buf);
}

static void start_scan(app_t *a, const char *folder) {
    scan_job_t *job = calloc(1, sizeof *job);
    if (job == nullptr || MultiByteToWideChar(CP_UTF8, 0, folder, -1, job->path, MAX_PATH) <= 0) {
        free(job);
        set_status(a, "That folder name is too long.");
        return;
    }
    size_t n = wcslen(job->path);
    while (n > 3 && (job->path[n - 1] == L'\\' || job->path[n - 1] == L'/')) job->path[--n] = L'\0';
    snprintf(a->folder, sizeof a->folder, "%s", folder);
    (void)gates_textbox_set_text(a->tree, a->path, (gates_str_t){ .ptr = (const gates_u8 *)a->folder, .size = strlen(a->folder) });
    job->scan = atomic_fetch_add(&g_scan, 1u) + 1u;
    job->target = gates_target(a->tree, a->table);
    if (!gates_is_ok(gates_app_sender(a->app, &job->sender))) {
        free(job);
        return;
    }
    a->count = 0;
    a->scanning = true;
    a->rev++;
    (void)gates_view_model_changed(a->tree, a->table);
    show_summary(a);
    HANDLE th = CreateThread(nullptr, 0, scan_main, job, 0, nullptr);
    if (th == nullptr) {
        gates_sender_release(job->sender);
        free(job);
        a->scanning = false;
        set_status(a, "Could not start reading the folder.");
        return;
    }
    CloseHandle(th); /* the worker releases its sender and job itself */
}

static void on_batch(gates_tree_t *tree, gates_node_t node, gates_u32 kind, void *payload, gates_usize_t bytes,
                     void *user) {
    (void)tree;
    (void)node;
    (void)bytes;
    app_t *a = user;
    const batch_t *b = payload;
    if (kind != MSG_BATCH || b->scan != atomic_load(&g_scan)) return; /* an older scan */
    if (a->count + b->n > a->cap) {
        gates_u32 cap = a->cap != 0 ? a->cap * 2 : 1024;
        while (cap < a->count + b->n) cap *= 2;
        entry_t *grown = realloc(a->rows, (size_t)cap * sizeof *grown);
        if (grown == nullptr) {
            set_status(a, "Out of memory: the listing is incomplete.");
            return;
        }
        a->rows = grown;
        a->cap = cap;
    }
    for (gates_u32 i = 0; i < b->n; i++) {
        a->rows[a->count] = b->e[i];
        a->rows[a->count].id = a->next_id++;
        a->count++;
    }
    if (b->last) a->scanning = false;
    sort_rows(a);
    a->rev++;
    (void)gates_view_model_changed(a->tree, a->table);
    if (b->failed) set_status(a, "Cannot read that folder.");
    else show_summary(a);
}

/* -- navigation -------------------------------------------------------------------------- */

static void open_child(app_t *a, const char *name) {
    char next[MAX_PATH * 3];
    size_t n = strlen(a->folder);
    bool sep = n > 0 && (a->folder[n - 1] == '\\' || a->folder[n - 1] == '/');
    if (snprintf(next, sizeof next, "%s%s%s", a->folder, sep ? "" : "\\", name) >= (int)sizeof next) return;
    start_scan(a, next);
}

static void go_up(app_t *a) {
    char up[MAX_PATH * 3];
    snprintf(up, sizeof up, "%s", a->folder);
    size_t n = strlen(up);
    while (n > 0 && (up[n - 1] == '\\' || up[n - 1] == '/')) up[--n] = '\0';
    char *cut = strrchr(up, '\\');
    if (cut == nullptr || n <= 3) return; /* a drive root has no parent */
    if (cut == up + 2) cut[1] = '\0';     /* "C:\" stays a root */
    else *cut = '\0';
    start_scan(a, up);
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    if (id == CMD_GO) {
        gates_str_t p = gates_textbox_text(tree, a->path);
        char buf[MAX_PATH * 3];
        snprintf(buf, sizeof buf, "%.*s", (int)p.size, (const char *)p.ptr);
        start_scan(a, buf);
        gates_tree_set_focus(tree, a->table);
    } else if (id == CMD_UP) {
        go_up(a);
    } else if (id == CMD_REFRESH) {
        char buf[MAX_PATH * 3];
        snprintf(buf, sizeof buf, "%s", a->folder);
        start_scan(a, buf);
    }
}

static void on_table(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    app_t *a = user;
    if (ev->kind == GATES_EVENT_SORT_REQUESTED) {
        a->descending = a->sort_col == ev->result ? !a->descending : false;
        a->sort_col = ev->result;
        sort_rows(a);
        a->rev++;
        (void)gates_view_model_changed(a->tree, a->table);
    } else if (ev->kind == GATES_EVENT_ACTIVATED) {
        gates_u64 row = 0;
        if (!m_index_of(a, ev->item, &row)) return;
        if (a->rows[row].dir) {
            char name[NAME_MAX_BYTES];
            snprintf(name, sizeof name, "%s", a->rows[row].name);
            open_child(a, name);
        } else {
            char size[32], buf[NAME_MAX_BYTES + 96];
            format_size(size, sizeof size, a->rows[row].size);
            snprintf(buf, sizeof buf, "%s - %s (files are not opened by this sample)", a->rows[row].name, size);
            set_status(a, buf);
        }
    }
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    app_t *a = user;
    if (!ev->down) return;
    if (ev->key == GATES_KEY_ESCAPE) gates_app_quit(a->app);
    else if (ev->key == GATES_KEY_UP && ev->alt) go_up(a);
    else if (ev->key == GATES_KEY_BACKSPACE && gates_node_eq(gates_tree_focus(a->tree), a->table)) go_up(a);
}

/* -- building ---------------------------------------------------------------------------- */

static gates_err_t command(app_t *a, gates_node_t bar, gates_command_id_t id, const char *label, gates_shortcut_t key,
                           gates_command_role_t role) {
    gates_command_desc_t c = { .id = id, .label = { .ptr = (const gates_u8 *)label, .size = strlen(label) },
                               .shortcut = key, .role = role, .enabled = true, .invoke = on_command, .user = a };
    gates_node_t root = gates_tree_root(a->tree), b;
    TRY(gates_command_register(a->tree, root, &c));
    TRY(gates_button_create(a->tree, bar, GATES_STR(""), nullptr, nullptr, &b));
    return gates_button_set_command(a->tree, b, root, id);
}

static gates_err_t build(app_t *a) {
    gates_tree_t *t = a->tree;
    gates_node_t root = gates_tree_root(t), label, row;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 12));
    TRY(gates_layout_set_gap(t, root, 8));
    TRY(gates_label_create(t, root, GATES_STR("Folder"), &label));
    TRY(gates_panel_create(t, root, &row));
    TRY(gates_layout_set(t, row, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, row, 8));
    TRY(gates_textbox_create(t, row, GATES_STR(""), 40, &a->path));
    TRY(gates_node_set_labelled_by(t, a->path, label));
    TRY(gates_layout_set_child_grow(t, a->path, 1));
    TRY(command(a, row, CMD_GO, "Go", (gates_shortcut_t){0}, GATES_COMMAND_DEFAULT));
    /* Shortcuts are Ctrl combinations or function keys; Backspace is handled in on_key. */
    TRY(command(a, row, CMD_UP, "Up (Backspace)", (gates_shortcut_t){0}, GATES_COMMAND_NORMAL));
    TRY(command(a, row, CMD_REFRESH, "Refresh (F5)", (gates_shortcut_t){ .key = GATES_KEY_F5 }, GATES_COMMAND_NORMAL));

    static const gates_column_desc_t cols[] = {
        { .id = COL_NAME, .label = GATES_STR_INIT("Name"), .width = 260, .min_width = 80 },
        { .id = COL_SIZE, .label = GATES_STR_INIT("Size"), .width = 100, .min_width = 50 },
        { .id = COL_DATE, .label = GATES_STR_INIT("Modified"), .width = 140, .min_width = 60 },
        { .id = COL_TYPE, .label = GATES_STR_INIT("Type"), .width = 100, .min_width = 50 },
    };
    TRY(gates_view_create(t, root, &(gates_view_desc_t){ .columns = cols, .column_count = 4, .header = true }, &a->table));
    TRY(gates_node_set_access_name(t, a->table, GATES_STR("Folder contents")));
    TRY(gates_layout_set_child_grow(t, a->table, 1));
    gates_rows_model_t model = { .user = a, .revision = m_revision, .count = m_count, .id_at = m_id_at,
                                 .index_of = m_index_of, .cell = m_cell };
    TRY(gates_view_set_model(t, a->table, &model));
    TRY(gates_widget_set_handler(t, a->table, on_table, a));
    TRY(gates_node_set_message_handler(t, a->table, on_batch, a));

    TRY(gates_label_create(t, root, GATES_STR(" "), &a->status));
    TRY(gates_node_set_live(t, a->status, GATES_LIVE_POLITE));
    a->sort_col = COL_NAME;
    const char *home = getenv("USERPROFILE");
    start_scan(a, home != nullptr ? home : "C:\\");
    gates_tree_set_focus(t, a->table);
    return GATES_OK;
}

int main(void) {
    static app_t a = { .next_id = 1 };
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &a.app))) return 1;
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &a };
    gates_window_desc_t desc = { .title = GATES_STR("Files"), .size = { 680, 500 } };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(a.app, &desc, &cb, &win))) {
        gates_app_destroy(a.app);
        return 1;
    }
    a.tree = gates_window_tree(win);
    gates_err_t err = build(&a);
    if (gates_is_ok(err)) err = gates_app_run(a.app);
    atomic_fetch_add(&g_scan, 1u); /* any running worker stops */
    gates_window_destroy(win);
    gates_app_destroy(a.app);      /* closes the sender: workers see CLOSED */
    free(a.rows);
    return gates_is_ok(err) ? 0 : 1;
}
