# 12장 - 숫자, 묶음, 배치

헤더: `gates/inputs.h`, `gates/event.h`, `gates/widget.h`, `gates/layout.h`.

## 여러 컨트롤에 처리기 하나

계산기에는 버튼이 스무 개 있고, 설정 페이지에는 모두 "무언가 바뀌었다"를 뜻하는 필드가 열몇 개 있다. `gates_node_set_bubble_handler(tree, node, fn, user)`는 패널, 폼, 루트 같은 담는 노드에 처리기 하나를 두고, 그 처리기는 자기 처리기가 없는 모든 자손의 이벤트를 듣는다. `ev->source`가 어느 컨트롤인지 알려 준다. 가장 가까운 담는 노드만 이벤트를 들으며, 오버레이는 따로 된 루트이다. 대화 상자의 버튼은 창의 처리기에 닿지 않고 대화 상자 안의 처리기에만 닿는다.

## 한 차례에 한 번: 미룬 호출

어떤 일은 변경 하나하나가 아니라 변경이 몰린 뒤에 해야 한다. 여러 필드의 합계, 목록의 거르기가 그렇다. `gates_tree_defer(tree, key, fn, user)`는 다음 안전한 때, 곧 지금 쌓인 이벤트를 전한 뒤에 `fn`이 한 번 돌게 하며, 그 전에 몇 번을 부탁해도 한 번이다. key가 일을 가리키고 `gates_tree_cancel_defer`가 그 일을 버린다. 미룬 일이 도는 동안 부탁한 일은 다음 차례를 기다리므로 일 하나가 프로그램을 계속 붙잡을 수 없다.

## 스핀 상자와 슬라이더

둘 다 범위 안의 정수를 고친다(`gates_range_t`: `min`, `max`, `step`, `page`, `value`). 소수 양은 `scale`을 쓴다. scale 100이면 값 125가 `1.25`로 보이므로 돈과 치수가 부동 소수점 반올림을 만나지 않는다. 사람이 바꾸면 `ev->value`를 담은 GATES_EVENT_VALUE_CHANGED가 오고, `gates_range_set_value`는 값을 조용히 바꾸며 `gates_range_set`은 한계를 바꾼다.

`gates_spin_create`는 위아래 화살표가 달린 글상자를 만든다. Up과 Down이 한 칸, PgUp과 PgDn이 한 쪽씩 움직이고, 화살표는 클릭으로 한 칸 움직인다. Left, Right, Home, End는 글자에 남는다. 입력한 글자가 범위 안의 숫자가 아닌 동안 상자는 잘못됨으로 표시되고, Enter나 상자를 떠나는 것이 그 글자를 확정한다. 그때도 범위 안의 숫자가 아니면 값으로 되돌아간다. `gates_spin_box`는 글상자를 돌려준다(레이블의 대상이나 너비에 쓴다).

`gates_slider_create`는 손잡이가 있는 트랙을 가로나 세로로 만든다(세로는 위가 더 크다). 화살표가 한 칸, PgUp과 PgDn이 한 쪽, Home과 End가 끝으로 가고, 손잡이를 끌면 온 칸 단위로 움직이며, 트랙을 누르면 포인터 쪽으로 한 쪽 간다. `gates_slider_set_ticks`는 n칸마다 눈금을 그린다.

`gates_range_format`과 `gates_range_parse`는 스핀 상자와 같은 방식으로 값과 글자를 바꾼다(`,`는 `.`로 읽는다).

