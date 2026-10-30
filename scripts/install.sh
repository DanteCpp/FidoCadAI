#!/bin/sh
# FidoCadAI — per-user installer / updater.
#
#   scripts/install.sh install      build and install under ~/.local
#   scripts/install.sh update       refresh an existing install (also run by
#                                   the post-commit git hook)
#   scripts/install.sh uninstall    remove the installed files, the git hook
#                                   and the agent skill links
#   scripts/install.sh status       show what is installed where
#   scripts/install.sh hook [enable|disable]
#
# FidoCadAI always installs into the invoking user's ~/.local: the binaries go
# to ~/.local/bin/{fcai,fcai-gui} and the data (libraries, fonts, skill) to
# ~/.local/share/fidocad. No sudo is needed, so run it as yourself.
#
# install/update build the tree, stage a `make install`, create the user
# library directory (~/.fcai/lib) and deliver share/skills/fidocad to the
# local coding agents: when the Omarchy default-agents channel is present and
# writable it gets a symlink at <channel>/fidocad, otherwise every agent's
# skills directory gets a symlink to the installed copy. That keeps a single
# skill on disk and pi/like agents deduplicate it.
#
# Works on Linux and macOS. ~/.local/bin must be on PATH; the installer warns
# when it is not.
#
# Options:
#   --omarchy DIR    default-agents skills channel (default: $FIDO_OMARCHY_SKILLS
#                    or $OMARCHY_PATH/default/agents/skills)
#   --no-omarchy     do not use the Omarchy channel (agents link to the prefix)
#   --no-gui         do not build/install fcai-gui
#   --no-build       use the existing build/ binaries
#   --no-skill       do not register the agent skill
#   --no-userlib     do not create ~/.fcai/lib
#   --no-hook        do not install/refresh the auto-update git hook
#   --force          uninstall even without the install-info marker
#   -q, --quiet      only progress errors (git hook mode)
set -eu

APP="FidoCadAI"
MARKER="install-info"
HOOK_MARK="# FidoCadAI automatic update"

CMD="install"
HOOK_ACTION="enable"
QUIET=0
WITH_GUI=1
WITH_BUILD=1
WITH_SKILL=1
WITH_USERLIB=1
WITH_HOOK=1
FORCE=0
WITH_OMARCHY=1
OMARCHY_SKILLS="${FIDO_OMARCHY_SKILLS:-${OMARCHY_PATH:-/usr/share/omarchy}/default/agents/skills}"
STAGE=""

usage() {
    sed -n '2,/^set -eu$/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

log() {
    [ "$QUIET" -eq 1 ] || printf '==> %s\n' "$*"
}

warn() {
    printf '%s: warning: %s\n' "$0" "$*" >&2
}

err() {
    printf '%s: error: %s\n' "$0" "$*" >&2
}

run() {
    if [ "$QUIET" -eq 1 ]; then "$@" 1>/dev/null; else "$@"; fi
}

# ---- environment --------------------------------------------------------

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ "$(id -u)" -eq 0 ] && [ -n "${SUDO_USER:-}${PKEXEC_UID:-}" ]; then
    err "$APP installs into your ~/.local and needs no sudo; run it as yourself"
    exit 1
fi

RUN_HOME="${HOME:-}"
[ -n "$RUN_HOME" ] || { err "HOME is not set"; exit 1; }
RUN_HOME=${RUN_HOME%/}
PREFIX="$RUN_HOME/.local"
BINDIR="$PREFIX/bin"
DATADIR="$PREFIX/share/fidocad"

# ---- arguments ----------------------------------------------------------

if [ $# -gt 0 ] && [ "$1" = "hook" ]; then
    CMD="hook"
    shift
    if [ $# -gt 0 ]; then
        case $1 in
            enable|disable) HOOK_ACTION=$1; shift ;;
            -*) : ;;
            *) err "unknown hook action '$1' (enable|disable)"; exit 2 ;;
        esac
    fi
