/* headless - gates without a window (plan-0015 consumer example).
 * Builds a small tree, lays it out with the builtin text backend, and reads the
 * accessible name of the button, as a screen reader would. */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

#define TRY(x) do { gates_err_t e_ = (x); if (!gates_is_ok(e_)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)

int main(void) {
    if (gates_version() != GATES_VERSION_NUMBER) {
        fprintf(stderr, "library %s does not match headers %s\n", gates_version_string(), GATES_VERSION_STRING);
        return 1;
    }
    gates_tree_t *t = nullptr;
    TRY(gates_tree_create(&(gates_tree_desc_t){0}, &t));
    gates_node_t root = gates_tree_root(t), label, button;
    TRY(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN));
    TRY(gates_label_create(t, root, GATES_STR("Hello from gates"), &label));
    TRY(gates_button_create(t, root, GATES_STR("Greet"), nullptr, nullptr, &button));
    TRY(gates_layout_run(t, (gates_size_t){ 320, 200 }, gates_text_backend_builtin()));

    gates_access_info_t info;
    TRY(gates_access_info(t, button, 0, &info));
    bool ok = info.role == GATES_ROLE_BUTTON && info.name.size == 5 && memcmp(info.name.ptr, "Greet", 5) == 0 &&
              info.bounds.w > 0;
    printf("gates %s: button \"%.*s\" at %d,%d %dx%d - %s\n", gates_version_string(), (int)info.name.size,
           (const char *)info.name.ptr, info.bounds.x, info.bounds.y, info.bounds.w, info.bounds.h,
           ok ? "ok" : "unexpected");
    gates_tree_destroy(t);
    return ok ? 0 : 1;
}
