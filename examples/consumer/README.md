# Consumer example

Two small programs built from the gates package alone - its `include/` and `lib/`, never the
library's sources. They show the link lines a program needs.

| Program | Command | Needs |
|---|---|---|
| `headless.c` | `make host` | any C23 compiler; links `-lgates_core -lproven -lm` |
| `main.c` | `make win` | mingw-w64; links `-lgates -lproven` and the Windows libraries in the Makefile |

`headless` builds a node tree, lays it out with the builtin text backend and reads what a
screen reader would hear - no window, no platform. `consumer.exe` opens a window with a text
box and a button that greets the name typed. Both check that the library matches the headers
(`gates_version()` against `GATES_VERSION_NUMBER`).

From an installed copy: `make host PKG=/some/prefix` (headers in `PREFIX/include`, libraries
in `PREFIX/lib` - use `-L$(PKG)/lib` there, see the Makefile).