fi

while [ $# -gt 0 ]; do
    case $1 in
        install|update|uninstall|status|help|-h|--help) CMD=$1 ;;
        --quiet|-q) QUIET=1 ;;
        --omarchy)
            shift
            [ $# -gt 0 ] || { err "--omarchy needs a directory"; exit 2; }
            OMARCHY_SKILLS=$1
            WITH_OMARCHY=1
            ;;
        --omarchy=*) OMARCHY_SKILLS=${1#--omarchy=}; WITH_OMARCHY=1 ;;
        --no-omarchy) WITH_OMARCHY=0 ;;
        --no-gui) WITH_GUI=0 ;;
        --no-build) WITH_BUILD=0 ;;
        --no-skill) WITH_SKILL=0 ;;
        --no-userlib) WITH_USERLIB=0 ;;
        --no-hook) WITH_HOOK=0 ;;
        --force) FORCE=1 ;;
        *) err "unknown argument '$1' (try --help)"; exit 2 ;;
    esac
    shift
done

case $CMD in
    help|-h|--help) usage; exit 0 ;;
esac

# ---- helpers ------------------------------------------------------------

user_lib_dir() {
    if [ -n "${FIDO_USER_LIB_DIR:-}" ]; then
        printf '%s\n' "$FIDO_USER_LIB_DIR"
    elif [ -n "${FCAI_HOME:-}" ]; then
        printf '%s/lib\n' "${FCAI_HOME%/}"
    else
        printf '%s/.fcai/lib\n' "$RUN_HOME"
    fi
}

hook_enabled() {
    [ -f "$ROOT/.git/hooks/post-commit" ] && grep -qF "$HOOK_MARK" "$ROOT/.git/hooks/post-commit" 2>/dev/null
}

check_source() {
    [ -f "$ROOT/Makefile" ] || { err "no Makefile in $ROOT"; exit 1; }
    command -v make >/dev/null 2>&1 || { err "make is not installed"; exit 1; }
}

# ---- build --------------------------------------------------------------

do_build() {
    [ "$WITH_BUILD" -eq 1 ] || { log "using existing build (--no-build)"; return 0; }
    jobs=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
    # `make sanitize` leaves an ASan build; never install that.
    if [ -x "$ROOT/build/fcai" ] && LC_ALL=C grep -q '__asan_init' "$ROOT/build/fcai" 2>/dev/null; then
        log "removing sanitizer build"
        run make -C "$ROOT" clean
    fi
    log "building $APP from $ROOT (-j$jobs)"
    if [ "$WITH_GUI" -eq 1 ]; then
        run make -C "$ROOT" -j"$jobs"
    else
        run make -C "$ROOT" -j"$jobs" build/fcai
    fi
}

# Stage a `make install`; sets STAGE to the staging directory.
do_stage() {
    STAGE=$(mktemp -d "${TMPDIR:-/tmp}/fcai-install.XXXXXX")
    log "staging make install -> $STAGE"
    run make -C "$ROOT" install DESTDIR="$STAGE" PREFIX="$PREFIX"

    ver=$("$STAGE$BINDIR/fcai" version 2>/dev/null || echo "unknown")
    rev="-"
    if command -v git >/dev/null 2>&1; then
        rev=$(git -C "$ROOT" describe --always --dirty --tags 2>/dev/null || echo "-")
    fi
    when=$(date +%Y-%m-%dT%H:%M:%S%z 2>/dev/null || echo "-")
    {
        printf 'version = %s\n' "$ver"
        printf 'revision = %s\n' "$rev"
        printf 'prefix = %s\n' "$PREFIX"
        printf 'source = %s\n' "$ROOT"
        printf 'installed = %s\n' "$when"
    } > "$STAGE$DATADIR/$MARKER"
}

cleanup_stage() {
    [ -n "$STAGE" ] && [ -d "$STAGE" ] && rm -rf "$STAGE"
    STAGE=""
}

