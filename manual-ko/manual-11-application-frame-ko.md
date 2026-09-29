# 11장 - 응용 프로그램 틀

헤더: `gates/frame.h`, `gates/command.h`, `gates/state.h`.

데스크톱 도구는 보통 내용 둘레에 틀을 갖는다. 메뉴 막대가 있고, 레이블과 버튼에는 키보드 접근 키가 있으며, 사람이 익히고 바꿀 수 있는 단축키가 있다. 이 모두는 4장의 명령으로 움직이므로, 한 동작은 어디에 나타나든 레이블 하나, 사용 가능 상태 하나, 단축키 하나를 지킨다.

## 니모닉

버튼, 체크 상자, 명령, 메뉴 막대 제목, 탭의 글자에서 `&x`는 `x`가 영문자나 숫자일 때 `x`를 니모닉으로 표시한다. 예를 들면 `"&Save"`, `"E&xit"`, `"Page &2"`이다. 그러면 Alt+x가 그 컨트롤을 작동시킨다. 버튼은 눌리고, 체크 상자는 바뀌고, 메뉴 막대 제목은 열린다. `&` 두 개는 하나로 보이고(`"Fish && chips"`), 그 밖의 `&`는 그대로 보이므로 `"Save & close"`는 따로 처리할 필요가 없다. 니모닉 표시는 자리를 차지하지 않는다. `"&Save"`는 `"Save"`와 너비가 똑같다.

그 글자는 다른 Windows 프로그램처럼 키보드 표시가 보이는 동안 밑줄이 그어진다. Alt를 누르거나 키보드로 메뉴 막대에 들어간 때부터 다음 포인터 누름까지이고, "선택키 밑줄 표시" 설정이 켜져 있으면 늘 그어진다.

레이블은 파일 이름 같은 자료를 보여 주는 일이 많으므로, 대상이 있을 때만 `&`를 해석한다. `gates_label_set_target(tree, label, box)`를 부르면 Alt+x가 `box`에 초점을 준다. 그 상자에 레이블을 접근성 이름으로도 주면(`gates_node_set_labelled_by`) 화면 낭독기가 이름과 함께 키를 읽는다.

여러 컨트롤이 같은 글자를 쓰면 Alt+x를 누를 때마다 초점이 다음 컨트롤로 옮겨 가고 아무것도 작동하지 않는다. 사람이 Space나 Enter를 누른다. 키보드가 닿는 컨트롤만 센다. 쓸 수 없는 버튼, 숨은 패널, 모달 대화 상자 뒤의 창은 건너뛴다. 니모닉은 ASCII 영문자와 숫자이다. 다른 문자에는 `"파일(&F)"`처럼 괄호 안에 하나 넣는다.

## 메뉴 막대

트리에는 메뉴 막대가 많아야 하나 있다. `gates_menubar_create(tree, parent, scope, &bar)`로 만든다. `gates_menubar_add(tree, bar, title, ids, count, nullptr)`를 부를 때마다 `scope`의 명령 목록 위에 제목 하나가 더해지고, id 0은 구분선을 그린다. 문맥 메뉴가 받는 목록과 같다. 메뉴는 보통 메뉴 오버레이이므로 4장에서 메뉴에 대해 말한 것이 모두 성립한다. Escape, 바깥 클릭, 창이 초점을 잃을 때 닫히고, 고른 항목은 명령을 실행하기 전에 다시 확인되며, 열린 메뉴에 처리기를 두면 MENU_CLOSED를 받는다.

포인터로는 제목을 클릭하면 그 아래에 메뉴가 열린다. 하나가 열려 있는 동안 다른 제목 위로 움직이면 그 메뉴로 바뀌고, 열린 제목을 클릭하면 닫힌다. 키보드로는 다음과 같다.

