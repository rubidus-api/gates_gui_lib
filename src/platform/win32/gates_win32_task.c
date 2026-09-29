/* gates_gui_lib - threads for background tasks on Windows (plan-0021): the
 * core starts, joins and yields through these; each window's tree gets them. */
#include "gates_win32_internal.h"

typedef struct thread_box_t {
    HANDLE handle;
    void (*fn)(void *);
    void *arg;
} thread_box_t;

static DWORD WINAPI run_box(LPVOID p) {
    thread_box_t *b = (thread_box_t *)p;
    b->fn(b->arg);
    return 0;
}

static gates_err_t start(void *ctx, void (*fn)(void *), void *arg, void **out_thread) {
    (void)ctx;
    thread_box_t *b = (thread_box_t *)HeapAlloc(GetProcessHeap(), 0, sizeof *b);
    if (b == nullptr) return PROVEN_ERR_NOMEM;
    b->fn = fn;
    b->arg = arg;
    b->handle = CreateThread(nullptr, 0, run_box, b, 0, nullptr);
    if (b->handle == nullptr) {
        HeapFree(GetProcessHeap(), 0, b);
        return PROVEN_ERR_IO;
    }
    *out_thread = b;
    return GATES_OK;
}

static void join(void *ctx, void *thread) {
    (void)ctx;
    thread_box_t *b = (thread_box_t *)thread;
    WaitForSingleObject(b->handle, INFINITE);
    CloseHandle(b->handle);
    HeapFree(GetProcessHeap(), 0, b);
}

static void yield(void *ctx) {
    (void)ctx;
    Sleep(1); /* waiting for queue room: let the UI thread drain it */
}

void gates_win32_install_threads(gates_window_t *win) {
    gates_threads_t t = { .start = start, .join = join, .yield = yield };
    gates_tree_set_threads(win->tree, &t);
}
