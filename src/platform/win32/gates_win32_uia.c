/* gates_gui_lib - UI Automation providers.
 *
 * Every element a client sees is a small COM object holding only a key: the
 * window, the tree's serial number and an accessible reference (node handle,
 * item id). Each call looks the key up again, so a provider that outlives its
 * control answers UIA_E_ELEMENTNOTAVAILABLE and never touches freed memory.
 * The objects are allocated from the process heap, not the application's
 * allocator: a client may release one after the window and the app are gone.
 *
 * Providers ask for COM threading and the UI thread is a single-threaded
 * apartment (gates_app_create), so UIA calls them on the UI thread; a call on
 * another thread is refused rather than touching the tree. */
#include "gates_win32_internal.h"

#include <gates/access.h>

#include <ole2.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <stddef.h>
#include <string.h>

#ifndef UIA_E_ELEMENTNOTENABLED
#define UIA_E_ELEMENTNOTENABLED ((HRESULT)0x80040200L)
#endif
#ifndef UIA_E_ELEMENTNOTAVAILABLE
#define UIA_E_ELEMENTNOTAVAILABLE ((HRESULT)0x80040201L)
#endif
#ifndef UIA_E_INVALIDOPERATION
#define UIA_E_INVALIDOPERATION ((HRESULT)0x80131509L)
#endif

/* What a client last heard about an element, to raise only real changes. */
typedef struct uia_snap_t {
    gates_u32 name, value, help;     /* FNV-1a hashes */
    gates_u32 states;
    gates_i32 range;
    gates_u32 caret, anchor;         /* edits */
    gates_u64 items;                 /* views: which rows are shown (count, first id) */
} uia_snap_t;

typedef struct uia_el_t {
    IRawElementProviderSimple simple;
    IRawElementProviderFragment frag;
    IRawElementProviderFragmentRoot root;
    IInvokeProvider invoke;
    IToggleProvider toggle;
    IValueProvider value;
    IRangeValueProvider range;
    ISelectionProvider sel;
    ISelectionItemProvider selitem;
    IExpandCollapseProvider expand;
    IGridProvider grid;
    IGridItemProvider griditem;
    IScrollProvider scroll;
    IWindowProvider window;
    ITextProvider2 text;
    LONG refs;
    gates_window_t *win;             /* null once the window is gone */
    gates_u64 serial;
    gates_access_ref_t ref;
    gates_u32 cell;                  /* a table row's cell (0.10.0): its shown column + 1; 0 = none */
    bool is_root;
    bool gone;                       /* its node was destroyed: disconnected */
    uia_snap_t snap;
    struct uia_el_t *next;           /* the window's list of live providers */
} uia_el_t;

#define EL_OF(ptr, field) ((uia_el_t *)((char *)(ptr) - offsetof(uia_el_t, field)))

/* -- small helpers ------------------------------------------------------------------ */

static gates_u32 hash_str(gates_str_t s) {
    gates_u32 h = 2166136261u;
    for (size_t i = 0; i < s.size; i++) h = (h ^ s.ptr[i]) * 16777619u;
    return h ^ (gates_u32)s.size;
}

static BSTR bstr_of(gates_str_t s) {
    if (s.size == 0 || s.ptr == nullptr) return SysAllocString(L"");
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char *)s.ptr, (int)s.size, nullptr, 0);
    BSTR b = SysAllocStringLen(nullptr, (UINT)(n > 0 ? n : 0));
    if (b != nullptr && n > 0) MultiByteToWideChar(CP_UTF8, 0, (const char *)s.ptr, (int)s.size, b, n);
    return b;
}

static void v_bool(VARIANT *v, bool b) {
    v->vt = VT_BOOL;
    v->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
}

static void v_i4(VARIANT *v, int i) {
    v->vt = VT_I4;
    v->lVal = i;
}

static void v_str(VARIANT *v, gates_str_t s) {
    BSTR b = bstr_of(s);
    if (b != nullptr) {
        v->vt = VT_BSTR;
        v->bstrVal = b;
    }
}

static bool ref_eq(gates_access_ref_t a, gates_access_ref_t b) {
    return gates_node_eq(a.node, b.node) && a.item == b.item;
}

static bool ref_null(gates_access_ref_t r) {
    return gates_node_eq(r.node, GATES_NODE_NULL);
}

static bool on_ui_thread(const gates_window_t *win) {
    return win != nullptr && win->hwnd != nullptr &&
           GetWindowThreadProcessId(win->hwnd, nullptr) == GetCurrentThreadId();
}

/* The element's info now, or why not: gone, stale, wrong thread. */
static HRESULT el_info(uia_el_t *e, gates_access_info_t *out) {
    if (e->gone || !on_ui_thread(e->win) || gates_tree_serial(e->win->tree) != e->serial) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    gates_err_t err = e->cell != 0 ? gates_access_cell_info(e->win->tree, e->ref.node, e->ref.item, e->cell, out)
                                   : gates_access_info(e->win->tree, e->ref.node, e->ref.item, out);
    return gates_is_ok(err) ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

static uia_snap_t snap_of(const gates_access_info_t *i) {
    return (uia_snap_t){
        .name = hash_str(i->name), .value = hash_str(i->value), .help = hash_str(i->description),
        .states = i->states, .range = i->range_value, .caret = i->caret, .anchor = i->anchor,
    };
}

/* Views: which rows are elements now (after the info: item_at asks the model). */
static gates_u64 items_of(gates_tree_t *t, gates_access_ref_t ref, const gates_access_info_t *i) {
    if (ref.item != 0 || (i->role != GATES_ROLE_LIST && i->role != GATES_ROLE_TABLE && i->role != GATES_ROLE_TREE)) {
        return 0;
    }
    return i->item_count * 0x9E3779B97F4A7C15ull ^ gates_access_item_at(t, ref.node, 0);
}

/* -- lifetime ------------------------------------------------------------------------ */

static const IRawElementProviderSimpleVtbl simple_vtbl;
static const IRawElementProviderFragmentVtbl frag_vtbl;
static const IRawElementProviderFragmentRootVtbl root_vtbl;
static const IInvokeProviderVtbl invoke_vtbl;
static const IToggleProviderVtbl toggle_vtbl;
static const IValueProviderVtbl value_vtbl;
static const IRangeValueProviderVtbl range_vtbl;
static const ISelectionProviderVtbl sel_vtbl;
static const ISelectionItemProviderVtbl selitem_vtbl;
static const IExpandCollapseProviderVtbl expand_vtbl;
static const IGridProviderVtbl grid_vtbl;
static const IGridItemProviderVtbl griditem_vtbl;
static const IScrollProviderVtbl scroll_vtbl;
static const IWindowProviderVtbl window_vtbl;
static const ITextProvider2Vtbl text_vtbl;

static ULONG el_addref(uia_el_t *e) {
    return (ULONG)InterlockedIncrement(&e->refs);
}

static ULONG el_release(uia_el_t *e) {
    LONG n = InterlockedDecrement(&e->refs);
    if (n == 0) {
        if (e->win != nullptr) {
            for (uia_el_t **p = &e->win->uia_els; *p != nullptr; p = &(*p)->next) {
                if (*p == e) {
                    *p = e->next;
                    break;
                }
            }
        }
        HeapFree(GetProcessHeap(), 0, e);
    }
    return (ULONG)n;
}

/* The provider for ref (one object per element while clients hold it), with
 * a reference for the caller; null for the null reference or no memory. */
static uia_el_t *el_get_cell(gates_window_t *win, gates_access_ref_t ref, gates_u32 cell) {
    if (win == nullptr || win->hwnd == nullptr || ref_null(ref)) return nullptr;
    gates_u64 serial = gates_tree_serial(win->tree);
    for (uia_el_t *e = win->uia_els; e != nullptr; e = e->next) {
        if (!e->gone && e->serial == serial && ref_eq(e->ref, ref) && e->cell == cell) {
            el_addref(e);
            return e;
        }
    }
    uia_el_t *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *e);
    if (e == nullptr) return nullptr;
    e->simple.lpVtbl = (IRawElementProviderSimpleVtbl *)&simple_vtbl;
    e->frag.lpVtbl = (IRawElementProviderFragmentVtbl *)&frag_vtbl;
    e->root.lpVtbl = (IRawElementProviderFragmentRootVtbl *)&root_vtbl;
    e->invoke.lpVtbl = (IInvokeProviderVtbl *)&invoke_vtbl;
    e->toggle.lpVtbl = (IToggleProviderVtbl *)&toggle_vtbl;
    e->value.lpVtbl = (IValueProviderVtbl *)&value_vtbl;
    e->range.lpVtbl = (IRangeValueProviderVtbl *)&range_vtbl;
    e->sel.lpVtbl = (ISelectionProviderVtbl *)&sel_vtbl;
    e->selitem.lpVtbl = (ISelectionItemProviderVtbl *)&selitem_vtbl;
    e->expand.lpVtbl = (IExpandCollapseProviderVtbl *)&expand_vtbl;
    e->grid.lpVtbl = (IGridProviderVtbl *)&grid_vtbl;
    e->griditem.lpVtbl = (IGridItemProviderVtbl *)&griditem_vtbl;
    e->scroll.lpVtbl = (IScrollProviderVtbl *)&scroll_vtbl;
    e->window.lpVtbl = (IWindowProviderVtbl *)&window_vtbl;
    e->text.lpVtbl = (ITextProvider2Vtbl *)&text_vtbl;
    e->refs = 1;
    e->win = win;
    e->serial = serial;
    e->ref = ref;
    e->cell = cell;
    e->is_root = cell == 0 && ref.item == 0 && gates_node_eq(ref.node, gates_tree_root(win->tree));
    gates_access_info_t info;
    if (SUCCEEDED(el_info(e, &info))) {
        e->snap = snap_of(&info);
        e->snap.items = items_of(win->tree, ref, &info);
    }
    e->next = win->uia_els;
    win->uia_els = e;
    return e;
}

static uia_el_t *el_get(gates_window_t *win, gates_access_ref_t ref) {
    return el_get_cell(win, ref, 0);
}

static HRESULT el_qi(uia_el_t *e, REFIID riid, void **out) {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRawElementProviderSimple)) {
        *out = &e->simple;
    } else if (IsEqualIID(riid, &IID_IRawElementProviderFragment)) {
        *out = &e->frag;
    } else if (IsEqualIID(riid, &IID_IRawElementProviderFragmentRoot) && e->is_root) {
        *out = &e->root;
    } else if (IsEqualIID(riid, &IID_IInvokeProvider)) {
        *out = &e->invoke;
    } else if (IsEqualIID(riid, &IID_IToggleProvider)) {
        *out = &e->toggle;
    } else if (IsEqualIID(riid, &IID_IValueProvider)) {
        *out = &e->value;
    } else if (IsEqualIID(riid, &IID_IRangeValueProvider)) {
        *out = &e->range;
    } else if (IsEqualIID(riid, &IID_ISelectionProvider)) {
        *out = &e->sel;
    } else if (IsEqualIID(riid, &IID_ISelectionItemProvider)) {
        *out = &e->selitem;
    } else if (IsEqualIID(riid, &IID_IExpandCollapseProvider)) {
        *out = &e->expand;
    } else if (IsEqualIID(riid, &IID_IGridProvider)) {
        *out = &e->grid;
    } else if (IsEqualIID(riid, &IID_IGridItemProvider)) {
        *out = &e->griditem;
    } else if (IsEqualIID(riid, &IID_IScrollProvider)) {
        *out = &e->scroll;
    } else if (IsEqualIID(riid, &IID_IWindowProvider)) {
        *out = &e->window;
    } else if (IsEqualIID(riid, &IID_ITextProvider) || IsEqualIID(riid, &IID_ITextProvider2)) {
        *out = &e->text;
    } else {
        return E_NOINTERFACE;
    }
    el_addref(e);
    return S_OK;
}

