/* the builtin text backend satisfies the text backend contract. */
#include "text_conformance.h"

int main(void) {
    gates_text_conformance(gates_text_backend_builtin());
    return gt_report("test_text_conformance(builtin)");
}