| 키 | 메뉴 모드(제목 하나가 강조됨) | 열린 메뉴 |
|---|---|---|
| F10, 또는 Alt만 눌렀다 뗌 | 메뉴 모드에 들어감 / 나감 | 나감 |
| Left / Right | 앞 / 다음 제목 | 이웃 메뉴를 엶 |
| Down, Up, Enter, Space | 강조된 제목을 엶 | 이동 / 고름 |
| 제목의 니모닉 글자 | 그 제목을 엶 | - |
| 항목의 니모닉 글자 | - | 그 항목을 고름 |
| Escape | 메뉴 모드에서 나감 | 강조된 제목으로 돌아감 |

제목의 글자로 Alt+x를 누르면 그 메뉴가 곧바로 열린다. 제목이 컨트롤보다 먼저이므로 컨트롤에는 제목이 쓰지 않는 글자를 준다. F10은 메뉴 막대가 닿을 수 있는 동안만 메뉴 키이다. 메뉴 막대가 없는 창에서는 보통 명령 단축키로 남는다. 막대는 Tab 정지 위치가 아니며, 모달 대화 상자가 있으면 닿을 수 없다. 이 판에서 메뉴에는 하위 메뉴가 없다.

Windows에서 gates는 Alt+글자가 니모닉과 맞을 때만 받는다. Alt+F4, Alt+Tab, Alt+Space는 Windows가 하는 일을 그대로 한다.

## 키맵: 단축키 나열과 다시 묶기

각 명령에 저장된 단축키가 곧 키맵이다. `gates_command_count`와 `gates_command_at`은 한 범위의 명령을 나열한다(키맵 편집기나 도움말 화면에 쓴다). `gates_command_set_shortcut`은 하나를 다시 묶고, 그 범위의 다른 명령이 이미 가진 단축키는 거절하며 그 명령을 `*conflict`에 알려 준다. `gates_shortcut_format`은 단축키를 메뉴와 화면 낭독기가 보여 주는 대로 적고(`"Ctrl+Shift+S"`, `"F5"`, `"Ctrl+Del"`), `gates_shortcut_parse`는 같은 글자를 다시 읽는다. 그래서 프로그램은 사람의 키맵을 `save=Ctrl+S` 같은 줄로 된 파일에 둘 수 있다. 4장의 규칙은 그대로이다. 단축키에는 Ctrl이 있어야 하거나 기능 키여야 하고, Alt는 단축키에 들어가지 않는다.

