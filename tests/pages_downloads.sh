#!/bin/sh
# The download page of the Pages site (docs/download/index.html) for version $1: every file in
# docs/download/ with its size, and the examples zip, which is a release asset only.
v=${1:?usage: pages_downloads.sh VERSION}
repo=https://github.com/rubidus-api/gates_gui_lib
cat <<H
<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>gates $v - downloads / 내려받기</title>
<style>body{font:16px/1.6 system-ui,sans-serif;max-width:46rem;margin:2rem auto;padding:0 1rem}td{padding:.2rem 1rem .2rem 0}</style>
</head><body>
<h1>gates $v</h1>
<p><a href="../">Manual / 매뉴얼</a> (<a href="../en/">English</a>, <a href="../ko/">한국어</a>) &middot; <a href="$repo">Git</a> &middot; <a href="$repo/releases/tag/v$v">release</a></p>
<table>
H
for f in gates-manual-$v-en.pdf gates-manual-$v-ko.pdf gates-$v-sdk.zip gates-$v-src.zip gates-$v-manual-web.zip; do
  [ -f docs/download/$f ] || continue
  kb=$(( ($(wc -c < docs/download/$f) + 1023) / 1024 ))
  echo "<tr><td><a href=\"$f\">$f</a></td><td>$kb KiB</td></tr>"
done
cat <<H
<tr><td><a href="$repo/releases/download/v$v/gates-$v-examples-win64.zip">gates-$v-examples-win64.zip</a></td><td>release asset (Windows x64 examples)</td></tr>
</table>
<p>MIT licence. Older versions: <a href="$repo/releases">releases</a>.</p>
</body></html>
H
