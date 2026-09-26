# 5장 - 내 데이터 위의 뷰

헤더: `gates/view.h`.

## 줄이 몇 개든 노드 하나

백만 줄짜리 목록도 노드 하나다. 줄은 응용의 것이다. 응용은 모델로 줄을 설명한다. 몇 줄인지, 한
자리의 id 가 무엇인지, id 의 자리가 어디인지, 칸의 글이 무엇인지를 말한다. gates 는 보이는 줄만
묻는다(한 번에 `GATES_VIEW_MAX_ROWS` 줄까지). gates 로 복사되는 것은 없고, 백만 줄을 스크롤하는
비용은 한 화면을 스크롤하는 비용과 같다.

줄에는 id(`gates_item_id_t`, 0 이 아니고 변하지 않음)가 있다. 고른 것(selection)은 자리가 아니라
id 이므로, 위에 줄이 끼어들거나 다른 곳이 지워지거나 정렬되어도 같은 줄에 머문다. 응용은 줄을
바꾼 뒤 `gates_view_model_changed` 를 부른다. 고른 줄이 없어졌으면 고른 것이 가장 가까운 남은 줄로
옮겨 가고, 출처가 PROGRAM 인 SELECTION_CHANGED 가 이를 알린다.

<!-- example: manual/examples/ex_06_view.c -->
```c
/* manual example (host): a list over the application's own rows.
 * expect: selected 3 (Cherry); after removing it: 4 (Damson), origin program */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

typedef struct row_t {
    gates_item_id_t id;          /* stable: survives inserts, removals, sorting */
    const char *name;
} row_t;

typedef struct fruits_t {
    row_t rows[8];
    gates_u64 n;
} fruits_t;

/* The model: gates asks only for the rows it shows, by position and by id. */
static gates_u64 f_count(void *u) { return ((fruits_t *)u)->n; }
static gates_item_id_t f_id_at(void *u, gates_u64 row) { return ((fruits_t *)u)->rows[row].id; }
static bool f_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    fruits_t *f = u;
    for (gates_u64 i = 0; i < f->n; i++) {
        if (f->rows[i].id == id) { *row = i; return true; }
    }
    return false;
}
static gates_err_t f_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    (void)col;
    gates_u64 row;
    if (!f_index_of(u, id, &row)) return PROVEN_ERR_INVALID_ARG;
    const char *s = ((fruits_t *)u)->rows[row].name;
    out->text = (gates_str_t){ .ptr = (const gates_u8 *)s, .size = strlen(s) };
    return GATES_OK;
}

static gates_item_id_t last_id;
static gates_event_origin_t last_origin;

static void on_view(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind == GATES_EVENT_SELECTION_CHANGED) {
        last_id = ev->item;
        last_origin = ev->origin;
    }
}

static const char *name_of(fruits_t *f, gates_item_id_t id) {
    gates_u64 row;
    return f_index_of(f, id, &row) ? f->rows[row].name : "none";
}

int main(void) {
    fruits_t f = { .rows = { { 1, "Apple" }, { 2, "Banana" }, { 3, "Cherry" }, { 4, "Damson" }, { 5, "Elder" } }, .n = 5 };
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t list;
    gates_rows_model_t model = { .user = &f, .count = f_count, .id_at = f_id_at, .index_of = f_index_of, .cell = f_cell };
    if (!gates_is_ok(gates_view_create(t, gates_tree_root(t), &(gates_view_desc_t){0}, &list)) ||
        !gates_is_ok(gates_view_set_model(t, list, &model)) ||
        !gates_is_ok(gates_widget_set_handler(t, list, on_view, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 200, 160 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* A person picks the third row. */
    if (!gates_is_ok(gates_access_select(t, list, 3))) return 1;
    (void)gates_tree_dispatch_events(t, 0);
    printf("selected %llu (%s); ", (unsigned long long)last_id, name_of(&f, last_id));

    /* The application removes that row, then tells the view its rows changed:
     * the selection moves to the nearest remaining row and says so. */
    memmove(&f.rows[2], &f.rows[3], 2 * sizeof f.rows[0]);
    f.n = 4;
    if (!gates_is_ok(gates_view_model_changed(t, list))) return 1;
    (void)gates_tree_dispatch_events(t, 0);
    printf("after removing it: %llu (%s), origin %s\n", (unsigned long long)last_id, name_of(&f, last_id),
           last_origin == GATES_ORIGIN_PROGRAM ? "program" : "user");
    gates_tree_destroy(t);
    return 0;
}
```

## 목록, 표, 트리, 로그

- 목록에는 열이 없다. 묻는 칸은 열 0 이다.
- 표에는 열(`gates_column_desc_t`: id, 이름, 너비, 최소 너비)이 있고 머리줄을 보일 수 있다. 머리
  칸을 누르면 열 id 와 함께 SORT_REQUESTED 가 온다. 응용이 줄을 정렬하고
  `gates_view_model_changed` 를 부른다. 머리 칸의 가장자리를 끌면 열 너비가 바뀐다.
- 트리는 모델이 `row_info` 까지 답하는 목록이다. 깊이, 열 수 있는지, 열려 있는지, 불러오는 중인지
  오류인지를 답한다. 열기와 닫기는 요청(EXPAND_REQUESTED)이다. 응용이 펼친 줄들을 바꾸고
  `gates_view_model_changed` 를 부른다. gates 는 보이지 않는 줄을 훑지 않는다.
- 로그(`gates_log_create`)는 줄을 스스로 갖는다. 글을 덧붙이고, 줄 수와 바이트 한도를 지키고,
  가장 오래된 줄부터 버리고, 사람이 스크롤해 떠나지 않은 동안 끝을 따라간다.

## 모델의 규칙

모델 콜백은 UI 스레드에서 돌고, 이벤트 전달 중이나 다른 콜백 안에서는 돌지 않으며,
`gates_view_set_model(view, nullptr)` 이 돌아온 뒤에는 불리지 않는다. 칸의 글은 모델을 다음에 부를
때까지 빌린 것이다. 모델은 `gates_view_model_changed` 로 알리지 않고 그 사이에 바뀌어서는 안 된다.
