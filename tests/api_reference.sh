#!/bin/sh
# the API reference (manual chapter 14), made from the public headers. Run from the
# repository root.
#   sh tests/api_reference.sh write   regenerate manual/manual-14-api-reference.md
#   sh tests/api_reference.sh check   (make manual-check) fail when a public function has no
#                                     comment, or the chapter is not what the headers make
# A function is documented when a comment starting in column 0 comes before it in the same
# paragraph (no blank line between): one comment may cover a run of declarations. Such a
# comment and the declarations under it go into the chapter together; a "/* -- title -- */"
# comment starts a section; the first comment of a header describes the header.
set -eu
mode=${1:-check}
out=manual/manual-14-api-reference.md
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

{
    printf '# Chapter 14 - API reference\n\n'
    printf '<!-- made by tests/api_reference.sh from include/gates/*.h: edit the headers, then run it -->\n\n'
    printf 'Every public function of gates, header by header, with the comment its header gives it.\n'
    printf 'The other chapters explain how the pieces work together; this one is for looking a\n'
    printf 'function up. Types, constants and struct fields are described in the headers themselves.\n'
    printf 'Declarations are shown on one line each; `[[nodiscard]]` marks a result that must be looked\n'
    printf 'at.\n'
    for h in $(LC_ALL=C ls include/gates/*.h); do
        awk -v name="gates/$(basename "$h")" -v undoc="$tmp/undocumented" '
        function flush_code() { if (code_open) { print "```"; code_open = 0 } }
        function strip(line) {
            sub(/^\/\*+ ?/, "", line); sub(/^ \*\/.*$/, "", line); sub(/^ \* ?/, "", line)
            sub(/^ \*$/, "", line); sub(/ *\*\/$/, "", line)
            return line
        }
        function blank_out() { if (!started) return; if (!last_blank) print ""; last_blank = 1 }
        function put(line) { print line; last_blank = (line == "") }
        # Prose: "*" and "<" as text, not emphasis or HTML (character by character, so every awk agrees).
        function esc(line,   i, c, r) {
            r = ""
            for (i = 1; i <= length(line); i++) {
                c = substr(line, i, 1)
                if (c == "*") r = r "\\*"
                else if (c == "<" && substr(line, i + 1, 1) ~ /[A-Za-z\/!]/) r = r "&lt;"
                else r = r c
            }
            return r
        }
        function say(line) { put(esc(line)) }
        BEGIN { depth = 0; ncomment = 0; para = 0; first = 1; started = 0; code_open = 0; section = ""; last_blank = 1 }
        # inside a brace body (struct, enum, inline function): skip to its end
        depth > 0 {
            n = gsub(/\{/, "{"); m = gsub(/\}/, "}"); depth += n - m
            next
        }
        # a comment in column 0
        /^\/\*/ {
            text = ""
            nlines = 0
            line = $0
            while (1) {
                done = (line ~ /\*\//)
                s = strip(line)
                lines[++nlines] = s
                if (done) break
                if ((getline line) <= 0) break
            }
            if (first) {
                first = 0
                desc = lines[1]
                sub(/^gates_gui_lib - /, "", desc)
                desc = toupper(substr(desc, 1, 1)) substr(desc, 2)
                print ""
                print "## " name
                print ""
                say(desc)
                for (i = 2; i <= nlines; i++) if (lines[i] != "" || i < nlines) say(lines[i])
                started = 1
                para = 1
                next
            }
            if (lines[1] ~ /^-- /) {
                title = lines[1]
                sub(/^-- */, "", title); sub(/ *-+ *$/, "", title)
                section = title
                ncomment = 0
                for (i = 2; i <= nlines; i++) if (lines[i] != "") { ncomment = 0; break }
                sect_n = 0
                for (i = 2; i <= nlines; i++) sect[++sect_n] = lines[i]
                para = 1
                next
            }
            for (i = 1; i <= nlines; i++) comment[++ncomment] = lines[i]
            para = 1
            next
        }
        /^[ \t]*$/ { flush_code(); para = 0; ncomment = 0; next }
        # a function declaration at file level
        /^(\[\[nodiscard\]\] )?(static inline )?[A-Za-z_][A-Za-z0-9_ \*]*gates_[a-z0-9_]+ *\(/ && !/^typedef/ {
            sig = $0
            while (sig !~ /[;{]/) { if ((getline more) <= 0) break; sub(/^[ \t]+/, " ", more); sig = sig more }
            if (sig ~ /\{/) {
                body = substr(sig, index(sig, "{"))
                depth = gsub(/\{/, "{", body) - gsub(/\}/, "}", body)
                sig = substr(sig, 1, index(sig, "{") - 1)
            }
            sub(/;.*$/, "", sig); sub(/[ \t]+$/, "", sig); gsub(/[ \t]+/, " ", sig)
            fname = sig; sub(/ *\(.*/, "", fname); sub(/.*[ \*]/, "", fname)
            if (!para) print FILENAME ":" FNR ": " fname > undoc
            if (section != "") {
                flush_code()
                blank_out()
                put("### " section)
                sep = 1
                for (i = 1; i <= sect_n; i++) if (sect[i] != "" || i < sect_n) { if (sep) { blank_out(); sep = 0 } say(sect[i]) }
                section = ""
                sect_n = 0
            }
            if (ncomment > 0) {
                flush_code()
                blank_out()
                for (i = 1; i <= ncomment; i++) say(comment[i])
                ncomment = 0
            }
            if (!code_open) { blank_out(); put("```c"); code_open = 1 }
            put(sig ";")
            next
        }
        # anything else (#define, typedef, a struct opening its body)
        {
            n = gsub(/\{/, "{"); m = gsub(/\}/, "}"); depth += n - m
            if (depth < 0) depth = 0
        }
        END { flush_code() }
        ' "$h"
    done
} > "$tmp/chapter"

# One blank line between blocks, none at the end.
awk 'NF == 0 { blank++; next } { if (blank && printed) print ""; blank = 0; print; printed = 1 }' "$tmp/chapter" > "$tmp/clean"

status=0
if [ -s "$tmp/undocumented" ]; then
    echo "FAIL: public functions without a comment:"
    cat "$tmp/undocumented"
    status=1
fi
count=$(grep -c '^\(\[\[nodiscard\]\] \)\?\(static inline \)\?[A-Za-z_].*gates_[a-z0-9_]* *(.*;$' "$tmp/clean" || true)
case $mode in
write)
    cp "$tmp/clean" "$out"
    echo "api reference: $count functions written to $out"
    ;;
check)
    if cmp -s "$tmp/clean" "$out"; then
        echo "api reference: $count functions, chapter up to date"
    else
        echo "FAIL: $out is not what the headers make (sh tests/api_reference.sh write)"
        diff "$out" "$tmp/clean" | head -20 || true
        status=1
    fi
    ;;
*)
    echo "usage: sh tests/api_reference.sh write|check" >&2
    exit 2
    ;;
esac
exit $status
