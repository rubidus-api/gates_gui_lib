# 12장 - 숫자, 묶음, 배치

헤더: `gates/inputs.h`, `gates/event.h`.

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

## 접근성

스핀 상자는 정수 단위의 RangeValue가 있는 Spinner이고, 그 글상자는 스핀 상자의 이름을 가진 편집기이다. 슬라이더는 RangeValue가 있는 Slider이다. 화면 낭독기와 자동화는 어느 쪽 값이든 정할 수 있으며, 그 값은 범위 안으로 맞춰지고 사람의 변경처럼 알려진다.
