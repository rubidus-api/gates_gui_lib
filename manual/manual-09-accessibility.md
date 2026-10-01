# Chapter 9 - Accessibility

Header: `gates/access.h`.

## Every window is readable and usable without sight or pointer

gates describes every node to assistive technology: a role (button, check box, edit, list, ...),
a name, a description, states (focused, disabled, checked, selected, expanded, read-only,
invalid, required, offscreen), a value, and the actions it accepts. Radio options, choice
options, menu entries and view rows are items of their control, addressed by id. On Windows the
window is a UI Automation provider, so Narrator and automation tools see all of it, and every
action a person can take with the keyboard can be taken through UI Automation: invoke, toggle,
select, expand, set a value, scroll, and read and select text.

Actions from assistive technology take the same paths as input: the same events, commands,
limits and refusals. A screen reader cannot do what the keyboard could not.

## Names

A control's accessible name comes, in this order, from: an explicit name
(`gates_node_set_access_name`); a label tied to it (`gates_node_set_labelled_by`, like HTML's
`<label for>`); its form label; its own text (a button's label, a check box's caption). A label
placed next to a text box is not tied to it until the program says so - the audit finds such a
field. A menu without a name of its own takes its menu bar title, the entry that opened it (a
submenu), or the name of the control that had focus when the program opened it (a context
menu) (0.10.0):

<!-- example: manual/examples/ex_10_access.c -->
```c
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
```

A form field's help and error become its description, so a screen reader reads "Name, edit,
required, enter a name". Automation ids (`gates_node_set_automation_id`) are stable names for
test scripts; forms and commands give `field-<id>` and `cmd-<id>` by default.

## The enforced rules

`gates_access_audit` checks a laid-out tree against rules that the examples and reference
applications are held to:

| Rule | Checks |
|---|---|
| NO_NAME | every interactive control has an accessible name |
| TARGET_SIZE | pointer targets are at least 24 x 24 units (WCAG 2.2, 2.5.8) |
| KEYBOARD | every interactive control can be reached with the keyboard |
| DUPLICATE_ID | automation ids are unique in a tree |
| CONTRAST | theme text and cues meet their contrast ratios |
| FOCUS_CUE | focus and error cues are at least 2 units wide |

Run the audit in tests. On Windows, `GATES_ACCESS_STRICT=1` in the environment audits every
window after each layout and shows the number of issues in its title.

## Speaking up

A status line that changes as the person works should be a live region
(`gates_node_set_live`): polite ones are read after the current speech, assertive ones at once.
For a message that belongs to no control, `gates_window_announce` speaks it. A hidden system
caret follows the text caret, so magnifiers keep the insertion point in view.

## Checking with Narrator

Start Narrator (Ctrl+Windows+Enter), then use the program with the keyboard alone: every control
should be announced with its role, name and state as focus reaches it, list rows as the
selection moves, and errors when a submit fails. Narrator's focus rectangle should sit on the
control that has focus.
