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

`gates_command_set_submenu(tree, scope, id, ids, count)` 는 항목에 같은 범위의 다른 명령들로 된 하위 메뉴를
준다. 그 항목에는 화살표가 붙고, Right, Enter, 클릭, 또는 포인터를 잠시 올려 두면 옆에 하위 메뉴가 열리며,
Left 나 Escape 가 다시 닫는다. 하위 메뉴에서 고르면 그 명령을 부르고 줄지어 열린 메뉴를 모두 닫으며, 처음 연
메뉴가 그 명령 id 로 MENU_CLOSED 를 알린다. 메뉴 막대의 메뉴에서도 같다.

## 실행 취소와 다시 실행

글 상자는 자기 입력을 스스로 되돌린다. 프로그램 자신의 데이터(이름을 바꾼 항목, 옮긴 줄, 바꾼 설정)에는
`gates_undo_create`(gates/undo.h)가 스택을 준다. 바꾼 뒤 이름표와 두 함수(되돌리기, 다시 하기)를 담은
항목을 넣는다. 같은 합치기 키로 이어진 항목은 하나가 되고(한 칸에 글 치기), 스택은 정해진 개수만 갖고,
`gates_undo_mark_clean` / `gates_undo_is_clean` 은 데이터가 저장한 그대로인지 알려 준다.
`gates_undo_bind` 는 명령 둘을 맞춰 둔다. 되돌리거나 다시 할 것이 있을 때만 켜지고, 이름은
"Undo Rename" / "Redo Rename" 처럼 된다(낱말은 프로그램의 언어로 줄 수 있다).

<!-- example: manual/examples/ex_04_undo.c -->
```c
/* manual example (host): Undo and Redo commands over the program's own data.
 * expect: menu shows "Undo Rename"; after Undo: name Draft, menu shows "Redo Rename"; clean again: yes */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { CMD_UNDO = 1, CMD_REDO = 2 };

static char name[32] = "Draft";

/* One entry remembers the name before and after the change. */
typedef struct rename_t {
    char before[32], after[32];
} rename_t;

static gates_err_t undo_rename(void *data) {
    memcpy(name, ((rename_t *)data)->before, sizeof name);
    return GATES_OK;
}
static gates_err_t redo_rename(void *data) {
    memcpy(name, ((rename_t *)data)->after, sizeof name);
    return GATES_OK;
}

static void on_command(gates_tree_t *tree, gates_command_id_t id, void *user) {
    (void)tree;
    gates_undo_t *undo = user;
    (void)(id == CMD_UNDO ? gates_undo_undo(undo) : gates_undo_redo(undo));
}

static void print_label(gates_tree_t *t, gates_command_id_t id) {
    gates_str_t l = gates_command_label(t, gates_tree_root(t), id);
    printf("\"%.*s\"", (int)l.size, (const char *)l.ptr);
}

int main(void) {
    gates_tree_t *t = nullptr;
    gates_undo_t *undo = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t)) ||
        !gates_is_ok(gates_undo_create((gates_allocator_t){0}, 0, &undo))) {
        return 1;
    }
    gates_node_t root = gates_tree_root(t);
    gates_command_desc_t cmds[] = {
        { .id = CMD_UNDO, .label = GATES_STR_INIT("Undo"), .shortcut = { .key = GATES_KEY_Z, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = undo },
        { .id = CMD_REDO, .label = GATES_STR_INIT("Redo"), .shortcut = { .key = GATES_KEY_Y, .ctrl = true },
          .enabled = true, .invoke = on_command, .user = undo },
    };
    if (!gates_is_ok(gates_command_register(t, root, &cmds[0])) || !gates_is_ok(gates_command_register(t, root, &cmds[1])) ||
        !gates_is_ok(gates_undo_bind(undo, t, root, CMD_UNDO, CMD_REDO, GATES_STR("Undo"), GATES_STR("Redo")))) {
        return 1;
    }
    gates_undo_mark_clean(undo); /* the name as saved */

    /* The program renames, then records how to take it back. */
    static rename_t change = { "Draft", "Final" };
    memcpy(name, change.after, sizeof name);
    gates_undo_entry_t entry = { .label = GATES_STR_INIT("Rename"), .undo = undo_rename, .undo_data = &change,
                                 .redo = redo_rename, .redo_data = &change };
    if (!gates_is_ok(gates_undo_push(undo, &entry))) return 1;
    printf("menu shows ");
    print_label(t, CMD_UNDO);

    /* A person presses Ctrl+Z. */
    gates_key_event_t ctrl_z = { .key = GATES_KEY_Z, .ctrl = true, .down = true };
    (void)gates_input_key(t, &ctrl_z);
    (void)gates_tree_dispatch_events(t, 0);
    printf("; after Undo: name %s, menu shows ", name);
    print_label(t, CMD_REDO);
    printf("; clean again: %s\n", gates_undo_is_clean(undo) ? "yes" : "no");

    gates_undo_destroy(undo);
    gates_tree_destroy(t);
    return 0;
}
```
