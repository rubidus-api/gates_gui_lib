#!/bin/sh
# the manual. Run from the repository root.
#   sh tests/manual_check.sh sync    copy every example file into the blocks that print it
#   sh tests/manual_check.sh check   (make manual-check) fail when the manual and its code disagree
# A chapter prints an example like this (the block is the file, verbatim):
#   <!-- example: manual/examples/ex_01_tree.c -->
#   ```c
#   ...
#   ```
# Checks: blocks equal their files; every example is printed; every English chapter has a
# Korean one printing the same examples in the same order; the English edition is ASCII;
# every public header is named in it; every example builds against the package (dist/)
# alone - host examples also run and print what their "expect:" line says; Windows
# examples build where the cross compiler is installed.
set -eu
mode=${1:-check}
CC=${CC:-gcc}
CC_WIN=${CC_WIN:-x86_64-w64-mingw32-gcc}
EN=manual
KO=manual-ko
fail=0
pass=0
ok() { pass=$((pass + 1)); }
bad() { fail=$((fail + 1)); echo "FAIL: $*"; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Rewrites every marked block of $1 from its file (to stdout); lists missing files on stderr.
render() {
    awk '
    function emit(path,   line) {
        while ((getline line < path) > 0) print line
        close(path)
    }
    skipping == 1 {
        if ($0 ~ /^```[ \t]*$/) { skipping = 0; print }
        next
    }
    {
        print
        if (match($0, /<!-- example: [^ ]+ -->/)) {
            path = substr($0, RSTART + 14, RLENGTH - 18)
            if ((getline fence) <= 0) exit
            print fence
            if ((getline test < path) < 0) { print "missing " path > "/dev/stderr"; next }
            close(path)
            emit(path)
            skipping = 1
        }
    }' "$1"
}

markers() {
    sed -n 's/.*<!-- example: \([^ ]*\) -->.*/\1/p' "$1"
}

chapters_en=$(ls $EN/*.md)
for f in $chapters_en $(ls $KO/*.md 2>/dev/null); do
    render "$f" > "$tmp/out" 2> "$tmp/missing"
    if [ -s "$tmp/missing" ]; then bad "$f: $(cat "$tmp/missing")"; continue; fi
    if cmp -s "$f" "$tmp/out"; then ok
    elif [ "$mode" = sync ]; then cp "$tmp/out" "$f"; echo "synced $f"; ok
    else bad "$f prints an example that differs from its file (run: sh tests/manual_check.sh sync)"; fi
done

# Every example printed; English and Korean in step.
for ex in $EN/examples/*.c; do
    if grep -q "<!-- example: $ex -->" $chapters_en; then ok; else bad "$ex is printed by no chapter"; fi
done
for f in $chapters_en; do
    base=$(basename "$f" .md)
    [ "$base" = manual ] && ko=$KO/manual-ko.md || ko=$KO/$base-ko.md
    if [ ! -f "$ko" ]; then bad "no Korean chapter $ko for $f"; continue; fi
    markers "$f" > "$tmp/en"
    markers "$ko" > "$tmp/ko"
    if cmp -s "$tmp/en" "$tmp/ko"; then ok; else bad "$ko does not print the same examples as $f"; fi
done
for ko in $KO/*.md; do
    base=$(basename "$ko" -ko.md)
    [ "$base" = manual ] && en=$EN/manual.md || en=$EN/$base.md
    [ -f "$en" ] && ok || bad "$ko has no English chapter"
done

# English edition: ASCII only; names every public header.
if LC_ALL=C grep -n '[^[:print:][:space:]]' $chapters_en > "$tmp/nonascii"; then
    bad "non-ASCII in the English edition: $(head -3 "$tmp/nonascii")"
else ok; fi
for h in include/gates/*.h; do
    name=gates/$(basename "$h")
    if grep -q "$name" $chapters_en; then ok; else bad "the manual never names $name"; fi
done

[ "$mode" = sync ] && { echo "manual sync: $pass ok, $fail failed"; [ "$fail" -eq 0 ]; exit; }

# Examples build against the package alone.
ver=$(sed -n 's/^#define GATES_VERSION_STRING "\(.*\)"/\1/p' include/gates/version.h)
pkg=dist/gates-$ver
[ -d "$pkg" ] || { bad "no package $pkg (make dist)"; echo "manual check: $pass passed, $fail failed"; exit 1; }
have_win=$(command -v "$CC_WIN" >/dev/null 2>&1 && echo yes || true)
for ex in $EN/examples/*.c; do
    name=$(basename "$ex" .c)
    kind=$(sed -n '1s/.*manual example (\([a-z]*\)).*/\1/p' "$ex")
    case "$kind" in
    host)
        if $CC -std=c23 -Wall -Wextra -Werror -I"$pkg/include" "$ex" -L"$pkg/lib/host" -lgates_core -lproven -lm \
               -o "$tmp/$name" 2> "$tmp/err"; then ok; else bad "$ex does not build: $(head -3 "$tmp/err")"; continue; fi
        if "$tmp/$name" > "$tmp/$name.out" 2>&1; then ok; else bad "$ex exits with an error: $(tail -2 "$tmp/$name.out")"; fi
        expect=$(sed -n 's/.*expect: \(.*\) \*\/.*/\1/p' "$ex" | head -1)
        if [ -z "$expect" ] || grep -qF "$expect" "$tmp/$name.out"; then ok
        else bad "$ex printed '$(head -1 "$tmp/$name.out")', expected '$expect'"; fi
        ;;
    windows)
        # Without windows.h the host compiler checks it too (gates' headers are platform-free).
        if ! grep -q '<windows.h>' "$ex"; then
            if $CC -std=c23 -Wall -Wextra -Werror -I"$pkg/include" -fsyntax-only "$ex" 2> "$tmp/err"; then ok
            else bad "$ex does not compile on the host: $(head -3 "$tmp/err")"; fi
        fi
        if [ -z "$have_win" ]; then echo "note: $ex NOT BUILT ($CC_WIN not found)"; continue; fi
        if $CC_WIN -std=c23 -Wall -Wextra -Werror -I"$pkg/include" "$ex" -L"$pkg/lib/win64" -lgates -lproven \
               -lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 -luiautomationcore -lole32 -loleaut32 -luuid -mwindows \
               -o "$tmp/$name.exe" 2> "$tmp/err"; then ok; else bad "$ex does not build: $(head -3 "$tmp/err")"; fi
        ;;
    *) bad "$ex: first line must say 'manual example (host)' or 'manual example (windows)'" ;;
    esac
done

echo "manual check: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