<!-- example: manual/examples/ex_11_menubar.c -->
```c
/* manual example (host): a menu bar, its keyboard and its mnemonics.
 * expect: new 1, open 1; F10 highlights File; access key Alt+F; shortcut Ctrl+Shift+N */
#include <gates/gates.h>

#include <stdio.h>

enum { CMD_NEW = 1, CMD_OPEN, CMD_QUIT };

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    ((int *)user)[id]++;
}

static bool press(gates_tree_t *t, gates_key_t key) {
    gates_key_event_t e = { .key = key, .down = true };
    return gates_input_key(t, &e);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), bar;
    int runs[4] = {0};
    /* "&" marks the mnemonic: Alt+N, and N while the menu is open. */
    gates_command_desc_t cmds[] = {
        { .id = CMD_NEW, .label = GATES_STR("&New"), .shortcut = { .letter = 'N', .ctrl = true },
          .enabled = true, .invoke = on_command, .user = runs },
        { .id = CMD_OPEN, .label = GATES_STR("&Open..."), .enabled = true, .invoke = on_command, .user = runs },
        { .id = CMD_QUIT, .label = GATES_STR("E&xit"), .enabled = true, .invoke = on_command, .user = runs },
    };
    for (int i = 0; i < 3; i++) {
        if (!gates_is_ok(gates_command_register(t, root, &cmds[i]))) return 1;
    }
    static const gates_command_id_t file[] = { CMD_NEW, CMD_OPEN, 0, CMD_QUIT }; /* 0: separator */
    if (!gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_menubar_create(t, root, root, &bar)) ||
        !gates_is_ok(gates_menubar_add(t, bar, GATES_STR("&File"), file, 4, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 300 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* F10 enters menu mode, Down opens File on its first entry, Enter chooses it. */
    (void)press(t, GATES_KEY_F10);
    bool highlighted = gates_menubar_highlighted(t, bar) == 0;
    (void)press(t, GATES_KEY_DOWN);
    (void)press(t, GATES_KEY_ENTER);
    /* Alt+F opens File directly (the platform sends Alt+letter here); O chooses Open. */
    (void)gates_input_mnemonic(t, 'f');
    gates_key_event_t o = { .letter = 'O', .down = true };
    (void)gates_input_key(t, &o);
    (void)gates_tree_dispatch_events(t, 0);

    /* The keymap: rebind New, and print the shortcut as menus do. */
    gates_command_id_t conflict = 0;
    if (!gates_is_ok(gates_command_set_shortcut(t, root, CMD_NEW,
                                                (gates_shortcut_t){ .letter = 'N', .ctrl = true, .shift = true },
                                                &conflict))) {
        return 1;
    }
    char keys[32];
    gates_shortcut_t now = gates_command_shortcut(t, root, CMD_NEW);
    (void)gates_shortcut_format(&now, keys, sizeof keys);

    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, bar, 1, &info))) return 1;   /* item 1: the File title */
    printf("new %d, open %d; F10 %s File; access key %.*s; shortcut %s\n", runs[CMD_NEW], runs[CMD_OPEN],
           highlighted ? "highlights" : "misses", (int)info.access_key.size, (const char *)info.access_key.ptr,
           keys);
    gates_tree_destroy(t);
    return 0;
}
```

## 도구 막대

`gates_toolbar_create(tree, parent, scope, &bar)`는 작은 버튼이 늘어선 줄을 만들고, `gates_toolbar_add(tree, bar, id)`를 부를 때마다 `scope`의 명령에 묶인 버튼이 하나씩 더해진다(id 0은 구분선). 버튼은 명령의 레이블을 니모닉 표시 없이 보여 주고, 명령을 쓸 수 없는 동안은 흐리게, 명령이 체크된 동안은 눌린 모습으로 보인다. 굵게 같은 켜고 끄는 버튼은 체크된 명령이다. 버튼은 포인터가 위에 올 때까지 평평하다.

클릭은 명령을 실행하고 키보드 초점은 그대로 둔다. 그래서 잘라내기와 붙여넣기 버튼은 사람이 입력하던 글상자에 작용한다. 키보드로는 도구 막대 전체가 Tab 정지 위치 하나이다. Left, Right, Home, End가 쓸 수 있는 버튼 사이를 옮기고, Space나 Enter가 초점이 있는 버튼을 실행한다. 도구 막대가 버튼보다 좁으면 들어가지 않는 버튼은 빠지고, 끝의 `>>` 버튼이 그 명령들을 메뉴로 보여 준다.

## 상태 줄

`gates_statusbar_create(tree, parent, &bar)`는 창 아래쪽을 따라 놓이는 띠를 만들고, `gates_statusbar_add(tree, bar, text, grow, &segment)`는 구역 하나를 더한다. 구역은 레이블이므로 글자는 `gates_widget_set_text`로 바꾼다. 구역 사이에는 가는 선이 있고, `grow`가 있는 구역은 레이아웃 자식처럼 남는 너비를 나눠 가진다. 상태 글자는 바뀔 때마다 읽히지 않는다. 소식이 중요한 구역만 라이브 영역으로 만든다(`gates_node_set_live`).

## 툴팁

