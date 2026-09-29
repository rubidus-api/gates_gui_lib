# 2장 - 컨트롤과 배치

헤더: `gates/widget.h`, `gates/layout.h`, `gates/geometry.h`.

## 컨트롤 표

컨트롤은 저마다 노드 하나다. 아래 표가 0.5.0 의 유한한 컨트롤 전부다. 사람이 그것으로 하는 일,
키, 프로그램이 받는 이벤트, 화면 읽기 프로그램이 듣는 것(9장)을 적었다.

| 컨트롤 | 만들기 | 사람은 | 키 | 이벤트 | 접근성으로는 |
|---|---|---|---|---|---|
| 패널 | `gates_panel_create` | 묶음을 본다 | - | - | 없음, 이름을 주면 그룹 |
| 이름표 | `gates_label_create` | 글을 읽는다 | - | - | 텍스트 |
| 버튼 | `gates_button_create` | 동작 하나를 청한다 | Space(뗄 때), Enter | ACTIVATED | 버튼(Invoke) |
| 체크박스 | `gates_checkbox_create` | 선택 사항을 켜고 끈다 | Space | VALUE_CHANGED | 체크박스(Toggle) |
| 텍스트 상자 | `gates_textbox_create` | 한 줄을 입력한다 | 편집 키, Ctrl+C/X/V/Z/Y, IME | TEXT_CHANGED, PREEDIT_CHANGED, LIMIT_EXCEEDED | 편집(Value, Text) |
| 라디오 그룹 | `gates_radio_create` | 몇 개 중 하나를 고른다 | 화살표, Home, End, Space | VALUE_CHANGED(result = 선택지 id) | 라디오 버튼의 그룹 |
| 선택 상자 | `gates_choice_create` | 목록에서 하나를 고른다 | Space, Enter, Alt+Down 으로 열기; 화살표, Enter, Escape | VALUE_CHANGED | 콤보 상자(Selection, Expand) |
| 진행 막대 | `gates_progress_create` | 일이 얼마나 되었는지 본다 | - | - | 진행 막대(백분율) |
| 구분선 | `gates_separator_create` | 나뉨을 본다 | - | - | 구분선 |
| 뷰 | `gates_view_create`, `gates_log_create` | 목록·표·트리·로그에서 고른다 | 화살표, PageUp/PageDown, Home, End, Enter; 트리는 Left/Right | SELECTION_CHANGED, ACTIVATED, SORT_REQUESTED, EXPAND_REQUESTED | 목록, 표, 트리(5장) |
| 폼 | `gates_form_create` | 이름표 달린 필드를 채운다 | - | 편집기들의 이벤트 | 필드의 그룹(3장) |
| 대화상자, 메뉴 | `gates_dialog_open`, `gates_menu_open` | 한 번 답한다, 명령을 고른다 | Enter, Escape, 화살표 | DIALOG_CLOSED, MENU_CLOSED | 창, 메뉴(4장) |
| 메뉴 막대 | `gates_menubar_create` | 메뉴에서 명령을 고른다 | F10 또는 Alt, 화살표, 글자, Escape | MENU_CLOSED, 명령 | 메뉴 막대(11장) |
| 도구 막대 | `gates_toolbar_create` | 한 번 클릭으로 명령을 실행한다 | 화살표, Space, Enter | 명령 | 도구 막대(11장) |
| 상태 줄 | `gates_statusbar_create` | 프로그램의 상태를 읽는다 | - | - | 상태 줄(11장) |
| 스핀 상자 | `gates_spin_create` | 숫자를 입력하거나 한 칸씩 바꾼다 | Up/Down, PgUp/PgDn, Enter | VALUE_CHANGED(value) | 스피너(RangeValue)(12장) |
| 슬라이더 | `gates_slider_create` | 트랙을 따라 숫자를 끈다 | 화살표, PgUp/PgDn, Home, End | VALUE_CHANGED(value) | 슬라이더(RangeValue)(12장) |
| 그룹 상자 | `gates_group_create` | 컨트롤 묶음을 보고 접는다 | 제목에서 Space, Enter | VALUE_CHANGED(checked = 펼침) | 그룹(ExpandCollapse)(12장) |
| 이미지 | `gates_image_create` | 그림을 본다 | - | - | 이름이 있으면 이미지(13장) |
| 탭 | `gates_tabs_create` | 페이지 사이를 오간다 | 화살표, Ctrl+Tab, Ctrl+PgUp/PgDn | VALUE_CHANGED(result = 번호) | 탭(Selection)(11장) |

