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
