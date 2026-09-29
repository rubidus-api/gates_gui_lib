/* gates_gui_lib — Win32 input translation to the unified pointer/key model
 * (RFC-0001 §15, Phase 1: mouse + keyboard + WM_CHAR). */
#include "gates_win32_internal.h"

#include <windowsx.h> /* GET_X_LPARAM / GET_Y_LPARAM */
#include <imm.h>

#include <gates/widget.h>

/* Platform virtual keys -> the core's semantic keys (RFC: core stays free of
 * platform codes). Unmapped keys arrive as GATES_KEY_NONE with vkey intact. */
static gates_key_t map_vkey(UINT vk) {
    switch (vk) {
    case VK_LEFT:   return GATES_KEY_LEFT;
    case VK_RIGHT:  return GATES_KEY_RIGHT;
    case VK_UP:     return GATES_KEY_UP;
    case VK_DOWN:   return GATES_KEY_DOWN;
    case VK_HOME:   return GATES_KEY_HOME;
    case VK_END:    return GATES_KEY_END;
    case VK_BACK:   return GATES_KEY_BACKSPACE;
    case VK_DELETE: return GATES_KEY_DELETE;
    case VK_RETURN: return GATES_KEY_ENTER;
    case VK_TAB:    return GATES_KEY_TAB;
    case VK_ESCAPE: return GATES_KEY_ESCAPE;
    case 'A':       return GATES_KEY_A;
    case 'C':       return GATES_KEY_C;
    case 'X':       return GATES_KEY_X;
    case 'V':       return GATES_KEY_V;
    case 'Z':       return GATES_KEY_Z;
    case 'Y':       return GATES_KEY_Y;
    case VK_SPACE:  return GATES_KEY_SPACE;
    case VK_PRIOR:  return GATES_KEY_PAGE_UP;
    case VK_NEXT:   return GATES_KEY_PAGE_DOWN;
    case VK_F1:  return GATES_KEY_F1;
    case VK_F2:  return GATES_KEY_F2;
    case VK_F3:  return GATES_KEY_F3;
    case VK_F4:  return GATES_KEY_F4;
    case VK_F5:  return GATES_KEY_F5;
    case VK_F6:  return GATES_KEY_F6;
    case VK_F7:  return GATES_KEY_F7;
    case VK_F8:  return GATES_KEY_F8;
    case VK_F9:  return GATES_KEY_F9;
    case VK_F10: return GATES_KEY_F10;
    case VK_F11: return GATES_KEY_F11;
    case VK_F12: return GATES_KEY_F12;
    default:        return GATES_KEY_NONE;
    }
}

static void fill_common(gates_window_t *win, gates_pointer_event_t *ev,
                        gates_point_t pos) {
    ev->pointer_id = 0;
    ev->type = GATES_POINTER_MOUSE;
    POINT sp = { pos.x, pos.y };
    ClientToScreen(win->hwnd, &sp);
    ev->screen_pos = (gates_point_t){ sp.x, sp.y }; /* device pixels, as the OS gives them */
    /* The tree lives in logical units (plan-0013): the unit containing the pixel. */
    pos = (gates_point_t){ gates_logical(pos.x, win->dpi), gates_logical(pos.y, win->dpi) };
    ev->pos = pos;
    if (win->have_last_pos) {
        ev->delta = (gates_vec2_t){ (float)(pos.x - win->last_pos.x),
                                    (float)(pos.y - win->last_pos.y) };
    }
    ev->buttons = win->buttons;
    ev->pressure = win->buttons != 0 ? 1.0f : 0.0f;
    ev->primary = true;
    win->last_pos = pos;
    win->have_last_pos = true;
}

static void dispatch(gates_window_t *win, const gates_pointer_event_t *ev) {
    /* Widget routing first (hover/press/click/toggle), then the app hook. */
    (void)gates_input_pointer(win->tree, ev);
    if (win->cb.on_pointer != nullptr) {
        win->cb.on_pointer(win, ev, win->cb.user_data);
    }
    gates_win32_after_input(win);
}

