/* gates_gui_lib - Win32 app lifecycle + message pump. */
#include "gates_win32_internal.h"
#include <gates/encoding.h>
#include <proven/heap.h>

#include <string.h>

static const wchar_t *GATES_WNDCLASS_NAME = L"gates_gui_lib_window";
static const wchar_t *GATES_POSTCLASS_NAME = L"gates_gui_lib_post";

/* -- posting: the platform half ------------------------------------------ */

/* SRWLOCK behind the core's sync interface; it lives inside the sender, so it
 * outlives the app when workers still hold references. */
static gates_err_t srw_init(void **lock) {
    SRWLOCK *l = (SRWLOCK *)HeapAlloc(GetProcessHeap(), 0, sizeof(SRWLOCK));
    if (l == nullptr) return PROVEN_ERR_NOMEM;
    InitializeSRWLock(l);
    *lock = l;
    return GATES_OK;
}
static void srw_fini(void *lock) { HeapFree(GetProcessHeap(), 0, lock); }
static void srw_lock(void *lock) { AcquireSRWLockExclusive((SRWLOCK *)lock); }
static void srw_unlock(void *lock) { ReleaseSRWLockExclusive((SRWLOCK *)lock); }

/* Called by a poster with the sender lock held, only while the app is open. */
static void post_wake(void *ctx) {
    gates_app_t *app = ctx;
    (void)PostMessageW(app->post_hwnd, GATES_WM_POST, 0, 0);
}

/* A message-only window, not a thread message: modal loops (moving, sizing a
 * window, menus) keep dispatching window messages, so posts are not lost. */
