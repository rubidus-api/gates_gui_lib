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
  `gates_view_set_column_hidden` 과 `gates_view_move_column` 으로 열을 고르고 순서를 정한다. 설명에
  `column_menu` 를 주면 머리줄을 오른쪽 단추로 누르거나(표에서는 Shift+F10) 열마다 체크 항목이 있는
  메뉴가 열리고, 마지막으로 보이는 열은 숨길 수 없다. gates/state.h 는 너비, 순서, 숨긴 열을 저장한다.
- 트리는 모델이 `row_info` 까지 답하는 목록이다. 깊이, 열 수 있는지, 열려 있는지, 불러오는 중인지
  오류인지를 답한다. 열기와 닫기는 요청(EXPAND_REQUESTED)이다. 응용이 펼친 줄들을 바꾸고
  `gates_view_model_changed` 를 부른다. gates 는 보이지 않는 줄을 훑지 않는다.
- 로그(`gates_log_create`)는 줄을 스스로 갖는다. 글을 덧붙이고, 줄 수와 바이트 한도를 지키고,
  가장 오래된 줄부터 버리고, 사람이 스크롤해 떠나지 않은 동안 끝을 따라간다.

## 사람이 바꿀 수 있는 칸

열은 글 말고도 보여 줄 수 있다(`gates_column_desc_t.kind`). `GATES_CELL_CHECK` 는 `cell.checked` 로
체크 상자를, `GATES_CELL_PROGRESS` 는 `cell.permille`(0~1000)로 막대를, `GATES_CELL_ICON_TEXT` 는 글
앞에 16 x 16 아이콘(`cell.icon`, 13장)을 그린다. `paint` 함수를 준 열은 칸마다 그 칸 안에서 스스로
그린다.

`editable` 로 표시한 열은 사람이 선택한 줄의 칸을 바꿀 수 있게 하고, 모델은 `set_cell` 함수로 받아들일지
정한다. F2 나 두 번 누르기는 칸 위에 모델의 글을 선택한 채로 글 상자를 연다. Enter 는 확정하고, Escape 는
취소하고, 포커스가 떠나면 확정한다. 체크 열은 Space 나 상자를 누르면 바뀐다. `set_cell` 이 오류를 돌려주면
편집기는 열린 채 잘못됨으로 표시된다(포커스가 이미 떠났다면 그 편집은 버린다). 바뀐 뒤 뷰는 모델을 다시
읽고 줄(`ev->item`)과 열(`ev->result`)을 담아 CELL_EDITED 를 보낸다. 프로그램은
`gates_view_edit` 와 `gates_view_end_edit` 로 같은 일을 할 수 있다.

