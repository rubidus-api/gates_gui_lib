/* gates_gui_lib - native dialogs: the Windows file, folder, colour and message
 * dialogs (0.5.0). They are Windows' own modal dialogs; the calls return
 * when the person answers. COM objects are made from GUIDs defined here and
 * ChooseColorW is looked up at run time, so the link line needs nothing more. */
#define COBJMACROS
#include "gates_win32_internal.h"

#include <shobjidl.h>
#include <commdlg.h>
#include <string.h>

static const GUID gates_clsid_open = { 0xdc1c5a9c, 0xe88a, 0x4dde, { 0xa5, 0xa1, 0x60, 0xf8, 0x2a, 0x20, 0xae, 0xf7 } };
static const GUID gates_clsid_save = { 0xc0b4e2f3, 0xba21, 0x4773, { 0x8d, 0xba, 0x33, 0x5e, 0xc9, 0x46, 0xeb, 0x8b } };
static const GUID gates_iid_file_dialog = { 0x42f85136, 0xdb7e, 0x439c, { 0x85, 0xf1, 0xe4, 0x07, 0x5d, 0x13, 0x5f, 0xc8 } };
static const GUID gates_iid_file_open_dialog = { 0xd57c7288, 0xd4ad, 0x4768, { 0xbe, 0x02, 0x9d, 0x96, 0x95, 0x32, 0xd9, 0x60 } };
static const GUID gates_iid_shell_item = { 0x43826d1e, 0xe718, 0x42ee, { 0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe } };

/* UTF-8 to a NUL-terminated UTF-16 string on the process heap (null for empty). */
static wchar_t *wide(gates_str_t s) {
    if (s.size == 0 || s.ptr == nullptr) return nullptr;
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)s.ptr, (int)s.size, nullptr, 0);
    if (n <= 0) return nullptr;
    wchar_t *w = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(n + 1) * sizeof(wchar_t));
    if (w == nullptr) return nullptr;
    MultiByteToWideChar(CP_UTF8, 0, (const char *)s.ptr, (int)s.size, w, n);
    w[n] = L'\0';
    return w;
}

static void wfree(void *p) {
    if (p != nullptr) HeapFree(GetProcessHeap(), 0, p);
}

/* The answer as UTF-8 into buf: OVERFLOW (nothing written) when it does not fit. */
static gates_err_t answer(const wchar_t *path, gates_u8 *buf, gates_usize_t cap, gates_usize_t *len) {
    int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return PROVEN_ERR_INVALID_STATE;
    *len = (gates_usize_t)(n - 1);
    if (buf == nullptr || cap < *len) return PROVEN_ERR_OVERFLOW;
    char *tmp = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n);
    if (tmp == nullptr) return PROVEN_ERR_NOMEM;
    WideCharToMultiByte(CP_UTF8, 0, path, -1, tmp, n, nullptr, nullptr);
    memcpy(buf, tmp, *len);
    if (cap > *len) buf[*len] = 0;
    wfree(tmp);
    return GATES_OK;
}

/* "Name|*.a;*.b|Name2|*.c" into filter specs; returns how many (at most 16). */
static UINT filters(wchar_t *spec, COMDLG_FILTERSPEC *out) {
    UINT n = 0;
    wchar_t *p = spec;
    while (p != nullptr && *p != L'\0' && n < 16) {
        wchar_t *bar = wcschr(p, L'|');
        if (bar == nullptr) break;
        *bar = L'\0';
        wchar_t *pat = bar + 1;
        wchar_t *next = wcschr(pat, L'|');
        if (next != nullptr) *next++ = L'\0';
        out[n++] = (COMDLG_FILTERSPEC){ .pszName = p, .pszSpec = pat };
        p = next;
    }
    return n;
}

