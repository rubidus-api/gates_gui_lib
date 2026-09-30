/* text_conformance - runs the RFC-0002 section 8 backend checklist against the
 * Win32 GDI backend on a real Windows machine (T017). Console app: run it
 * from a terminal and read the pass/fail counts. */
#include "../../tests/text_conformance.h"

#include <stdio.h>

int main(void) {
    printf("gates text backend conformance (RFC-0002 8)\n");

    printf("\n-- builtin backend --\n");
    gates_text_conformance(gates_text_backend_builtin());
    int builtin_fail = gt_fail;

    printf("\n-- win32 GDI backend --\n");
    gates_text_conformance(gates_text_backend_win32_gdi());

    printf("\nbuiltin failures: %d, gdi failures: %d\n",
           builtin_fail, gt_fail - builtin_fail);
    return gt_report("text_conformance");
}