/* IUnknown for each interface of the object. */
#define IUNKNOWN_OF(Iface, field)                                                               \
    static HRESULT STDMETHODCALLTYPE field##_qi(Iface *This, REFIID riid, void **out) {          \
        return el_qi(EL_OF(This, field), riid, out);                                            \
    }                                                                                           \
    static ULONG STDMETHODCALLTYPE field##_addref(Iface *This) { return el_addref(EL_OF(This, field)); } \
    static ULONG STDMETHODCALLTYPE field##_release(Iface *This) { return el_release(EL_OF(This, field)); }

IUNKNOWN_OF(IRawElementProviderSimple, simple)
IUNKNOWN_OF(IRawElementProviderFragment, frag)
IUNKNOWN_OF(IRawElementProviderFragmentRoot, root)
IUNKNOWN_OF(IInvokeProvider, invoke)
IUNKNOWN_OF(IToggleProvider, toggle)
IUNKNOWN_OF(IValueProvider, value)
IUNKNOWN_OF(IRangeValueProvider, range)
IUNKNOWN_OF(ISelectionProvider, sel)
IUNKNOWN_OF(ISelectionItemProvider, selitem)
IUNKNOWN_OF(IExpandCollapseProvider, expand)
IUNKNOWN_OF(IGridProvider, grid)
IUNKNOWN_OF(IGridItemProvider, griditem)
IUNKNOWN_OF(IScrollProvider, scroll)
IUNKNOWN_OF(IWindowProvider, window)
IUNKNOWN_OF(ITextProvider2, text)

/* An element as the interface a caller wants, reference passed on. */
static IRawElementProviderSimple *as_simple(uia_el_t *e) {
    return e != nullptr ? &e->simple : nullptr;
}

static IRawElementProviderFragment *as_frag(uia_el_t *e) {
    return e != nullptr ? &e->frag : nullptr;
}

/* After an action: the same pass as after input (events, repaint, UIA events). */
static HRESULT acted(uia_el_t *e, gates_err_t err) {
    if (e->win != nullptr) gates_win32_after_input(e->win);
    if (gates_is_ok(err)) return S_OK;
    if (err == PROVEN_ERR_INVALID_STATE) return UIA_E_ELEMENTNOTENABLED;
    if (err == PROVEN_ERR_PERMISSION) return UIA_E_INVALIDOPERATION;
    if (err == PROVEN_ERR_NOMEM) return E_OUTOFMEMORY;
    return UIA_E_INVALIDOPERATION;
}

/* -- IRawElementProviderSimple ------------------------------------------------------- */

static bool has_pattern(const gates_access_info_t *i, PATTERNID p) {
    switch (p) {
    case UIA_InvokePatternId:         return (i->actions & GATES_ACCESS_INVOKE) != 0;
    case UIA_TogglePatternId:         return i->role == GATES_ROLE_CHECK_BOX ||
                                             (i->role == GATES_ROLE_CELL && (i->states & GATES_ACCESS_CHECKABLE) != 0);
    case UIA_ValuePatternId:          return i->role == GATES_ROLE_EDIT || i->role == GATES_ROLE_COMBO_BOX ||
                                             (i->role == GATES_ROLE_CELL && (i->states & GATES_ACCESS_CHECKABLE) == 0 && !i->has_range);
    case UIA_RangeValuePatternId:     return i->has_range;
    case UIA_SelectionPatternId:      return i->role == GATES_ROLE_RADIO_GROUP || i->role == GATES_ROLE_COMBO_BOX ||
                                             i->role == GATES_ROLE_LIST || i->role == GATES_ROLE_TABLE ||
                                             i->role == GATES_ROLE_TREE || i->role == GATES_ROLE_TAB;
    case UIA_SelectionItemPatternId:  return i->role == GATES_ROLE_RADIO_ITEM || i->role == GATES_ROLE_LIST_ITEM ||
                                             i->role == GATES_ROLE_ROW || i->role == GATES_ROLE_TREE_ITEM ||
                                             i->role == GATES_ROLE_TAB_ITEM;
    case UIA_ExpandCollapsePatternId: return (i->states & GATES_ACCESS_EXPANDABLE) != 0;
    case UIA_GridPatternId:           return i->role == GATES_ROLE_TABLE;
    case UIA_GridItemPatternId:       return i->role == GATES_ROLE_CELL; /* cells are the grid's items (0.10.0) */
    case UIA_ScrollPatternId:         return (i->actions & GATES_ACCESS_SCROLL) != 0;
    case UIA_WindowPatternId:         return i->role == GATES_ROLE_DIALOG;
    case UIA_TextPatternId:
    case UIA_TextPattern2Id:          return i->role == GATES_ROLE_EDIT && (i->states & GATES_ACCESS_PASSWORD) == 0;
    default:                          return false;
    }
}

static int control_type(gates_role_t role) {
    switch (role) {
    case GATES_ROLE_GROUP:
    case GATES_ROLE_RADIO_GROUP:
    case GATES_ROLE_FORM:         return UIA_GroupControlTypeId;
    case GATES_ROLE_TEXT:         return UIA_TextControlTypeId;
    case GATES_ROLE_BUTTON:       return UIA_ButtonControlTypeId;
    case GATES_ROLE_CHECK_BOX:    return UIA_CheckBoxControlTypeId;
    case GATES_ROLE_RADIO_ITEM:   return UIA_RadioButtonControlTypeId;
    case GATES_ROLE_COMBO_BOX:    return UIA_ComboBoxControlTypeId;
    case GATES_ROLE_LIST_ITEM:    return UIA_ListItemControlTypeId;
    case GATES_ROLE_EDIT:         return UIA_EditControlTypeId;
    case GATES_ROLE_PROGRESS_BAR: return UIA_ProgressBarControlTypeId;
    case GATES_ROLE_SEPARATOR:    return UIA_SeparatorControlTypeId;
    case GATES_ROLE_DIALOG:       return UIA_WindowControlTypeId;
    case GATES_ROLE_MENU:         return UIA_MenuControlTypeId;
    case GATES_ROLE_MENU_ITEM:    return UIA_MenuItemControlTypeId;
    case GATES_ROLE_LIST:         return UIA_ListControlTypeId;
    case GATES_ROLE_TABLE:        return UIA_TableControlTypeId;
    case GATES_ROLE_TREE:         return UIA_TreeControlTypeId;
    case GATES_ROLE_ROW:          return UIA_DataItemControlTypeId;
    case GATES_ROLE_TREE_ITEM:    return UIA_TreeItemControlTypeId;
    case GATES_ROLE_MENU_BAR:     return UIA_MenuBarControlTypeId;
    case GATES_ROLE_TOOL_BAR:     return UIA_ToolBarControlTypeId;
    case GATES_ROLE_STATUS_BAR:   return UIA_StatusBarControlTypeId;
    case GATES_ROLE_TAB:          return UIA_TabControlTypeId;
    case GATES_ROLE_TAB_ITEM:     return UIA_TabItemControlTypeId;
    case GATES_ROLE_SPINNER:      return UIA_SpinnerControlTypeId;
    case GATES_ROLE_SLIDER:       return UIA_SliderControlTypeId;
    case GATES_ROLE_IMAGE:        return UIA_ImageControlTypeId;
    case GATES_ROLE_TOOLTIP:      return UIA_ToolTipControlTypeId;
    default:                      return UIA_PaneControlTypeId;
    }
}