static LRESULT CALLBACK post_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    gates_app_t *app = (gates_app_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    bool turn = msg == GATES_WM_POST || (msg == WM_TIMER && wparam == GATES_WIN32_POST_TIMER_ID);
    if (turn && gates_win32_perf_on()) gates_win32_perf_log("wake,%llu,post", (unsigned long long)gates_win32_perf_now_us());
    if (turn && app != nullptr && app->sender != nullptr) {
        if (msg == WM_TIMER) {
            KillTimer(hwnd, GATES_WIN32_POST_TIMER_ID);
            app->post_pending = false;
        } else if (HIWORD(GetQueueStatus(QS_INPUT | QS_PAINT)) != 0) {
            /* A wake while input or painting waits: let them go first. */
            if (!app->post_pending) {
                app->post_pending =
                    SetTimer(hwnd, GATES_WIN32_POST_TIMER_ID, USER_TIMER_MINIMUM, nullptr) != 0;
            }
            return 0;
        }
        gates_u32 left = gates_sender_dispatch(app->sender, GATES_POST_PER_TURN);
        /* Handlers changed trees: events, destroys, repaint for every window. */
        for (gates_window_t *w = app->windows; w != nullptr; w = w->next_window) {
            gates_win32_after_input(w); /* timer changes re-arm through the clock callback */
        }
        /* The rest waits for a timer, not a posted message: GetMessage returns
         * posted messages before input and never builds WM_PAINT while any are
         * queued, so re-posting under a flooding producer starved input and
         * painting (found in a field run). WM_TIMER comes after both. */
        if (left > 0 && !app->post_pending) {
            app->post_pending =
                SetTimer(hwnd, GATES_WIN32_POST_TIMER_ID, USER_TIMER_MINIMUM, nullptr) != 0;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

/* -- DPI --------------------------------------------------------------- */

gates_u32 gates_win32_dpi_for_window(HWND hwnd) {
    typedef UINT (WINAPI *dpi_fn)(HWND);
    static dpi_fn fn;
    static bool looked;
    if (!looked) {
        fn = (dpi_fn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        looked = true;
    }
    UINT d = fn != nullptr && hwnd != nullptr ? fn(hwnd) : 0;
    return d != 0 ? (gates_u32)d : gates_win32_system_dpi();
}

gates_u32 gates_win32_system_dpi(void) {
    HDC dc = GetDC(nullptr);
    int d = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 0;
    if (dc != nullptr) ReleaseDC(nullptr, dc);
    return d > 0 ? (gates_u32)d : GATES_DPI_BASE;
}

gates_err_t gates_app_sender(gates_app_t *app, gates_sender_t **out_sender) {
    if (app == nullptr || out_sender == nullptr || app->sender == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_sender_retain(app->sender);
    *out_sender = app->sender;
    return GATES_OK;
}

/* A UTF-8 face name in UTF-16 (empty stays empty); false when it does not fit. */
static bool face_name(gates_str_t name, wchar_t out[LF_FACESIZE]) {
    out[0] = L'\0';
    if (name.size == 0) return true;
    if (name.ptr == nullptr || name.size >= 4 * LF_FACESIZE) return false;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)name.ptr, (int)name.size, out,
                                LF_FACESIZE - 1);
    if (n <= 0) return false;
    out[n] = L'\0';
    return true;
}

gates_err_t gates_app_create(const gates_app_desc_t *desc, gates_app_t **out_app) {
    if (out_app == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_app = nullptr;
    if (!gates_encoding_has_codepage_converter()) {
        gates_encoding_set_codepage_converter(gates_codepage_converter_win32()); /* 0.10.0 */
    }

    gates_allocator_t alloc = (desc != nullptr && proven_alloc_is_valid(desc->allocator))
                                  ? desc->allocator
                                  : proven_heap_allocator();

    proven_result_mem_mut_t res = alloc.alloc_fn(alloc.ctx, sizeof(gates_app_t),
                                                 alignof(gates_app_t));
    if (!proven_is_ok(res.err)) {
        return res.err;
    }
    gates_app_t *app = (gates_app_t *)res.value.ptr;
    memset(app, 0, sizeof *app);
    app->alloc = alloc;
    app->hinstance = GetModuleHandleW(nullptr);

    /* The program's UI face (0.12.0); empty keeps the system message font. */
    wchar_t face[LF_FACESIZE] = L"";
    if (desc != nullptr) (void)face_name(desc->ui_font, face);
    (void)gates_win32_text_set_ui_face(face);

    /* Per-monitor DPI v2: process-wide and one-shot, so before any
     * window exists. Older Windows: system-aware as before. */
    typedef BOOL (WINAPI *set_ctx_fn)(HANDLE);
    set_ctx_fn set_ctx = (set_ctx_fn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                            "SetProcessDpiAwarenessContext");
    if (set_ctx == nullptr || !set_ctx((HANDLE)(INT_PTR)-4 /* PER_MONITOR_AWARE_V2 */)) {
        SetProcessDPIAware();
    }

    WNDCLASSEXW wc = {
        .cbSize = sizeof wc,
        .style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, /* double clicks for views */
        .lpfnWndProc = gates_win32_wndproc,
        .hInstance = app->hinstance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .lpszClassName = GATES_WNDCLASS_NAME,
    };
    app->window_class = RegisterClassExW(&wc);
    if (app->window_class == 0) {
        alloc.free_fn(alloc.ctx, app);
        return PROVEN_ERR_INVALID_STATE;
    }

    WNDCLASSEXW pc = {
        .cbSize = sizeof pc,
        .lpfnWndProc = post_wndproc,
        .hInstance = app->hinstance,
        .lpszClassName = GATES_POSTCLASS_NAME,
    };
    app->post_class = RegisterClassExW(&pc);
    if (app->post_class != 0) {
        app->post_hwnd = CreateWindowExW(0, GATES_POSTCLASS_NAME, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                         nullptr, app->hinstance, app);
    }
    gates_err_t err = app->post_hwnd != nullptr ? GATES_OK : PROVEN_ERR_INVALID_STATE;
    if (gates_is_ok(err)) {
        gates_sender_desc_t sd = {
            .allocator = alloc,
            .sync = { srw_init, srw_fini, srw_lock, srw_unlock },
            .wake = post_wake,
            .wake_ctx = app,
            .max_messages = desc != nullptr ? desc->post_max_messages : 0,
            .max_bytes = desc != nullptr ? desc->post_max_bytes : 0,
        };
        err = gates_sender_create(&sd, &app->sender);
    }
    if (!gates_is_ok(err)) {
        if (app->post_hwnd != nullptr) DestroyWindow(app->post_hwnd);
        if (app->post_class != 0) UnregisterClassW(GATES_POSTCLASS_NAME, app->hinstance);
        UnregisterClassW(GATES_WNDCLASS_NAME, app->hinstance);
        alloc.free_fn(alloc.ctx, app);
        return err;
    }

    /* UI Automation calls providers through COM on this thread:
     * an apartment of its own. An application that already chose the
     * multithreaded apartment keeps it; the providers then refuse calls that
     * arrive on other threads. */
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    app->com_init = SUCCEEDED(hr);

    *out_app = app;
    return GATES_OK;
}

void gates_app_destroy(gates_app_t *app) {
    if (app == nullptr) {
        return;
    }
    /* Close first (no wake can reach the window after this), then the window,
     * then the app's reference; workers may keep the sender alive a while. */
    gates_sender_close(app->sender);
    if (app->post_hwnd != nullptr) {
        DestroyWindow(app->post_hwnd);
    }
    gates_sender_release(app->sender);
    if (app->post_class != 0) {
        UnregisterClassW(GATES_POSTCLASS_NAME, app->hinstance);
    }
    if (app->window_class != 0) {
        UnregisterClassW(GATES_WNDCLASS_NAME, app->hinstance);
    }
    bool com_init = app->com_init;
    gates_allocator_t alloc = app->alloc;
    alloc.free_fn(alloc.ctx, app);
    if (com_init) {
        CoUninitialize();
    }
}

gates_err_t gates_app_run(gates_app_t *app) {
    if (app == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    app->running = true;
    for (gates_window_t *w = app->windows; w != nullptr; w = w->next_window) gates_win32_show(w);
    MSG msg;
    BOOL got;
    while ((got = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (got == -1) {
            app->running = false;
            return PROVEN_ERR_INVALID_STATE;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    app->running = false;
    return GATES_OK;
}

void gates_app_quit(gates_app_t *app) {
    (void)app;
    PostQuitMessage(0);
}

gates_err_t gates_app_set_ui_font(gates_app_t *app, gates_str_t face) {
    wchar_t w[LF_FACESIZE];
    if (app == nullptr || !face_name(face, w)) return PROVEN_ERR_INVALID_ARG;
    bool found = gates_win32_text_set_ui_face(w);
    for (gates_window_t *win = app->windows; win != nullptr; win = win->next_window) {
        win->last_layout_size = (gates_size_t){ 0, 0 }; /* as for a system font change */
        if (win->hwnd != nullptr) InvalidateRect(win->hwnd, nullptr, FALSE);
    }
    return found ? GATES_OK : PROVEN_ERR_NOT_FOUND;
}

const wchar_t *gates_win32_class_name(void) {
    return GATES_WNDCLASS_NAME;
}
