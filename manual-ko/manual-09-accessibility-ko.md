# 9장 - 접근성

헤더: `gates/access.h`.

## 모든 창은 눈과 포인터 없이도 읽고 쓸 수 있다

gates 는 모든 노드를 보조 기술에 설명한다. 역할(버튼, 체크박스, 편집, 목록 ...), 이름, 설명, 상태
(포커스, 꺼짐, 체크, 고름, 펼침, 읽기 전용, 틀림, 필수, 화면 밖), 값, 받아들이는 동작이다. 라디오
선택지, 선택 상자의 선택지, 메뉴 항목, 뷰의 줄은 제 컨트롤의 항목이며 id 로 부른다. Windows 에서
창은 UI Automation 제공자여서 내레이터(Narrator)와 자동화 도구가 그 전부를 본다. 사람이 키보드로 할
수 있는 모든 동작을 UI Automation 으로도 할 수 있다. 누르기, 켜고 끄기, 고르기, 펼치기, 값 넣기,
스크롤, 글 읽기와 고르기가 그렇다.

보조 기술에서 온 동작은 입력과 같은 길을 간다. 같은 이벤트, 명령, 한도, 거절을 거친다. 키보드가 할
수 없는 일은 화면 읽기 프로그램도 할 수 없다.

## 이름

컨트롤의 접근성 이름은 이 차례로 정해진다. 직접 준 이름(`gates_node_set_access_name`), 묶인 이름표
(`gates_node_set_labelled_by`, HTML 의 `<label for>` 와 같다), 폼의 이름표, 제 글(버튼의 이름,
체크박스의 글)이다. 텍스트 상자 옆에 놓은 이름표는 프로그램이 묶기 전까지는 묶이지 않는다. 감사가
그런 필드를 찾아낸다.

제 이름이 없는 메뉴는 메뉴 막대의 제목, 자기를 연 항목(하위 메뉴), 또는 프로그램이 열 때 포커스를 가진 컨트롤의
이름(맥락 메뉴)을 이름으로 쓴다(0.10.0).

<!-- example: manual/examples/ex_10_access.c -->
```c
/* manual example (host): the audit finds a field without a name; a label fixes it.
 * expect: before: 1 issue (no name); after: 0 issues, name "City" */
#include <gates/gates.h>

#include <stdio.h>

static gates_u32 audit(gates_tree_t *t, gates_access_rule_t *first) {
    (void)gates_layout_run(t, (gates_size_t){ 300, 120 }, gates_text_backend_builtin());
    gates_access_issue_t issues[8];
    gates_u32 n = gates_access_audit(t, gates_theme_light(), issues, 8);
    if (n > 0) *first = issues[0].rule;
    return n;
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), caption, city;
    if (!gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_label_create(t, root, GATES_STR("City"), &caption)) ||
        !gates_is_ok(gates_textbox_create(t, root, GATES_STR(""), 20, &city))) {
        return 1;
    }
    gates_access_rule_t rule = 0;
    gates_u32 before = audit(t, &rule);
    /* A label beside a field is not tied to it until the program says so. */
    if (!gates_is_ok(gates_node_set_labelled_by(t, city, caption))) return 1;
    gates_u32 after = audit(t, &rule);
    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, city, 0, &info))) return 1;
    printf("before: %u issue%s (%s); after: %u issues, name \"%.*s\"\n", before, before == 1 ? "" : "s",
           rule == GATES_RULE_NO_NAME ? "no name" : "other", after, (int)info.name.size, (const char *)info.name.ptr);
    gates_tree_destroy(t);
    return after == 0 ? 0 : 1;
}
```

폼 필드의 도움말과 오류는 설명이 된다. 그래서 화면 읽기 프로그램은 "Name, 편집, 필수, enter a name"
처럼 읽는다. 자동화 id(`gates_node_set_automation_id`)는 시험 스크립트를 위한 변하지 않는 이름이며,
폼과 명령은 기본으로 `field-<id>` 와 `cmd-<id>` 를 준다.

## 강제하는 규칙

`gates_access_audit` 은 배치를 마친 트리를, 예제와 참조 응용이 지키는 규칙에 비추어 검사한다.

| 규칙 | 검사 |
|---|---|
| NO_NAME | 조작하는 컨트롤마다 접근성 이름이 있다 |
| TARGET_SIZE | 포인터 목표가 적어도 24 x 24 단위다(WCAG 2.2, 2.5.8) |
| KEYBOARD | 조작하는 컨트롤마다 키보드로 닿을 수 있다 |
| DUPLICATE_ID | 자동화 id 가 트리 안에서 유일하다 |
| CONTRAST | 테마의 글자와 표시가 대비 비율을 지킨다 |
| FOCUS_CUE | 포커스와 오류 표시가 적어도 2 단위 넓다 |

감사는 시험에서 돌린다. Windows 에서는 환경의 `GATES_ACCESS_STRICT=1` 이 배치할 때마다 모든 창을
감사하고 문제 수를 제목에 보인다.

## 소리 내어 알리기

사람이 일하는 동안 바뀌는 상태 줄은 라이브 영역(`gates_node_set_live`)이어야 한다. 정중한(polite)
것은 지금 읽는 말 뒤에, 단호한(assertive) 것은 곧바로 읽힌다. 어떤 컨트롤에도 속하지 않는 메시지는
`gates_window_announce` 가 말한다. 숨은 시스템 캐럿이 글 캐럿을 따라가므로 돋보기는 넣는 자리를 계속
보여 준다.

## 내레이터로 확인하기

내레이터를 켜고(Ctrl+Windows+Enter) 키보드만으로 프로그램을 쓴다. 포커스가 닿는 컨트롤마다 역할,
이름, 상태가 읽혀야 하고, 고른 것이 옮겨 가면 목록의 줄이, 제출이 실패하면 오류가 읽혀야 한다.
내레이터의 포커스 사각형은 포커스를 가진 컨트롤 위에 있어야 한다.