static void button_event(gates_window_t *win, gates_point_t pos, gates_u32 button,
                         bool down, gates_u32 clicks) {
    if (down) {
        /* Finish a composition before the click can move focus or the caret,
         * so its result lands in the box it was typed into. */
        gates_win32_ime_complete(win);
        if (win->buttons == 0) {
            SetCapture(win->hwnd); /* keep drags coherent outside the client */
        }
        win->buttons |= button;
    } else {
        win->buttons &= ~button;
    }
    gates_pointer_event_t ev = {
        .action = down ? GATES_POINTER_DOWN : GATES_POINTER_UP,
        .button = button,
        .clicks = clicks,
    };
    fill_common(win, &ev, pos);
    dispatch(win, &ev);
    /* Release only after the UP was routed: ReleaseCapture sends
     * WM_CAPTURECHANGED at once, which cancels any press still held. */
    if (!down && win->buttons == 0 && GetCapture() == win->hwnd) {
        ReleaseCapture();
    }
}

bool gates_win32_handle_input(gates_window_t *win, UINT msg, WPARAM wparam, LPARAM lparam) {
    gates_point_t pos = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };

    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!win->leave_tracked) {
            /* Hear when the pointer leaves, so hover and tooltips end (plan-0018). */
            TRACKMOUSEEVENT tme = { .cbSize = sizeof tme, .dwFlags = TME_LEAVE, .hwndTrack = win->hwnd };
            win->leave_tracked = TrackMouseEvent(&tme) != FALSE;
        }
        gates_pointer_event_t ev = { .action = GATES_POINTER_MOVE };
        fill_common(win, &ev, pos);
        dispatch(win, &ev);
        return true;
    }
    case WM_MOUSELEAVE: {
        win->leave_tracked = false;
        gates_pointer_event_t ev = { .action = GATES_POINTER_MOVE };
        fill_common(win, &ev, (gates_point_t){ -1, -1 }); /* over nothing */
        dispatch(win, &ev);
        return true;
    }
    /* The class has CS_DBLCLKS: the second press of a double click arrives as
     * WM_*BUTTONDBLCLK instead of WM_*BUTTONDOWN (plan-0011 views). */
    case WM_LBUTTONDOWN:   button_event(win, pos, GATES_BUTTON_LEFT, true, 1);   return true;
    case WM_LBUTTONDBLCLK: button_event(win, pos, GATES_BUTTON_LEFT, true, 2);   return true;
    case WM_LBUTTONUP:     button_event(win, pos, GATES_BUTTON_LEFT, false, 1);  return true;
    case WM_RBUTTONDOWN:   button_event(win, pos, GATES_BUTTON_RIGHT, true, 1);  return true;
    case WM_RBUTTONDBLCLK: button_event(win, pos, GATES_BUTTON_RIGHT, true, 2);  return true;
    case WM_RBUTTONUP:     button_event(win, pos, GATES_BUTTON_RIGHT, false, 1); return true;
    case WM_MBUTTONDOWN:   button_event(win, pos, GATES_BUTTON_MIDDLE, true, 1); return true;
    case WM_MBUTTONDBLCLK: button_event(win, pos, GATES_BUTTON_MIDDLE, true, 2); return true;
    case WM_MBUTTONUP:     button_event(win, pos, GATES_BUTTON_MIDDLE, false, 1); return true;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        /* Wheel coordinates arrive in screen space; convert to client. */
        POINT cp = { pos.x, pos.y };
        ScreenToClient(win->hwnd, &cp);
        float notches = (float)GET_WHEEL_DELTA_WPARAM(wparam) / (float)WHEEL_DELTA;
        gates_pointer_event_t ev = { .action = GATES_POINTER_WHEEL };
        fill_common(win, &ev, (gates_point_t){ cp.x, cp.y });
        /* Shift+wheel scrolls sideways, as in most Windows lists. */
        bool sideways = msg == WM_MOUSEHWHEEL || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        ev.wheel = sideways ? (gates_vec2_t){ msg == WM_MOUSEHWHEEL ? notches : -notches, 0.0f }
                            : (gates_vec2_t){ 0.0f, notches };
        dispatch(win, &ev);
        return true;
    }

    case WM_SYSCHAR:
        /* Alt+letter: a mnemonic (plan-0018); anything else stays with Windows
         * (Alt+Space, Alt+F4 come as other messages or fail to match). */
        if (gates_input_mnemonic(win->tree, (gates_u32)wparam)) {
            gates_win32_after_input(win);
            return true;
        }
        return false;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (wparam == VK_MENU && msg == WM_SYSKEYDOWN) {
            gates_input_show_cues(win->tree); /* underline mnemonics while Alt is used */
            gates_win32_after_input(win);
            return false;                     /* Windows still turns Alt alone into SC_KEYMENU */
        }
        if (wparam == VK_DOWN || wparam == VK_UP) {
            /* Alt+Down opens a choice's list (plan-0010); unused, it goes to Windows. */
            gates_key_event_t akev = {
                .vkey = (gates_u32)wparam,
                .key = map_vkey((UINT)wparam),
                .down = msg == WM_SYSKEYDOWN,
                .repeat = msg == WM_SYSKEYDOWN && (lparam & 0x40000000) != 0,
                .shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                .ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                .alt = true,
            };
            if (!gates_input_key(win->tree, &akev)) {
                return false;
            }
            gates_win32_after_input(win);
            return true;
        }
        if (wparam != VK_F10) {
            return false; /* Alt combinations (Alt+F4, menus) stay with Windows */
        }
        [[fallthrough]];
    case WM_KEYDOWN:
    case WM_KEYUP: {
        if (wparam == VK_PROCESSKEY) {
            return true; /* the IME took this key; it is not the textbox's */
        }
        gates_key_event_t kev = {
            .vkey = (gates_u32)wparam,
            .key = map_vkey((UINT)wparam),
            .down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN,
            .repeat = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && (lparam & 0x40000000) != 0,
            .shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0,
            .ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
            .alt = (GetKeyState(VK_MENU) & 0x8000) != 0,
            .letter = ((wparam >= 'A' && wparam <= 'Z') || (wparam >= '0' && wparam <= '9'))
                          ? (gates_u8)wparam
                          : 0,
        };
        /* The focused widget gets first refusal, then the application. */
        bool consumed = gates_input_key(win->tree, &kev);
        if (!consumed && win->cb.on_key != nullptr) {
            win->cb.on_key(win, &kev, win->cb.user_data);
        }
        gates_win32_after_input(win);
        return true;
    }
    case WM_CHAR: {
        /* UTF-16 code units arrive one at a time; join surrogate pairs before
         * handing a full codepoint to the core. */
        gates_u32 unit = (gates_u32)wparam;
        gates_u32 cp = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            win->pending_lead = unit;
            return true;
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF && win->pending_lead != 0) {
            cp = 0x10000 + ((win->pending_lead - 0xD800) << 10) + (unit - 0xDC00);
            win->pending_lead = 0;
        }
        bool consumed = gates_input_char(win->tree, cp);
        if (!consumed && win->cb.on_char != nullptr) {
            win->cb.on_char(win, cp, win->cb.user_data);
        }
        gates_win32_after_input(win);
        return true;
    }

    default:
        return false;
    }
}

