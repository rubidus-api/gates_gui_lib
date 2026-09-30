# 3장 - 폼

헤더: `gates/form.h`.

## 폼은 데이터를 갖지 않는다

폼은 이름표 달린 필드를 배치한다. 편집기 옆에 이름표, 그 아래 도움말 줄과 오류 줄을 둔다. 그
밖의 일은 하지 않는다. 값을 갖지도, 검사하지도, 저장하지도 않는다. 응용이 초안(draft)을 갖고,
편집기를 읽거나 그 이벤트를 따르고, 무엇이 맞는지 정하고, 필드에 메시지를 달고, 제출이 성공한
뒤에 저장한다. 이 나눔이 요점이다. 맞는 데이터의 규칙은 프로그램의 것이고, 필드와 오류를
보여 주는 일은 gates 의 것이어서, 둘이 누가 진실을 쥐었는지 다투지 않는다.

필드마다 프로그램이 고른 id(`field_id`, 0 이 아니고 폼 안에서 유일)가 있다. 줄을 숨기거나 순서를
바꿔도 id 는 그대로다. `gates_form_editor` 가 필드의 편집기를 준다.

| 더하기 | 편집기 | 값 |
|---|---|---|
| `gates_form_add_text` | 텍스트 상자 | `gates_textbox_text` |
| `gates_form_add_checkbox` | 체크박스 | `gates_checkbox_checked` |
| `gates_form_add_choice` | 선택 상자 | VALUE_CHANGED 의 선택지 id |
| `gates_form_add_radio` | 라디오 그룹 | VALUE_CHANGED 의 선택지 id |

`required` 는 이름표에 표시를 붙이고 필드가 필수임을 보조 기술에 알린다. `help` 는 편집기 아래의
한 줄이다. `gates_form_set_error` 는 필드 아래에 오류 색으로 메시지를 보이고 텍스트 상자를 틀린
상태로 표시한다. 빈 메시지는 지운다. 좁은 폼은 이름표를 편집기 옆이 아니라 위에 두고, 그렇게 쌓인 줄만큼 높아지므로
그 아래 것은 가려지지 않고 밑으로 내려간다.

<!-- example: manual/examples/ex_04_form.c -->
```c
/* manual example (host): a form, a draft, validation on submit.
 * expect: first submit: 1 error; second submit: saved Kim */
#include <gates/gates.h>

#include <stdio.h>

enum { FIELD_NAME = 1, FIELD_NEWS = 2 };

/* The application's draft: the form holds no data of its own. */
typedef struct draft_t {
    char name[32];
    bool news;
} draft_t;

/* Reads the editors, checks, and puts messages on the fields. */
static int submit(gates_tree_t *t, gates_node_t form, draft_t *d) {
    gates_str_t name = gates_textbox_text(t, gates_form_editor(t, form, FIELD_NAME));
    int errors = 0;
    if (name.size == 0) {
        (void)gates_form_set_error(t, form, FIELD_NAME, GATES_STR("enter a name"));
        errors++;
    } else {
        (void)gates_form_set_error(t, form, FIELD_NAME, GATES_STR("")); /* clears it */
        snprintf(d->name, sizeof d->name, "%.*s", (int)name.size, (const char *)name.ptr);
    }
    d->news = gates_checkbox_checked(t, gates_form_editor(t, form, FIELD_NEWS));
    return errors;
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t form, name_box;
    gates_field_desc_t name = { .label = GATES_STR("Name"), .required = true, .max_bytes = 30,
                                .help = GATES_STR("as it should appear on the badge") };
    gates_field_desc_t news = { .label = GATES_STR("News"), .text = GATES_STR("send me news") };
    if (!gates_is_ok(gates_form_create(t, gates_tree_root(t), &form)) ||
        !gates_is_ok(gates_form_add_text(t, form, FIELD_NAME, &name, &name_box)) ||
        !gates_is_ok(gates_form_add_checkbox(t, form, FIELD_NEWS, &news, nullptr))) {
        return 1;
    }
    draft_t d = {0};
    int first = submit(t, form, &d);
    /* The error is part of the field now: screen readers read it with the name. */
    gates_access_info_t info;
    if (!gates_is_ok(gates_access_info(t, name_box, 0, &info)) || info.description.size == 0) return 1;

    if (!gates_is_ok(gates_textbox_set_text(t, name_box, GATES_STR("Kim")))) return 1;
    int second = submit(t, form, &d);
    printf("first submit: %d error%s; second submit: %s %s\n", first, first == 1 ? "" : "s",
           second == 0 ? "saved" : "refused", d.name);
    gates_tree_destroy(t);
    return 0;
}
```

## 초안 검사

검사는 키마다가 아니라 제출할 때 한다. 전자우편 주소를 반쯤 쓴 사람은 아직 틀리지 않았다. 제출이
실패하면 오류를 모두 제 필드에 달고, 첫 오류를 고칠 수 있는 곳에 포커스를 두고, 무슨 일이
있었는지 상태 줄에 말한다(9장이 그 줄이 스스로 알리게 하는 법을 보인다). 필드의 오류는 사람이 그
필드를 바꿀 때나 다음 제출 때 지운다.

참조 응용 `app_settings` 가 이 모두를 한다. 필수 필드, 도움말, 오류, 바꾼 것 버리기, 맞을 때만
초안 저장하기가 들어 있다.