/* Several answers (0.8.0): each path and a NUL; OVERFLOW (nothing written) when they do not fit. */
static gates_err_t answers(IShellItemArray *arr, gates_u8 *buf, gates_usize_t cap, gates_usize_t *len,
                           gates_u32 *count) {
    DWORD n = 0;
    if (FAILED(IShellItemArray_GetCount(arr, &n))) return PROVEN_ERR_INVALID_STATE;
    gates_usize_t total = 0;
    gates_err_t err = GATES_OK;
    for (int pass = 0; pass < 2 && gates_is_ok(err); pass++) {
        gates_usize_t at = 0;
        for (DWORD k = 0; k < n && gates_is_ok(err); k++) {
            IShellItem *item = nullptr;
            wchar_t *path = nullptr;
            HRESULT hr = IShellItemArray_GetItemAt(arr, k, &item);
            if (SUCCEEDED(hr)) hr = IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path);
            int m = SUCCEEDED(hr) ? WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr) : 0;
            if (m <= 0) {
                err = PROVEN_ERR_INVALID_STATE;
            } else if (pass == 1) {
                WideCharToMultiByte(CP_UTF8, 0, path, -1, (char *)buf + at, m, nullptr, nullptr); /* its NUL too */
            }
            at += m > 0 ? (gates_usize_t)m : 0;
            if (path != nullptr) CoTaskMemFree(path);
            if (item != nullptr) IShellItem_Release(item);
        }
        total = at;
        if (pass == 0 && (buf == nullptr || cap < total)) {
            *len = total;
            return gates_is_ok(err) ? PROVEN_ERR_OVERFLOW : err;
        }
    }
    if (!gates_is_ok(err)) return err;
    *len = total;
    *count = (gates_u32)n;
    return GATES_OK;
}

typedef HRESULT(WINAPI *item_from_path_fn)(PCWSTR, IBindCtx *, REFIID, void **);

/* kind: 0 open, 1 save, 2 folder, 3 open several (count is set only then). */
static gates_err_t file_dialog(gates_window_t *win, const gates_file_dialog_t *d, int kind, gates_u8 *buf,
                               gates_usize_t cap, gates_usize_t *len, gates_u32 *count) {
    if (win == nullptr || len == nullptr || (kind == 3 && count == nullptr)) return PROVEN_ERR_INVALID_ARG;
    *len = 0;
    if (count != nullptr) *count = 0;
    gates_file_dialog_t none = {0};
    if (d == nullptr) d = &none;
    gates_tree_dismiss_menus(win->tree); /* the window's own menus close first */
    IFileDialog *dlg = nullptr;
    HRESULT hr = CoCreateInstance(kind == 1 ? &gates_clsid_save : &gates_clsid_open, nullptr, CLSCTX_INPROC_SERVER,
                                  kind == 3 ? &gates_iid_file_open_dialog : &gates_iid_file_dialog, (void **)&dlg);
    if (FAILED(hr)) return PROVEN_ERR_UNSUPPORTED;
    wchar_t *title = wide(d->title), *spec = wide(d->filters), *name = wide(d->name), *folder = wide(d->folder);
    COMDLG_FILTERSPEC fs[16];
    FILEOPENDIALOGOPTIONS opts = 0;
    IFileDialog_GetOptions(dlg, &opts);
    opts |= FOS_FORCEFILESYSTEM;
    if (kind == 2) opts |= FOS_PICKFOLDERS;
    if (kind == 1) opts |= FOS_OVERWRITEPROMPT;
    if (kind == 3) opts |= FOS_ALLOWMULTISELECT;
    IFileDialog_SetOptions(dlg, opts);
    if (title != nullptr) IFileDialog_SetTitle(dlg, title);
    if (kind != 2 && spec != nullptr) {
        UINT n = filters(spec, fs);
        if (n > 0) IFileDialog_SetFileTypes(dlg, n, fs);
    }
    if (name != nullptr) IFileDialog_SetFileName(dlg, name);
    if (folder != nullptr) {
        HMODULE shell = GetModuleHandleW(L"shell32.dll");
        if (shell == nullptr) shell = LoadLibraryW(L"shell32.dll");
        item_from_path_fn make = shell != nullptr
                                     ? (item_from_path_fn)(void (*)(void))GetProcAddress(shell, "SHCreateItemFromParsingName")
                                     : nullptr;
        IShellItem *where = nullptr;
        if (make != nullptr && SUCCEEDED(make(folder, nullptr, &gates_iid_shell_item, (void **)&where))) {
            IFileDialog_SetFolder(dlg, where);
            IShellItem_Release(where);
        }
    }
    hr = IFileDialog_Show(dlg, win->hwnd);
    gates_err_t err = GATES_OK;
    if (SUCCEEDED(hr) && kind == 3) {
        IShellItemArray *arr = nullptr; /* an IFileOpenDialog: it begins as an IFileDialog */
        hr = IFileOpenDialog_GetResults((IFileOpenDialog *)dlg, &arr);
        err = SUCCEEDED(hr) ? answers(arr, buf, cap, len, count) : PROVEN_ERR_INVALID_STATE;
        if (arr != nullptr) IShellItemArray_Release(arr);
    } else if (SUCCEEDED(hr)) {
        IShellItem *item = nullptr;
        hr = IFileDialog_GetResult(dlg, &item);
        wchar_t *path = nullptr;
        if (SUCCEEDED(hr)) hr = IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path);
        err = SUCCEEDED(hr) ? answer(path, buf, cap, len) : PROVEN_ERR_INVALID_STATE;
        if (path != nullptr) CoTaskMemFree(path);
        if (item != nullptr) IShellItem_Release(item);
    } else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        err = PROVEN_ERR_INVALID_STATE;
    } /* cancelled: OK with *len = 0 */
    IFileDialog_Release(dlg);
    wfree(title);
    wfree(spec);
    wfree(name);
    wfree(folder);
    gates_win32_after_input(win);
    return err;
}