`gates_node_set_tooltip(tree, node, text)`는 노드에 짧은 도움말을 준다. 포인터가 `GATES_TOOLTIP_DELAY_MS`(500 ms) 동안 머물거나 키보드 초점이 온 뒤 그만큼 지나면 노드 아래(창 아래쪽 가까이에서는 위)의 작은 상자에 나타나고, 누름, 키, 포인터나 초점이 떠날 때, 그리고 `GATES_TOOLTIP_SHOW_MS`가 지나면 숨는다. 툴팁 하나가 보이는 동안 다른 툴팁으로 옮기면 곧바로 바뀐다. 도구 막대 버튼은 따로 부탁하지 않아도 명령의 레이블과 단축키로 된 툴팁을 가진다(`"Paste (Ctrl+V)"`). 툴팁은 입력을 받지 않으며, 화면 낭독기는 그 글자를 노드의 도움말로 읽는다. 툴팁은 트리의 시계로 시간을 재며, 포인터가 올라가 있거나 초점이 있는 노드에 툴팁이 없는 동안에는 아무것도 재지 않는다.

<!-- example: manual/examples/ex_11_toolbar.c -->
```c
/* manual example (host): a toolbar, a status bar and a tooltip.
 * expect: pasted 1, focus kept; tooltip "Paste (Ctrl+V)" after 500 ms; status Pasted */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { CMD_CUT = 1, CMD_PASTE };

typedef struct app_t {
    gates_node_t status;
    int pasted;
} app_t;

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    app_t *a = user;
    if (id == CMD_PASTE) {
        a->pasted++;
        (void)gates_widget_set_text(tree, a->status, GATES_STR("Pasted"));
    }
}

/* A test clock: a window's tree has a real one. */
static gates_u64 now_ms;
static gates_u64 clock_now(void *ctx) { (void)ctx; return now_ms; }
static void clock_changed(void *ctx) { (void)ctx; }

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_tree_set_clock(t, clock_now, clock_changed, nullptr);
    gates_node_t root = gates_tree_root(t), bar, body, status_bar;
    app_t a = {0};
    gates_command_desc_t cmds[] = {
        { .id = CMD_CUT, .label = GATES_STR("Cu&t"), .shortcut = { .key = GATES_KEY_X, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = &a },
        { .id = CMD_PASTE, .label = GATES_STR("&Paste"), .shortcut = { .key = GATES_KEY_V, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = &a },
    };
    if (!gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_command_register(t, root, &cmds[0])) ||
        !gates_is_ok(gates_command_register(t, root, &cmds[1])) ||
        !gates_is_ok(gates_toolbar_create(t, root, root, &bar)) ||
        !gates_is_ok(gates_toolbar_add(t, bar, CMD_CUT)) ||
        !gates_is_ok(gates_toolbar_add(t, bar, CMD_PASTE)) ||
        !gates_is_ok(gates_textbox_create(t, root, GATES_STR(""), 30, &body)) ||
        !gates_is_ok(gates_node_set_access_name(t, body, GATES_STR("Text"))) ||
        !gates_is_ok(gates_layout_set_child_grow(t, body, 1)) ||
        !gates_is_ok(gates_statusbar_create(t, root, &status_bar)) ||
        !gates_is_ok(gates_statusbar_add(t, status_bar, GATES_STR("Ready"), 1, &a.status)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 300 }, gates_text_backend_builtin()))) {
        return 1;
    }
    gates_tree_set_focus(t, body);

    /* Find the Paste button through the accessibility model: item 2 of the bar. */
    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, bar, 2, &info))) return 1;
    gates_point_t p = { info.bounds.x + info.bounds.w / 2, info.bounds.y + info.bounds.h / 2 };

    /* Rest the pointer on it: the tooltip shows after GATES_TOOLTIP_DELAY_MS. */
    gates_pointer_event_t move = { .action = GATES_POINTER_MOVE, .pos = p };
    (void)gates_input_pointer(t, &move);
    now_ms += GATES_TOOLTIP_DELAY_MS;
    (void)gates_tree_run_timers(t);
    gates_str_t tip = {0};
    char tip_text[64] = "";
    if (gates_tooltip_shown(t, nullptr, nullptr, &tip, nullptr) && tip.size < sizeof tip_text) {
        memcpy(tip_text, tip.ptr, tip.size);
    }

    /* A click invokes the command; the focus stays in the text box. */
    gates_pointer_event_t down = { .action = GATES_POINTER_DOWN, .button = GATES_BUTTON_LEFT, .pos = p };
    gates_pointer_event_t up = { .action = GATES_POINTER_UP, .button = GATES_BUTTON_LEFT, .pos = p };
    (void)gates_input_pointer(t, &down);
    (void)gates_input_pointer(t, &up);
    (void)gates_tree_dispatch_events(t, 0);

    gates_str_t s = gates_widget_text(t, a.status);
    printf("pasted %d, focus %s; tooltip \"%s\" after %u ms; status %.*s\n", a.pasted,
           gates_node_eq(gates_tree_focus(t), body) ? "kept" : "moved", tip_text, GATES_TOOLTIP_DELAY_MS,
           (int)s.size, (const char *)s.ptr);
    gates_tree_destroy(t);
    return 0;
}
```