<!-- example: manual/examples/ex_12_numbers.c -->
```c
/* manual example (host): a spin box and a slider, one bubble handler, one deferred total.
 * expect: width 2.50, volume 30; the panel heard 2 reports, the total was computed 1 time: 2.50 x 30 = 75.00 */
#include <gates/gates.h>

#include <stdio.h>

typedef struct app_t {
    gates_node_t width, volume;
    int heard, totals;
    char total[128];
} app_t;

/* Runs once after all the changes of a turn, however many there were. */
static void recompute(gates_tree_t *tree, gates_u32 key, void *user) {
    (void)key;
    app_t *a = user;
    a->totals++;
    char w[32];
    gates_i64 wv = gates_range_value(tree, a->width), vv = gates_range_value(tree, a->volume);
    (void)gates_range_format(wv, 100, w, sizeof w);
    char t[32];
    (void)gates_range_format(wv * vv, 100, t, sizeof t);
    snprintf(a->total, sizeof a->total, "%s x %lld = %s", w, (long long)vv, t);
}

/* One handler on the panel hears every control in it (ev->source says which). */
static void on_change(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    if (ev->kind != GATES_EVENT_VALUE_CHANGED) return;
    a->heard++;
    (void)gates_tree_defer(tree, 1, recompute, a);
}

static bool key(gates_tree_t *t, gates_key_t k) {
    gates_key_event_t e = { .key = k, .down = true };
    return gates_input_key(t, &e);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t);
    app_t a = {0};
    /* Width in hundredths: 1.50 to 5.00 in steps of 0.25. */
    gates_range_t width = { .min = 150, .max = 500, .step = 25, .value = 200, .scale = 100 };
    gates_range_t volume = { .min = 0, .max = 100, .step = 5, .page = 20, .value = 10 };
    if (!gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_spin_create(t, root, &width, &a.width)) ||
        !gates_is_ok(gates_slider_create(t, root, &volume, false, &a.volume)) ||
        !gates_is_ok(gates_node_set_bubble_handler(t, root, on_change, &a)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 200 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* The person: two steps up in the spin box, a page up on the slider. */
    gates_tree_set_focus(t, gates_spin_box(t, a.width));
    (void)key(t, GATES_KEY_UP);
    (void)key(t, GATES_KEY_UP);
    gates_tree_set_focus(t, a.volume);
    (void)key(t, GATES_KEY_PAGE_UP);
    (void)gates_tree_dispatch_events(t, 0);

    char w[32];
    (void)gates_range_format(gates_range_value(t, a.width), 100, w, sizeof w);
    /* The spin box's two steps coalesce into one report of the latest value. */
    printf("width %s, volume %lld; the panel heard %d reports, the total was computed %d time: %s\n", w,
           (long long)gates_range_value(t, a.volume), a.heard, a.totals, a.total);
    gates_tree_destroy(t);
    return 0;
}
```

## 그룹 상자

`gates_group_create(tree, parent, title, collapsible, &group, &content)`는 세로 패널 둘레에 제목이 있는 틀을 그린다. 관련된 컨트롤을 `content`에 넣는다. 제목은 니모닉 표시를 받는다. 보통 그룹에서 Alt+x는 대상이 있는 레이블처럼 안의 첫 컨트롤로 간다. 접을 수 있는 그룹의 제목은 열림/닫힘 표시가 있는 Tab 정지 위치이다. Space, Enter, 클릭, 니모닉이 내용을 보이거나 숨기며, 숨은 내용의 컨트롤은 자리를 차지하지 않고 닿을 수도 없다. 사람이 바꾸면 그룹에 VALUE_CHANGED가 오고 `ev->checked`는 펼쳐졌는지이다. `gates_group_set_expanded`는 조용히 바꾼다.

## 격자와 줄바꿈 배치

`GATES_LAYOUT_KIND_GRID`는 자식을 `gates_layout_set_grid` 개의 열로 된 행에 놓는다(기본 두 열). 열은 가장 넓은 자식만큼 넓고, 행은 가장 높은 자식만큼 높으며, 자식은 행 안에서 세로 가운데에 놓이고 정렬이 자기 너비를 청하지 않으면 칸의 너비를 채운다. `gates_layout_set_grid_column_grow`는 남는 너비를 열에 나눠 준다. 흔히 레이블은 고정 열에, 필드는 늘어나는 열에 둔다. `gates_layout_set_child_span`은 자식이 여러 열을 덮게 한다. `GATES_LAYOUT_KIND_WRAP`는 자식을 제 크기로 왼쪽에서 오른쪽으로 놓고, 다음 자식이 들어가지 않으면 종이 위의 낱말처럼 새 줄을 시작한다. 그 높이는 받은 너비를 따른다. 두 배치 모두 간격은 두 방향에 쓰인다.

