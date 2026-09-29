/* gates_gui_lib — Win32 window: surface, DIB present, paint path (RFC-0001 §21).
 * Present path: draw list -> gates_render_soft -> BGRA DIB section -> BitBlt. */
#include "gates_win32_internal.h"
#include <dwmapi.h>

#include <stdio.h>
#include <string.h>

#define GATES_WINDOW_DEFAULT_W 800
#define GATES_WINDOW_DEFAULT_H 600

static void destroy_dib(gates_window_t *win) {
    if (win->mem_dc != nullptr) {
        if (win->dib_old != nullptr) {
            SelectObject(win->mem_dc, win->dib_old);
        }
        if (win->dib != nullptr) {
            DeleteObject(win->dib);
        }
        DeleteDC(win->mem_dc);
    }
    win->mem_dc = nullptr;
    win->dib = nullptr;
    win->dib_old = nullptr;
    win->dib_pixels = nullptr;
    win->dib_size = (gates_size_t){ 0, 0 };
}

static bool ensure_dib(gates_window_t *win, HDC window_dc, gates_size_t size) {
    if (size.w <= 0 || size.h <= 0) {
        return false;
    }
    if (win->dib_pixels != nullptr && win->dib_size.w == size.w && win->dib_size.h == size.h) {
        return true;
    }
    destroy_dib(win);

    BITMAPINFO bmi = { .bmiHeader = {
        .biSize = sizeof(BITMAPINFOHEADER),
        .biWidth = size.w,
        .biHeight = -size.h,           /* top-down */
        .biPlanes = 1,
        .biBitCount = 32,              /* BGRA8 */
        .biCompression = BI_RGB,
    }};
    void *pixels = nullptr;
    HBITMAP dib = CreateDIBSection(window_dc, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (dib == nullptr || pixels == nullptr) {
        return false;
    }
    HDC mem_dc = CreateCompatibleDC(window_dc);
    if (mem_dc == nullptr) {
        DeleteObject(dib);
        return false;
    }
    win->mem_dc = mem_dc;
    win->dib = dib;
    win->dib_old = (HBITMAP)SelectObject(mem_dc, dib);
    win->dib_pixels = pixels;
    win->dib_size = size;
    return true;
}

/* Device pixels of the client area. */
static gates_size_t client_px_of(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    return (gates_size_t){ rc.right - rc.left, rc.bottom - rc.top };
}

/* The client area in logical units: enough units to cover every pixel. */
static gates_size_t client_size_of(gates_window_t *win) {
    gates_size_t px = client_px_of(win->hwnd);
    return (gates_size_t){ px.w > 0 ? gates_logical(px.w - 1, win->dpi) + 1 : 0,
                           px.h > 0 ? gates_logical(px.h - 1, win->dpi) + 1 : 0 };
}

/* GATES_ACCESS_STRICT=1 (plan-0014): after every layout, check the tree and
 * theme against the enforced accessibility rules; the count goes into the
 * title and each issue to the debugger output, so a violation is seen at once. */
static void strict_audit(gates_window_t *win) {
    gates_access_issue_t is[16];
    gates_u32 n = gates_access_audit(win->tree, win->theme, is, 16);
    if (n == win->strict_issues) return;
    win->strict_issues = n;
    static const char *const rule[] = { "", "no-name", "target-size", "keyboard", "duplicate-id",
                                        "contrast", "focus-cue" };
    for (gates_u32 k = 0; k < n && k < 16; k++) {
        char line[96];
        wsprintfA(line, "gates a11y: %s node %lu item %lu\n", rule[is[k].rule <= 6 ? is[k].rule : 0],
                  (unsigned long)is[k].node.index, (unsigned long)is[k].item);
        OutputDebugStringA(line);
    }
    wchar_t title[300];
    int len = GetWindowTextW(win->hwnd, title, 240);
    for (int k = 0; k < len; k++) {
        if (title[k] == L' ' && title[k + 1] == L'[' && title[k + 2] == L'a' && title[k + 3] == L'1') {
            len = k; /* drop the previous report */
            break;
        }
    }
    title[len] = L'\0';
    if (n > 0) wsprintfW(title + len, L" [a11y issues: %lu]", (unsigned long)n);
    SetWindowTextW(win->hwnd, title);
}

static void paint_window(gates_window_t *win, HDC dc) {
    bool perf = gates_win32_perf_on();
    gates_u64 p0 = perf ? gates_win32_perf_now_us() : 0, p1 = p0, p2 = p0;
    gates_size_t size = client_px_of(win->hwnd);
    gates_size_t logical = client_size_of(win);
    if (!ensure_dib(win, dc, size)) {
        return; /* zero-sized (minimized) or DIB failure: nothing to present */
    }

    /* Fallback background (windows without a widget tree). */
    memset(win->dib_pixels, 0xFF,
           (gates_usize_t)size.w * (gates_usize_t)size.h * 4u);

    gates_draw_list_reset(&win->draw_list);

    /* Widget tree drives layout + paint when the root has children;
     * the app's on_paint stays as an overlay hook. */
    bool has_widgets =
        gates_node_child_count(win->tree, gates_tree_root(win->tree)) > 0;
    if (has_widgets) {
        /* The tree lives in logical units (plan-0013); only drawing scales. */
        bool size_changed = win->last_layout_size.w != logical.w ||
                            win->last_layout_size.h != logical.h;
        if (size_changed || (gates_tree_dirty(win->tree) & GATES_TREE_DIRTY_LAYOUT) != 0) {
            if (gates_is_ok(gates_layout_run(win->tree, logical, win->text))) {
                win->last_layout_size = logical;
                if (win->strict) strict_audit(win);
            }
        }
        if (perf) p1 = gates_win32_perf_now_us();
        (void)gates_paint_tree(win->tree, &win->draw_list, win->theme, win->text);
    } else if (perf) {
        p1 = gates_win32_perf_now_us();
    }
    if (win->cb.on_paint != nullptr) {
        win->cb.on_paint(win, &win->draw_list, win->cb.user_data);
    }

    gates_pixels_t px = {
        .ptr = win->dib_pixels,
        .w = size.w,
        .h = size.h,
        .stride_bytes = (gates_u32)size.w * 4u,
    };
    if (perf) p2 = gates_win32_perf_now_us();
    gates_err_t err = gates_render_soft_scaled(&win->draw_list, px, win->text, win->dpi);
    (void)err; /* an invalid list presents the plain background */
    gates_u64 p3 = perf ? gates_win32_perf_now_us() : 0;

    BitBlt(dc, 0, 0, size.w, size.h, win->mem_dc, 0, 0, SRCCOPY);
    if (perf) {
        GdiFlush();
        gates_u64 p4 = gates_win32_perf_now_us();
        long long in_to_present = win->perf_input_us != 0 ? (long long)(p4 - win->perf_input_us) : -1;
        gates_win32_perf_log("frame,%llu,%lld,%llu,%llu,%llu,%llu,%d,%d,%u,%u", (unsigned long long)p4, in_to_present,
                             (unsigned long long)(p1 - p0), (unsigned long long)(p2 - p1), (unsigned long long)(p3 - p2),
                             (unsigned long long)(p4 - p3), size.w, size.h, win->dpi, gates_draw_list_len(&win->draw_list));
        win->perf_input_us = 0;
        if (!win->perf_first_done) {
            win->perf_first_done = true;
            gates_win32_perf_log("first_frame,%llu,%llu", (unsigned long long)p4,
                                 (unsigned long long)gates_win32_perf_process_ms());
        }
    }
}

static void resolve_theme(gates_window_t *win);
static gates_u32 forced_dpi(void);

/* "Underline access keys" in the system settings (plan-0018). */
static void cues_setting(gates_window_t *win) {
    BOOL always = FALSE;
    if (SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &always, 0)) {
        gates_tree_set_cues_always(win->tree, always != FALSE);
    }
}