<!-- example: manual/examples/ex_05_cells.c -->
```c
/* manual example (host): a shopping list edited in place - a text column and a check column.
 * expect: item 2 is now Bread (column 1), done: yes (column 2); refused empty name: editor still open */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { COL_NAME = 1, COL_DONE = 2 };

typedef struct item_t {
    gates_item_id_t id;
    char name[32];
    bool done;
} item_t;

typedef struct list_t {
    item_t items[4];
    gates_u64 n;
} list_t;

static gates_u64 l_count(void *u) { return ((list_t *)u)->n; }
static gates_item_id_t l_id_at(void *u, gates_u64 row) { return ((list_t *)u)->items[row].id; }
static bool l_index_of(void *u, gates_item_id_t id, gates_u64 *row) {
    list_t *l = u;
    for (gates_u64 i = 0; i < l->n; i++) {
        if (l->items[i].id == id) { *row = i; return true; }
    }
    return false;
}
static gates_err_t l_cell(void *u, gates_item_id_t id, gates_column_id_t col, gates_cell_t *out) {
    gates_u64 row;
    if (!l_index_of(u, id, &row)) return PROVEN_ERR_INVALID_ARG;
    item_t *it = &((list_t *)u)->items[row];
    if (col == COL_DONE) {
        out->checked = it->done;
    } else {
        out->text = (gates_str_t){ .ptr = (const gates_u8 *)it->name, .size = strlen(it->name) };
    }
    return GATES_OK;
}
/* A person changed a cell: the model decides. An empty name is refused. */
static gates_err_t l_set_cell(void *u, gates_item_id_t id, gates_column_id_t col, const gates_cell_t *value) {
    gates_u64 row;
    if (!l_index_of(u, id, &row)) return PROVEN_ERR_INVALID_ARG;
    item_t *it = &((list_t *)u)->items[row];
    if (col == COL_DONE) {
        it->done = value->checked;
        return GATES_OK;
    }
    if (value->text.size == 0 || value->text.size >= sizeof it->name) return PROVEN_ERR_INVALID_ARG;
    memcpy(it->name, value->text.ptr, value->text.size);
    it->name[value->text.size] = 0;
    return GATES_OK;
}

static list_t list = { .items = { { 1, "Milk", false }, { 2, "Bred", false }, { 3, "Eggs", true } }, .n = 3 };

static void on_view(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind != GATES_EVENT_CELL_EDITED) return;
    gates_u64 row;
    if (!l_index_of(&list, ev->item, &row)) return;
    if (ev->result == COL_NAME) {
        printf("item %llu is now %s (column %u), ", (unsigned long long)ev->item, list.items[row].name, ev->result);
    } else {
        printf("done: %s (column %u); ", list.items[row].done ? "yes" : "no", ev->result);
    }
}

static void press(gates_tree_t *t, gates_key_t key) {
    gates_key_event_t ev = { .key = key, .down = true };
    (void)gates_input_key(t, &ev);
    ev.down = false;
    (void)gates_input_key(t, &ev);
}

static void type(gates_tree_t *t, const char *s) {
    for (; *s != 0; s++) (void)gates_input_char(t, (gates_u32)(unsigned char)*s);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    const gates_column_desc_t cols[] = {
        { .id = COL_NAME, .label = GATES_STR_INIT("Item"), .width = 120, .editable = true },
        { .id = COL_DONE, .label = GATES_STR_INIT("Done"), .width = 60, .kind = GATES_CELL_CHECK, .editable = true },
    };
    gates_rows_model_t model = { .user = &list, .count = l_count, .id_at = l_id_at, .index_of = l_index_of,
                                 .cell = l_cell, .set_cell = l_set_cell };
    gates_node_t table;
    if (!gates_is_ok(gates_view_create(t, gates_tree_root(t),
                                       &(gates_view_desc_t){ .columns = cols, .column_count = 2, .header = true },
                                       &table)) ||
        !gates_is_ok(gates_view_set_model(t, table, &model)) ||
        !gates_is_ok(gates_widget_set_handler(t, table, on_view, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 240, 160 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* A person selects the second row, presses F2, retypes the name and presses Enter. */
    gates_tree_set_focus(t, table);
    press(t, GATES_KEY_DOWN);
    press(t, GATES_KEY_DOWN);
    press(t, GATES_KEY_F2);
    type(t, "Bread");
    press(t, GATES_KEY_ENTER);
    /* Space toggles the row's check column. */
    press(t, GATES_KEY_SPACE);
    (void)gates_tree_dispatch_events(t, 0);

    /* An empty name is refused: the editor stays open, marked invalid, until Escape. */
    press(t, GATES_KEY_F2);
    press(t, GATES_KEY_DELETE);
    press(t, GATES_KEY_ENTER);
    printf("refused empty name: editor %s\n", gates_view_editing(t, table, nullptr, nullptr) ? "still open" : "closed");
    press(t, GATES_KEY_ESCAPE);
    gates_tree_destroy(t);
    return 0;
}
```

## 모델의 규칙

모델 콜백은 UI 스레드에서 돌고, 이벤트 전달 중이나 다른 콜백 안에서는 돌지 않으며,
`gates_view_set_model(view, nullptr)` 이 돌아온 뒤에는 불리지 않는다. 칸의 글은 모델을 다음에 부를
때까지 빌린 것이다. 모델은 `gates_view_model_changed` 로 알리지 않고 그 사이에 바뀌어서는 안 된다.
`set_cell` 만은 모델을 바꿔도 되는 콜백이다(뷰가 그 뒤에 다시 읽는다).