/* -- IMM32 composition (plan-0006) -------------------------------------------
 * The installed IME (Microsoft IME, or a TSF IME through the system's IMM32
 * compatibility layer) owns language conversion; this adapter only moves its
 * strings into the focused textbox. GCS_RESULTSTR is the only commit;
 * END composition merely clears a leftover preedit. */

static bool ime_has_target(gates_window_t *win) {
    gates_node_t focus = gates_tree_focus(win->tree);
    return gates_textbox_edit(win->tree, focus) != nullptr &&
           !gates_widget_disabled(win->tree, focus);
}

/* Application code (an event handler) moved focus while the IME composes:
 * the core already dropped the preedit, so the IME must drop its composition
 * too, or its next update would compose into the new focus. */
static void ime_follow_focus(gates_window_t *win) {
    if (!win->ime_active) {
        return;
    }
    gates_node_t now = gates_tree_focus(win->tree);
    if (now.index == win->ime_node.index && now.generation == win->ime_node.generation) {
        return;
    }
    HIMC himc = ImmGetContext(win->hwnd);
    if (himc != nullptr) {
        ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
        ImmReleaseContext(win->hwnd, himc);
    }
    win->ime_active = false;
}

/* Read-only and password boxes (and read-only editors) take no IME
 * composition: detach the input context while one has focus, re-attach it
 * afterwards. */
static void ime_policy(gates_window_t *win) {
    gates_node_t f = gates_tree_focus(win->tree);
    bool off = gates_textbox_read_only(win->tree, f) || gates_textbox_password(win->tree, f) ||
               gates_editor_read_only(win->tree, f);
    if (off && !win->ime_off) {
        win->ime_saved = ImmAssociateContext(win->hwnd, nullptr);
        win->ime_off = true;
    } else if (!off && win->ime_off) {
        (void)ImmAssociateContext(win->hwnd, win->ime_saved);
        win->ime_off = false;
    }
}

