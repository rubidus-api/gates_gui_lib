# Chapter 3 - Forms

Header: `gates/form.h`.

## A form holds no data

A form lays out labelled fields - a label beside each editor, a help line and an error line
under it - and does nothing else. It does not keep values, does not validate, and does not
save. The application keeps a draft, reads the editors (or follows their events), decides what
is valid, puts messages on the fields, and saves after a successful submit. This split is the
point: the rules about valid data belong to the program, the presentation of fields and errors
to gates, and the two never argue about who holds the truth.

Each field has an id chosen by the program (`field_id`, nonzero, unique in the form); ids stay
stable when rows are hidden or reordered. `gates_form_editor` gives the editor of a field.

| Add | Editor | Value |
|---|---|---|
| `gates_form_add_text` | text box | `gates_textbox_text` |
| `gates_form_add_checkbox` | check box | `gates_checkbox_checked` |
| `gates_form_add_choice` | choice | the option id in VALUE_CHANGED |
| `gates_form_add_radio` | radio group | the option id in VALUE_CHANGED |

`required` adds a marker to the label and tells assistive technology the field is required;
`help` is a line under the editor; `gates_form_set_error` shows a message under the field in the
error colour and marks a text box invalid - an empty message clears it. A narrow form puts each
label above its editor instead of beside it.

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

## Draft validation

Validate on submit, not on every key: a person half-way through typing an e-mail address has
not made a mistake yet. When a submit fails, put every error on its field, keep focus where the
person can fix the first one, and say in a status line what happened (chapter 9 shows how to
make that line announce itself). Clear a field's error when the person changes it, or at the
next submit.

The reference application `app_settings` does all of this: required fields, help, errors,
discarding changes, and saving a draft only when it is valid.
