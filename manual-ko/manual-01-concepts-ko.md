# 1장 - 개념

헤더: `gates/tree.h`, `gates/types.h`, `gates/event.h`.

## 노드 트리

창에 보이는 것은 모두 창 트리의 노드다. 묶는 패널, 이름표, 버튼, 텍스트 상자, 뷰 전체가 그렇다.
프로그램은 노드를 만들어 부모 밑에 두고 바꾼다. gates 는 노드를 배치하고, 그리고, 입력을 보내고,
보조 기술에 설명한다. 트리는 유지된다(retained). 프로그램이 손으로 다시 그리는 것은 없다. 트리를
바꾸면 창이 바뀐 곳을 다시 그린다.

노드는 핸들 `gates_node_t` 로 부른다. 핸들은 칸 번호와 세대(generation)다. 노드가 없어지면 그 칸은
나중에 새 세대로 다른 노드를 담을 수 있으므로, 옛 핸들은 새 노드에 닿지 못한다. 모든 호출이 이를
확인하며, 낡은 핸들은 따르지 않고 거절한다(`PROVEN_ERR_INVALID_ARG`). 없애기는 하위 트리 전체를
한 번에 표시하고, 칸은 안전한 시점에 풀린다(`gates_tree_flush_destroys`, 창은 입력 차례마다 이것을
부른다). 그래서 처리기는 자기에게 이벤트를 보낸 노드를 없애도 된다.

<!-- example: manual/examples/ex_01_handles.c -->
```c
/* manual example (host): handles, generations and copies.
 * expect: stale handle refused; label kept its copy */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t root = gates_tree_root(tree), old, fresh;

    /* Text is copied: the caller's buffer may change or go away at once. */
    char buf[16] = "first";
    if (!gates_is_ok(gates_label_create(tree, root, (gates_str_t){ .ptr = (const gates_u8 *)buf, .size = strlen(buf) }, &old))) return 1;
    strcpy(buf, "XXXXX");

    /* Destroying marks the node; its slot is freed at a safe point. */
    if (!gates_is_ok(gates_node_destroy(tree, old))) return 1;
    (void)gates_tree_flush_destroys(tree);

    /* A new node may reuse the slot, with a new generation: the old handle
     * never reaches it. */
    if (!gates_is_ok(gates_label_create(tree, root, GATES_STR("second"), &fresh))) return 1;
    bool stale_refused = !gates_node_is_valid(tree, old) &&
                         gates_widget_set_text(tree, old, GATES_STR("oops")) == PROVEN_ERR_INVALID_ARG;
    gates_str_t now = gates_widget_text(tree, fresh);
    bool kept = now.size == 6 && memcmp(now.ptr, "second", 6) == 0;
    printf("%s; %s\n", stale_refused ? "stale handle refused" : "STALE HANDLE WORKED",
           kept ? "label kept its copy" : "label changed");
    gates_tree_destroy(tree);
    return stale_refused && kept ? 0 : 1;
}
```

## 소유: 복사하는 것과 빌리는 것

프로그램이 gates 에 주는 문자열은 호출이 성공하면 모두 복사된다. 이름표, 글자, 명령 이름, 선택지
이름, 접근성 이름이 그렇다. 호출한 쪽의 버퍼는 곧바로 바꿔도 된다. gates 가 돌려주는 것은
빌린 것이다.

| 돌려받는 것 | 유효한 동안 |
|---|---|
| `gates_widget_text`, `gates_textbox_text` | 그 노드가 다음에 바뀔 때까지 |
| 이벤트의 `ev->text` | 처리기가 돌아갈 때까지 |
| 뷰의 칸(`gates_cell_t.text`, 내 모델이 준 것) | 모델을 다음에 부를 때까지 |
| `gates_access_info_t` 의 문자열 | 그 트리에서 `gates_access_info` 를 다시 부르거나 무엇이든 바뀔 때까지 |

gates 에 등록한 콜백과 `user` 포인터(처리기, 모델, 명령 함수)는 등록된 동안 빌려 쓴다. 살려 두거나
먼저 등록을 푼다.