void gates_win32_after_input(gates_window_t *win) {
    if (win->hwnd == nullptr) {
        return;
    }
    /* Safe point: the input operation has restored its invariants. */
    gates_u32 left = gates_tree_dispatch_events(win->tree, GATES_WIN32_EVENTS_PER_TURN);
    (void)gates_tree_flush_destroys(win->tree);
    ime_follow_focus(win);
    ime_policy(win);
    if (left > 0 && !win->dispatch_posted) {
        win->dispatch_posted = PostMessageW(win->hwnd, GATES_WM_DISPATCH, 0, 0) != 0;
    }
    if ((gates_tree_dirty(win->tree) &
         (GATES_TREE_DIRTY_PAINT | GATES_TREE_DIRTY_LAYOUT)) != 0) {
        InvalidateRect(win->hwnd, nullptr, FALSE);
    }
    gates_win32_uia_events(win);
    gates_win32_caret_follow(win);
}

void gates_win32_cancel_pointer(gates_window_t *win) {
    win->buttons = 0;
    gates_input_cancel_pointer(win->tree);
    gates_win32_after_input(win);
}

/* Reads one composition string as UTF-8 (allocated with the app allocator).
 * `cursor_units` (UTF-16 units, may be negative = none) becomes a byte offset
 * in *out_cursor. Returns false on allocation failure. */