LRESULT CALLBACK gates_win32_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    gates_window_t *win;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lparam;
        win = (gates_window_t *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)win);
        win->hwnd = hwnd;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    win = (gates_window_t *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (win == nullptr) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint_window(win, dc);
        EndPaint(hwnd, &ps);
        if (gates_input_composing(win->tree)) {
            gates_win32_ime_place(win); /* the caret moved with the preedit */
        }
        gates_win32_caret_follow(win); /* layout may have moved it */
        gates_win32_uia_events(win);   /* and the bounds clients see */
        return 0;
    }
    case WM_GETOBJECT: {
        LRESULT r = 0;
        if (gates_win32_uia_getobject(win, wparam, lparam, &r)) {
            return r;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    case WM_SETFOCUS:
        /* Activation: tell clients where focus is (Narrator reads it), and
         * put the system caret back. */
        win->uia_focus = (gates_access_ref_t){ GATES_NODE_NULL, 0 };
        gates_win32_after_input(win);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_KILLFOCUS:
        gates_win32_caret_drop(win);
        gates_win32_ime_complete(win);
        gates_tree_dismiss_menus(win->tree); /* menus close when the window loses focus */
        gates_win32_after_input(win);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_CAPTURECHANGED:
        if ((HWND)lparam != hwnd) {
            gates_win32_cancel_pointer(win); /* someone else has the mouse now */
        }
        return 0;
    case WM_CANCELMODE:
        gates_win32_cancel_pointer(win);
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case GATES_WM_DISPATCH:
        if (gates_win32_perf_on()) gates_win32_perf_log("wake,%llu,dispatch", (unsigned long long)gates_win32_perf_now_us());
        win->dispatch_posted = false;
        gates_win32_after_input(win);
        return 0;
    case WM_TIMER:
        if (wparam == GATES_WIN32_TIMER_ID) {
            if (gates_win32_perf_on()) gates_win32_perf_log("wake,%llu,timer", (unsigned long long)gates_win32_perf_now_us());
            win->timer_armed = false;
            (void)gates_tree_run_timers(win->tree);
            gates_win32_after_input(win);
            gates_win32_arm_timer(win);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_DPICHANGED: {
        /* Moved to a monitor with another scale, or the scale changed: take the
         * rectangle Windows suggests, then lay out again in the same logical size. */
        win->monitor_dpi = LOWORD(wparam) != 0 ? LOWORD(wparam) : GATES_DPI_BASE;
        gates_win32_rescale(win);
        const RECT *r = (const RECT *)lparam;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        win->last_layout_size = (gates_size_t){ 0, 0 };
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_SYSCOMMAND:
        /* Alt released alone or F10 (lparam 0): menu mode on the menu bar
         * (plan-0018). Without a reachable bar Windows keeps its own. */
        if ((wparam & 0xFFF0) == SC_KEYMENU && lparam == 0 && gates_input_menu_key(win->tree)) {
            gates_win32_after_input(win);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_SETTINGCHANGE:   /* app mode ("ImmersiveColorSet"), high contrast, text size */
        cues_setting(win);
        [[fallthrough]];
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
        resolve_theme(win);
        gates_win32_rescale(win);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    case WM_ERASEBKGND:
        return 1; /* full client repaint via DIB; no GDI background erase */
    case WM_SIZE:
        if (win->cb.on_resize != nullptr && wparam != SIZE_MINIMIZED) {
            win->cb.on_resize(win, client_size_of(win), win->cb.user_data);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_CLOSE:
        if (win->cb.on_close != nullptr && !win->cb.on_close(win, win->cb.user_data)) {
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        gates_win32_caret_drop(win);
        gates_win32_uia_detach(win);
        win->hwnd = nullptr;
        if (win->app->window_count > 0) {
            win->app->window_count--;
        }
        if (win->app->window_count == 0) {
            PostQuitMessage(0);
        }
        return 0;
    default: {
        /* plan-0017: the first input since the last frame that invalidates the window starts
         * the input-to-present clock; input that changes nothing (a key release, a mouse move
         * over nothing) does not, so idle time between keys is not counted. */
        bool stamped = false;
        if (win->perf_input_us == 0 && ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST) ||
                                        (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) ||
                                        (msg >= WM_IME_STARTCOMPOSITION && msg <= WM_IME_KEYLAST)) &&
            gates_win32_perf_on()) {
            win->perf_input_us = gates_win32_perf_now_us();
            stamped = true;
        }
        LRESULT result = 0;
        bool handled = gates_win32_handle_ime(win, msg, wparam, lparam, &result);
        if (!handled && gates_win32_handle_input(win, msg, wparam, lparam)) {
            handled = true;
            result = 0;
        }
        /* IsWindow first: the input may have closed the window (and freed win). */
        if (stamped && IsWindow(hwnd) && !GetUpdateRect(hwnd, nullptr, FALSE)) win->perf_input_us = 0;
        return handled ? result : DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    }
}

/* -- DPI (plan-0013) ----------------------------------------------------------- */

/* Diagnostics: GATES_FORCE_DPI=144 makes every window draw at that DPI whatever
 * the monitor says (for checking scaled output where the display scale cannot
 * be changed, such as a remote desktop session). 0 = not forced. */
static gates_u32 forced_dpi(void) {
    wchar_t buf[16];
    DWORD n = GetEnvironmentVariableW(L"GATES_FORCE_DPI", buf, 16);
    if (n == 0 || n >= 16) return 0;
    gates_u32 v = 0;
    for (DWORD i = 0; i < n && buf[i] >= L'0' && buf[i] <= L'9'; i++) v = v * 10 + (gates_u32)(buf[i] - L'0');
    return v >= 48 && v <= 960 ? v : 0;
}

/* Windows "Text size" (Settings > Accessibility), percent 100..225. */
static gates_u32 text_scale(void) {
    DWORD value = 100, size = sizeof value;
    LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility",
                              L"TextScaleFactor", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return st == ERROR_SUCCESS && value >= 100 && value <= 225 ? (gates_u32)value : 100u;
}

/* The drawing DPI: the monitor's (or GATES_FORCE_DPI) times the application's
 * zoom times the Windows text size - logical units make both a zoom of the
 * whole interface, as for a magnified page (plan-0014). */
void gates_win32_rescale(gates_window_t *win) {
    gates_u32 f = forced_dpi();
    gates_u64 base = f != 0 ? f : (win->monitor_dpi != 0 ? win->monitor_dpi : GATES_DPI_BASE);
    gates_u64 d = base * (win->zoom != 0 ? win->zoom : 100u) * text_scale() / 10000u;
    if (d < 24) d = 24;
    if (d > 3840) d = 3840;
    if ((gates_u32)d == win->dpi) return;
    win->dpi = (gates_u32)d;
    win->last_layout_size = (gates_size_t){ 0, 0 };
    if (win->hwnd != nullptr) {
        InvalidateRect(win->hwnd, nullptr, FALSE);
    }
}

/* -- theme (plan-0013) --------------------------------------------------------- */

static bool system_high_contrast(void) {
    HIGHCONTRASTW hc = { .cbSize = sizeof hc };
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) &&
           (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

/* Windows 10+ app mode: AppsUseLightTheme = 0 means dark. Unknown -> light. */
static bool system_dark(void) {
    DWORD value = 1, size = sizeof value;
    LSTATUS st = RegGetValueW(HKEY_CURRENT_USER,
                              L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                              L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return st == ERROR_SUCCESS && value == 0;
}

static gates_color_t sys_color(int index) {
    COLORREF c = GetSysColor(index);
    return (gates_color_t){ GetRValue(c), GetGValue(c), GetBValue(c), 255 };
}

/* Picks the theme for the mode (or keeps an application theme) and repaints. */
static void resolve_theme(gates_window_t *win) {
    bool dark = false;
    if (!win->theme_custom) {
        gates_theme_mode_t m = win->theme_mode;
        if (m == GATES_THEME_HIGH_CONTRAST || (m == GATES_THEME_SYSTEM && system_high_contrast())) {
            gates_system_colors_t sys = {
                .window = sys_color(COLOR_WINDOW), .window_text = sys_color(COLOR_WINDOWTEXT),
                .highlight = sys_color(COLOR_HIGHLIGHT),
                .highlight_text = sys_color(COLOR_HIGHLIGHTTEXT),
                .button_face = sys_color(COLOR_BTNFACE), .button_text = sys_color(COLOR_BTNTEXT),
                .gray_text = sys_color(COLOR_GRAYTEXT), .hotlight = sys_color(COLOR_HOTLIGHT),
            };
            gates_theme_high_contrast(&sys, &win->theme_store);
            dark = GetRValue(GetSysColor(COLOR_WINDOW)) < 128;
        } else if (m == GATES_THEME_DARK || (m == GATES_THEME_SYSTEM && system_dark())) {
            win->theme_store = *gates_theme_dark();
            dark = true;
        } else {
            win->theme_store = *gates_theme_light();
        }
    }
    win->theme = &win->theme_store;
    if (win->hwnd != nullptr) {
        BOOL on = dark ? TRUE : FALSE; /* DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+) */
        (void)DwmSetWindowAttribute(win->hwnd, 20, &on, sizeof on);
        InvalidateRect(win->hwnd, nullptr, FALSE);
    }
}

void gates_window_set_theme_mode(gates_window_t *win, gates_theme_mode_t mode) {
    if (win == nullptr || mode > GATES_THEME_HIGH_CONTRAST) return;
    win->theme_mode = mode;
    win->theme_custom = false;
    resolve_theme(win);
}

gates_theme_mode_t gates_window_theme_mode(const gates_window_t *win) {
    return win != nullptr ? win->theme_mode : GATES_THEME_SYSTEM;
}

gates_err_t gates_window_set_theme(gates_window_t *win, const gates_theme_t *theme) {
    if (win == nullptr || theme == nullptr) return PROVEN_ERR_INVALID_ARG;
    win->theme_store = *theme;
    win->theme_custom = true;
    resolve_theme(win);
    return GATES_OK;
}

const gates_theme_t *gates_window_theme(const gates_window_t *win) {
    return win != nullptr ? win->theme : nullptr;
}

/* -- timers (plan-0012) ------------------------------------------------------- */

static gates_u64 clock_ms(void *ctx) {
    (void)ctx;
    return (gates_u64)GetTickCount64();
}

void gates_win32_arm_timer(gates_window_t *win) {
    if (win == nullptr || win->hwnd == nullptr) return;
    gates_u32 next = gates_tree_next_timer(win->tree);
    if (next == GATES_TIMER_NONE) {
        if (win->timer_armed) KillTimer(win->hwnd, GATES_WIN32_TIMER_ID);
        win->timer_armed = false; /* idle: no platform timer at all */
        return;
    }
    /* SetTimer on an armed id restarts its countdown: re-arming for an unchanged
     * due time (on every delivery turn, say) would keep a timer from ever firing. */
    gates_u64 due_at = (gates_u64)GetTickCount64() + next;
    if (win->timer_armed && win->timer_due_at == due_at) return;
    if (next < USER_TIMER_MINIMUM) next = USER_TIMER_MINIMUM;
    win->timer_armed = SetTimer(win->hwnd, GATES_WIN32_TIMER_ID, next, nullptr) != 0;
    win->timer_due_at = due_at;
}

static void timers_changed(void *ctx) {
    gates_win32_arm_timer(ctx);
}

/* -- public API ------------------------------------------------------------ */

gates_err_t gates_window_create(gates_app_t *app, const gates_window_desc_t *desc,
                                const gates_window_callbacks_t *callbacks,
                                gates_window_t **out_window) {
    if (app == nullptr || out_window == nullptr) {
        return PROVEN_ERR_INVALID_ARG;
    }
    *out_window = nullptr;

    proven_result_mem_mut_t res = app->alloc.alloc_fn(app->alloc.ctx, sizeof(gates_window_t),
                                                      alignof(gates_window_t));
    if (!proven_is_ok(res.err)) {
        return res.err;
    }
    gates_window_t *win = (gates_window_t *)res.value.ptr;
    memset(win, 0, sizeof *win);
    win->app = app;
    win->zoom = 100;
    win->uia_focus = (gates_access_ref_t){ GATES_NODE_NULL, 0 };
    win->uia_opened = (gates_access_ref_t){ GATES_NODE_NULL, 0 };
    {
        wchar_t v[4];
        win->strict = GetEnvironmentVariableW(L"GATES_ACCESS_STRICT", v, 4) > 0 && v[0] != L'0';
        win->strict_issues = (gates_u32)-1;
    }
    win->theme_mode = GATES_THEME_SYSTEM;
    resolve_theme(win); /* again once the window exists, for the title bar */
    win->text = gates_text_backend_win32_gdi(); /* real glyphs (RFC-0002 §5) */
    if (callbacks != nullptr) {
        win->cb = *callbacks;
    }

    gates_tree_desc_t tree_desc = { .allocator = app->alloc };
    gates_err_t err = gates_tree_create(&tree_desc, &win->tree);
    if (!gates_is_ok(err)) {
        app->alloc.free_fn(app->alloc.ctx, win);
        return err;
    }
    gates_win32_install_clipboard(win); /* copy/cut/paste for textboxes */
    gates_win32_install_image_decoder(win); /* PNG, JPEG, ... through WIC (plan-0020) */
    gates_win32_install_threads(win); /* background tasks (plan-0021) */
    cues_setting(win);
    gates_tree_set_clock(win->tree, clock_ms, timers_changed, win);
    if (app->sender != nullptr) {
        err = gates_sender_attach(app->sender, win->tree);
        if (!gates_is_ok(err)) {
            gates_tree_destroy(win->tree);
            app->alloc.free_fn(app->alloc.ctx, win);
            return err;
        }
    }
    err = gates_draw_list_init(&win->draw_list, app->alloc, 0);
    if (!gates_is_ok(err)) {
        gates_tree_destroy(win->tree);
        app->alloc.free_fn(app->alloc.ctx, win);
        return err;
    }

    /* UTF-8 title -> UTF-16 (bounded stack buffer; long titles truncate). */
    wchar_t wtitle[256] = L"gates";
    if (desc != nullptr && desc->title.ptr != nullptr && desc->title.size > 0) {
        int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)desc->title.ptr,
                                    (int)desc->title.size, wtitle, 255);
        wtitle[n < 0 ? 0 : n] = L'\0';
    }

    gates_size_t want = (desc != nullptr && desc->size.w > 0 && desc->size.h > 0)
                            ? desc->size
                            : (gates_size_t){ GATES_WINDOW_DEFAULT_W, GATES_WINDOW_DEFAULT_H };
    /* The size is logical (plan-0013): scale it for the monitor the window
     * starts on; WM_DPICHANGED corrects it if Windows places it elsewhere. */
    win->monitor_dpi = gates_win32_system_dpi();
    gates_win32_rescale(win);
    RECT rc = { 0, 0, gates_px(want.w, win->dpi), gates_px(want.h, win->dpi) };
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rc, style, FALSE);

    HWND hwnd = CreateWindowExW(0, gates_win32_class_name(), wtitle, style,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, app->hinstance, win);
    if (hwnd == nullptr) {
        gates_draw_list_deinit(&win->draw_list);
        gates_tree_destroy(win->tree);
        app->alloc.free_fn(app->alloc.ctx, win);
        return PROVEN_ERR_INVALID_STATE;
    }
    app->window_count++;
    win->monitor_dpi = gates_win32_dpi_for_window(hwnd);
    gates_win32_rescale(win);
    resolve_theme(win);
    win->next_window = app->windows;
    app->windows = win;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    *out_window = win;
    return GATES_OK;
}

void gates_window_destroy(gates_window_t *win) {
    if (win == nullptr) {
        return;
    }
    if (win->hwnd != nullptr) {
        DestroyWindow(win->hwnd); /* WM_DESTROY updates the app window count */
    }
    for (gates_window_t **p = &win->app->windows; *p != nullptr; p = &(*p)->next_window) {
        if (*p == win) {
            *p = win->next_window;
            break;
        }
    }
    destroy_dib(win);
    gates_draw_list_deinit(&win->draw_list);
    gates_tree_destroy(win->tree);
    gates_allocator_t alloc = win->app->alloc;
    alloc.free_fn(alloc.ctx, win);
}

gates_tree_t *gates_window_tree(gates_window_t *win) {
    return win == nullptr ? nullptr : win->tree;
}

gates_size_t gates_window_client_size(const gates_window_t *win) {
    if (win == nullptr || win->hwnd == nullptr) {
        return (gates_size_t){ 0, 0 };
    }
    return client_size_of((gates_window_t *)win);
}

/* -- accessibility (plan-0014) ------------------------------------------------------ */

void gates_window_set_zoom(gates_window_t *win, gates_u32 percent) {
    if (win == nullptr) return;
    win->zoom = percent < 25 ? 25 : percent > 400 ? 400 : percent;
    gates_win32_rescale(win);
}

gates_u32 gates_window_zoom(const gates_window_t *win) {
    return win != nullptr ? win->zoom : 100;
}

bool gates_window_reduced_motion(const gates_window_t *win) {
    (void)win;
    BOOL anim = TRUE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0) && !anim;
}

gates_err_t gates_window_announce(gates_window_t *win, gates_str_t text, bool assertive) {
    if (win == nullptr) return PROVEN_ERR_INVALID_ARG;
    gates_err_t err = gates_access_announce(win->tree, text, assertive);
    if (gates_is_ok(err)) gates_win32_uia_events(win);
    return err;
}

void gates_window_request_repaint(gates_window_t *win) {
    if (win != nullptr && win->hwnd != nullptr) {
        InvalidateRect(win->hwnd, nullptr, FALSE);
    }
}

/* -- placement (plan-0018, RFC-0005 A5) ------------------------------------------------ */

gates_err_t gates_window_placement(const gates_window_t *win, gates_u8 *buf, gates_usize_t cap,
                                   gates_usize_t *needed) {
    if (win == nullptr || needed == nullptr) return PROVEN_ERR_INVALID_ARG;
    WINDOWPLACEMENT wp = { .length = sizeof wp };
    if (!GetWindowPlacement(win->hwnd, &wp)) return PROVEN_ERR_INVALID_STATE;
    RECT r = wp.rcNormalPosition;
    bool max = wp.showCmd == SW_SHOWMAXIMIZED || (IsZoomed(win->hwnd) && !IsIconic(win->hwnd));
    char tmp[96];
    int n = snprintf(tmp, sizeof tmp, "%ld,%ld,%ld,%ld,%s", (long)r.left, (long)r.top, (long)(r.right - r.left),
                     (long)(r.bottom - r.top), max ? "maximized" : "normal");
    if (n <= 0 || (gates_usize_t)n >= sizeof tmp) return PROVEN_ERR_INVALID_STATE;
    *needed = (gates_usize_t)n;
    if (buf == nullptr) return GATES_OK;
    if ((gates_usize_t)n > cap) return PROVEN_ERR_OVERFLOW;
    memcpy(buf, tmp, (size_t)n);
    return GATES_OK;
}

/* A signed decimal field ending at `,` or the end; advances *p. */
static bool field(const gates_u8 **p, const gates_u8 *end, long *out) {
    const gates_u8 *q = *p;
    bool neg = q < end && *q == '-';
    if (neg) q++;
    long v = 0;
    int digits = 0;
    while (q < end && *q >= '0' && *q <= '9' && digits < 9) {
        v = v * 10 + (*q - '0');
        q++;
        digits++;
    }
    if (digits == 0 || q >= end || *q != ',') return false;
    *out = neg ? -v : v;
    *p = q + 1;
    return true;
}

gates_err_t gates_window_set_placement(gates_window_t *win, gates_str_t text) {
    if (win == nullptr || (text.size > 0 && text.ptr == nullptr)) return PROVEN_ERR_INVALID_ARG;
    const gates_u8 *p = text.ptr, *end = text.ptr + text.size;
    while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ')) end--;
    long x, y, w, h;
    if (!field(&p, end, &x) || !field(&p, end, &y) || !field(&p, end, &w) || !field(&p, end, &h) || w <= 0 ||
        h <= 0) {
        return PROVEN_ERR_INVALID_ARG;
    }
    gates_usize_t rest = (gates_usize_t)(end - p);
    bool max;
    if (rest == 9 && memcmp(p, "maximized", 9) == 0) max = true;
    else if (rest == 6 && memcmp(p, "normal", 6) == 0) max = false;
    else return PROVEN_ERR_INVALID_ARG;
    RECT r = { x, y, x + w, y + h };
    /* A rectangle on a monitor that is gone moves onto the nearest one. */
    if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL) == nullptr) {
        MONITORINFO mi = { .cbSize = sizeof mi };
        if (GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi)) {
            RECT wa = mi.rcWork;
            if (w > wa.right - wa.left) w = wa.right - wa.left;
            if (h > wa.bottom - wa.top) h = wa.bottom - wa.top;
            long nx = r.left < wa.left ? wa.left : (r.left + w > wa.right ? wa.right - w : r.left);
            long ny = r.top < wa.top ? wa.top : (r.top + h > wa.bottom ? wa.bottom - h : r.top);
            r = (RECT){ nx, ny, nx + w, ny + h };
        }
    }
    WINDOWPLACEMENT wp = { .length = sizeof wp };
    if (!GetWindowPlacement(win->hwnd, &wp)) return PROVEN_ERR_INVALID_STATE;
    wp.flags = 0;
    wp.showCmd = max ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    wp.rcNormalPosition = r;
    return SetWindowPlacement(win->hwnd, &wp) ? GATES_OK : PROVEN_ERR_INVALID_STATE;
}