## 오류

실패할 수 있는 함수는 `gates_err_t` 를 돌려주고 `[[nodiscard]]` 가 붙어 있다. `GATES_OK`
(`gates_is_ok` 로 본다)는 바꿈이 전부 일어났다는 뜻이고, 다른 값이면 아무것도 일어나지 않았다.
만들지 못한 노드는 반쯤 지은 것을 남기지 않는다. 오류 값은 proven 의 것이다.
`PROVEN_ERR_INVALID_ARG`(틀렸거나 낡은 인자), `PROVEN_ERR_INVALID_STATE`(지금은 안 됨: 꺼진 컨트롤,
닫힌 대화상자), `PROVEN_ERR_NOMEM`, `PROVEN_ERR_OUT_OF_BOUNDS`(한도)가 있다. 메모리 부족은 충돌이
아니라 평범한 대답이며, 참조 응용들은 이를 처리한다.

## 이벤트와 안전한 시점

프로그램은 사람이 한 일을 이벤트로 안다. 위젯마다 처리기가 많아야 하나 있다
(`gates_widget_set_handler`). 사람이 무언가를 바꾸면 gates 는 그것을 설명하는 이벤트를 대기열에
넣고, 창은 입력 메시지를 다 처리한 뒤의 안전한 시점에 대기열을 전달한다
(`gates_tree_dispatch_events`). 입력 처리 도중에는 아무것도 전달하지 않으므로, 처리기는 자기를
부른 노드를 포함해 무엇이든 바꾸거나 없애도 된다.

프로그램이 스스로 한 바꿈은 조용하다. 이미 알고 있기 때문이다. 나머지 프로그램이 들어야 할 때는
`gates_widget_notify` 가 출처가 `GATES_ORIGIN_PROGRAM` 인 이벤트를 넣는다.

<!-- example: manual/examples/ex_02_events.c -->
```c
/* manual example (host): events - what the person did, delivered at a safe point.
 * expect: user checked=1, program checked=0 */
#include <gates/gates.h>

#include <stdio.h>

static char log_text[128];
static int log_len;

static void on_value(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind != GATES_EVENT_VALUE_CHANGED) return;
    log_len += snprintf(log_text + log_len, sizeof log_text - (size_t)log_len, "%s%s checked=%d",
                        log_len > 0 ? ", " : "", ev->origin == GATES_ORIGIN_USER ? "user" : "program",
                        ev->checked ? 1 : 0);
}

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t box;
    if (!gates_is_ok(gates_checkbox_create(tree, gates_tree_root(tree), GATES_STR("Send news"), false, nullptr, nullptr, &box)) ||
        !gates_is_ok(gates_widget_set_handler(tree, box, on_value, nullptr))) {
        return 1;
    }
    /* A person ticks the box (here through the accessibility action, which
     * takes the same path as a click): an event is queued, not delivered. */
    if (!gates_is_ok(gates_access_toggle(tree, box))) return 1;
    /* The window delivers queued events after each input message; without a
     * window, the program does it. */
    (void)gates_tree_dispatch_events(tree, 0);

    /* Setters are silent: the program knows what it did. Announce it
     * explicitly when the rest of the program should hear it. */
    if (!gates_is_ok(gates_checkbox_set_checked(tree, box, false))) return 1;
    if (!gates_is_ok(gates_widget_notify(tree, box))) return 1;
    (void)gates_tree_dispatch_events(tree, 0);

    printf("%s\n", log_text);
    gates_tree_destroy(tree);
    return 0;
}
```

여기서 규칙 둘이 나온다. 무엇이 바뀌었는지 알려고 그리는 도중에 위젯 상태를 읽지 않는다. 이벤트를
따른다. 그리고 그리기 콜백 안에서 모델을 바꾸지 않는다. 그리기는 그리기만 한다.

## 단위

API 의 모든 좌표와 크기는 논리 단위, 1/96 인치다. 창은 가장자리에서 한 번 바꾼다. 자세한 것은
8장에 있고, 그때까지는 픽셀을 떠올릴 자리에서 "단위"로 읽으면 된다.
