# 11장 - 응용 프로그램 틀

헤더: `gates/frame.h`, `gates/command.h`.

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

## 접근성

메뉴 막대는 MenuBar 요소이고 그 제목은 ExpandCollapse가 있는 메뉴 항목이다. 메뉴 모드에서는 강조된 제목이 키보드 초점을 가진다. 니모닉이 있는 컨트롤은 모두 그것을 접근 키로 알린다("Alt+F", 열린 메뉴의 항목은 글자만). 메뉴 항목과 명령에 묶인 버튼은 명령의 단축키를 가속 키로 알리며, 접근성 이름에는 `&` 표시가 들어가지 않는다.
