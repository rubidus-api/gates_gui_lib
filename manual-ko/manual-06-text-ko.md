# 6장 - 글자 입력

헤더: `gates/widget.h`(텍스트 상자), `gates/text_edit.h`, `gates/clipboard.h`, `gates/input.h`,
`gates/ui.h`.

## 텍스트 상자

텍스트 상자는 UTF-8 글 한 줄을 편집한다. 사람은 입력하고, Shift 와 포인터로 고르고, 글자 단위와
처음·끝(Home, End)으로 옮기고, 복사·잘라내기·붙여넣기(Ctrl+C, Ctrl+X, Ctrl+V)와 되돌리기·다시
하기(Ctrl+Z, Ctrl+Y)를 한다. 편집이 성공할 때마다 확정된 글과 함께 TEXT_CHANGED 가 대기열에 들어간다.
프로그램이 부르는 `gates_textbox_set_text` 는 조용하고 되돌리기 이력을 지운다. 모든 텍스트 상자
밑에는 편집 코어(`gates/text_edit.h`)가 있다. 글, 캐럿, 고른 범위, 열린 조합을 UTF-8 바이트 위치로
다룬다.

| 설정 | 효과 |
|---|---|
| `gates_textbox_set_read_only` | 포커스, 고르기, 복사는 된다. 편집, 붙여넣기, 잘라내기, 되돌리기, 조합은 거절한다 |
| `gates_textbox_set_password` | 글자마다 `*` 하나, 복사·잘라내기 없음, 이벤트에 글 없음, 되돌리기 없음, IME 없음 |
| `gates_textbox_set_max_bytes` | UTF-8 바이트로 잰 최대 길이(아래를 보라) |
| `gates_textbox_set_undo_limits` | 되돌리기 이력의 한도. 기본 64개, 16384 바이트 |
| `gates_textbox_set_invalid` | 틀림 모양(폼이 대신 해 준다) |

## 한도는 질문이다

입력이 최대 길이를 넘으려 하면 gates 는 아무것도 넣지 않고 묻는다. LIMIT_EXCEEDED 이벤트에 거절된
입력과, 그 가운데 몇 바이트가 들어갈 수 있는지가 담긴다. 프로그램은 사람에게 들어가는 만큼 넣을지,
버릴지 묻고, `gates_textbox_accept_fit` 이나 `gates_textbox_discard_rejected` 로 답한다. 붙여 넣은
계좌 번호를 소리 없이 반으로 자르는 실패를 이렇게 피한다.

<!-- example: manual/examples/ex_07_text.c -->
```c
/* manual example (host): a text box with a limit - the overflow is a question.
 * expect: offered 7 bytes, 5 fit; kept "Seoul" */
#include <gates/gates.h>

#include <stdio.h>

static gates_u32 offered, fit;

static void on_box(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind == GATES_EVENT_LIMIT_EXCEEDED) {
        offered = (gates_u32)ev->text.size;
        fit = ev->fit_bytes;
    }
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t city;
    if (!gates_is_ok(gates_textbox_create(t, gates_tree_root(t), GATES_STR(""), 20, &city)) ||
        !gates_is_ok(gates_textbox_set_max_bytes(t, city, 5)) ||
        !gates_is_ok(gates_widget_set_handler(t, city, on_box, nullptr))) {
        return 1;
    }
    gates_tree_set_focus(t, city);
    /* Typed or pasted input (an IME delivers its result the same way). */
    (void)gates_input_commit(t, GATES_STR("Seoul!!"));
    (void)gates_tree_dispatch_events(t, 0);
    /* Nothing was inserted: the box holds the input as an offer. Here the
     * answer is "keep what fits"; gates_textbox_discard_rejected drops it. */
    if (!gates_is_ok(gates_textbox_accept_fit(t, city))) return 1;
    gates_str_t text = gates_textbox_text(t, city);
    printf("offered %u bytes, %u fit; kept \"%.*s\"\n", offered, fit, (int)text.size, (const char *)text.ptr);
    gates_tree_destroy(t);
    return 0;
}
```

## 설치된 입력기

gates 는 입력기를 구현하지 않는다. Windows 에서는 설치된 IME(한국어, 일본어, 중국어 ...)가 IMM32 를
통해 모든 텍스트 상자에서 동작한다. 조합 중인 음절은 캐럿 자리에 바로 보이고, 후보 창은 캐럿 아래에
열리며, 끝난 글은 편집 하나로 들어온다. 읽기 전용이나 비밀번호 상자는 포커스를 가진 동안 IME 를
끈다. 글을 스스로 넣는 프로그램(시험, 자동화)을 위해 `gates_input_commit` 은 IME 처럼 글을 넣고,
`gates_input_preedit` 은 조합을 보인다.

## 0.6.0 의 유니코드 한계

- 글은 어디서나 UTF-8 이다. 잘못된 입력은 거절하며, 몰래 고치지 않는다.
- 글자마다 글꼴에서 가져온 제 폭(advance)이 있다. UI 글꼴에서는 비례폭, 고정폭 글꼴에서는
  고정폭이다(8장). 글줄의 폭은 글자 폭의 합과 정확히 같으므로 캐럿, 고른 범위, 누른 자리는 늘
  글자 사이에 떨어진다.
- 커닝, 합자, 셰이핑은 없다. 셰이핑이 필요한 문자(아랍 문자, 인도계 문자, 타이 문자)는 글자가
  하나씩 따로 보이고, 오른쪽에서 왼쪽으로 쓰는 글은 다시 배열하지 않는다.
- 편집은 문자소 묶음(grapheme cluster)이 아니라 코드 포인트 단위로 움직인다. 바탕 글자와 결합
  부호는 두 걸음이다.
- Win32 백엔드는 시스템의 진짜 글꼴로 그리며, 그 글꼴에 없는 글자(Segoe UI 의 한글)는 시스템의
  대체 글꼴에서 가져온다. 내장 백엔드(시험, 창 없음)는 고정폭이고 ASCII 를 그리며, 나머지는 맞는
  너비의 상자로 그린다.
- 텍스트 상자는 한 줄이다. 여러 줄 편집기는 아직 없다.

## 클립보드

창이 플랫폼 클립보드를 대 준다(`gates/clipboard.h`). 글은 UTF-8 로 넘나들고 가장자리에서 바뀐다.
클립보드 제공자가 없는 트리(창 없음)에서는 복사, 잘라내기, 붙여넣기가 아무 일도 하지 않는다.