gates_err_t gates_window_open_file(gates_window_t *win, const gates_file_dialog_t *desc, gates_u8 *buf,
                                   gates_usize_t cap, gates_usize_t *len) {
    return file_dialog(win, desc, 0, buf, cap, len, nullptr);
}

gates_err_t gates_window_open_files(gates_window_t *win, const gates_file_dialog_t *desc, gates_u8 *buf,
                                    gates_usize_t cap, gates_usize_t *len, gates_u32 *count) {
    return file_dialog(win, desc, 3, buf, cap, len, count);
}

gates_err_t gates_window_save_file(gates_window_t *win, const gates_file_dialog_t *desc, gates_u8 *buf,
                                   gates_usize_t cap, gates_usize_t *len) {
    return file_dialog(win, desc, 1, buf, cap, len, nullptr);
}

gates_err_t gates_window_choose_folder(gates_window_t *win, const gates_file_dialog_t *desc, gates_u8 *buf,
                                       gates_usize_t cap, gates_usize_t *len) {
    return file_dialog(win, desc, 2, buf, cap, len, nullptr);
}

typedef BOOL(WINAPI *choose_color_fn)(LPCHOOSECOLORW);

gates_err_t gates_window_choose_color(gates_window_t *win, gates_color_t *color, bool *chosen) {
    if (win == nullptr || color == nullptr || chosen == nullptr) return PROVEN_ERR_INVALID_ARG;
    *chosen = false;
    HMODULE lib = LoadLibraryW(L"comdlg32.dll");
    choose_color_fn choose = lib != nullptr ? (choose_color_fn)(void (*)(void))GetProcAddress(lib, "ChooseColorW") : nullptr;
    if (choose == nullptr) return PROVEN_ERR_UNSUPPORTED;
    gates_tree_dismiss_menus(win->tree);
    static COLORREF custom[16]; /* the dialog's custom colours, kept for the process */
    CHOOSECOLORW cc = { .lStructSize = sizeof cc, .hwndOwner = win->hwnd,
                        .rgbResult = RGB(color->r, color->g, color->b), .lpCustColors = custom,
                        .Flags = CC_RGBINIT | CC_FULLOPEN };
    if (choose(&cc)) {
        *color = GATES_RGBA(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult), color->a);
        *chosen = true;
    }
    gates_win32_after_input(win);
    return GATES_OK;
}

gates_answer_t gates_window_message(gates_window_t *win, gates_str_t title, gates_str_t text,
                                    gates_message_buttons_t buttons, gates_message_icon_t icon) {
    if (win == nullptr) return GATES_ANSWER_NONE;
    gates_tree_dismiss_menus(win->tree);
    wchar_t *wt = wide(title), *wx = wide(text);
    UINT flags = buttons == GATES_MESSAGE_OK_CANCEL      ? MB_OKCANCEL
                 : buttons == GATES_MESSAGE_YES_NO        ? MB_YESNO
                 : buttons == GATES_MESSAGE_YES_NO_CANCEL ? MB_YESNOCANCEL
                                                          : MB_OK;
    flags |= icon == GATES_MESSAGE_WARNING ? MB_ICONWARNING
             : icon == GATES_MESSAGE_ERROR  ? MB_ICONERROR
             : icon == GATES_MESSAGE_QUESTION ? MB_ICONQUESTION
                                              : MB_ICONINFORMATION;
    int r = MessageBoxW(win->hwnd, wx != nullptr ? wx : L"", wt != nullptr ? wt : L"", flags);
    wfree(wt);
    wfree(wx);
    gates_win32_after_input(win);
    switch (r) {
    case IDOK: return GATES_ANSWER_OK;
    case IDCANCEL: return GATES_ANSWER_CANCEL;
    case IDYES: return GATES_ANSWER_YES;
    case IDNO: return GATES_ANSWER_NO;
    default: return GATES_ANSWER_NONE;
    }
}