## 탭

`gates_tabs_create(tree, parent, &tabs)`는 페이지 위에 제목이 늘어선 띠를 만들고, `gates_tabs_add(tree, tabs, title, &page)`를 부를 때마다 탭 하나가 더해지며 그 페이지를 돌려준다. 페이지는 컨트롤을 담는 세로 패널이다. 한 번에 한 페이지만 보이고, 다른 페이지의 컨트롤은 포인터, 키보드, 보조 기술이 닿지 않는다.

띠 전체가 Tab 정지 위치 하나이다. Left, Right, Home, End는 곧바로 고르고, Ctrl+Tab과 Ctrl+Shift+Tab(또는 Ctrl+PgDn과 Ctrl+PgUp)은 탭 안 어디서나 바꾸며 끝에서 처음으로 돌아간다. 클릭은 고르고 띠에 초점을 주며, 제목의 니모닉은 그 탭을 고른다. 초점이 사라지는 페이지에 있었으면 새 페이지의 첫 컨트롤로, 없으면 띠로 옮겨 간다. 사람이 바꾸면 탭 노드에 GATES_EVENT_VALUE_CHANGED가 오고 `result`는 새 번호이다. `gates_tabs_set_selected`는 조용히 바꾼다. 이 판에서 들어가지 않는 제목은 오른쪽 끝에서 잘린다.

## 배치 기억하기

분할선을 끌고, 탭을 고르고, 열을 넓히고, 스크롤한 사람은 다음에도 그대로이기를 바란다. `gates_state_save`는 자동화 id가 있는 모든 노드의 그런 배치를 글자로 적고(`split`, `tabs`, `columns`, `scroll` 줄), `gates_state_load`는 그 글자를 새로 만든 트리에 적용한다. 글자를 어디에 둘지는 프로그램이 정하며, 보통 설정 옆의 파일이다. id가 열쇠이므로 판이 바뀌어도 id를 그대로 둔다. 더는 맞지 않는 줄은 건너뛰므로 오래된 파일이 새 프로그램을 망가뜨리지 않는다. Windows에서는 `gates_window_placement`와 `gates_window_set_placement`가 창의 위치, 크기, 최대화 상태를 같은 방식으로 다루고, 모니터가 사라진 창은 가장 가까운 모니터로 데려온다.