# ---- install tree --------------------------------------------------------

# ~/.local is shared with other software, so only FidoCadAI's own files are
# touched: the two binaries and share/fidocad (replaced whole, so files
# dropped from the source tree do not linger).
install_tree() {
    src="$STAGE$PREFIX"
    [ -d "$src" ] || { err "staging failed: $src missing"; exit 1; }
    mkdir -p "$BINDIR" "$PREFIX/share"
    for b in fcai fcai-gui; do
        if [ -f "$src/bin/$b" ]; then
            # Replace through a temporary name: a running fcai-gui keeps its inode.
            cp -p "$src/bin/$b" "$BINDIR/.$b.new"
            mv -f "$BINDIR/.$b.new" "$BINDIR/$b"
        fi
    done
    rm -rf "$DATADIR"
    cp -R "$src/share/fidocad" "$DATADIR"
    log "installed into $BINDIR and $DATADIR"
}

remove_tree() {
    for b in fcai fcai-gui; do
        if [ -e "$BINDIR/$b" ] || [ -L "$BINDIR/$b" ]; then
            rm -f "$BINDIR/$b"
            log "removed $BINDIR/$b"
        fi
    done
    if [ -d "$DATADIR" ]; then
        rm -rf "$DATADIR"
        log "removed $DATADIR"
    fi
}

path_has() {
    case ":${PATH}:" in
        *":$1:"*) return 0 ;;
        *) return 1 ;;
    esac
}

check_path() {
    path_has "$BINDIR" && return 0
    warn "$BINDIR is not on PATH; add it to your shell profile, e.g."
    warn "  export PATH=\"\$HOME/.local/bin:\$PATH\""
}

# ---- user library directory --------------------------------------------

do_userlib() {
    [ "$WITH_USERLIB" -eq 1 ] || return 0
    libdir=$(user_lib_dir)
    mkdir -p "$libdir"
    # Old Documents/FidoLib libraries are not copied: fcai loads them where
    # they are (see fc_user_lib_dir in include/fidocad.h).
    if [ ! -f "$libdir/README.md" ]; then
        cat > "$libdir/README.md" <<'EOF'
# FidoLib — your FidoCad libraries

Every `*.fcl` file in this directory is loaded automatically by `fcai` and
`fcai-gui`, after the bundled standard libraries. Put your own parts here:
elements and patterns you reuse — connectors, mounting holes, decoupling
capacitors, power flags, logos, repeated sub-circuits.

- The file name is the key prefix: `power_flags.fcl` gives `power_flags.flag5v`.
- Macro keys are lowercased; keep them unique.
- Draw a macro body relative to the origin (100,100); place instances with
  `MC x y orientation mirror prefix.key`.
- Do not edit the standard libraries shipped with FidoCadAI; `fcai lib` lists
  every loadable key and `fcai expand` shows what a macro becomes.

Agents: when a pattern or element occurs more than once in a drawing, write it
once as a macro here and place `MC` instances instead of copying primitives.
EOF
    fi
    log "user library directory: $libdir"
}

# ---- agent skill (Omarchy default-agents channel) -----------------------

channel_link() {
    printf '%s/fidocad\n' "$OMARCHY_SKILLS"
}

channel_target() {
    printf '%s/skills/fidocad\n' "$DATADIR"
}

# Point the Omarchy default-agents channel at the installed skill when the
# channel is writable without privileges. Returns 0 when the channel serves it.
ensure_channel_link() {
    link=$(channel_link)
    target=$(channel_target)
    [ -d "$OMARCHY_SKILLS" ] && [ -w "$OMARCHY_SKILLS" ] || return 1
    if [ -L "$link" ] && [ "$(readlink "$link")" = "$target" ]; then
        return 0
    fi
    if [ -e "$link" ] || [ -L "$link" ]; then
        if [ -L "$link" ] || [ -f "$link/SKILL.md" ]; then
            rm -rf "$link" || return 1
        else
            warn "$link exists and is not a fidocad skill; leaving it"
            return 1
        fi
    fi
    ln -sfn "$target" "$link" || return 1
    [ -L "$link" ] && [ "$(readlink "$link")" = "$target" ]
}