Tab 과 Shift+Tab 은 켜져 있고 보이는 컨트롤을 트리 차례대로 오간다.
`gates_widget_set_focusable` 로 차례에서 뺄 수 있다. 끈 컨트롤(`gates_widget_set_disabled`)은 흐리게
그려지고, 입력을 무시하고, Tab 이 건너뛴다. 숨긴 노드(`gates_node_set_hidden`)와 그 아래 전부는
자리를 차지하지 않고, 그려지지 않고, 닿을 수 없다. 라디오 그룹과 선택 상자는 선택지를 자리가 아닌
변하지 않는 id(`gates_option_t`)로 부른다. id 0 은 "고른 것 없음"이다.

만들기 함수의 옛 `on_click` / `on_toggle` 인자는 입력 전달 안에서 돌며 호환을 위해 남아 있다. 새
코드는 null 을 주고 처리기를 쓴다.

## 배치

배치(layout)는 내재적이다. 컨트롤은 저마다 원하는 크기를 알고, 담는 쪽이 자식을 놓는다. 프로그램은
담는 쪽의 종류를 고른다(`gates_layout_set`).

| 종류 | 자식을 놓는 법 |
|---|---|
| `GATES_LAYOUT_KIND_COLUMN` | 위에서 아래로, 저마다 세로줄만큼 넓게 |
| `GATES_LAYOUT_KIND_ROW` | 왼쪽에서 오른쪽으로, 저마다 가로줄만큼 높게 |
| `GATES_LAYOUT_KIND_STACK` | 겹쳐서, 활성인 하나만 보인다(쪽) |
| `GATES_LAYOUT_KIND_SPLIT` | 두 칸과 사람이 끄는 손잡이 |
| `GATES_LAYOUT_KIND_SCROLL` | 휠과 스크롤 막대가 있는 창구 안의 세로줄 |
| `GATES_LAYOUT_KIND_FORM` | 줄마다 이름표 옆에 편집기(3장) |
| `GATES_LAYOUT_KIND_ABSOLUTE` | 프로그램이 준 사각형에 |

안쪽 여백(padding)과 간격(gap)은 담는 쪽마다 정한다. `gates_layout_set_child_grow` 는 남는 공간의
몫을 자식에게 주고, `gates_layout_set_child_align` 은 늘어나지 않는 자식의 자리를 정한다. 보통
흐름에서 자식들은 겹치지 않는다. 창은 그리기 전에 논리 단위로 트리를 배치한다. 창이 없으면
`gates_layout_run` 이 한다.

<!-- example: manual/examples/ex_03_layout.c -->
```c
/* manual example (host): layout - a column with a growing row.
 * expect: the list takes the rest; buttons in a row */
#include <gates/gates.h>

#include <stdio.h>

#define TRY(x) do { if (!gates_is_ok(x)) return 1; } while (0)

int main(void) {
    gates_tree_t *t = nullptr;
    TRY(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), title, list, bar, ok, cancel;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_layout_set_padding(t, root, 8));   /* logical units: 1/96 inch */
    TRY(gates_layout_set_gap(t, root, 6));
    TRY(gates_label_create(t, root, GATES_STR("Files"), &title));
    TRY(gates_panel_create(t, root, &list));
    TRY(gates_layout_set_child_grow(t, list, 1)); /* takes the space left over */
    TRY(gates_panel_create(t, root, &bar));
    TRY(gates_layout_set(t, bar, GATES_LAYOUT_KIND_ROW));
    TRY(gates_layout_set_gap(t, bar, 6));
    TRY(gates_button_create(t, bar, GATES_STR("OK"), nullptr, nullptr, &ok));
    TRY(gates_button_create(t, bar, GATES_STR("Cancel"), nullptr, nullptr, &cancel));
    TRY(gates_layout_run(t, (gates_size_t){ 240, 320 }, gates_text_backend_builtin()));

    gates_rect_t l = gates_node_layout_rect(t, list), a = gates_node_layout_rect(t, ok),
                 b = gates_node_layout_rect(t, cancel), r = gates_node_layout_rect(t, bar);
    /* The list ends one gap above the button row, which ends at the padding. */
    bool rest = l.y + l.h + 6 == r.y && r.y + r.h == 320 - 8;
    printf("%s; buttons %s\n", rest ? "the list takes the rest" : "the list does not grow",
           a.y == b.y && b.x > a.x ? "in a row" : "not in a row");
    gates_tree_destroy(t);
    return 0;
}
```

스크롤 담기는 키보드 포커스를 따라간다. 창구 아래의 컨트롤로 Tab 하면 보이는 곳으로 스크롤된다.
활성이 아닌 겹침(stack) 쪽은 그려지지도 닿지도 않는다.