<!-- example: manual/examples/ex_12_layouts.c -->
```c
/* manual example (host): a grid form inside a collapsible group, and wrapped chips.
 * expect: labels in column 1, fields in column 2 at x 40; 7 chips on 3 lines; folded: 0 fields reachable */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), group, content, grid, chips, field[2], chip[7];
    const gates_text_backend_t *be = gates_text_backend_builtin(); /* 8 units a character */
    bool ok = gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) &&
              gates_is_ok(gates_group_create(t, root, GATES_STR("&Connection"), true, &group, &content)) &&
              gates_is_ok(gates_panel_create(t, content, &grid)) &&
              gates_is_ok(gates_layout_set(t, grid, GATES_LAYOUT_KIND_GRID)) &&   /* two columns */
              gates_is_ok(gates_layout_set_gap(t, grid, 8)) &&
              gates_is_ok(gates_layout_set_grid_column_grow(t, grid, 1, 1));    /* fields take the rest */
    static const char *names[2] = { "Host", "Port" };
    for (int i = 0; ok && i < 2; i++) {
        gates_node_t l;
        ok = gates_is_ok(gates_label_create(t, grid, (gates_str_t){ .ptr = (const gates_u8 *)names[i], .size = 4 }, &l)) &&
             gates_is_ok(gates_textbox_create(t, grid, GATES_STR(""), 12, &field[i])) &&
             gates_is_ok(gates_node_set_labelled_by(t, field[i], l));
    }
    ok = ok && gates_is_ok(gates_panel_create(t, root, &chips)) &&
         gates_is_ok(gates_layout_set(t, chips, GATES_LAYOUT_KIND_WRAP)) &&
         gates_is_ok(gates_layout_set_gap(t, chips, 4));
    for (int i = 0; ok && i < 7; i++) {
        ok = gates_is_ok(gates_button_create(t, chips, GATES_STR("tag"), nullptr, nullptr, &chip[i]));
    }
    if (!ok || !gates_is_ok(gates_layout_run(t, (gates_size_t){ 160, 400 }, be))) return 1;

    gates_rect_t g = gates_node_layout_rect(t, grid), f = gates_node_layout_rect(t, field[0]);
    int lines = 1;
    for (int i = 1; i < 7; i++) {
        lines += gates_node_layout_rect(t, chip[i]).y > gates_node_layout_rect(t, chip[i - 1]).y;
    }
    /* Folding the group hides its content: nothing inside takes room or focus. */
    if (!gates_is_ok(gates_group_set_expanded(t, group, false)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 160, 400 }, be))) {
        return 1;
    }
    int shown = gates_widget_focusable(t, field[0]) + gates_widget_focusable(t, field[1]);
    printf("labels in column 1, fields in column 2 at x %d; 7 chips on %d lines; folded: %d fields reachable\n",
           f.x - g.x, lines, shown);
    gates_tree_destroy(t);
    return 0;
}
```

## 접근성

스핀 상자는 정수 단위의 RangeValue가 있는 Spinner이고, 그 글상자는 스핀 상자의 이름을 가진 편집기이다. 슬라이더는 RangeValue가 있는 Slider이다. 화면 낭독기와 자동화는 어느 쪽 값이든 정할 수 있으며, 그 값은 범위 안으로 맞춰지고 사람의 변경처럼 알려진다. 그룹 상자는 제목을 이름으로 가진 Group이다. 접을 수 있는 그룹에는 ExpandCollapse가 있고, 그 제목의 초점은 그룹의 초점이다.