remove_channel_link() {
    link=$(channel_link)
    target=$(channel_target)
    if [ -L "$link" ] && [ "$(readlink "$link")" = "$target" ]; then
        rm -f "$link" || { warn "cannot remove $link"; return 0; }
        log "removed channel link $link"
    fi
}

do_skill() {
    [ "$WITH_SKILL" -eq 1 ] || return 0
    [ -x "$BINDIR/fcai" ] || { warn "fcai not installed; skill skipped"; return 0; }
    skill_source="$DATADIR/skills"
    if [ "$WITH_OMARCHY" -eq 1 ]; then
        if ensure_channel_link; then
            skill_source="$OMARCHY_SKILLS"
            log "omarchy channel: $(channel_link) -> $(channel_target)"
        elif [ -d "$OMARCHY_SKILLS" ]; then
            log "omarchy channel $OMARCHY_SKILLS is not writable; agents link to $skill_source"
        else
            log "no omarchy channel at $OMARCHY_SKILLS; agents link to $skill_source"
        fi
    fi
    log "linking the fidocad skill into the local agents"
    if [ "$QUIET" -eq 1 ]; then
        FIDO_SKILL_DIR="$skill_source" "$BINDIR/fcai" skill install 1>/dev/null || warn "some skill directories could not be written"
    elif ! FIDO_SKILL_DIR="$skill_source" "$BINDIR/fcai" skill install; then
        warn "some skill directories could not be written"
    fi
}

# Needs the installed fcai, so it runs before the files are removed.
remove_skill() {
    [ -x "$BINDIR/fcai" ] || { warn "fcai not installed; skill links kept"; return 0; }
    run "$BINDIR/fcai" skill uninstall || warn "some skill links could not be removed"
}

# ---- git auto-update hook ----------------------------------------------

hook_strip() {
    awk -v mark="$HOOK_MARK" '
        $0 ~ mark { skip = 1; next }
        skip && /^# --- end FidoCadAI ---$/ { skip = 0; next }
        !skip { print }
    ' "$1"
}

hook_enable() {
    [ -d "$ROOT/.git/hooks" ] || { warn "$ROOT is not a git checkout; no update hook"; return 0; }
    hook="$ROOT/.git/hooks/post-commit"
    if [ -f "$hook" ]; then
        tmp=$(mktemp)
        hook_strip "$hook" > "$tmp"
        cat "$tmp" > "$hook"
        rm -f "$tmp"
    else
        printf '#!/bin/sh\n' > "$hook"
    fi
    {
        printf '\n%s (managed by scripts/install.sh) ---\n' "$HOOK_MARK"
        printf '[ -x "%s/scripts/install.sh" ] && "%s/scripts/install.sh" update --quiet --no-hook || true\n' "$ROOT" "$ROOT"
        printf '# --- end FidoCadAI ---\n'
    } >> "$hook"
    chmod 755 "$hook"
    log "update hook: $hook (runs scripts/install.sh update after every commit)"
}

hook_disable() {
    hook="$ROOT/.git/hooks/post-commit"
    if ! hook_enabled; then
        log "no FidoCadAI update hook installed"
        return 0
    fi
    tmp=$(mktemp)
    hook_strip "$hook" > "$tmp"
    if [ "$(grep -vE '^#!|^[[:space:]]*$' "$tmp" | wc -l)" -eq 0 ]; then
        rm -f "$hook"
    else
        cat "$tmp" > "$hook"
        chmod 755 "$hook"
    fi
    rm -f "$tmp"
    log "update hook removed from $hook"
}

# ---- commands -----------------------------------------------------------

