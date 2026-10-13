#!/bin/sh
# Persistent user style and library-path checks; isolate all writes from the user.
set -eu
fcai=${1:-./build/fcai}
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' 0 HUP INT TERM
export FCAI_HOME="$task_tmp/app"
unset FIDO_USER_LIB_DIR FIDO_JSON

"$fcai" style path | grep -qxF "$FCAI_HOME/style.md"
"$fcai" style show --json > "$task_tmp/empty.json"
grep -q '"learning":false,"preferences":\[\]' "$task_tmp/empty.json"
"$fcai" style history > "$task_tmp/history"
test ! -s "$task_tmp/history"
test ! -e "$FCAI_HOME"

"$fcai" style show > "$task_tmp/base.md"
"$fcai" style set mos-labels 'Put names beside the body: "M1", µm.' \
    --scope 'MOS schematics' --source 'User label example' --base "$task_tmp/base.md"
"$fcai" style show --json > "$task_tmp/saved.json"
grep -q '"scope":"MOS schematics","source":"User label example"' "$task_tmp/saved.json"
grep -q '\\"M1\\", µm' "$task_tmp/saved.json"
"$fcai" style history > "$task_tmp/history"
test "$(wc -l < "$task_tmp/history" | tr -d ' ')" -eq 1
initial_id=$(cat "$task_tmp/history")

# No-op edits and forgetting unknown keys must not produce revisions.
cp "$FCAI_HOME/style.md" "$task_tmp/before.md"
"$fcai" style set mos-labels 'Put names beside the body: "M1", µm.'
"$fcai" style forget unknown
cmp "$FCAI_HOME/style.md" "$task_tmp/before.md"
"$fcai" style history > "$task_tmp/after-history"
cmp "$task_tmp/history" "$task_tmp/after-history"

# A stale full-profile base must refuse the update without touching the file.
if "$fcai" style set ground 'Wrong update' --base "$task_tmp/base.md" 2>/dev/null; then exit 1; fi
cmp "$FCAI_HOME/style.md" "$task_tmp/before.md"
"$fcai" style show > "$task_tmp/base.md"
"$fcai" style set mos-labels 'Names and values beside the body.' --base "$task_tmp/base.md"
"$fcai" style show --json | grep -q '"scope":"MOS schematics","source":"User label example"'

# Two writers editing different keys preserve both changes under the lock.
"$fcai" style set ground 'Use a downward triangle.' --scope schematic &
first_pid=$!
"$fcai" style set supply 'Use a bar.' --scope schematic &
second_pid=$!
wait "$first_pid"
wait "$second_pid"
"$fcai" style show --json > "$task_tmp/concurrent.json"
grep -q '"key":"ground"' "$task_tmp/concurrent.json"
grep -q '"key":"supply"' "$task_tmp/concurrent.json"

"$fcai" style learning on
"$fcai" style show --json | grep -q '"learning":true'
"$fcai" style learning off
"$fcai" style show --json | grep -q '"key":"mos-labels"'
"$fcai" style set dash-text -- '- Prefer compact branches.'
"$fcai" style show --json | grep -q '"text":"- Prefer compact branches\."'
"$fcai" style forget dash-text
"$fcai" style forget ground
if "$fcai" style show --json | grep -q '"key":"ground"'; then exit 1; fi

printf '[FIDOCAD]\nLI 0 0 10 10 0\n' > "$task_tmp/drawing.fcd"
"$fcai" context "$task_tmp/drawing.fcd" > "$task_tmp/context"
grep -q 'mos-labels \[MOS schematics\]: Names and values beside the body.' "$task_tmp/context"
"$fcai" context --json "$task_tmp/drawing.fcd" > "$task_tmp/drawing.json"
if grep -q 'mos-labels' "$task_tmp/drawing.json"; then exit 1; fi

