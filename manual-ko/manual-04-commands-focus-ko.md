# 4장 - 명령, 포커스, 대화상자, 메뉴

헤더: `gates/command.h`, `gates/overlay.h`.

## 동작 하나, 청하는 길 여럿

"저장"은 사람이 버튼을 누르든, Ctrl+S 를 누르든, 문맥 메뉴에서 고르든 같은 동작 하나다. 명령
(command)이 이것을 한 번만 갖는다. id, 이름, 단축키(있으면), 켜짐과 체크 상태, 실행할 함수다.
명령은 범위(scope) 노드에 속하고, 단축키는 포커스가 그 범위 안에 있을 때 동작한다. 명령에 묶인
버튼(`gates_button_set_command`)은 명령의 이름과 켜짐 상태를 가져오고, 명령이 바뀌면 따라간다.
명령을 끄면 그것을 부르는 모든 길이 한꺼번에 꺼진다.

부르기는 대기열에 들어가고 다음 안전한 시점에, 명령을 다시 확인한 뒤에 돈다. 그 사이에 꺼진
명령은 돌지 않는다.

<!-- example: manual/examples/ex_05_commands.c -->
```c
/* manual example (host): one command for a button and a shortcut.
 * expect: saved 1 time; button disabled with the command */
#include <gates/gates.h>

#include <stdio.h>

enum { CMD_SAVE = 1 };

static void on_save(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    (void)id;
    (*(int *)user)++;
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), save;
    int saves = 0;
    gates_command_desc_t cmd = { .id = CMD_SAVE, .label = GATES_STR("Save"), .enabled = true,
                                 .shortcut = { .letter = 'S', .ctrl = true },
                                 .invoke = on_save, .user = &saves };
    if (!gates_is_ok(gates_command_register(t, root, &cmd)) ||
        !gates_is_ok(gates_button_create(t, root, GATES_STR(""), nullptr, nullptr, &save)) ||
        !gates_is_ok(gates_button_set_command(t, save, root, CMD_SAVE))) { /* label, state from the command */
        return 1;
    }
    /* Ctrl+S: the command is queued and runs at the next safe point. */
    gates_key_event_t ctrl_s = { .letter = 'S', .ctrl = true, .down = true };
    (void)gates_input_key(t, &ctrl_s);
    (void)gates_tree_dispatch_events(t, 0);

    /* Disabling the command disables every button bound to it, and its shortcut. */
    if (!gates_is_ok(gates_command_set_enabled(t, root, CMD_SAVE, false))) return 1;
    (void)gates_input_key(t, &ctrl_s);
    (void)gates_tree_dispatch_events(t, 0);

    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, save, 0, &info))) return 1;
    printf("saved %d time%s; button %s with the command\n", saves, saves == 1 ? "" : "s",
           (info.states & GATES_ACCESS_DISABLED) != 0 ? "disabled" : "enabled");
    gates_tree_destroy(t);
    return 0;
}
```

명령에는 역할이 있을 수 있다. 범위의 DEFAULT 명령은 그 범위의 텍스트 상자나 체크박스에서 Enter 를
누르면 돌고, CANCEL 명령은 범위 어디서든 Escape 로 돈다(대화상자라면 확인과 취소).

## 포커스

키보드 포커스는 컨트롤 하나에 있다(`gates_tree_focus`, `gates_tree_set_focus`). Tab 과 Shift+Tab 은
트리 차례로 옮기며 지금의 포커스 범위 안에서 돈다. 창의 범위는 트리 전체이고, 열린 대화상자의
범위는 그 대화상자다. 포커스를 가진 컨트롤이 숨겨지거나 없어지면 포커스를 놓는다(그 안의 누름과
끌기도 놓는다). 포커스를 다음에 어디 둘지는 프로그램이 `gates_tree_set_focus` 로 정한다. 대화상자가
닫히면 포커스는 그 전에 가졌던 컨트롤로 돌아간다.

## 대화상자

`gates_dialog_open` 은 창 안에 모달 대화상자를 보인다. 창의 나머지는 흐려지고 입력을 무시하며, Tab
은 안에만 머물고, 창 자신의 단축키는 기다린다. 열기는 곧바로 돌아온다. 중첩된 이벤트 루프는 없다.
프로그램은 대화상자의 내용 패널을 채우고, 대화상자를 범위로 삼아 명령을 등록하고, 답을
DIALOG_CLOSED 이벤트 한 번(`ev->result`: ACCEPTED 또는 CANCELED)으로 안다. 포커스는 원래 자리로
돌아간다. 대화상자와 메뉴는 한 번에 여덟 개까지 열린다.

## 문맥 메뉴

`gates_menu_open` 은 범위의 명령들을 한 점에 나열한다. 이름, 체크 표시, 단축키, 켜짐 상태는 명령에서
온다. 항목을 고르면 단축키와 같은, 대기열을 거치고 다시 확인하는 길로 명령을 부른다. 화살표가
옮기고, Enter 가 고르고, Escape 나 바깥 누름은 닫기만 한다(바깥을 누른 것이 그 아래 것을 함께
누르는 일은 없다).