do_install() {
    check_source
    do_build
    trap cleanup_stage EXIT INT TERM
    do_stage
    install_tree
    do_userlib
    do_skill
    if [ "$WITH_HOOK" -eq 1 ]; then hook_enable; fi
    cleanup_stage
    trap - EXIT INT TERM
    check_path
    log "$APP installed; update later with: scripts/install.sh update"
}

do_update() {
    if [ ! -x "$BINDIR/fcai" ] && [ ! -f "$DATADIR/$MARKER" ]; then
        err "$APP is not installed in $PREFIX yet; run: scripts/install.sh install"
        exit 1
    fi
    check_source
    do_build
    trap cleanup_stage EXIT INT TERM
    do_stage
    install_tree
    do_userlib
    do_skill
    if hook_enabled; then hook_enable; fi
    cleanup_stage
    trap - EXIT INT TERM
    log "$APP updated"
}

do_uninstall() {
    if [ ! -f "$DATADIR/$MARKER" ] && [ "$FORCE" -ne 1 ]; then
        if [ -e "$BINDIR/fcai" ] || [ -e "$DATADIR" ]; then
            err "$PREFIX does not hold a FidoCadAI install from this script (use --force)"
            exit 1
        fi
    fi
    if [ "$WITH_SKILL" -eq 1 ]; then
        remove_skill
    fi
    remove_channel_link
    remove_tree
    hook_disable
    log "kept $(user_lib_dir) (your libraries)"
}

# readlink -f is GNU-only; resolve a symlink chain by hand (macOS has no -f).
real_path() {
    p=$1
    i=0
    while [ -L "$p" ] && [ "$i" -lt 40 ]; do
        t=$(readlink "$p") || break
        case $t in
            /*) p=$t ;;
            *) p=$(dirname "$p")/$t ;;
        esac
        i=$((i + 1))
    done
    printf '%s\n' "$p"
}

do_status() {
    printf '%s\n' "prefix:   $PREFIX"
    if [ -f "$DATADIR/$MARKER" ]; then
        sed 's/^/          /' "$DATADIR/$MARKER"
    else
        printf '          not installed (run: scripts/install.sh install)\n'
    fi
    for b in fcai fcai-gui; do
        p=$(command -v "$b" 2>/dev/null || true)
        if [ -n "$p" ]; then
            printf '%-9s %s -> %s\n' "$b:" "$p" "$(real_path "$p")"
        else
            printf '%-9s not on PATH\n' "$b:"
        fi
    done
    libdir=$(user_lib_dir)
    n=$(find "$libdir" -maxdepth 1 -name '*.fcl' 2>/dev/null | wc -l | tr -d ' ')
    printf 'user lib: %s (%s .fcl)\n' "$libdir" "$n"
    if path_has "$BINDIR"; then
        printf 'path:     %s on PATH\n' "$BINDIR"
    else
        printf 'path:     %s not on PATH (add it to your shell profile)\n' "$BINDIR"
    fi
    if [ -L "$(channel_link)" ]; then
        printf 'channel:  %s -> %s\n' "$(channel_link)" "$(readlink "$(channel_link)")"
    else
        printf 'channel:  %s (no fidocad link)\n' "$OMARCHY_SKILLS"
    fi
    if hook_enabled; then
        printf 'git hook: enabled (%s)\n' "$ROOT/.git/hooks/post-commit"
    else
        printf 'git hook: disabled\n'
    fi
    printf 'skill:\n'
    if [ -x "$BINDIR/fcai" ]; then
        "$BINDIR/fcai" skill status | sed 's/^/          /'
    fi
}

case $CMD in
    install) do_install ;;
    update) do_update ;;
    uninstall) do_uninstall ;;
    status) do_status ;;
    hook)
        case $HOOK_ACTION in
            enable) hook_enable ;;
            disable) hook_disable ;;
        esac
        ;;
    *) usage; exit 2 ;;
esac
