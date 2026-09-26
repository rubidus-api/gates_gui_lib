/* T016: the builtin text backend satisfies the RFC-0002 §8 contract
 * (docs/tests/cases/T016-text-conformance.md). */
#include "text_conformance.h"

int main(void) {
    gates_text_conformance(gates_text_backend_builtin());
    return gt_report("test_text_conformance(builtin)");
}
