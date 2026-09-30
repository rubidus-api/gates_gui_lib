# 8장 - 테마, 단위, 배율

헤더: `gates/theme.h`, `gates/geometry.h`, `gates/draw.h`, `gates/render.h`, `gates/text.h`.

## 논리 단위

API 의 모든 좌표와 크기는 논리 단위, 1/96 인치다. 배치 값, 노드 사각형, 바라는 크기, 창 크기,
포인터 위치가 다 그렇다. 창은 가장자리에서 한 번 바꾼다. 그릴 때는 장치 픽셀로, 포인터를 알릴 때는
다시 단위로 바꾼다. 그래서 같은 프로그램이 100 % 화면과 200 % 화면에서 같은 실제 크기다.
`gates_px` 와 `gates_logical` 이 그 변환이다(정수 단위에서는 정확한 역).

<!-- example: manual/examples/ex_09_units.c -->
```c
/* manual example (host): logical units and themes.
 * expect: 100 units = 150 px at 144 dpi; 150 px = 100 units; light theme: 0 issues */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    /* Every coordinate in the API is a logical unit, 1/96 inch. A window
     * scales once, at its edge: to pixels when drawing, back when reading
     * pointer positions. */
    gates_u32 dpi = 144; /* 150 % */
    gates_i32 px = gates_px(100, dpi);
    gates_i32 back = gates_logical(px, dpi);
    /* A theme is checked against the enforced rules: contrast and cues. */
    gates_access_issue_t issues[8];
    gates_u32 n = gates_theme_audit(gates_theme_light(), issues, 8);
    printf("100 units = %d px at %u dpi; %d px = %d units; light theme: %u issues\n", px, dpi, px, back, n);
    return n == 0 ? 0 : 1;
}
```

창은 자기 모니터(모니터별 DPI, 모니터 사이 옮기기), Windows 의 "텍스트 크기" 설정, 응용의 확대
(`gates_window_set_zoom`, 25 % 부터 400 % 까지)를 따른다. 셋 모두 확대한 쪽처럼 화면 전체를 키운다.

## 테마

색은 값이 아니라 토큰(`gates_color_token_t`)이다. 프로그램은 "글자", "포커스", "오류"라고 말하고,
그것이 무슨 색인지는 테마가 말한다. 창은 시스템을 따른다. 밝은 모드와 어두운 모드, 시스템 자신의
색으로 만든 고대비를 따르며, 사람이 바꾸면 곧바로 바뀐다. 제목 표시줄도 따라간다.
`gates_window_set_theme_mode` 는 모드를 고정하고, `gates_window_set_theme` 는 응용의 테마를 정한다.
모든 테마는 9장의 대비·표시 규칙을 지켜야 하며, `gates_theme_audit` 이 테마 하나를 검사한다.

포커스와 오류는 색으로만 보이지 않는다. 포커스 테두리와 오류 테두리에는 너비(적어도 2 단위)가 있어서
어떤 테마에서도 읽힌다.

## 글꼴

글은 두 글꼴 가운데 하나를 쓴다(`gates/text.h` 의 `gates_font_t`).

| 글꼴 | 무엇 | Windows 에서 |
|---|---|---|
| `GATES_FONT_UI`(기본) | 플랫폼의 UI 글꼴, 비례폭 | 시스템 메시지 글꼴: Segoe UI, 한국어 Windows 에서는 맑은 고딕 |
| `GATES_FONT_MONO` | 고정폭. 코드, 로그, 열을 맞춰야 하는 글 | Consolas |

`gates_node_set_font` 는 노드의 글꼴을 정하고, CSS 의 `font-family` 처럼 그 아래에서 제 글꼴을
정하지 않은 모든 것에도 적용된다. `GATES_FONT_INHERIT` 는 부모의 것으로 돌아가고,
`gates_node_font` 는 실제로 쓰이는 글꼴을 읽는다. 패널에 정하면 한 구역 전체가 바뀌고, 뷰 하나에
정하면 로그 하나가 바뀐다. 대화상자와 메뉴는 창의 루트 아래에 있지 않으므로, 따로 정하지 않으면
UI 글꼴에서 시작한다.

크기(0.10.0)는 시스템 글자 크기의 백분율이라 사람이 고른 Windows "텍스트 크기"를 그대로 따른다.
`gates_node_set_font_size(tree, node, percent)` 는 `GATES_FONT_SIZE_MIN`(50)부터 `GATES_FONT_SIZE_MAX`(400)
사이로 크기를 정한다. 흔히 쓰는 값은 `GATES_FONT_SIZE_SMALL`(85), `GATES_FONT_SIZE_LARGE`(125),
`GATES_FONT_SIZE_HEADING`(150)이고, 0 은 부모의 것으로 돌아간다. 크기는 글꼴처럼 물려받지만 글꼴과는
따로다. 제목 패널 안의 MONO 뷰는 150 % 고정폭이다. 노드가 재고 그리는 모든 것이 그 크기를 따른다. 글,
줄과 행의 높이, 캐럿, 텍스트 상자의 `cols` 가 그렇다. `gates_node_font_size` 는 실제 크기를 읽고,
`gates_node_font` 는 글꼴과 크기를 함께 돌려준다(`gates_font_face`, `gates_font_percent`).

```c
(void)gates_node_set_font(tree, log_view, GATES_FONT_MONO);            /* aligned log lines */
(void)gates_node_set_font_size(tree, title, GATES_FONT_SIZE_HEADING); /* a page heading */
(void)gates_node_set_font_size(tree, note, GATES_FONT_SIZE_SMALL);    /* small print */
```

## 그리기와 글자 백엔드

창은 트리를 그리기 목록(`gates/draw.h`)에 그리고, 소프트웨어 그리기(`gates/render.h`)가 그것을
픽셀로 바꾼다. 글은 글자 백엔드(`gates/text.h`)를 거친다. 백엔드는 글꼴마다 줄 높이, 평균 글자 폭
(크기 가늠용: 텍스트 상자의 `cols`), 글자마다의 폭을 알린다. 글줄의 폭은 글자 폭의 합과 정확히
같고, 백엔드는 글자를 그 자리에 그린다. 그래서 어느 백엔드에서든, 어느 배율에서든 배치, 그리기,
캐럿, 누른 자리가 서로 맞는다. 내장 백엔드는 두 글꼴 모두 고정폭이어서 시험이 결정적으로 돈다(크기는 8 x 16 칸을 늘인다). 직접 만든 백엔드는
`gates_font_face(font)` 로 글꼴을 읽고 `gates_font_percent(font)` 만큼 키운다.
응용은 창의 그리기 콜백에서 제 것을 더 그릴 수 있다. 거기서 위젯 상태를 읽지는 않는다.