# Restore an earlier empty profile, and retain the displaced file verbatim.
cp "$FCAI_HOME/style.md" "$task_tmp/before-restore.md"
"$fcai" style restore "$initial_id"
"$fcai" style show --json | grep -q '"learning":false,"preferences":\[\]'
found=0
for snapshot in "$FCAI_HOME"/history/style/*; do
    if cmp -s "$snapshot" "$task_tmp/before-restore.md"; then found=1; fi
done
test "$found" -eq 1

# Reject malformed/oversized profiles, duplicate keys and unsupported fields.
"$fcai" style set ground 'Use a triangle.'
cp "$FCAI_HOME/style.md" "$task_tmp/good.md"
for bad in 'Bad Key' '../escape' ''; do
    if "$fcai" style set "$bad" text 2>/dev/null; then exit 1; fi
done
multiline='line one
line two'
if "$fcai" style set test "$multiline" 2>/dev/null; then exit 1; fi
if "$fcai" style restore '../escape' 2>/dev/null; then exit 1; fi
if "$fcai" style learning maybe 2>/dev/null; then exit 1; fi
cmp "$FCAI_HOME/style.md" "$task_tmp/good.md"
sed -n '/^## ground/,$p' "$task_tmp/good.md" >> "$FCAI_HOME/style.md"
cp "$FCAI_HOME/style.md" "$task_tmp/duplicate.md"
if "$fcai" style set test text 2>/dev/null; then exit 1; fi
cmp "$FCAI_HOME/style.md" "$task_tmp/duplicate.md"
# Drawing inspection still works when style data is corrupt.
"$fcai" context "$task_tmp/drawing.fcd" > "$task_tmp/context" 2> "$task_tmp/warning"
grep -q 'FidoCadAI context' "$task_tmp/context"
grep -q 'style profile' "$task_tmp/warning"
printf 'unsupported profile\n' > "$FCAI_HOME/style.md"
if "$fcai" style show 2>/dev/null; then exit 1; fi
dd if=/dev/zero of="$FCAI_HOME/style.md" bs=65537 count=1 2>/dev/null
if "$fcai" style show 2>/dev/null; then exit 1; fi
cp "$task_tmp/good.md" "$FCAI_HOME/style.md"

# A failed history write must leave the current profile intact.
mv "$FCAI_HOME/history" "$task_tmp/history-backup"
printf 'blocks directory creation\n' > "$FCAI_HOME/history"
if "$fcai" style set ground 'Changed triangle.' 2>/dev/null; then exit 1; fi
cmp "$FCAI_HOME/style.md" "$task_tmp/good.md"
rm "$FCAI_HOME/history"
mv "$task_tmp/history-backup" "$FCAI_HOME/history"

# The app root and library overrides are shared by CLI/GUI through lib.c.
mkdir -p "$FCAI_HOME/lib" "$task_tmp/override"
printf '[part New default]\nLI 100 100 110 100 0\n' > "$FCAI_HOME/lib/style-test.fcl"
printf '[part Explicit override]\nLI 100 100 110 100 0\n' > "$task_tmp/override/override-test.fcl"
"$fcai" config | grep -qxF "user_lib = $FCAI_HOME/lib"
"$fcai" config | grep -qxF "style_profile = $FCAI_HOME/style.md"
"$fcai" lib | grep -q '^style-test.part'
FIDO_USER_LIB_DIR="$task_tmp/override" "$fcai" lib > "$task_tmp/override-lib"
grep -q '^override-test.part' "$task_tmp/override-lib"
if grep -q '^style-test.part' "$task_tmp/override-lib"; then exit 1; fi
FCAI_HOME="$FCAI_HOME/" "$fcai" style path | grep -qxF "$FCAI_HOME/style.md"
mkdir -p "$task_tmp/docs/FidoLib"
printf '[part Legacy library]\nLI 100 100 110 100 0\n' > "$task_tmp/docs/FidoLib/style-legacy.fcl"
XDG_DOCUMENTS_DIR="$task_tmp/docs" "$fcai" lib > "$task_tmp/isolated-lib"
if grep -q '^style-legacy.part' "$task_tmp/isolated-lib"; then exit 1; fi
(
    unset FCAI_HOME FIDO_USER_LIB_DIR
    "$fcai" style path | grep -qxF "$HOME/.fcai/style.md"
    "$fcai" config | grep -qxF "user_lib = $HOME/.fcai/lib"
    XDG_DOCUMENTS_DIR="$task_tmp/docs" "$fcai" lib | grep -q '^style-legacy.part'
    # A file in the user directory replaces the legacy file of the same name,
    # so a macro deleted from a migrated copy stays deleted.
    HOME="$task_tmp/home"
    mkdir -p "$HOME/.fcai/lib"
    printf '[other Only in the new copy]\nLI 100 100 110 100 0\n' > "$HOME/.fcai/lib/style-legacy.fcl"
    XDG_DOCUMENTS_DIR="$task_tmp/docs" "$fcai" lib > "$task_tmp/shadowed-lib"
    grep -q '^style-legacy.other' "$task_tmp/shadowed-lib"
    if grep -q '^style-legacy.part' "$task_tmp/shadowed-lib"; then exit 1; fi
)
printf 'Style profile and library-path checks passed\n'