<!-- example: manual/examples/ex_11_tabs_state.c -->
```c
/* manual example (host): tabs, and state kept between runs.
 * expect: tab 1 (Advanced) after Ctrl+Tab; saved 2 lines; next run: tab 1, split 700 */
#include <gates/gates.h>

#include <stdio.h>

/* Builds the same window each run: tabs and a split, both with automation ids. */
static bool build(gates_tree_t **out, gates_node_t *tabs, gates_node_t *split, gates_node_t *apply) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return false;
    gates_node_t root = gates_tree_root(t), page, left, right, reset;
    bool ok = gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) &&
              gates_is_ok(gates_tabs_create(t, root, tabs)) &&
              gates_is_ok(gates_tabs_add(t, *tabs, GATES_STR("&General"), &page)) &&
              gates_is_ok(gates_button_create(t, page, GATES_STR("Apply"), nullptr, nullptr, apply)) &&
              gates_is_ok(gates_tabs_add(t, *tabs, GATES_STR("&Advanced"), &page)) &&
              gates_is_ok(gates_button_create(t, page, GATES_STR("Reset"), nullptr, nullptr, &reset)) &&
              gates_is_ok(gates_node_set_automation_id(t, *tabs, GATES_STR("settings.tabs"))) &&
              gates_is_ok(gates_panel_create(t, root, split)) &&
              gates_is_ok(gates_layout_set(t, *split, GATES_LAYOUT_KIND_SPLIT)) &&
              gates_is_ok(gates_panel_create(t, *split, &left)) &&
              gates_is_ok(gates_panel_create(t, *split, &right)) &&
              gates_is_ok(gates_node_set_automation_id(t, *split, GATES_STR("main.split"))) &&
              gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 300 }, gates_text_backend_builtin()));
    *out = t;
    return ok;
}

int main(void) {
    gates_tree_t *t;
    gates_node_t tabs, split, apply;
    if (!build(&t, &tabs, &split, &apply)) return 1;
    /* The person works in General, then Ctrl+Tab switches to Advanced. */
    gates_tree_set_focus(t, apply);
    gates_key_event_t ctrl_tab = { .key = GATES_KEY_TAB, .ctrl = true, .down = true };
    (void)gates_input_key(t, &ctrl_tab);
    gates_u32 now = gates_tabs_selected(t, tabs);
    gates_str_t title = gates_tabs_title(t, tabs, now);
    /* ...and drags the split. Save the state (a real program writes it to a file). */
    if (!gates_is_ok(gates_layout_set_split(t, split, GATES_SPLIT_HORIZONTAL, 700))) return 1;
    gates_u8 text[256];
    gates_usize_t len = 0;
    if (!gates_is_ok(gates_state_save(t, text, sizeof text, &len))) return 1;
    int lines = 0;
    for (gates_usize_t i = 0; i < len; i++) lines += text[i] == '\n' && i > 16;
    gates_tree_destroy(t);

    /* The next run builds the window again and loads what was saved. */
    if (!build(&t, &tabs, &split, &apply)) return 1;
    if (!gates_is_ok(gates_state_load(t, (gates_str_t){ .ptr = text, .size = len }, nullptr))) return 1;
    printf("tab %u (%.*s) after Ctrl+Tab; saved %d lines; next run: tab %u, split %d\n", now,
           (int)title.size - 1, (const char *)title.ptr + 1, lines, gates_tabs_selected(t, tabs),
           gates_layout_split_ratio(t, split));
    gates_tree_destroy(t);
    return 0;
}
```

## 접근성

메뉴 막대는 MenuBar 요소이고 그 제목은 ExpandCollapse가 있는 메뉴 항목이다. 메뉴 모드에서는 강조된 제목이 키보드 초점을 가진다. 니모닉이 있는 컨트롤은 모두 그것을 접근 키로 알린다("Alt+F", 열린 메뉴의 항목은 글자만). 메뉴 항목과 명령에 묶인 버튼은 명령의 단축키를 가속 키로 알리며, 접근성 이름에는 `&` 표시가 들어가지 않는다. 도구 막대는 버튼을 항목으로 가진 ToolBar 요소이고(구분선은 항목이 아니며, `>>`는 보이는 동안 More라는 항목이다), 상태 줄은 구역을 글자로 가진 StatusBar 요소이다. 탭 띠는 제목을 SelectionItem이 있는 TabItem 항목으로 가진 Tab 요소이고, 각 페이지는 탭 이름을 가진 그룹이다.
