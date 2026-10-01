#!/bin/sh
# CLI behavior checks: no Python, jq, or extra runtime dependencies.
set -eu
fcai=${1:-./build/fcai}
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' 0 HUP INT TERM
cat > "$task_tmp/a.fcd" <<'FCD'
[FIDOCAD]
MC 40 20 0 0 080
FCJ
TY 42 8 4 3 0 0 0 * R1
TY 42 13 4 3 0 0 0 * 10k
LI 0 40 20 40 0
FCD
"$fcai" edit move "$task_tmp/a.fcd" --index 0 --by 5 10 -o "$task_tmp/moved.fcd"
grep -q '^MC 45 30 0 0 080$' "$task_tmp/moved.fcd"
grep -q '^TY 47 18 4 3 0 0 0 \* R1$' "$task_tmp/moved.fcd"
grep -q '^TY 47 23 4 3 0 0 0 \* 10k$' "$task_tmp/moved.fcd"
grep -q '^LI 0 40 20 40 0$' "$task_tmp/moved.fcd"
"$fcai" edit set "$task_tmp/moved.fcd" --index 0 --name R2 --value 22k -o "$task_tmp/named.fcd"
grep -q '^TY 47 18 4 3 0 0 0 \* R2$' "$task_tmp/named.fcd"
"$fcai" edit rm "$task_tmp/named.fcd" --index 0 -o "$task_tmp/line.fcd"
if grep -Eq '^(TY|MC)' "$task_tmp/line.fcd"; then exit 1; fi
"$fcai" hit "$task_tmp/line.fcd" --at 10 40 --tol 1 | grep -q '"index":0'
"$fcai" hit "$task_tmp/line.fcd" --at 100 100 --tol 1 | grep -q '"index":-1'
"$fcai" find "$task_tmp/a.fcd" --near 10 40 2 | grep -q '"indices":\[1\]'
"$fcai" find "$task_tmp/a.fcd" --bbox 0 38 20 42 | grep -q '"indices":\[1\]'
"$fcai" edit add "$task_tmp/line.fcd" --from "$task_tmp/line.fcd" -o "$task_tmp/overlap.fcd"
"$fcai" lint "$task_tmp/overlap.fcd" --json | grep -q 'overlapping-wires'
"$fcai" edit set "$task_tmp/line.fcd" --index 0 --dash 4 --layer 2 -o "$task_tmp/dash.fcd"
grep -q '^FCJ 0 0 3 1 4 0$' "$task_tmp/dash.fcd"
"$fcai" validate "$task_tmp/dash.fcd" >/dev/null
if "$fcai" edit rm "$task_tmp/line.fcd" --index 9 -o "$task_tmp/bad.fcd" 2>/dev/null; then exit 1; fi
test ! -e "$task_tmp/bad.fcd"
cat > "$task_tmp/parts.fcl" <<'FCL'
[part Example]
LI 100 100 110 100 0
FCL
"$fcai" --no-stdlib --lib "$task_tmp/parts.fcl" macro parts.part --orient 1 | grep -q '"b":{"x":100,"y":110}'
"$fcai" --no-stdlib --lib "$task_tmp/parts.fcl" macro parts.part --mirror | grep -q '"b":{"x":90,"y":100}'
"$fcai" render "$task_tmp/line.fcd" --crop 0 35 20 45 --scale 2 --margin 0 --ppm -o "$task_tmp/crop.ppm"
head -n 3 "$task_tmp/crop.ppm" | grep -q '^40 20$'
if "$fcai" render "$task_tmp/line.fcd" --crop 20 0 0 10 -o "$task_tmp/bad.png" 2>/dev/null; then exit 1; fi
"$fcai" edit layer "$task_tmp/line.fcd" --layer 2 --color 00AAFF --alpha 0.4 --name "Top Cu" -o "$task_tmp/layer.fcd"
grep -q '^FJC L 2 43775 0.4$' "$task_tmp/layer.fcd"
grep -q '^FJC N 2 Top Cu$' "$task_tmp/layer.fcd"
"$fcai" json "$task_tmp/layer.fcd" | grep -q '"id":2,"name":"Top Cu","rgb":43775,"alpha":0.4,"modified":true'
"$fcai" diff "$task_tmp/line.fcd" "$task_tmp/layer.fcd" | grep -q '^L \[2\] #FF0000 alpha 1 "Top copper" -> #00AAFF alpha 0.4 "Top Cu"$'
"$fcai" --json diff "$task_tmp/line.fcd" "$task_tmp/layer.fcd" | grep -q '"changes":1,.*"layers_changed":1,.*"layers":\[{"layer":2,'
"$fcai" edit layer "$task_tmp/layer.fcd" --layer 2 --reset -o "$task_tmp/reset.fcd"
! grep -q '^FJC' "$task_tmp/reset.fcd"
"$fcai" diff "$task_tmp/line.fcd" "$task_tmp/reset.fcd" | grep -q 'no changes'
if "$fcai" edit layer "$task_tmp/line.fcd" --layer 2 --color red 2>/dev/null; then exit 1; fi
if "$fcai" edit layer "$task_tmp/line.fcd" --layer 2 --alpha 1.5 2>/dev/null; then exit 1; fi
if "$fcai" edit layer "$task_tmp/line.fcd" --layer 2 2>/dev/null; then exit 1; fi
printf 'CLI feedback tools passed\n'
