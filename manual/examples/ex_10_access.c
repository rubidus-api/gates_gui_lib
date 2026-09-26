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