static HRESULT STDMETHODCALLTYPE simple_options(IRawElementProviderSimple *This, enum ProviderOptions *out) {
    (void)This;
    if (out == nullptr) return E_POINTER;
    *out = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE simple_pattern(IRawElementProviderSimple *This, PATTERNID p, IUnknown **out) {
    uia_el_t *e = EL_OF(This, simple);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (!has_pattern(&i, p)) return S_OK;
    void *iface = nullptr;
    switch (p) {
    case UIA_InvokePatternId:         iface = &e->invoke; break;
    case UIA_TogglePatternId:         iface = &e->toggle; break;
    case UIA_ValuePatternId:          iface = &e->value; break;
    case UIA_RangeValuePatternId:     iface = &e->range; break;
    case UIA_SelectionPatternId:      iface = &e->sel; break;
    case UIA_SelectionItemPatternId:  iface = &e->selitem; break;
    case UIA_ExpandCollapsePatternId: iface = &e->expand; break;
    case UIA_GridPatternId:           iface = &e->grid; break;
    case UIA_GridItemPatternId:       iface = &e->griditem; break;
    case UIA_ScrollPatternId:         iface = &e->scroll; break;
    case UIA_WindowPatternId:         iface = &e->window; break;
    case UIA_TextPatternId:
    case UIA_TextPattern2Id:          iface = &e->text; break;
    default:                          return S_OK;
    }
    el_addref(e);
    *out = (IUnknown *)iface;
    return S_OK;
}

#ifndef UIA_ScrollPatternNoScroll
#define UIA_ScrollPatternNoScroll (-1)   /* "does not scroll this way" (UIAutomationCoreApi.h) */
#endif
#ifndef UIA_LevelPropertyId
#define UIA_LevelPropertyId 30154
#endif
#ifndef UIA_IsDialogPropertyId
#define UIA_IsDialogPropertyId 30174
#endif
#ifndef UIA_PositionInSetPropertyId
#define UIA_PositionInSetPropertyId 30152
#endif
#ifndef UIA_SizeOfSetPropertyId
#define UIA_SizeOfSetPropertyId 30153
#endif

static HRESULT STDMETHODCALLTYPE simple_property(IRawElementProviderSimple *This, PROPERTYID id, VARIANT *out) {
    uia_el_t *e = EL_OF(This, simple);
    if (out == nullptr) return E_POINTER;
    VariantInit(out);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    bool exposed = i.role != GATES_ROLE_NONE;
    if (e->is_root && (id == UIA_HasKeyboardFocusPropertyId || id == UIA_IsKeyboardFocusablePropertyId ||
                       id == UIA_IsEnabledPropertyId || id == UIA_IsOffscreenPropertyId ||
                       id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId)) {
        return S_OK; /* the window's own: the host provider answers */
    }
    switch (id) {
    case UIA_ControlTypePropertyId:
        if (!e->is_root) {
            int ct = control_type(i.role);
            if (i.role == GATES_ROLE_CELL) {
                ct = (i.states & GATES_ACCESS_CHECKABLE) != 0 ? UIA_CheckBoxControlTypeId
                   : i.has_range                              ? UIA_ProgressBarControlTypeId
                                                              : UIA_EditControlTypeId;
            }
            v_i4(out, ct); /* the root: the host window's */
        }
        break;
    case UIA_NamePropertyId:
        if (i.name.size > 0) v_str(out, i.name);        /* the root: the window title */
        break;
    case UIA_AutomationIdPropertyId:
        if (i.automation_id.size > 0) v_str(out, i.automation_id);
        break;
    case UIA_HelpTextPropertyId:
        if (i.description.size > 0) v_str(out, i.description);
        break;
    case UIA_AccessKeyPropertyId:        /* "Alt+F" */
        if (i.access_key.size > 0) v_str(out, i.access_key);
        break;
    case UIA_AcceleratorKeyPropertyId:   /* the command's shortcut */
        if (i.accelerator.size > 0) v_str(out, i.accelerator);
        break;
    case UIA_FrameworkIdPropertyId:
        v_str(out, GATES_STR("gates"));
        break;
    case UIA_IsEnabledPropertyId:
        v_bool(out, (i.states & GATES_ACCESS_DISABLED) == 0);
        break;
    case UIA_HasKeyboardFocusPropertyId: {
        gates_access_ref_t f = gates_access_focus_ref(e->win->tree);
        v_bool(out, !ref_null(f) && ref_eq(f, e->ref) && e->cell == 0); /* a cell shares its row's ref */
        break;
    }
    case UIA_IsKeyboardFocusablePropertyId:
        /* an item takes focus by being selected */
        v_bool(out, (i.states & GATES_ACCESS_FOCUSABLE) != 0 ||
                    (e->ref.item != 0 && (i.actions & (GATES_ACCESS_SELECT | GATES_ACCESS_INVOKE)) != 0));
        break;
    case UIA_IsOffscreenPropertyId:
        v_bool(out, (i.states & GATES_ACCESS_OFFSCREEN) != 0);
        break;
    case UIA_IsPasswordPropertyId:
        v_bool(out, (i.states & GATES_ACCESS_PASSWORD) != 0);
        break;
    case UIA_IsRequiredForFormPropertyId:
        v_bool(out, (i.states & GATES_ACCESS_REQUIRED) != 0);
        break;
    case UIA_IsDataValidForFormPropertyId:
        v_bool(out, (i.states & GATES_ACCESS_INVALID) == 0);
        break;
    case UIA_IsControlElementPropertyId:
        v_bool(out, exposed);
        break;
    case UIA_IsContentElementPropertyId:
        v_bool(out, exposed && i.role != GATES_ROLE_SEPARATOR);
        break;
    case UIA_IsDialogPropertyId:
        v_bool(out, i.role == GATES_ROLE_DIALOG);
        break;
    case UIA_LiveSettingPropertyId:
        v_i4(out, i.live == GATES_LIVE_ASSERTIVE ? Assertive : i.live == GATES_LIVE_POLITE ? Polite : Off);
        break;
    case UIA_LabeledByPropertyId:
        if (!gates_node_eq(i.labelled_by, GATES_NODE_NULL)) {
            uia_el_t *l = el_get(e->win, (gates_access_ref_t){ i.labelled_by, 0 });
            if (l != nullptr) {
                out->vt = VT_UNKNOWN;
                out->punkVal = (IUnknown *)&l->simple; /* the reference goes with the variant */
            }
        }
        break;
    case UIA_LevelPropertyId:
        if (i.level > 0) v_i4(out, (int)i.level);
        break;
    case UIA_PositionInSetPropertyId:
    case UIA_SizeOfSetPropertyId:
        if (i.set_position != 0) {
            gates_u64 n = id == UIA_SizeOfSetPropertyId ? i.set_size : i.set_position;
            v_i4(out, n > 0x7FFFFFFF ? 0x7FFFFFFF : (int)n);
        }
        break;
    default:
        break;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE simple_host(IRawElementProviderSimple *This, IRawElementProviderSimple **out) {
    uia_el_t *e = EL_OF(This, simple);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    if (!e->is_root) return S_OK;
    if (e->win == nullptr || e->win->hwnd == nullptr) return UIA_E_ELEMENTNOTAVAILABLE;
    return UiaHostProviderFromHwnd(e->win->hwnd, out);
}

static const IRawElementProviderSimpleVtbl simple_vtbl = {
    simple_qi, simple_addref, simple_release,
    simple_options, simple_pattern, simple_property, simple_host,
};

/* -- IRawElementProviderFragment ----------------------------------------------------- */

static HRESULT STDMETHODCALLTYPE frag_navigate(IRawElementProviderFragment *This, enum NavigateDirection dir,
                                               IRawElementProviderFragment **out) {
    uia_el_t *e = EL_OF(This, frag);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    gates_tree_t *t = e->win->tree;
    gates_access_ref_t to;
    gates_u32 nc = e->cell == 0 && e->ref.item != 0 ? gates_access_cell_count(t, e->ref.node, e->ref.item) : 0;
    if (e->cell != 0) { /* a cell (0.10.0): its row, and the cells beside it */
        gates_u32 n = gates_access_cell_count(t, e->ref.node, e->ref.item);
        switch (dir) {
        case NavigateDirection_Parent:          *out = as_frag(el_get(e->win, e->ref)); return S_OK;
        case NavigateDirection_NextSibling:     if (e->cell < n) *out = as_frag(el_get_cell(e->win, e->ref, e->cell + 1)); return S_OK;
        case NavigateDirection_PreviousSibling: if (e->cell > 1) *out = as_frag(el_get_cell(e->win, e->ref, e->cell - 1)); return S_OK;
        case NavigateDirection_FirstChild:
        case NavigateDirection_LastChild:       return S_OK;
        default:                                return E_INVALIDARG;
        }
    }
    if (nc > 0 && (dir == NavigateDirection_FirstChild || dir == NavigateDirection_LastChild)) { /* a table row's cells */
        *out = as_frag(el_get_cell(e->win, e->ref, dir == NavigateDirection_FirstChild ? 1 : nc));
        return S_OK;
    }
    switch (dir) {
    case NavigateDirection_Parent:          to = e->is_root ? (gates_access_ref_t){ GATES_NODE_NULL, 0 }
                                                             : gates_access_parent(t, e->ref); break;
    case NavigateDirection_NextSibling:     to = e->is_root ? (gates_access_ref_t){ GATES_NODE_NULL, 0 }
                                                             : gates_access_next(t, e->ref); break;
    case NavigateDirection_PreviousSibling: to = e->is_root ? (gates_access_ref_t){ GATES_NODE_NULL, 0 }
                                                             : gates_access_prev(t, e->ref); break;
    case NavigateDirection_FirstChild:      to = gates_access_first_child(t, e->ref); break;
    case NavigateDirection_LastChild:       to = gates_access_last_child(t, e->ref); break;
    default:                                return E_INVALIDARG;
    }
    *out = as_frag(el_get(e->win, to));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frag_runtime_id(IRawElementProviderFragment *This, SAFEARRAY **out) {
    uia_el_t *e = EL_OF(This, frag);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (e->is_root) return S_OK; /* UIA makes the root's from the window handle */
    LONG np = e->cell != 0 ? 6 : 5;
    SAFEARRAY *sa = SafeArrayCreateVector(VT_I4, 0, (ULONG)np);
    if (sa == nullptr) return E_OUTOFMEMORY;
    int parts[6] = { UiaAppendRuntimeId, (int)e->ref.node.index, (int)e->ref.node.generation,
                     (int)(e->ref.item & 0xffffffffu), (int)(e->ref.item >> 32), (int)e->cell };
    for (LONG k = 0; k < np; k++) SafeArrayPutElement(sa, &k, &parts[k]);
    *out = sa;
    return S_OK;
}

/* Logical window rectangle -> screen pixels. */
static struct UiaRect screen_rect(const gates_window_t *win, gates_rect_t r) {
    if (gates_rect_is_empty(r)) return (struct UiaRect){ 0, 0, 0, 0 };
    gates_rect_t px = gates_rect_px(r, win->dpi);
    POINT o = { 0, 0 };
    ClientToScreen(win->hwnd, &o);
    return (struct UiaRect){ o.x + px.x, o.y + px.y, px.w, px.h };
}

static HRESULT STDMETHODCALLTYPE frag_bounds(IRawElementProviderFragment *This, struct UiaRect *out) {
    uia_el_t *e = EL_OF(This, frag);
    if (out == nullptr) return E_POINTER;
    *out = (struct UiaRect){ 0, 0, 0, 0 };
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (e->is_root) {
        RECT rc;
        GetClientRect(e->win->hwnd, &rc);
        POINT o = { 0, 0 };
        ClientToScreen(e->win->hwnd, &o);
        *out = (struct UiaRect){ o.x, o.y, rc.right, rc.bottom };
    } else {
        *out = screen_rect(e->win, i.bounds);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frag_embedded(IRawElementProviderFragment *This, SAFEARRAY **out) {
    (void)This;
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frag_set_focus(IRawElementProviderFragment *This) {
    uia_el_t *e = EL_OF(This, frag);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (e->is_root) return S_OK;
    return acted(e, gates_access_focus(e->win->tree, e->ref.node, e->ref.item));
}

static HRESULT STDMETHODCALLTYPE frag_root(IRawElementProviderFragment *This, IRawElementProviderFragmentRoot **out) {
    uia_el_t *e = EL_OF(This, frag);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    uia_el_t *r = el_get(e->win, (gates_access_ref_t){ gates_tree_root(e->win->tree), 0 });
    *out = r != nullptr ? &r->root : nullptr;
    return r != nullptr ? S_OK : E_OUTOFMEMORY;
}

static const IRawElementProviderFragmentVtbl frag_vtbl = {
    frag_qi, frag_addref, frag_release,
    frag_navigate, frag_runtime_id, frag_bounds, frag_embedded, frag_set_focus, frag_root,
};

/* -- IRawElementProviderFragmentRoot ------------------------------------------------- */

static HRESULT STDMETHODCALLTYPE root_from_point(IRawElementProviderFragmentRoot *This, double x, double y,
                                                 IRawElementProviderFragment **out) {
    uia_el_t *e = EL_OF(This, root);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    POINT pt = { (LONG)x, (LONG)y };
    ScreenToClient(e->win->hwnd, &pt);
    gates_point_t p = { gates_logical(pt.x, e->win->dpi), gates_logical(pt.y, e->win->dpi) };
    gates_access_ref_t at = gates_access_at_point(e->win->tree, p);
    gates_u32 cell = at.item != 0 ? gates_access_cell_at(e->win->tree, at.node, p) : 0; /* a table's cell */
    *out = as_frag(el_get_cell(e->win, at, cell));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE root_focus(IRawElementProviderFragmentRoot *This, IRawElementProviderFragment **out) {
    uia_el_t *e = EL_OF(This, root);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    gates_access_ref_t f = gates_access_focus_ref(e->win->tree);
    if (!ref_null(f) && !ref_eq(f, e->ref)) *out = as_frag(el_get(e->win, f));
    return S_OK;
}

static const IRawElementProviderFragmentRootVtbl root_vtbl = {
    root_qi, root_addref, root_release, root_from_point, root_focus,
};

/* -- patterns ------------------------------------------------------------------------ */

static HRESULT STDMETHODCALLTYPE invoke_invoke(IInvokeProvider *This) {
    uia_el_t *e = EL_OF(This, invoke);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    return acted(e, gates_access_invoke(e->win->tree, e->ref.node, e->ref.item));
}

static const IInvokeProviderVtbl invoke_vtbl = {
    invoke_qi, invoke_addref, invoke_release, invoke_invoke,
};

static HRESULT STDMETHODCALLTYPE toggle_toggle(IToggleProvider *This) {
    uia_el_t *e = EL_OF(This, toggle);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (e->cell != 0) return acted(e, gates_access_cell_toggle(e->win->tree, e->ref.node, e->ref.item, e->cell));
    return acted(e, gates_access_toggle(e->win->tree, e->ref.node));
}

static HRESULT STDMETHODCALLTYPE toggle_state(IToggleProvider *This, enum ToggleState *out) {
    uia_el_t *e = EL_OF(This, toggle);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = (i.states & GATES_ACCESS_CHECKED) != 0 ? ToggleState_On : ToggleState_Off;
    return S_OK;
}

static const IToggleProviderVtbl toggle_vtbl = {
    toggle_qi, toggle_addref, toggle_release, toggle_toggle, toggle_state,
};

static HRESULT STDMETHODCALLTYPE value_set(IValueProvider *This, LPCWSTR text) {
    uia_el_t *e = EL_OF(This, value);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (text == nullptr) return E_INVALIDARG;
    if ((i.actions & GATES_ACCESS_SET_VALUE) == 0) {
        return (i.states & GATES_ACCESS_DISABLED) != 0 ? UIA_E_ELEMENTNOTENABLED : UIA_E_INVALIDOPERATION;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return E_INVALIDARG;
    char *buf = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n);
    if (buf == nullptr) return E_OUTOFMEMORY;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, n, nullptr, nullptr);
    gates_str_t v = { .ptr = (const gates_u8 *)buf, .size = (size_t)n - 1 };
    gates_err_t err = e->cell != 0 ? gates_access_cell_set_value(e->win->tree, e->ref.node, e->ref.item, e->cell, v)
                                   : gates_access_set_value(e->win->tree, e->ref.node, v);
    HeapFree(GetProcessHeap(), 0, buf);
    return acted(e, err);
}

static HRESULT STDMETHODCALLTYPE value_get(IValueProvider *This, BSTR *out) {
    uia_el_t *e = EL_OF(This, value);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = bstr_of(i.value); /* a password box answers empty */
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE value_read_only(IValueProvider *This, WINBOOL *out) {
    uia_el_t *e = EL_OF(This, value);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = (i.actions & GATES_ACCESS_SET_VALUE) == 0;
    return S_OK;
}

static const IValueProviderVtbl value_vtbl = {
    value_qi, value_addref, value_release, value_set, value_get, value_read_only,
};

/* The range as shown (0.8.0): the model's numbers divided by the scale, so a
 * scaled spin box reads 1.25 and a progress bar a percentage; Small and Large
 * change are the step and page. */
static double range_num(const gates_access_info_t *i, int which) {
    double k = i->range_scale > 1 ? (double)i->range_scale : 1.0;
    double span = (double)i->range_max - (double)i->range_min;
    switch (which) {
    case 0:  return (double)i->range_value / k;
    case 1:  return (double)i->range_min / k;
    case 2:  return (double)i->range_max / k;
    case 3:  return (i->range_page > 0 ? (double)i->range_page : span / 10.0) / k;
    default: return (i->range_step > 0 ? (double)i->range_step : span / 100.0) / k;
    }
}

static HRESULT range_get(IRangeValueProvider *This, double *out, int which) {
    uia_el_t *e = EL_OF(This, range);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = range_num(&i, which);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE range_set(IRangeValueProvider *This, double v) {
    uia_el_t *e = EL_OF(This, range);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if ((i.actions & GATES_ACCESS_SET_VALUE) == 0) {
        return (i.states & GATES_ACCESS_DISABLED) ? UIA_E_ELEMENTNOTENABLED : UIA_E_INVALIDOPERATION;
    }
    v *= i.range_scale > 1 ? (double)i.range_scale : 1.0; /* as shown -> the model's number */
    if (!(v == v) || v > 9.2e18 || v < -9.2e18) return E_INVALIDARG; /* NaN or beyond 64 bits */
    gates_i64 n = (gates_i64)(v < 0 ? v - 0.5 : v + 0.5);
    return acted(e, gates_access_set_range_value(e->win->tree, e->ref.node, n));
}

static HRESULT STDMETHODCALLTYPE range_value(IRangeValueProvider *This, double *out) { return range_get(This, out, 0); }
static HRESULT STDMETHODCALLTYPE range_min(IRangeValueProvider *This, double *out) { return range_get(This, out, 1); }
static HRESULT STDMETHODCALLTYPE range_max(IRangeValueProvider *This, double *out) { return range_get(This, out, 2); }
static HRESULT STDMETHODCALLTYPE range_large(IRangeValueProvider *This, double *out) { return range_get(This, out, 3); }
static HRESULT STDMETHODCALLTYPE range_small(IRangeValueProvider *This, double *out) { return range_get(This, out, 4); }

static HRESULT STDMETHODCALLTYPE range_read_only(IRangeValueProvider *This, WINBOOL *out) {
    uia_el_t *e = EL_OF(This, range);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = (i.actions & GATES_ACCESS_SET_VALUE) == 0; /* progress bars; disabled controls */
    return S_OK;
}

static const IRangeValueProviderVtbl range_vtbl = {
    range_qi, range_addref, range_release, range_set, range_value, range_read_only,
    range_max, range_min, range_large, range_small,
};

/* The selected item of a node, 0 when none. */
static gates_u64 selected_item(gates_tree_t *t, gates_node_t node) {
    gates_u64 n = gates_access_item_count(t, node);
    for (gates_u64 k = 0; k < n; k++) {
        gates_u64 id = gates_access_item_at(t, node, k);
        gates_access_info_t i;
        if (gates_is_ok(gates_access_info(t, node, id, &i)) && (i.states & GATES_ACCESS_SELECTED) != 0) return id;
    }
    return 0;
}

static HRESULT STDMETHODCALLTYPE sel_get(ISelectionProvider *This, SAFEARRAY **out) {
    uia_el_t *e = EL_OF(This, sel);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if ((i.states & GATES_ACCESS_MULTISELECT) == 0) {
        gates_u64 id = selected_item(e->win->tree, e->ref.node);
        uia_el_t *s = id != 0 ? el_get(e->win, (gates_access_ref_t){ e->ref.node, id }) : nullptr;
        SAFEARRAY *sa = SafeArrayCreateVector(VT_UNKNOWN, 0, s != nullptr ? 1 : 0);
        if (sa == nullptr) {
            if (s != nullptr) el_release(s);
            return E_OUTOFMEMORY;
        }
        if (s != nullptr) {
            LONG k = 0;
            SafeArrayPutElement(sa, &k, (IUnknown *)&s->simple); /* the array takes its own reference */
            el_release(s);
        }
        *out = sa;
        return S_OK;
    }
    /* Multi-select (0.9.0): the selected items among those described (shown rows and the focus row). */
    gates_tree_t *t = e->win->tree;
    gates_u64 n = gates_access_item_count(t, e->ref.node), m = 0;
    for (gates_u64 k = 0; k < n; k++) {
        gates_access_info_t ii;
        gates_u64 id = gates_access_item_at(t, e->ref.node, k);
        if (gates_is_ok(gates_access_info(t, e->ref.node, id, &ii)) && (ii.states & GATES_ACCESS_SELECTED) != 0) m++;
    }
    SAFEARRAY *sa = SafeArrayCreateVector(VT_UNKNOWN, 0, (ULONG)m);
    if (sa == nullptr) return E_OUTOFMEMORY;
    LONG put = 0;
    for (gates_u64 k = 0; k < n && put < (LONG)m; k++) {
        gates_access_info_t ii;
        gates_u64 id = gates_access_item_at(t, e->ref.node, k);
        if (!gates_is_ok(gates_access_info(t, e->ref.node, id, &ii)) || (ii.states & GATES_ACCESS_SELECTED) == 0) continue;
        uia_el_t *s = el_get(e->win, (gates_access_ref_t){ e->ref.node, id });
        if (s == nullptr) continue;
        SafeArrayPutElement(sa, &put, (IUnknown *)&s->simple);
        el_release(s);
        put++;
    }
    *out = sa;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sel_multiple(ISelectionProvider *This, WINBOOL *out) {
    uia_el_t *e = EL_OF(This, sel);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = (i.states & GATES_ACCESS_MULTISELECT) != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE sel_required(ISelectionProvider *This, WINBOOL *out) {
    uia_el_t *e = EL_OF(This, sel);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = i.role == GATES_ROLE_RADIO_GROUP || i.role == GATES_ROLE_TAB; /* a tab strip always has one */
    return S_OK;
}

static const ISelectionProviderVtbl sel_vtbl = {
    sel_qi, sel_addref, sel_release, sel_get, sel_multiple, sel_required,
};

static HRESULT STDMETHODCALLTYPE selitem_select(ISelectionItemProvider *This) {
    uia_el_t *e = EL_OF(This, selitem);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    return acted(e, gates_access_select(e->win->tree, e->ref.node, e->ref.item));
}

/* The container is multi-select: add and remove are requests (0.9.0). */
static bool in_multi(uia_el_t *e) {
    gates_access_info_t c;
    return gates_is_ok(gates_access_info(e->win->tree, e->ref.node, 0, &c)) && (c.states & GATES_ACCESS_MULTISELECT) != 0;
}

static HRESULT STDMETHODCALLTYPE selitem_add(ISelectionItemProvider *This) {
    uia_el_t *e = EL_OF(This, selitem);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (!in_multi(e)) return acted(e, gates_access_select(e->win->tree, e->ref.node, e->ref.item));
    return acted(e, gates_access_set_item_selected(e->win->tree, e->ref.node, e->ref.item, true));
}

static HRESULT STDMETHODCALLTYPE selitem_remove(ISelectionItemProvider *This) {
    uia_el_t *e = EL_OF(This, selitem);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (in_multi(e)) return acted(e, gates_access_set_item_selected(e->win->tree, e->ref.node, e->ref.item, false));
    return (i.states & GATES_ACCESS_SELECTED) != 0 ? UIA_E_INVALIDOPERATION : S_OK; /* single selection */
}

static HRESULT STDMETHODCALLTYPE selitem_is(ISelectionItemProvider *This, WINBOOL *out) {
    uia_el_t *e = EL_OF(This, selitem);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = (i.states & GATES_ACCESS_SELECTED) != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE selitem_container(ISelectionItemProvider *This, IRawElementProviderSimple **out) {
    uia_el_t *e = EL_OF(This, selitem);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = as_simple(el_get(e->win, (gates_access_ref_t){ e->ref.node, 0 }));
    return S_OK;
}

static const ISelectionItemProviderVtbl selitem_vtbl = {
    selitem_qi, selitem_addref, selitem_release,
    selitem_select, selitem_add, selitem_remove,
    selitem_is, selitem_container,
};

static HRESULT expand_do(IExpandCollapseProvider *This, bool open) {
    uia_el_t *e = EL_OF(This, expand);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    return acted(e, gates_access_expand(e->win->tree, e->ref.node, e->ref.item, open));
}

static HRESULT STDMETHODCALLTYPE expand_expand(IExpandCollapseProvider *This) { return expand_do(This, true); }
static HRESULT STDMETHODCALLTYPE expand_collapse(IExpandCollapseProvider *This) { return expand_do(This, false); }

static HRESULT STDMETHODCALLTYPE expand_state(IExpandCollapseProvider *This, enum ExpandCollapseState *out) {
    uia_el_t *e = EL_OF(This, expand);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = (i.states & GATES_ACCESS_EXPANDABLE) == 0 ? ExpandCollapseState_LeafNode
           : (i.states & GATES_ACCESS_EXPANDED) != 0 ? ExpandCollapseState_Expanded
                                                     : ExpandCollapseState_Collapsed;
    return S_OK;
}

static const IExpandCollapseProviderVtbl expand_vtbl = {
    expand_qi, expand_addref, expand_release, expand_expand, expand_collapse, expand_state,
};

/* -- Grid, GridItem (tables: the cells are the grid's items, 0.10.0) ---------------- */

/* The shown row at model position `row`, or 0. */
static gates_u64 shown_row(gates_tree_t *t, gates_node_t node, gates_u64 row) {
    gates_u64 n = gates_access_item_count(t, node);
    for (gates_u64 k = 0; k < n; k++) {
        gates_u64 id = gates_access_item_at(t, node, k);
        gates_access_info_t i;
        if (gates_is_ok(gates_access_info(t, node, id, &i)) && i.set_position == row + 1) return id;
    }
    return 0;
}

static HRESULT STDMETHODCALLTYPE grid_item(IGridProvider *This, int row, int column, IRawElementProviderSimple **out) {
    uia_el_t *e = EL_OF(This, grid);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (row < 0 || column < 0 || (gates_u32)column >= (i.column_count > 0 ? i.column_count : 1)) return E_INVALIDARG;
    gates_u64 id = shown_row(e->win->tree, e->ref.node, (gates_u64)row);
    if (id == 0) return E_INVALIDARG; /* only shown rows are elements */
    *out = as_simple(el_get_cell(e->win, (gates_access_ref_t){ e->ref.node, id }, (gates_u32)column + 1));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE grid_rows(IGridProvider *This, int *out) {
    uia_el_t *e = EL_OF(This, grid);
    if (out == nullptr) return E_POINTER;
    *out = 0;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    gates_u64 first = gates_access_item_at(e->win->tree, e->ref.node, 0);
    if (first != 0 && gates_is_ok(gates_access_info(e->win->tree, e->ref.node, first, &i))) {
        *out = i.set_size > 0x7FFFFFFF ? 0x7FFFFFFF : (int)i.set_size;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE grid_columns(IGridProvider *This, int *out) {
    uia_el_t *e = EL_OF(This, grid);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = (int)i.column_count;
    return S_OK;
}

static const IGridProviderVtbl grid_vtbl = {
    grid_qi, grid_addref, grid_release, grid_item, grid_rows, grid_columns,
};

static HRESULT griditem_get(IGridItemProvider *This, int *out, int which) {
    uia_el_t *e = EL_OF(This, griditem);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    gates_u64 row = i.set_position > 0 ? i.set_position - 1 : 0;
    *out = which == 0 ? (row > 0x7FFFFFFF ? 0x7FFFFFFF : (int)row) : which == 1 ? (int)i.column : 1; /* a cell */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE griditem_row(IGridItemProvider *This, int *out) { return griditem_get(This, out, 0); }
static HRESULT STDMETHODCALLTYPE griditem_column(IGridItemProvider *This, int *out) { return griditem_get(This, out, 1); }
static HRESULT STDMETHODCALLTYPE griditem_rowspan(IGridItemProvider *This, int *out) { return griditem_get(This, out, 2); }
static HRESULT STDMETHODCALLTYPE griditem_colspan(IGridItemProvider *This, int *out) { return griditem_get(This, out, 3); }

static HRESULT STDMETHODCALLTYPE griditem_grid(IGridItemProvider *This, IRawElementProviderSimple **out) {
    uia_el_t *e = EL_OF(This, griditem);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = as_simple(el_get(e->win, (gates_access_ref_t){ e->ref.node, 0 }));
    return S_OK;
}

static const IGridItemProviderVtbl griditem_vtbl = {
    griditem_qi, griditem_addref, griditem_release,
    griditem_row, griditem_column, griditem_rowspan, griditem_colspan, griditem_grid,
};

/* -- Scroll (views and scroll areas; sideways for wide tables, 0.10.0) ------------------------- */

static HRESULT STDMETHODCALLTYPE scroll_scroll(IScrollProvider *This, enum ScrollAmount h, enum ScrollAmount v) {
    uia_el_t *e = EL_OF(This, scroll);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (h != ScrollAmount_NoAmount) { /* sideways: a wide table (0.10.0) */
        gates_u32 pos = 0, page = 0;
        if (!gates_access_hscroll_info(e->win->tree, e->ref.node, &pos, &page)) return UIA_E_INVALIDOPERATION;
        gates_i64 step = h == ScrollAmount_LargeDecrement || h == ScrollAmount_LargeIncrement ? page : page / 10 + 1;
        gates_i64 to = (gates_i64)pos + (h == ScrollAmount_LargeDecrement || h == ScrollAmount_SmallDecrement ? -step : step);
        to = to < 0 ? 0 : to > GATES_ACCESS_SCROLL_MAX ? GATES_ACCESS_SCROLL_MAX : to;
        hr = acted(e, gates_access_hscroll_to(e->win->tree, e->ref.node, (gates_u32)to));
        if (FAILED(hr) || v == ScrollAmount_NoAmount) return hr;
    }
    if (v == ScrollAmount_NoAmount) return S_OK;
    gates_i32 amount = v == ScrollAmount_LargeDecrement || v == ScrollAmount_SmallDecrement ? -1 : 1;
    bool page = v == ScrollAmount_LargeDecrement || v == ScrollAmount_LargeIncrement;
    return acted(e, gates_access_scroll_by(e->win->tree, e->ref.node, amount, page));
}

static HRESULT STDMETHODCALLTYPE scroll_set(IScrollProvider *This, double h, double v) {
    uia_el_t *e = EL_OF(This, scroll);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    if (h != (double)UIA_ScrollPatternNoScroll) {
        if (h < 0.0 || h > 100.0) return E_INVALIDARG;
        hr = acted(e, gates_access_hscroll_to(e->win->tree, e->ref.node,
                                              (gates_u32)(h * (double)GATES_ACCESS_SCROLL_MAX / 100.0 + 0.5)));
        if (FAILED(hr)) return hr;
    }
    if (v == (double)UIA_ScrollPatternNoScroll) return S_OK;
    if (v < 0.0 || v > 100.0) return E_INVALIDARG;
    return acted(e, gates_access_scroll_to(e->win->tree, e->ref.node,
                                           (gates_u32)(v * (double)GATES_ACCESS_SCROLL_MAX / 100.0 + 0.5)));
}

static HRESULT scroll_get(IScrollProvider *This, double *out, bool view_size) {
    uia_el_t *e = EL_OF(This, scroll);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    gates_u32 pos = 0, page = GATES_ACCESS_SCROLL_MAX;
    bool can = gates_access_scroll_info(e->win->tree, e->ref.node, &pos, &page);
    *out = !can ? (view_size ? 100.0 : (double)UIA_ScrollPatternNoScroll)
                : (double)(view_size ? page : pos) * 100.0 / (double)GATES_ACCESS_SCROLL_MAX;
    return S_OK;
}

static HRESULT hscroll_get(IScrollProvider *This, double *out, bool view_size) {
    uia_el_t *e = EL_OF(This, scroll);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    gates_u32 pos = 0, page = GATES_ACCESS_SCROLL_MAX;
    bool can = gates_access_hscroll_info(e->win->tree, e->ref.node, &pos, &page);
    *out = !can ? (view_size ? 100.0 : (double)UIA_ScrollPatternNoScroll)
                : (double)(view_size ? page : pos) * 100.0 / (double)GATES_ACCESS_SCROLL_MAX;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE scroll_hpct(IScrollProvider *This, double *out) { return hscroll_get(This, out, false); }
static HRESULT STDMETHODCALLTYPE scroll_vpct(IScrollProvider *This, double *out) { return scroll_get(This, out, false); }
static HRESULT STDMETHODCALLTYPE scroll_hsize(IScrollProvider *This, double *out) { return hscroll_get(This, out, true); }
static HRESULT STDMETHODCALLTYPE scroll_vsize(IScrollProvider *This, double *out) { return scroll_get(This, out, true); }
static HRESULT STDMETHODCALLTYPE scroll_hcan(IScrollProvider *This, WINBOOL *out) {
    uia_el_t *e = EL_OF(This, scroll);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    gates_u32 pos, page;
    *out = gates_access_hscroll_info(e->win->tree, e->ref.node, &pos, &page);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE scroll_vcan(IScrollProvider *This, WINBOOL *out) {
    uia_el_t *e = EL_OF(This, scroll);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = gates_access_scroll_info(e->win->tree, e->ref.node, nullptr, nullptr);
    return S_OK;
}

static const IScrollProviderVtbl scroll_vtbl = {
    scroll_qi, scroll_addref, scroll_release, scroll_scroll, scroll_set,
    scroll_hpct, scroll_vpct, scroll_hsize, scroll_vsize, scroll_hcan, scroll_vcan,
};

/* -- Window (dialogs) ------------------------------------------------------------------------ */

static HRESULT STDMETHODCALLTYPE window_state_set(IWindowProvider *This, enum WindowVisualState st) {
    uia_el_t *e = EL_OF(This, window);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    return st == WindowVisualState_Normal ? S_OK : UIA_E_INVALIDOPERATION;
}

static HRESULT STDMETHODCALLTYPE window_close(IWindowProvider *This) {
    uia_el_t *e = EL_OF(This, window);
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    return acted(e, gates_dialog_close(e->win->tree, e->ref.node, GATES_DIALOG_CANCELED)); /* as Escape */
}

static HRESULT STDMETHODCALLTYPE window_idle(IWindowProvider *This, int ms, WINBOOL *out) {
    (void)This;
    (void)ms;
    if (out == nullptr) return E_POINTER;
    *out = TRUE; /* called on the UI thread: it is idle now */
    return S_OK;
}

static HRESULT window_flag(IWindowProvider *This, WINBOOL *out, WINBOOL value) {
    uia_el_t *e = EL_OF(This, window);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE window_can_max(IWindowProvider *This, WINBOOL *out) { return window_flag(This, out, FALSE); }
static HRESULT STDMETHODCALLTYPE window_can_min(IWindowProvider *This, WINBOOL *out) { return window_flag(This, out, FALSE); }
static HRESULT STDMETHODCALLTYPE window_modal(IWindowProvider *This, WINBOOL *out) { return window_flag(This, out, TRUE); }
static HRESULT STDMETHODCALLTYPE window_topmost(IWindowProvider *This, WINBOOL *out) { return window_flag(This, out, TRUE); }

static HRESULT STDMETHODCALLTYPE window_state(IWindowProvider *This, enum WindowVisualState *out) {
    uia_el_t *e = EL_OF(This, window);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = WindowVisualState_Normal;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE window_interaction(IWindowProvider *This, enum WindowInteractionState *out) {
    uia_el_t *e = EL_OF(This, window);
    if (out == nullptr) return E_POINTER;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = WindowInteractionState_ReadyForUserInteraction;
    return S_OK;
}

static const IWindowProviderVtbl window_vtbl = {
    window_qi, window_addref, window_release, window_state_set, window_close, window_idle,
    window_can_max, window_can_min, window_modal, window_state, window_interaction, window_topmost,
};

/* -- Text: offsets are UTF-16 units at this boundary, UTF-8 bytes in gates ------------------- */

static gates_u32 u8_len_at(gates_u8 lead) {
    return lead < 0x80 ? 1u : lead < 0xE0 ? 2u : lead < 0xF0 ? 3u : 4u;
}

/* UTF-16 offset of UTF-8 offset b (clamped), and the reverse (to a character start). */
static LONG u16_of(gates_str_t s, gates_u32 b) {
    LONG w = 0;
    for (gates_u32 k = 0; k < s.size && k < b;) {
        gates_u32 n = u8_len_at(s.ptr[k]);
        w += n == 4 ? 2 : 1;
        k += n;
    }
    return w;
}

static gates_u32 u8_of(gates_str_t s, LONG w) {
    gates_u32 k = 0;
    LONG at = 0;
    while (k < s.size) {
        gates_u32 n = u8_len_at(s.ptr[k]);
        LONG units = n == 4 ? 2 : 1;
        if (at + units > w) break;
        at += units;
        k += n;
    }
    return k < s.size ? k : (gates_u32)s.size;
}

typedef struct uia_range_t {
    ITextRangeProvider iface;
    LONG refs;
    uia_el_t *el;                    /* holds a reference */
    LONG start, end;                 /* UTF-16 units into the edit's text */
} uia_range_t;

static const ITextRangeProviderVtbl range_text_vtbl;

static ITextRangeProvider *range_new(uia_el_t *el, LONG start, LONG end) {
    uia_range_t *r = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof *r);
    if (r == nullptr) return nullptr;
    r->iface.lpVtbl = (ITextRangeProviderVtbl *)&range_text_vtbl;
    r->refs = 1;
    r->el = el;
    el_addref(el);
    r->start = start < end ? start : end;
    r->end = start < end ? end : start;
    return &r->iface;
}

static uia_range_t *range_of(ITextRangeProvider *p) {
    return p != nullptr && p->lpVtbl == &range_text_vtbl ? (uia_range_t *)p : nullptr;
}

/* The edit's text as UTF-16 (caller frees), its info, and the range clamped to it. */
static HRESULT range_text(uia_range_t *r, gates_access_info_t *i, BSTR *w, LONG *n) {
    *w = nullptr;
    HRESULT hr = el_info(r->el, i);
    if (FAILED(hr)) return hr;
    if (!has_pattern(i, UIA_TextPatternId)) return UIA_E_ELEMENTNOTAVAILABLE;
    *w = bstr_of(i->value);
    if (*w == nullptr) return E_OUTOFMEMORY;
    *n = (LONG)SysStringLen(*w);
    if (r->end > *n) r->end = *n;
    if (r->start > r->end) r->start = r->end;
    return S_OK;
}

static LONG next_char(const WCHAR *w, LONG n, LONG p) {
    if (p >= n) return n;
    return IS_HIGH_SURROGATE(w[p]) && p + 1 < n && IS_LOW_SURROGATE(w[p + 1]) ? p + 2 : p + 1;
}

static LONG prev_char(const WCHAR *w, LONG p) {
    if (p <= 0) return 0;
    return p >= 2 && IS_LOW_SURROGATE(w[p - 1]) && IS_HIGH_SURROGATE(w[p - 2]) ? p - 2 : p - 1;
}

static bool space_at(const WCHAR *w, LONG p) {
    return w[p] == L' ' || w[p] == L'\t';
}

static bool word_start(const WCHAR *w, LONG n, LONG p) {
    return p == 0 || p >= n || (!space_at(w, p) && space_at(w, p - 1));
}

/* Page, Format and Document span the whole text; Paragraph ends after each
 * line break (a single-line box is one), and Line after each shown row: an
 * editor with wrap answers by rows (0.8.0), others as Paragraph (0.7.0). */
static bool whole_unit(enum TextUnit u) {
    return u != TextUnit_Character && u != TextUnit_Word && u != TextUnit_Line && u != TextUnit_Paragraph;
}

static bool line_unit(enum TextUnit u) {
    return u == TextUnit_Line || u == TextUnit_Paragraph;
}

static bool line_start(const WCHAR *w, LONG p) {
    return p == 0 || w[p - 1] == L'\n';
}

/* The text the unit functions walk: UTF-16, and the node that lays it out. */
typedef struct unit_text_t {
    const WCHAR *w;
    LONG n;
    gates_tree_t *tree;
    gates_node_t node;
    gates_str_t u8;
} unit_text_t;

/* The shown row holding UTF-16 offset p, from the model; false when it lays out no rows. */
static bool row_of(const unit_text_t *x, enum TextUnit u, LONG p, LONG *b, LONG *e) {
    gates_u32 rb, re;
    if (u != TextUnit_Line || !gates_access_text_line(x->tree, x->node, u8_of(x->u8, p), &rb, &re)) return false;
    *b = u16_of(x->u8, rb);
    *e = u16_of(x->u8, re);
    return true;
}

static LONG unit_start(enum TextUnit u, const unit_text_t *x, LONG p) {
    const WCHAR *w = x->w;
    LONG n = x->n, b, e;
    if (whole_unit(u)) return 0;
    if (p >= n) return n;
    if (u == TextUnit_Character) return p > 0 && IS_LOW_SURROGATE(w[p]) ? p - 1 : p;
    if (row_of(x, u, p, &b, &e)) return b;
    if (line_unit(u)) {
        while (p > 0 && !line_start(w, p)) p--;
        return p;
    }
    while (p > 0 && !word_start(w, n, p)) p--;
    return p;
}

static LONG unit_next(enum TextUnit u, const unit_text_t *x, LONG p) {
    const WCHAR *w = x->w;
    LONG n = x->n, b, e;
    if (whole_unit(u)) return n;
    if (u == TextUnit_Character) return next_char(w, n, p);
    if (p >= n) return n;
    if (row_of(x, u, p, &b, &e) && e > p) return e;
    if (line_unit(u)) {
        do p++; while (p < n && !line_start(w, p));
        return p;
    }
    do p++; while (p < n && !word_start(w, n, p));
    return p;
}

static LONG unit_prev(enum TextUnit u, const unit_text_t *x, LONG p) {
    const WCHAR *w = x->w;
    LONG n = x->n, b, e;
    if (whole_unit(u)) return 0;
    if (u == TextUnit_Character) return prev_char(w, p);
    if (p <= 0) return 0;
    if (row_of(x, u, prev_char(w, p), &b, &e)) return b; /* the row before p's place */
    if (line_unit(u)) {
        do p--; while (p > 0 && !line_start(w, p));
        return p;
    }
    do p--; while (p > 0 && !word_start(w, n, p));
    return p;
}

static HRESULT STDMETHODCALLTYPE rt_qi(ITextRangeProvider *This, REFIID riid, void **out) {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_ITextRangeProvider)) return E_NOINTERFACE;
    InterlockedIncrement(&((uia_range_t *)This)->refs);
    *out = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE rt_addref(ITextRangeProvider *This) {
    return (ULONG)InterlockedIncrement(&((uia_range_t *)This)->refs);
}

static ULONG STDMETHODCALLTYPE rt_release(ITextRangeProvider *This) {
    uia_range_t *r = (uia_range_t *)This;
    LONG n = InterlockedDecrement(&r->refs);
    if (n == 0) {
        el_release(r->el);
        HeapFree(GetProcessHeap(), 0, r);
    }
    return (ULONG)n;
}

static HRESULT STDMETHODCALLTYPE rt_clone(ITextRangeProvider *This, ITextRangeProvider **out) {
    uia_range_t *r = (uia_range_t *)This;
    if (out == nullptr) return E_POINTER;
    *out = range_new(r->el, r->start, r->end);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE rt_compare(ITextRangeProvider *This, ITextRangeProvider *other, WINBOOL *out) {
    uia_range_t *r = (uia_range_t *)This, *o = range_of(other);
    if (out == nullptr) return E_POINTER;
    if (o == nullptr) return E_INVALIDARG;
    *out = o->el == r->el && o->start == r->start && o->end == r->end;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_compare_ends(ITextRangeProvider *This, enum TextPatternRangeEndpoint ep,
                                                 ITextRangeProvider *other, enum TextPatternRangeEndpoint oep,
                                                 int *out) {
    uia_range_t *r = (uia_range_t *)This, *o = range_of(other);
    if (out == nullptr) return E_POINTER;
    if (o == nullptr || o->el != r->el) return E_INVALIDARG;
    LONG a = ep == TextPatternRangeEndpoint_Start ? r->start : r->end;
    LONG b = oep == TextPatternRangeEndpoint_Start ? o->start : o->end;
    *out = a < b ? -1 : a > b ? 1 : 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_expand(ITextRangeProvider *This, enum TextUnit unit) {
    uia_range_t *r = (uia_range_t *)This;
    gates_access_info_t i;
    BSTR w;
    LONG n = 0;
    HRESULT hr = range_text(r, &i, &w, &n);
    if (FAILED(hr)) return hr;
    unit_text_t ut = { w, n, r->el->win->tree, r->el->ref.node, i.value };
    r->start = unit_start(unit, &ut, r->start);
    r->end = unit_next(unit, &ut, r->start);
    SysFreeString(w);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_find_attr(ITextRangeProvider *This, TEXTATTRIBUTEID id, VARIANT val,
                                              WINBOOL backward, ITextRangeProvider **out) {
    (void)This;
    (void)id;
    (void)val;
    (void)backward;
    if (out == nullptr) return E_POINTER;
    *out = nullptr; /* one run of plain text: no attribute ranges to find */
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_find_text(ITextRangeProvider *This, BSTR text, WINBOOL backward,
                                              WINBOOL ignore_case, ITextRangeProvider **out) {
    uia_range_t *r = (uia_range_t *)This;
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    if (text == nullptr) return E_INVALIDARG;
    gates_access_info_t i;
    BSTR w;
    LONG n = 0;
    HRESULT hr = range_text(r, &i, &w, &n);
    if (FAILED(hr)) return hr;
    LONG k = (LONG)SysStringLen(text);
    LONG found = -1;
    if (k > 0 && k <= r->end - r->start) {
        for (LONG s0 = backward ? r->end - k : r->start; backward ? s0 >= r->start : s0 <= r->end - k;
             s0 += backward ? -1 : 1) {
            if (CompareStringOrdinal(w + s0, k, text, k, ignore_case) == CSTR_EQUAL) {
                found = s0;
                break;
            }
        }
    }
    SysFreeString(w);
    if (found >= 0) {
        *out = range_new(r->el, found, found + k);
        if (*out == nullptr) return E_OUTOFMEMORY;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_attr(ITextRangeProvider *This, TEXTATTRIBUTEID id, VARIANT *out) {
    uia_range_t *r = (uia_range_t *)This;
    if (out == nullptr) return E_POINTER;
    VariantInit(out);
    gates_access_info_t i;
    HRESULT hr = el_info(r->el, &i);
    if (FAILED(hr)) return hr;
    if (id == UIA_IsReadOnlyAttributeId) {
        v_bool(out, (i.actions & GATES_ACCESS_SET_VALUE) == 0);
        return S_OK;
    }
    IUnknown *ns = nullptr;
    hr = UiaGetReservedNotSupportedValue(&ns);
    if (SUCCEEDED(hr)) {
        out->vt = VT_UNKNOWN;
        out->punkVal = ns;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE rt_rects(ITextRangeProvider *This, SAFEARRAY **out) {
    uia_range_t *r = (uia_range_t *)This;
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    BSTR w;
    LONG n = 0;
    HRESULT hr = range_text(r, &i, &w, &n);
    if (FAILED(hr)) return hr;
    SysFreeString(w);
    /* One rectangle per row the range covers (a multi-line editor has several). */
    gates_rect_t lr[64];
    gates_u32 got = r->end > r->start ? gates_access_text_rects(r->el->win->tree, r->el->ref.node,
                                                                u8_of(i.value, r->start), u8_of(i.value, r->end), lr, 64)
                                      : 0;
    gates_u32 keep = 0;
    for (gates_u32 k = 0; k < got; k++) {
        if (lr[k].w > 0) lr[keep++] = lr[k];
    }
    SAFEARRAY *sa = SafeArrayCreateVector(VT_R8, 0, (ULONG)(4 * keep));
    if (sa == nullptr) return E_OUTOFMEMORY;
    for (gates_u32 k = 0; k < keep; k++) {
        struct UiaRect sr = screen_rect(r->el->win, lr[k]);
        double v[4] = { sr.left, sr.top, sr.width, sr.height };
        for (LONG q = 0; q < 4; q++) {
            LONG at = (LONG)(4 * k) + q;
            SafeArrayPutElement(sa, &at, &v[q]);
        }
    }
    *out = sa;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_element(ITextRangeProvider *This, IRawElementProviderSimple **out) {
    uia_range_t *r = (uia_range_t *)This;
    if (out == nullptr) return E_POINTER;
    el_addref(r->el);
    *out = &r->el->simple;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_text(ITextRangeProvider *This, int max, BSTR *out) {
    uia_range_t *r = (uia_range_t *)This;
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    BSTR w;
    LONG n = 0;
    HRESULT hr = range_text(r, &i, &w, &n);
    if (FAILED(hr)) return hr;
    LONG len = r->end - r->start;
    if (max >= 0 && len > max) len = max;
    *out = SysAllocStringLen(w + r->start, (UINT)len);
    SysFreeString(w);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE rt_move(ITextRangeProvider *This, enum TextUnit unit, int count, int *out) {
    uia_range_t *r = (uia_range_t *)This;
    if (out == nullptr) return E_POINTER;
    *out = 0;
    gates_access_info_t i;
    BSTR w;
    LONG n = 0;
    HRESULT hr = range_text(r, &i, &w, &n);
    if (FAILED(hr)) return hr;
    unit_text_t ut = { w, n, r->el->win->tree, r->el->ref.node, i.value };
    bool degenerate = r->start == r->end;
    LONG p = degenerate ? r->start : unit_start(unit, &ut, r->start);
    int moved = 0;
    for (; count > 0; count--) {
        LONG q = unit_next(unit, &ut, p);
        if (q == p || (!degenerate && q >= n)) break;
        p = q;
        moved++;
    }
    for (; count < 0; count++) {
        LONG q = unit_prev(unit, &ut, p);
        if (q == p) break;
        p = q;
        moved--;
    }
    r->start = p;
    r->end = degenerate ? p : unit_next(unit, &ut, p);
    SysFreeString(w);
    *out = moved;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_move_end(ITextRangeProvider *This, enum TextPatternRangeEndpoint ep,
                                             enum TextUnit unit, int count, int *out) {
    uia_range_t *r = (uia_range_t *)This;
    if (out == nullptr) return E_POINTER;
    *out = 0;
    gates_access_info_t i;
    BSTR w;
    LONG n = 0;
    HRESULT hr = range_text(r, &i, &w, &n);
    if (FAILED(hr)) return hr;
    unit_text_t ut = { w, n, r->el->win->tree, r->el->ref.node, i.value };
    LONG p = ep == TextPatternRangeEndpoint_Start ? r->start : r->end;
    int moved = 0;
    for (; count > 0; count--) {
        LONG q = unit_next(unit, &ut, p);
        if (q == p) break;
        p = q;
        moved++;
    }
    for (; count < 0; count++) {
        LONG q = unit_prev(unit, &ut, p);
        if (q == p) break;
        p = q;
        moved--;
    }
    if (ep == TextPatternRangeEndpoint_Start) {
        r->start = p;
        if (r->end < p) r->end = p;
    } else {
        r->end = p;
        if (r->start > p) r->start = p;
    }
    SysFreeString(w);
    *out = moved;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_move_end_range(ITextRangeProvider *This, enum TextPatternRangeEndpoint ep,
                                                   ITextRangeProvider *other, enum TextPatternRangeEndpoint oep) {
    uia_range_t *r = (uia_range_t *)This, *o = range_of(other);
    if (o == nullptr || o->el != r->el) return E_INVALIDARG;
    LONG p = oep == TextPatternRangeEndpoint_Start ? o->start : o->end;
    if (ep == TextPatternRangeEndpoint_Start) {
        r->start = p;
        if (r->end < p) r->end = p;
    } else {
        r->end = p;
        if (r->start > p) r->start = p;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rt_select(ITextRangeProvider *This) {
    uia_range_t *r = (uia_range_t *)This;
    gates_access_info_t i;
    BSTR w;
    LONG n = 0;
    HRESULT hr = range_text(r, &i, &w, &n);
    if (FAILED(hr)) return hr;
    SysFreeString(w);
    gates_u32 a = u8_of(i.value, r->start), c = u8_of(i.value, r->end);
    return acted(r->el, gates_access_select_text(r->el->win->tree, r->el->ref.node, a, c));
}

static HRESULT STDMETHODCALLTYPE rt_add_sel(ITextRangeProvider *This) {
    (void)This;
    return UIA_E_INVALIDOPERATION; /* one selection */
}

static HRESULT STDMETHODCALLTYPE rt_scroll(ITextRangeProvider *This, WINBOOL top) {
    (void)This;
    (void)top;
    return S_OK; /* a one-line edit keeps its caret in view */
}

static HRESULT STDMETHODCALLTYPE rt_children(ITextRangeProvider *This, SAFEARRAY **out) {
    (void)This;
    if (out == nullptr) return E_POINTER;
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static const ITextRangeProviderVtbl range_text_vtbl = {
    rt_qi, rt_addref, rt_release, rt_clone, rt_compare, rt_compare_ends, rt_expand, rt_find_attr,
    rt_find_text, rt_attr, rt_rects, rt_element, rt_text, rt_move, rt_move_end, rt_move_end_range,
    rt_select, rt_add_sel, rt_add_sel, rt_scroll, rt_children,
};

/* A SAFEARRAY holding one range (the array takes the reference). */
static HRESULT one_range(ITextRangeProvider *range, SAFEARRAY **out) {
    if (range == nullptr) return E_OUTOFMEMORY;
    SAFEARRAY *sa = SafeArrayCreateVector(VT_UNKNOWN, 0, 1);
    if (sa == nullptr) {
        range->lpVtbl->Release(range);
        return E_OUTOFMEMORY;
    }
    LONG k = 0;
    SafeArrayPutElement(sa, &k, (IUnknown *)range);
    range->lpVtbl->Release(range);
    *out = sa;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_selection(ITextProvider2 *This, SAFEARRAY **out) {
    uia_el_t *e = EL_OF(This, text);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    return one_range(range_new(e, u16_of(i.value, i.anchor), u16_of(i.value, i.caret)), out);
}

static HRESULT STDMETHODCALLTYPE text_visible(ITextProvider2 *This, SAFEARRAY **out) {
    uia_el_t *e = EL_OF(This, text);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    gates_rect_t b = i.bounds;
    gates_u32 a = gates_access_text_offset_at(e->win->tree, e->ref.node, (gates_point_t){ b.x, b.y });
    gates_u32 z = gates_access_text_offset_at(e->win->tree, e->ref.node, (gates_point_t){ b.x + b.w, b.y });
    if (FAILED(el_info(e, &i))) return UIA_E_ELEMENTNOTAVAILABLE; /* fresh strings */
    return one_range(range_new(e, u16_of(i.value, a), u16_of(i.value, z)), out);
}

static HRESULT STDMETHODCALLTYPE text_from_child(ITextProvider2 *This, IRawElementProviderSimple *child,
                                                 ITextRangeProvider **out) {
    (void)This;
    (void)child;
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    return E_INVALIDARG; /* an edit has no child elements */
}

static HRESULT STDMETHODCALLTYPE text_from_point(ITextProvider2 *This, struct UiaPoint pt, ITextRangeProvider **out) {
    uia_el_t *e = EL_OF(This, text);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    POINT p = { (LONG)pt.x, (LONG)pt.y };
    ScreenToClient(e->win->hwnd, &p);
    gates_u32 off = gates_access_text_offset_at(e->win->tree, e->ref.node,
                                                (gates_point_t){ gates_logical(p.x, e->win->dpi),
                                                                 gates_logical(p.y, e->win->dpi) });
    if (FAILED(el_info(e, &i))) return UIA_E_ELEMENTNOTAVAILABLE;
    LONG w = u16_of(i.value, off);
    *out = range_new(e, w, w);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE text_document(ITextProvider2 *This, ITextRangeProvider **out) {
    uia_el_t *e = EL_OF(This, text);
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *out = range_new(e, 0, u16_of(i.value, (gates_u32)i.value.size));
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE text_supported(ITextProvider2 *This, enum SupportedTextSelection *out) {
    (void)This;
    if (out == nullptr) return E_POINTER;
    *out = SupportedTextSelection_Single;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE text_from_annotation(ITextProvider2 *This, IRawElementProviderSimple *a,
                                                      ITextRangeProvider **out) {
    (void)This;
    (void)a;
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    return E_INVALIDARG; /* no annotations */
}

static HRESULT STDMETHODCALLTYPE text_caret(ITextProvider2 *This, WINBOOL *active, ITextRangeProvider **out) {
    uia_el_t *e = EL_OF(This, text);
    if (out == nullptr || active == nullptr) return E_POINTER;
    *out = nullptr;
    gates_access_info_t i;
    HRESULT hr = el_info(e, &i);
    if (FAILED(hr)) return hr;
    *active = (i.states & GATES_ACCESS_FOCUSED) != 0 && GetFocus() == e->win->hwnd;
    LONG c = u16_of(i.value, i.caret);
    *out = range_new(e, c, c);
    return *out != nullptr ? S_OK : E_OUTOFMEMORY;
}

static const ITextProvider2Vtbl text_vtbl = {
    text_qi, text_addref, text_release, text_selection, text_visible, text_from_child, text_from_point,
    text_document, text_supported, text_from_annotation, text_caret,
};

/* -- the window side ----------------------------------------------------------------- */

bool gates_win32_uia_getobject(gates_window_t *win, WPARAM wparam, LPARAM lparam, LRESULT *result) {
    if ((LONG)lparam != UiaRootObjectId || win->hwnd == nullptr) return false;
    uia_el_t *r = el_get(win, (gates_access_ref_t){ gates_tree_root(win->tree), 0 });
    if (r == nullptr) return false;
    if (!gates_access_enabled(win->tree)) {
        gates_access_enable(win->tree, true); /* record changes from now on */
        win->uia_focus = gates_access_focus_ref(win->tree);
        win->uia_overlays = gates_tree_overlay_count(win->tree);
    }
    *result = UiaReturnRawElementProvider(win->hwnd, wparam, lparam, &r->simple);
    el_release(r);
    return true;
}

void gates_win32_uia_detach(gates_window_t *win) {
    if (win->hwnd != nullptr && gates_access_enabled(win->tree)) {
        (void)UiaReturnRawElementProvider(win->hwnd, 0, 0, nullptr); /* the window is going */
    }
    while (win->uia_els != nullptr) {
        uia_el_t *e = win->uia_els;
        win->uia_els = e->next;
        e->next = nullptr;
        e->win = nullptr; /* answers "not available" from now on */
        (void)UiaDisconnectProvider(&e->simple);
    }
    gates_access_enable(win->tree, false);
}

static void raise_children_invalidated(gates_window_t *win, gates_access_ref_t ref);

static void raise_prop(uia_el_t *e, PROPERTYID id, VARIANT now) {
    VARIANT old;
    VariantInit(&old);
    (void)UiaRaiseAutomationPropertyChangedEvent(&e->simple, id, old, now);
    VariantClear(&now);
}

/* Raises what changed in an element a client holds since it last heard. */
static void diff(uia_el_t *e) {
    gates_access_info_t i;
    if (FAILED(el_info(e, &i))) return;
    uia_snap_t now = snap_of(&i), was = e->snap;
    bool items_changed = false;
    if (e->win != nullptr) {
        gates_access_ref_t ref = e->ref;
        gates_u64 items = items_of(e->win->tree, ref, &i);
        items_changed = items != was.items;
        now.items = items;
        if (FAILED(el_info(e, &i))) return; /* the model call above may have reused the strings */
    }
    e->snap = now;
    /* The info's strings are borrowed from the tree, and a client may call
     * back into a provider while an event is raised: copy them all first. */
    VARIANT name, help, value, v;
    VariantInit(&name);
    VariantInit(&help);
    VariantInit(&value);
    bool name_changed = now.name != was.name, help_changed = now.help != was.help;
    bool value_changed = now.value != was.value && has_pattern(&i, UIA_ValuePatternId);
    if (name_changed) v_str(&name, i.name);
    if (help_changed) v_str(&help, i.description);
    if (value_changed) v_str(&value, i.value);
    bool text = has_pattern(&i, UIA_TextPatternId);
    bool text_changed = text && now.value != was.value;
    bool sel_changed = text && (now.caret != was.caret || now.anchor != was.anchor);
    if (name_changed) raise_prop(e, UIA_NamePropertyId, name);
    if (help_changed) raise_prop(e, UIA_HelpTextPropertyId, help);
    if (value_changed) raise_prop(e, UIA_ValueValuePropertyId, value);
    if (text_changed) (void)UiaRaiseAutomationEvent(&e->simple, UIA_Text_TextChangedEventId);
    if (sel_changed) (void)UiaRaiseAutomationEvent(&e->simple, UIA_Text_TextSelectionChangedEventId);
    if (items_changed && e->win != nullptr) raise_children_invalidated(e->win, e->ref);
    if (now.range != was.range && i.has_range) {
        VariantInit(&v);
        v.vt = VT_R8;
        v.dblVal = range_num(&i, 0);
        raise_prop(e, UIA_RangeValueValuePropertyId, v);
    }
    gates_u32 flip = now.states ^ was.states;
    if (flip & GATES_ACCESS_DISABLED) {
        VariantInit(&v);
        v_bool(&v, (now.states & GATES_ACCESS_DISABLED) == 0);
        raise_prop(e, UIA_IsEnabledPropertyId, v);
    }
    if ((flip & GATES_ACCESS_CHECKED) && i.role == GATES_ROLE_CHECK_BOX) {
        VariantInit(&v);
        v_i4(&v, (now.states & GATES_ACCESS_CHECKED) != 0 ? ToggleState_On : ToggleState_Off);
        raise_prop(e, UIA_ToggleToggleStatePropertyId, v);
    }
    if (flip & GATES_ACCESS_EXPANDED) {
        VariantInit(&v);
        v_i4(&v, (now.states & GATES_ACCESS_EXPANDED) != 0 ? ExpandCollapseState_Expanded
                                                           : ExpandCollapseState_Collapsed);
        raise_prop(e, UIA_ExpandCollapseExpandCollapseStatePropertyId, v);
    }
    if (flip & GATES_ACCESS_SELECTED) {
        VariantInit(&v);
        v_bool(&v, (now.states & GATES_ACCESS_SELECTED) != 0);
        raise_prop(e, UIA_SelectionItemIsSelectedPropertyId, v);
        if (now.states & GATES_ACCESS_SELECTED) {
            (void)UiaRaiseAutomationEvent(&e->simple, UIA_SelectionItem_ElementSelectedEventId);
        }
    }
    if (flip & GATES_ACCESS_OFFSCREEN) {
        VariantInit(&v);
        v_bool(&v, (now.states & GATES_ACCESS_OFFSCREEN) != 0);
        raise_prop(e, UIA_IsOffscreenPropertyId, v);
    }
    if (flip & GATES_ACCESS_INVALID) {
        VariantInit(&v);
        v_bool(&v, (now.states & GATES_ACCESS_INVALID) == 0);
        raise_prop(e, UIA_IsDataValidForFormPropertyId, v);
    }
    if (flip & GATES_ACCESS_READ_ONLY) {
        VariantInit(&v);
        v_bool(&v, (now.states & GATES_ACCESS_READ_ONLY) != 0);
        raise_prop(e, UIA_ValueIsReadOnlyPropertyId, v);
    }
}

static void raise_on(gates_window_t *win, gates_access_ref_t ref, EVENTID id) {
    uia_el_t *e = el_get(win, ref);
    if (e == nullptr) return;
    (void)UiaRaiseAutomationEvent(&e->simple, id);
    el_release(e);
}

static void raise_children_invalidated(gates_window_t *win, gates_access_ref_t ref) {
    uia_el_t *e = el_get(win, ref);
    if (e == nullptr) return;
    SAFEARRAY *rid = nullptr;
    int one[5] = { 0 };
    int n = 0;
    if (SUCCEEDED(frag_runtime_id(&e->frag, &rid)) && rid != nullptr) {
        for (LONG k = 0; k < 5; k++) SafeArrayGetElement(rid, &k, &one[k]);
        n = 5;
        SafeArrayDestroy(rid);
    }
    (void)UiaRaiseStructureChangedEvent(&e->simple, StructureChangeType_ChildrenInvalidated, n > 0 ? one : nullptr, n);
    el_release(e);
}

/* Windows 10 1709+: speech for an application announcement (loaded at run time). */
typedef HRESULT(WINAPI *notify_fn)(IRawElementProviderSimple *, enum NotificationKind,
                                   enum NotificationProcessing, BSTR, BSTR);

static void announce(gates_window_t *win) {
    static notify_fn fn;
    static bool looked;
    if (!looked) {
        looked = true;
        HMODULE m = GetModuleHandleW(L"uiautomationcore.dll");
        if (m != nullptr) fn = (notify_fn)(void (*)(void))GetProcAddress(m, "UiaRaiseNotificationEvent");
    }
    bool assertive = false;
    gates_str_t text = gates_access_announcement(win->tree, &assertive);
    if (fn == nullptr || text.size == 0) return;
    BSTR s = bstr_of(text), act = SysAllocString(L"gates.announce"); /* before any call out */
    uia_el_t *r = el_get(win, (gates_access_ref_t){ gates_tree_root(win->tree), 0 });
    if (r == nullptr) {
        SysFreeString(s);
        SysFreeString(act);
        return;
    }
    if (s != nullptr && act != nullptr) {
        (void)fn(&r->simple, NotificationKind_Other,
                 assertive ? NotificationProcessing_ImportantMostRecent : NotificationProcessing_MostRecent, s, act);
    }
    SysFreeString(s);
    SysFreeString(act);
    el_release(r);
}

void gates_win32_uia_events(gates_window_t *win) {
    if (win->hwnd == nullptr || !gates_access_enabled(win->tree) || win->uia_draining) return;
    win->uia_draining = true;
    gates_tree_t *t = win->tree;
    bool listening = UiaClientsAreListening() != FALSE;
    gates_access_change_t ch[64];
    bool overflow = false, any_changed = false;
    gates_u32 n;
    while ((n = gates_access_take_changes(t, ch, 64, &overflow)) > 0 || overflow) {
        for (gates_u32 k = 0; k < n; k++) {
            gates_access_ref_t ref = { ch[k].node, ch[k].item };
            switch (ch[k].kind) {
            case GATES_ACCESS_REMOVED: {
                /* Mark first, then disconnect (disconnecting may release providers). */
                uia_el_t *dead[32];
                gates_u32 nd = 0;
                for (uia_el_t *e = win->uia_els; e != nullptr && nd < 32; e = e->next) {
                    if (!e->gone && e->ref.node.index == ch[k].node.index &&
                        e->ref.node.generation == ch[k].node.generation) {
                        e->gone = true;
                        el_addref(e);
                        dead[nd++] = e;
                    }
                }
                for (gates_u32 d = 0; d < nd; d++) {
                    (void)UiaDisconnectProvider(&dead[d]->simple);
                    el_release(dead[d]);
                }
                break;
            }
            case GATES_ACCESS_CHANGED:
                any_changed = true;
                break;
            case GATES_ACCESS_STRUCTURE:
                if (listening) raise_children_invalidated(win, ref);
                break;
            case GATES_ACCESS_LIVE:
                if (listening) raise_on(win, ref, UIA_LiveRegionChangedEventId);
                break;
            case GATES_ACCESS_ANNOUNCE:
                if (listening) announce(win);
                break;
            case GATES_ACCESS_TOOLTIP_OPENED:
                if (listening) raise_on(win, ref, UIA_ToolTipOpenedEventId); /* 0.10.0 */
                break;
            default:
                break;
            }
        }
        if (overflow) {
            any_changed = true;
            if (listening) raise_children_invalidated(win, (gates_access_ref_t){ gates_tree_root(t), 0 });
            overflow = false;
        }
        if (n == 0) break;
    }
    /* Dialogs and menus come and go outside the tree's links. */
    gates_u32 overlays = gates_tree_overlay_count(t);
    if (overlays != win->uia_overlays) {
        bool opened = overlays > win->uia_overlays;
        win->uia_overlays = overlays;
        any_changed = true; /* a closed list collapses its choice, and so on */
        gates_access_ref_t root = { gates_tree_root(t), 0 };
        if (listening) {
            raise_children_invalidated(win, root);
            gates_access_ref_t top = gates_access_last_child(t, root);
            gates_access_info_t i;
            if (opened && !ref_null(top) && gates_is_ok(gates_access_info(t, top.node, 0, &i))) {
                if (i.role == GATES_ROLE_MENU) raise_on(win, top, UIA_MenuOpenedEventId);
                if (i.role == GATES_ROLE_DIALOG) win->uia_opened = top; /* once it is laid out */
            }
        }
    }
    /* Property changes for the elements clients hold (few: those they asked for). */
    if (any_changed && listening) {
        /* A raise may run client code that releases providers: work on a
         * referenced snapshot of the list, not the list itself. */
        uia_el_t *snap[128];
        gates_u32 count = 0;
        bool more = false;
        for (uia_el_t *e = win->uia_els; e != nullptr; e = e->next) {
            if (e->gone) continue;
            if (count == 128) {
                more = true;
                break;
            }
            el_addref(e);
            snap[count++] = e;
        }
        for (gates_u32 k = 0; k < count; k++) {
            diff(snap[k]);
            el_release(snap[k]);
        }
        if (more) raise_children_invalidated(win, (gates_access_ref_t){ gates_tree_root(t), 0 });
    }
    /* A dialog is announced as opened when it has its place on screen. */
    if (!ref_null(win->uia_opened)) {
        gates_access_info_t i;
        if (!gates_is_ok(gates_access_info(t, win->uia_opened.node, 0, &i))) {
            win->uia_opened = (gates_access_ref_t){ GATES_NODE_NULL, 0 };
        } else if ((i.states & GATES_ACCESS_OFFSCREEN) == 0) {
            if (listening) raise_on(win, win->uia_opened, UIA_Window_WindowOpenedEventId);
            win->uia_opened = (gates_access_ref_t){ GATES_NODE_NULL, 0 };
        }
    }
    /* Focus: a node, a radio option, a menu entry. */
    gates_access_ref_t f = gates_access_focus_ref(t);
    if (!ref_eq(f, win->uia_focus) && GetFocus() == win->hwnd) {
        win->uia_focus = f; /* only what clients were told: inactive changes wait */
        if (listening && !ref_null(f)) raise_on(win, f, UIA_AutomationFocusChangedEventId);
    }
    win->uia_draining = false;
}

/* -- the system caret (magnifiers and older screen readers follow it) --------------------- */

void gates_win32_caret_follow(gates_window_t *win) {
    if (win->hwnd == nullptr) return;
    gates_rect_t r;
    bool want = GetFocus() == win->hwnd && gates_input_caret_rect(win->tree, &r);
    if (!want) {
        if (win->caret_made) {
            DestroyCaret();
            win->caret_made = false;
        }
        return;
    }
    gates_rect_t px = gates_rect_px(r, win->dpi);
    if (px.w < 1) px.w = 1;
    if (!win->caret_made || win->caret_h != px.h) {
        if (win->caret_made) DestroyCaret();
        win->caret_made = CreateCaret(win->hwnd, nullptr, px.w, px.h) != FALSE; /* never shown: gates draws its own */
        win->caret_h = px.h;
    }
    if (win->caret_made) SetCaretPos(px.x, px.y);
}

void gates_win32_caret_drop(gates_window_t *win) {
    if (win->caret_made) {
        DestroyCaret();
        win->caret_made = false;
    }
}