static bool ime_read(gates_window_t *win, HIMC himc, DWORD which, LONG cursor_units,
                     gates_u8 **out, gates_u32 *out_len, gates_u32 *out_cursor) {
    *out = nullptr;
    *out_len = 0;
    *out_cursor = 0;
    LONG bytes = ImmGetCompositionStringW(himc, which, nullptr, 0);
    if (bytes <= 0) {
        return true; /* empty string */
    }
    gates_allocator_t a = win->app->alloc;
    gates_u32 units = (gates_u32)bytes / sizeof(wchar_t);
    proven_result_mem_mut_t wres = a.alloc_fn(a.ctx, (gates_usize_t)bytes, alignof(wchar_t));
    if (!proven_is_ok(wres.err)) {
        return false;
    }
    wchar_t *w = (wchar_t *)wres.value.ptr;
    ImmGetCompositionStringW(himc, which, w, (DWORD)bytes);

    /* A UTF-16 unit never needs more than 3 UTF-8 bytes (a pair needs 4 for 2). */
    proven_result_mem_mut_t ures = a.alloc_fn(a.ctx, (gates_usize_t)units * 3u + 1u, 1);
    if (!proven_is_ok(ures.err)) {
        a.free_fn(a.ctx, w);
        return false;
    }
    gates_u8 *u = (gates_u8 *)ures.value.ptr;
    gates_u32 n = 0;
    gates_u32 cursor = 0;
    for (gates_u32 i = 0; i < units; i++) {
        if (cursor_units >= 0 && (LONG)i == cursor_units) {
            cursor = n;
        }
        gates_u32 cp = w[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units && w[i + 1] >= 0xDC00 &&
            w[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((gates_u32)w[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD; /* lone surrogate */
        }
        if (cp < 0x80) {
            u[n++] = (gates_u8)cp;
        } else if (cp < 0x800) {
            u[n++] = (gates_u8)(0xC0 | (cp >> 6));
            u[n++] = (gates_u8)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            u[n++] = (gates_u8)(0xE0 | (cp >> 12));
            u[n++] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
            u[n++] = (gates_u8)(0x80 | (cp & 0x3F));
        } else {
            u[n++] = (gates_u8)(0xF0 | (cp >> 18));
            u[n++] = (gates_u8)(0x80 | ((cp >> 12) & 0x3F));
            u[n++] = (gates_u8)(0x80 | ((cp >> 6) & 0x3F));
            u[n++] = (gates_u8)(0x80 | (cp & 0x3F));
        }
    }
    if (cursor_units < 0 || (gates_u32)cursor_units >= units) {
        cursor = n;
    }
    a.free_fn(a.ctx, w);
    *out = u;
    *out_len = n;
    *out_cursor = cursor;
    return true;
}

void gates_win32_ime_place(gates_window_t *win) {
    gates_rect_t r;
    if (!gates_input_caret_rect(win->tree, &r)) {
        return;
    }
    HIMC himc = ImmGetContext(win->hwnd);
    if (himc == nullptr) {
        return;
    }
    r = gates_rect_px(r, win->dpi); /* the IME works in device pixels */
    COMPOSITIONFORM cf = { .dwStyle = CFS_POINT, .ptCurrentPos = { r.x, r.y } };
    ImmSetCompositionWindow(himc, &cf);
    /* Candidates below the caret, never over the line being composed. */
    CANDIDATEFORM cand = {
        .dwIndex = 0,
        .dwStyle = CFS_EXCLUDE,
        .ptCurrentPos = { r.x, r.y + r.h },
        .rcArea = { r.x, r.y, r.x + r.w, r.y + r.h },
    };
    ImmSetCandidateWindow(himc, &cand);
    ImmReleaseContext(win->hwnd, himc);
}

void gates_win32_ime_complete(gates_window_t *win) {
    if (!gates_input_composing(win->tree)) {
        return;
    }
    HIMC himc = ImmGetContext(win->hwnd);
    if (himc != nullptr) {
        /* The IME delivers what it has as GCS_RESULTSTR, synchronously. */
        ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
        ImmReleaseContext(win->hwnd, himc);
    }
    if (gates_input_composing(win->tree)) {
        (void)gates_input_preedit_cancel(win->tree); /* IME kept it: drop, never invent */
    }
    gates_win32_after_input(win);
}

bool gates_win32_handle_ime(gates_window_t *win, UINT msg, WPARAM wparam, LPARAM lparam,
                            LRESULT *result) {
    switch (msg) {
    case WM_IME_SETCONTEXT:
        /* We draw the composition inline; the IME keeps its candidate UI. */
        if (wparam) {
            lparam &= ~(LPARAM)ISC_SHOWUICOMPOSITIONWINDOW;
        }
        *result = DefWindowProcW(win->hwnd, msg, wparam, lparam);
        return true;

    case WM_IME_STARTCOMPOSITION:
        if (!ime_has_target(win)) {
            return false; /* default IME window for windows without a textbox */
        }
        win->ime_node = gates_tree_focus(win->tree);
        win->ime_active = true;
        gates_win32_ime_place(win);
        *result = 0;
        return true;

    case WM_IME_COMPOSITION: {
        if (!ime_has_target(win)) {
            return false;
        }
        HIMC himc = ImmGetContext(win->hwnd);
        if (himc == nullptr) {
            return false;
        }
        gates_allocator_t a = win->app->alloc;
        gates_u8 *s = nullptr;
        gates_u32 len = 0, cur = 0;
        if ((lparam & GCS_RESULTSTR) != 0 &&
            ime_read(win, himc, GCS_RESULTSTR, -1, &s, &len, &cur)) {
            if (len > 0) {
                (void)gates_input_commit(win->tree, (gates_str_t){ .ptr = s, .size = len });
            }
            if (s != nullptr) {
                a.free_fn(a.ctx, s);
            }
        }
        if ((lparam & GCS_COMPSTR) != 0) {
            LONG cursor_units = -1;
            if ((lparam & GCS_CURSORPOS) != 0) {
                cursor_units = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
            }
            if (ime_read(win, himc, GCS_COMPSTR, cursor_units, &s, &len, &cur)) {
                (void)gates_input_preedit(win->tree, (gates_str_t){ .ptr = s, .size = len },
                                          cur);
                if (s != nullptr) {
                    a.free_fn(a.ctx, s);
                }
            }
        } else if (lparam == 0) {
            /* lParam 0: composition cancelled. Other flag-only updates (cursor or
             * attribute changes without GCS_COMPSTR) keep the preedit as it is. */
            (void)gates_input_preedit_cancel(win->tree);
        }
        ImmReleaseContext(win->hwnd, himc);
        gates_win32_after_input(win);
        *result = 0; /* handled: no WM_IME_CHAR/WM_CHAR copies of the result */
        return true;
    }

    case WM_IME_ENDCOMPOSITION:
        if (!ime_has_target(win)) {
            return false;
        }
        (void)gates_input_preedit_cancel(win->tree); /* leftover only; never commits */
        win->ime_active = false;
        gates_win32_after_input(win);
        *result = 0;
        return true;

    case WM_IME_CHAR:
        /* Results arrive through GCS_RESULTSTR; a stray WM_IME_CHAR would
         * become a second WM_CHAR copy of the same text. */
        if (!ime_has_target(win)) {
            return false;
        }
        *result = 0;
        return true;

    default:
        return false;
    }
}
