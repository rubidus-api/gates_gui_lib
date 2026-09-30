#!/bin/sh
# the package. Run from the repository root: make package-check
#   1. every public header compiles on its own; gates.h includes all of them;
#   2. the version agrees: version.h, CHANGELOG, the library at run time;
#   3. the libraries export only gates_* (libgates*) and proven_* (libproven) names;
#   4. make dist lays out the package;
#   5. a consumer builds from a copy of the package alone - the source tree is not on
#      any path - and runs (host). With a Windows cross compiler present, the Windows
#      consumer builds the same way (it is run on Windows by hand).
set -eu
CC=${CC:-gcc}
CC_WIN=${CC_WIN:-x86_64-w64-mingw32-gcc}
fail=0
pass=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL: $*"; }
check() { if eval "$1"; then ok; else bad "$2"; fi; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. headers
for h in include/gates/*.h; do
    name=$(basename "$h")
    printf '#include <gates/%s>\n#include <gates/%s>\nint main(void) { return 0; }\n' "$name" "$name" > "$tmp/h.c"
    check "$CC -std=c23 -Wall -Wextra -Werror -Iinclude -Ivendor/proven/include -fsyntax-only $tmp/h.c 2>$tmp/err" \
          "header $name does not compile alone: $(head -3 "$tmp/err" 2>/dev/null)"
    [ "$name" = gates.h ] && continue
    check "grep -q \"#include <gates/$name>\" include/gates/gates.h" "gates.h does not include $name"
    check "! grep -q 'gates_i_\\|_internal\\.h' $h" "header $name names internal things"
done

# 2. version
maj=$(sed -n 's/^#define GATES_VERSION_MAJOR \([0-9]*\).*/\1/p' include/gates/version.h)
min=$(sed -n 's/^#define GATES_VERSION_MINOR \([0-9]*\).*/\1/p' include/gates/version.h)
pat=$(sed -n 's/^#define GATES_VERSION_PATCH \([0-9]*\).*/\1/p' include/gates/version.h)
ver="$maj.$min.$pat"
check "echo \"$ver\" | grep -qE '^[0-9]+\\.[0-9]+\\.[0-9]+$'" "version.h says $ver"
check "grep -q '#define GATES_VERSION_STRING \"$ver\"' include/gates/version.h" "GATES_VERSION_STRING differs from $ver"
check "[ \"\$(grep -m1 '^## \\[' CHANGELOG.md)\" != '## [Unreleased]' ] && grep -m1 '^## \\[' CHANGELOG.md | grep -q \"\\[$ver\\]\"" \
      "the newest CHANGELOG section is not [$ver]"

# 3. libraries and their symbols
make -s lib-host >/dev/null
for lib in build/lib/host/libgates_core.a build/lib/host/libproven.a; do
    check "[ -f $lib ]" "$lib not built"
done
stray=$(nm -g --defined-only build/lib/host/libgates_core.a 2>/dev/null | awk 'NF==3{print $3}' | grep -v '^gates_' || true)
check "[ -z \"$stray\" ]" "libgates_core.a exports non-gates names: $stray"
stray=$(nm -g --defined-only build/lib/host/libproven.a 2>/dev/null | awk 'NF==3{print $3}' | grep -v '^proven_' || true)
check "[ -z \"$stray\" ]" "libproven.a exports non-proven names: $stray"
check "! nm -g --defined-only build/lib/host/libgates_core.a | grep -q ' proven_'" "libgates_core.a carries proven code"

# 4. package layout
make -s dist >/dev/null
pkg=dist/gates-$ver
for f in include/gates/gates.h include/gates/version.h include/proven/types.h include/proven/u8str.h \
         lib/host/libgates_core.a lib/host/libproven.a LICENSE LICENSE-proven README.md CHANGELOG.md \
         examples/consumer/Makefile examples/consumer/headless.c examples/consumer/main.c \
         manual/manual.md manual-ko/manual-ko.md; do
    check "[ -f $pkg/$f ]" "package lacks $f"
done
check "[ ! -e $pkg/src ] && [ ! -e $pkg/include/gates/gates_tree_internal.h ]" "package carries sources"
for h in "$pkg"/include/proven/*.h; do
    printf '#include <proven/%s>\nint main(void) { return 0; }\n' "$(basename "$h")" > "$tmp/p.c"
    check "$CC -std=c23 -I$pkg/include -fsyntax-only $tmp/p.c 2>/dev/null" "shipped proven header $(basename "$h") needs one not shipped"
done

# 5. consumer from a copy of the package only
cp -R "$pkg" "$tmp/pkg"
check "make -s -C $tmp/pkg/examples/consumer host CC=$CC >$tmp/build.log 2>&1 || { tail -5 $tmp/build.log; false; }" "headless consumer does not build"
check "(cd $tmp && ./pkg/examples/consumer/headless > $tmp/run.log) || { cat $tmp/run.log; false; }" "headless consumer failed"
check "grep -q \"gates $ver: button .Greet. .* ok\" $tmp/run.log || { cat $tmp/run.log; false; }" "headless consumer printed something else"
if command -v "$CC_WIN" >/dev/null 2>&1; then
    check "[ -f $pkg/lib/win64/libgates.a ] && [ -f $pkg/lib/win64/libproven.a ]" "package lacks the Windows libraries"
    check "make -s -C $tmp/pkg/examples/consumer win >$tmp/win.log 2>&1 || { tail -5 $tmp/win.log; false; }" "Windows consumer does not build"
    # .refptr.* are mingw's merged (COMDAT) references to external data, not names of ours.
    stray=$(x86_64-w64-mingw32-nm -g --defined-only "$pkg/lib/win64/libgates.a" 2>/dev/null | awk 'NF==3{print $3}' |
            grep -v '^gates_' | grep -v '^\.refptr\.' || true)
    check "[ -z \"$stray\" ]" "libgates.a exports non-gates names: $stray"
else
    echo "note: $CC_WIN not found - Windows package part NOT RUN"
fi

echo "test_package: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
