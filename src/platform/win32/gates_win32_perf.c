/* gates_gui_lib - performance log for field measurements (0.3.0).
 *
 * GATES_PERF=<file> in the environment appends one CSV line per event:
 *   frame,<t_us>,<input_to_present_us|-1>,<layout_us>,<paint_us>,<render_us>,<present_us>,<w>,<h>,<dpi>,<cmds>
 *   wake,<t_us>,<what>              timer, dispatch, post (work done without input)
 *   first_frame,<t_us>,<ms since the process started>
 * Times are microseconds from the first log line (QueryPerformanceCounter).
 * Without the variable nothing is measured or written. */
#include "gates_win32_internal.h"
#include "gates/version.h"

#include <stdarg.h>
#include <stdio.h>

static FILE *g_perf;
static bool g_perf_ready;
static LARGE_INTEGER g_freq, g_t0;

bool gates_win32_perf_on(void) {
    if (!g_perf_ready) {
        g_perf_ready = true;
        wchar_t path[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"GATES_PERF", path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            g_perf = _wfopen(path, L"a");
            QueryPerformanceFrequency(&g_freq);
            QueryPerformanceCounter(&g_t0);
            if (g_perf != nullptr) {
                wchar_t exe[MAX_PATH] = L"?";
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                const wchar_t *base = wcsrchr(exe, L'\\');
                fprintf(g_perf, "# gates %s perf log, program %ls\n", gates_version_string(), base != nullptr ? base + 1 : exe);
            }
        }
    }
    return g_perf != nullptr;
}

gates_u64 gates_win32_perf_now_us(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return g_freq.QuadPart > 0 ? (gates_u64)((t.QuadPart - g_t0.QuadPart) * 1000000 / g_freq.QuadPart) : 0;
}

void gates_win32_perf_log(const char *fmt, ...) {
    if (!gates_win32_perf_on()) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_perf, fmt, ap);
    va_end(ap);
    fputc('\n', g_perf);
    fflush(g_perf);
}

/* Milliseconds since this process was created. */
gates_u64 gates_win32_perf_process_ms(void) {
    FILETIME created, exited, kernel, user, now;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a = { .LowPart = created.dwLowDateTime, .HighPart = created.dwHighDateTime };
    ULARGE_INTEGER b = { .LowPart = now.dwLowDateTime, .HighPart = now.dwHighDateTime };
    return b.QuadPart > a.QuadPart ? (b.QuadPart - a.QuadPart) / 10000 : 0;
}
