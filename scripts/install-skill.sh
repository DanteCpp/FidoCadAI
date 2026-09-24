#!/bin/sh
# Copy the fidocad agent skill into every agent harness present for the user.
#
#   scripts/install-skill.sh SRC_DIR
#
# SRC_DIR is the skill directory (holds SKILL.md). Each target gets a real copy,
# never a symlink, so the installed skill does not depend on this checkout.
# Targets are chosen only when the harness exists in $HOME; override the whole
# list with FIDO_SKILL_DIRS (colon-separated skills directories).
#
# A previous install is recognised by the marker file and replaced. A symlink
# in the slot (the old FidoCadJS skill was installed that way) is replaced too.
# A real directory without the marker is someone else's: it is left alone with
# a warning, and the rest of the install goes on.
#
# Run under sudo (as `sudo make install` does), the copy is made as $SUDO_USER
# so the user's agent directories never become root-owned.
set -eu

NAME=fidocad
MARKER=.fidocadai-skill

src=${1:?usage: install-skill.sh SRC_DIR}
[ -f "$src/SKILL.md" ] || { echo "install-skill: $src/SKILL.md not found" >&2; exit 1; }
src=$(cd "$src" && pwd)

if [ "$(id -u)" = 0 ] && [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
    exec sudo -u "$SUDO_USER" -H env FIDO_SKILL_DIRS="${FIDO_SKILL_DIRS:-}" sh "$0" "$src"
fi

if [ -n "${FIDO_SKILL_DIRS:-}" ]; then
    dirs=$FIDO_SKILL_DIRS
else
    dirs=
    add() { [ -d "$1" ] && dirs="$dirs${dirs:+:}$2"; return 0; }
    add "$HOME/.claude"         "$HOME/.claude/skills"                # Claude Code
    add "$HOME/.codex"          "$HOME/.codex/skills"                 # Codex
    add "$HOME/.gemini/config"  "$HOME/.gemini/config/skills"         # Antigravity
    add "$HOME/.pi/agent"       "$HOME/.pi/agent/skills"              # Pi
    add "$HOME/.hermes/skills"  "$HOME/.hermes/skills/note-taking"    # Hermes (categorised)
    add "$HOME/.agents/skills"  "$HOME/.agents/skills"                # shared agents dir
fi

[ -n "$dirs" ] || { echo "install-skill: no agent harness found in $HOME; skipped"; exit 0; }

old_ifs=$IFS
IFS=:
for dir in $dirs; do
    IFS=$old_ifs
    dst="$dir/$NAME"
    if [ -L "$dst" ]; then
        rm "$dst"
    elif [ -d "$dst" ] && [ ! -f "$dst/$MARKER" ]; then
        echo "install-skill: warning: $dst exists and was not installed by fcai; left alone" >&2
        continue
    fi
    mkdir -p "$dir"
    tmp="$dir/.$NAME.tmp.$$"
    rm -rf "$tmp"
    cp -R "$src" "$tmp"
    : > "$tmp/$MARKER"
    rm -rf "$dst"
    mv "$tmp" "$dst"
    echo "installed skill $NAME -> $dst"
done
IFS=$old_ifs
