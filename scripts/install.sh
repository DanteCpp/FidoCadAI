#!/bin/sh
# FidoCadAI — system installer / updater.
#
#   scripts/install.sh install      build and install under /opt/FidoCadAI
#   scripts/install.sh update       refresh an existing install (also run by
#                                   the post-commit git hook)
#   scripts/install.sh uninstall    remove the install, the /usr/local/bin
#                                   links, the git hook and the agent skill
#   scripts/install.sh status       show what is installed where
#   scripts/install.sh hook [enable|disable]
#
# install/update build the tree, stage a `make install` into the prefix, create
# the user library directory (~/.fcai/lib) and deliver
# share/skills/fidocad to the local coding agents: when the Omarchy
# default-agents channel is present (Linux) it gets one canonical copy with a
# symlink at /usr/share/omarchy/default/agents/skills/fidocad, otherwise every
# agent's skills directory gets a symlink to the installed copy. That keeps a
# single skill on disk and pi/like agents deduplicate it.
#
# Works on Linux and macOS: system PATH files are /etc/profile.d +
# environment.d + fish on Linux and /etc/paths.d on macOS, and the Omarchy
# channel is used only where it exists.
#
# The first install may need sudo for /opt, /usr/local/bin and the system PATH
# files; the prefix is then owned by the invoking user, so later updates run
# without sudo.
#
# Options:
#   --prefix DIR     install prefix (default: $FIDO_PREFIX or /opt/FidoCadAI)
#   --omarchy DIR    default-agents skills channel (default: $FIDO_OMARCHY_SKILLS
#                    or $OMARCHY_PATH/default/agents/skills)
#   --no-omarchy     do not use the Omarchy channel (agents link to the prefix)
#   --no-gui         do not build/install fcai-gui
#   --no-build       use the existing build/ binaries (install from a build done as the user)
#   --no-path        do not add the install prefix to system PATH files
#   --no-skill       do not register the agent skill
#   --no-userlib     do not create/migrate ~/.fcai/lib
#   --no-hook        do not install/refresh the auto-update git hook
#   --link           create the /usr/local/bin symlinks (default: only for the
#                    default prefix)
#   --no-link        never create the symlinks
#   --force          overwrite files/symlinks not owned by FidoCadAI
#   -q, --quiet      only progress errors (git hook mode)
set -eu

APP="FidoCadAI"
DEFAULT_PREFIX="/opt/FidoCadAI"
LINK_DIR="/usr/local/bin"
MARKER="install-info"
HOOK_MARK="# FidoCadAI automatic update"
UNAME_S=$(uname -s 2>/dev/null || echo unknown)
PROFILE_FILE="/etc/profile.d/fidocadai.sh"
ENV_FILE="/etc/environment.d/99-fidocadai.conf"
FISH_FILE="/etc/fish/conf.d/fidocadai.fish"
PATHS_FILE="/etc/paths.d/fidocadai"

CMD="install"
HOOK_ACTION="enable"
PREFIX="${FIDO_PREFIX:-$DEFAULT_PREFIX}"
QUIET=0
WITH_GUI=1
WITH_BUILD=1
WITH_PATH=1
WITH_SKILL=1
WITH_USERLIB=1
WITH_HOOK=1
FORCE=0
LINK="auto"
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

run_root() {
    if [ "$(id -u)" -eq 0 ]; then
        run "$@"
    else
        run sudo "$@"
    fi
}

# ---- environment --------------------------------------------------------

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ "$(id -u)" -eq 0 ]; then
    if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
        RUN_USER=$SUDO_USER
    elif [ -n "${PKEXEC_UID:-}" ]; then
        RUN_USER=$(getent passwd "$PKEXEC_UID" 2>/dev/null | cut -d: -f1 || true)
        [ -n "$RUN_USER" ] || RUN_USER=root
    else
        RUN_USER=root
    fi
else
    RUN_USER=$(id -un)
fi
RUN_HOME=""
if command -v getent >/dev/null 2>&1; then
    RUN_HOME=$(getent passwd "$RUN_USER" 2>/dev/null | cut -d: -f6 || true)
fi
if [ -z "$RUN_HOME" ] && [ "$RUN_USER" = "$(id -un)" ]; then
    RUN_HOME="${HOME:-}"
fi
if [ -z "$RUN_HOME" ]; then
    RUN_HOME=$(eval echo "~$RUN_USER" 2>/dev/null || true)
fi
[ -n "$RUN_HOME" ] || RUN_HOME=/root
RUN_UID=$(id -u "$RUN_USER" 2>/dev/null || echo 0)
RUN_GID=$(id -g "$RUN_USER" 2>/dev/null || echo 0)

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
        --prefix)
            shift
            [ $# -gt 0 ] || { err "--prefix needs a directory"; exit 2; }
            PREFIX=$1
            ;;
        --prefix=*) PREFIX=${1#--prefix=} ;;
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
        --no-path) WITH_PATH=0 ;;
        --no-skill) WITH_SKILL=0 ;;
        --no-userlib) WITH_USERLIB=0 ;;
        --no-hook) WITH_HOOK=0 ;;
        --link) LINK="yes" ;;
        --no-link) LINK="no" ;;
        --force) FORCE=1 ;;
        *) err "unknown argument '$1' (try --help)"; exit 2 ;;
    esac
    shift
done

case $CMD in
    help|-h|--help) usage; exit 0 ;;
esac

# Sanity-check the prefix before anything can delete or write through it.
while [ "${PREFIX%/}" != "$PREFIX" ] && [ "$PREFIX" != "/" ]; do
    PREFIX=${PREFIX%/}
done
[ -n "$PREFIX" ] || { err "empty --prefix"; exit 2; }
case $PREFIX in
    /*) ;;
    *) err "prefix must be an absolute path: $PREFIX"; exit 2 ;;
esac
[ "$PREFIX" != "/" ] || { err "refusing to use / as the prefix"; exit 2; }

# ---- helpers ------------------------------------------------------------

# Documents directory for the invoking user: xdg-user-dir when present, else
# ~/Documents. HOME is forced to the invoking user's home, because under
# sudo/pkexec the process runs as root with HOME=/root.
documents_dir() {
    d=""
    if command -v xdg-user-dir >/dev/null 2>&1; then
        d=$(HOME="$RUN_HOME" XDG_CONFIG_HOME="$RUN_HOME/.config" xdg-user-dir DOCUMENTS 2>/dev/null || true)
        case $d in
            "$RUN_HOME"/*) ;;
            *) d="" ;;
        esac
    fi
    [ -n "$d" ] || d="$RUN_HOME/Documents"
    printf '%s\n' "$d"
}

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

    ver=$("$STAGE$PREFIX/bin/fcai" version 2>/dev/null || echo "unknown")
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
        printf 'user = %s\n' "$RUN_USER"
        printf 'installed = %s\n' "$when"
    } > "$STAGE$PREFIX/share/fidocad/$MARKER"
}

cleanup_stage() {
    [ -n "$STAGE" ] && [ -d "$STAGE" ] && rm -rf "$STAGE"
    STAGE=""
}

# ---- prefix & links -----------------------------------------------------

install_tree() {
    src="$STAGE$PREFIX"
    [ -d "$src" ] || { err "staging failed: $src missing"; exit 1; }
    # Find the first existing ancestor to test writability against.
    base=$PREFIX
    while [ ! -e "$base" ] && [ "$base" != "/" ]; do
        base=$(dirname "$base")
    done
    if [ -w "$base" ]; then
        need_root=0
    else
        need_root=1
    fi
    if [ "$need_root" -eq 1 ] && [ "$(id -u)" -ne 0 ] && ! command -v sudo >/dev/null 2>&1; then
        err "cannot write $PREFIX and sudo is not installed"
        exit 1
    fi
    if [ "$need_root" -eq 1 ]; then
        run_root mkdir -p "$PREFIX"
        run_root cp -a "$src/." "$PREFIX/"
    else
        mkdir -p "$PREFIX"
        cp -a "$src/." "$PREFIX/"
    fi
    # After the first install the prefix belongs to the invoking user so that
    # updates (in particular the git hook) never need privileges again. This
    # also covers a whole-script run as root (sudo/pkexec when the caller has
    # no terminal): `cp -a` then leaves root-owned files behind.
    if [ "$need_root" -eq 1 ] || { [ "$(id -u)" -eq 0 ] && [ "$RUN_USER" != "root" ]; }; then
        run_root chown -R "$RUN_UID:$RUN_GID" "$PREFIX"
    fi
    log "installed into $PREFIX"
}

link_bins() {
    [ "$LINK" = "no" ] && return 0
    if [ "$LINK" = "auto" ] && [ "$PREFIX" != "$DEFAULT_PREFIX" ]; then
        log "custom prefix: skipping $LINK_DIR symlinks (use --link)"
        return 0
    fi
    for b in fcai fcai-gui; do
        [ -x "$PREFIX/bin/$b" ] || continue
        dst="$LINK_DIR/$b"
        if [ -L "$dst" ] && [ "$(readlink "$dst")" = "$PREFIX/bin/$b" ]; then
            continue
        fi
        if { [ -e "$dst" ] || [ -L "$dst" ]; } && [ ! -L "$dst" ] && [ "$FORCE" -ne 1 ]; then
            warn "$dst exists and is not a symlink; leaving it (use --force)"
            continue
        fi
        [ -d "$LINK_DIR" ] || run_root mkdir -p "$LINK_DIR"
        if [ -w "$LINK_DIR" ]; then
            ln -sfn "$PREFIX/bin/$b" "$dst"
        else
            run_root ln -sfn "$PREFIX/bin/$b" "$dst"
        fi
        log "$dst -> $PREFIX/bin/$b"
    done
}

# ---- PATH ----------------------------------------------------------------

path_has() {
    case ":${PATH}:" in
        *":$1:"*) return 0 ;;
        *) return 1 ;;
    esac
}

# Install a root-owned file in /etc (writable by the invoking user afterwards,
# so `update` never needs privileges again for it). $1 destination, $2 content.
write_etc_file() {
    if [ -f "$1" ] && cmp -s "$2" "$1"; then
        return 0
    fi
    dir=$(dirname "$1")
    [ -d "$dir" ] || run_root mkdir -p "$dir"
    if [ -w "$dir" ] && { [ ! -e "$1" ] || [ -w "$1" ]; }; then
        install -m 644 "$2" "$1"
    else
        run_root install -m 644 "$2" "$1"
    fi
}

# Make `fcai` (and `fcai-gui`) callable everywhere. Linux: POSIX login shells
# via /etc/profile.d, systemd user sessions (graphical agents) via
# environment.d, fish via conf.d. macOS: /etc/paths.d, read by path_helper in
# login shells. The /usr/local/bin symlinks cover the common case; these
# guarantee it even where that directory is missing from PATH.
ensure_path() {
    [ "$WITH_PATH" -eq 1 ] || return 0
    bindir="$PREFIX/bin"
    tmp=$(mktemp)

    if [ "$UNAME_S" = "Darwin" ]; then
        printf '%s\n' "$bindir" > "$tmp"
        if ! write_etc_file "$PATHS_FILE" "$tmp"; then
            warn "cannot write $PATHS_FILE; fcai may not be on PATH"
        fi
    elif [ -d /etc/profile.d ]; then
        {
            printf '# FidoCadAI: put fcai/fcai-gui on PATH (managed by scripts/install.sh).\n'
            printf 'case ":$PATH:" in\n'
            printf '    *:%s:*) ;;\n' "$bindir"
            printf '    *) export PATH="%s:$PATH" ;;\n' "$bindir"
            printf 'esac\n'
        } > "$tmp"
        if ! write_etc_file "$PROFILE_FILE" "$tmp"; then
            warn "cannot write $PROFILE_FILE; fcai may not be on PATH"
        fi
    fi

    if [ -d /usr/lib/environment.d ] || [ -d /etc/environment.d ]; then
        {
            printf '# FidoCadAI: put fcai/fcai-gui on PATH (managed by scripts/install.sh).\n'
            printf 'PATH=$PATH:%s\n' "$bindir"
        } > "$tmp"
        if ! write_etc_file "$ENV_FILE" "$tmp"; then
            warn "cannot write $ENV_FILE; graphical sessions may not see fcai"
        fi
    fi

    if [ -d /etc/fish/conf.d ]; then
        {
            printf '# FidoCadAI: put fcai/fcai-gui on PATH (managed by scripts/install.sh).\n'
            printf 'if not contains %s $PATH\n' "$bindir"
            printf '    set -gx PATH %s $PATH\n' "$bindir"
            printf 'end\n'
        } > "$tmp"
        if ! write_etc_file "$FISH_FILE" "$tmp"; then
            warn "cannot write $FISH_FILE; fish shells may not see fcai"
        fi
    fi

    rm -f "$tmp"
    log "PATH: $bindir (system PATH files and /usr/local/bin symlinks)"
}

remove_path_files() {
    for f in "$PROFILE_FILE" "$ENV_FILE" "$FISH_FILE"; do
        if [ -f "$f" ] && head -n 1 "$f" 2>/dev/null | grep -q "FidoCadAI"; then
            if [ -w "$f" ]; then
                rm -f "$f"
            else
                run_root rm -f "$f" || true
            fi
            log "removed $f"
        fi
    done
    if [ -f "$PATHS_FILE" ] && grep -qxF "$PREFIX/bin" "$PATHS_FILE" 2>/dev/null; then
        if [ -w "$PATHS_FILE" ]; then
            rm -f "$PATHS_FILE"
        else
            run_root rm -f "$PATHS_FILE" || true
        fi
        log "removed $PATHS_FILE"
    fi
}

# ---- user library directory --------------------------------------------

do_userlib() {
    [ "$WITH_USERLIB" -eq 1 ] || return 0
    libdir=$(user_lib_dir)
    mkdir -p "$libdir"
    # Copy legacy libraries without removing the originals or overwriting new
    # definitions. Explicit overrides choose an isolated directory instead.
    if [ -z "${FIDO_USER_LIB_DIR:-}" ] && [ -z "${FCAI_HOME:-}" ]; then
        legacy="$(documents_dir)/FidoLib"
        for oldlib in "$legacy"/*.fcl; do
            [ -f "$oldlib" ] || continue
            newlib="$libdir/$(basename "$oldlib")"
            if [ -e "$newlib" ] || [ -L "$newlib" ]; then
                cmp -s "$oldlib" "$newlib" || warn "keeping $newlib; legacy library differs: $oldlib"
            else
                cp -p "$oldlib" "$newlib"
                log "migrated library: $newlib (original retained)"
            fi
        done
    fi
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
    if [ "$(id -u)" -eq 0 ] && [ "$RUN_USER" != "root" ]; then
        chown -R "$RUN_UID:$RUN_GID" "$libdir"
    fi
    log "user library directory: $libdir"
}

# ---- agent skill (Omarchy default-agents channel) -----------------------

channel_link() {
    printf '%s/fidocad\n' "$OMARCHY_SKILLS"
}

channel_target() {
    printf '%s/share/fidocad/skills/fidocad\n' "$PREFIX"
}

# Point the Omarchy default-agents channel at the installed skill (a symlink,
# so updates never need sudo again). Returns 0 when the channel serves it.
ensure_channel_link() {
    link=$(channel_link)
    target=$(channel_target)
    [ -d "$OMARCHY_SKILLS" ] || return 1
    if [ -L "$link" ] && [ "$(readlink "$link")" = "$target" ]; then
        return 0
    fi
    if [ -e "$link" ]; then
        if [ -L "$link" ] || [ -f "$link/SKILL.md" ]; then
            run_root rm -rf "$link" || true
        else
            warn "$link exists and is not a fidocad skill; leaving it"
            return 1
        fi
    fi
    if [ -w "$OMARCHY_SKILLS" ]; then
        ln -sfn "$target" "$link" || true
    else
        run_root ln -sfn "$target" "$link" || true
    fi
    [ -L "$link" ] && [ "$(readlink "$link")" = "$target" ]
}

remove_channel_link() {
    link=$(channel_link)
    target=$(channel_target)
    if [ -L "$link" ] && [ "$(readlink "$link")" = "$target" ]; then
        if [ -w "$OMARCHY_SKILLS" ]; then
            rm -f "$link"
        else
            run_root rm -f "$link" || true
        fi
        log "removed channel link $link"
    fi
}

do_skill() {
    [ "$WITH_SKILL" -eq 1 ] || return 0
    [ -x "$PREFIX/bin/fcai" ] || { warn "fcai not installed; skill skipped"; return 0; }
    skill_source="$PREFIX/share/fidocad/skills"
    if [ "$WITH_OMARCHY" -eq 1 ]; then
        if ensure_channel_link; then
            skill_source="$OMARCHY_SKILLS"
            log "omarchy channel: $(channel_link) -> $(channel_target)"
        elif [ -d "$OMARCHY_SKILLS" ]; then
            warn "cannot link $(channel_link); agents link to $skill_source instead"
        else
            log "no omarchy channel at $OMARCHY_SKILLS; agents link to $skill_source"
        fi
    fi
    log "linking the fidocad skill into the local agents"
    if [ "$QUIET" -eq 1 ]; then
        HOME="$RUN_HOME" FIDO_SKILL_DIR="$skill_source" "$PREFIX/bin/fcai" skill install 1>/dev/null || warn "some skill directories could not be written"
    elif ! HOME="$RUN_HOME" FIDO_SKILL_DIR="$skill_source" "$PREFIX/bin/fcai" skill install; then
        warn "some skill directories could not be written"
    fi
    if [ "$(id -u)" -eq 0 ] && [ "$RUN_USER" != "root" ]; then
        for rel in .agents/skills .config/agents/skills .pi/agent/skills .claude/skills \
                   .codex/skills .gemini/skills .gemini/config/skills .config/opencode/skills \
                   .config/crush/skills .config/zed/skills .cursor/skills; do
            if [ -L "$RUN_HOME/$rel/fidocad" ] || [ -e "$RUN_HOME/$rel/fidocad" ]; then
                chown -hR "$RUN_UID:$RUN_GID" "$RUN_HOME/$rel/fidocad"
            fi
        done
    fi
}

remove_skill() {
    for rel in .agents/skills .config/agents/skills .pi/agent/skills .claude/skills \
               .codex/skills .gemini/skills .gemini/config/skills .config/opencode/skills \
               .config/crush/skills .config/zed/skills .cursor/skills; do
        d="$RUN_HOME/$rel/fidocad"
        if [ -L "$d" ] || [ -f "$d/SKILL.md" ]; then
            rm -rf "$d"
            log "removed skill $d"
        fi
    done
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
    if [ "$(id -u)" -eq 0 ] && [ "$RUN_USER" != "root" ]; then
        chown "$RUN_UID:$RUN_GID" "$hook"
    fi
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
    link_bins
    ensure_path
    do_userlib
    do_skill
    if [ "$WITH_HOOK" -eq 1 ]; then hook_enable; fi
    cleanup_stage
    trap - EXIT INT TERM
    log "$APP installed; update later with: scripts/install.sh update"
}

do_update() {
    if [ ! -x "$PREFIX/bin/fcai" ] && [ ! -f "$PREFIX/share/fidocad/$MARKER" ]; then
        err "$PREFIX is not installed yet; run: scripts/install.sh install"
        exit 1
    fi
    check_source
    do_build
    trap cleanup_stage EXIT INT TERM
    do_stage
    install_tree
    link_bins
    ensure_path
    do_userlib
    do_skill
    if hook_enabled; then hook_enable; fi
    cleanup_stage
    trap - EXIT INT TERM
    log "$APP updated"
}

do_uninstall() {
    if [ ! -f "$PREFIX/share/fidocad/$MARKER" ] && [ "$FORCE" -ne 1 ]; then
        if [ -e "$PREFIX" ]; then
            err "$PREFIX does not look like a FidoCadAI install (use --force)"
            exit 1
        fi
    fi
    for b in fcai fcai-gui; do
        dst="$LINK_DIR/$b"
        if [ -L "$dst" ]; then
            target=$(readlink "$dst")
            case $target in
                "$PREFIX"/*)
                    if [ -w "$LINK_DIR" ]; then rm -f "$dst"; else run_root rm -f "$dst"; fi
                    log "removed $dst"
                    ;;
            esac
        fi
    done
    if [ -d "$PREFIX" ]; then
        if [ -w "$PREFIX" ] || [ "$(id -u)" -eq 0 ]; then
            rm -rf "$PREFIX"
        else
            run_root rm -rf "$PREFIX"
        fi
        log "removed $PREFIX"
    fi
    hook_disable
    remove_path_files
    remove_channel_link
    if [ "$WITH_SKILL" -eq 1 ]; then
        remove_skill
    fi
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
    if [ -f "$PREFIX/share/fidocad/$MARKER" ]; then
        sed 's/^/          /' "$PREFIX/share/fidocad/$MARKER"
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
    if command -v fcai >/dev/null 2>&1; then
        printf 'path:     fcai callable (%s)\n' "$(command -v fcai)"
    elif path_has "$PREFIX/bin"; then
        printf 'path:     %s on PATH\n' "$PREFIX/bin"
    elif [ -f "$PROFILE_FILE" ] || [ -f "$ENV_FILE" ] || [ -f "$FISH_FILE" ] || [ -f "$PATHS_FILE" ]; then
        printf 'path:     system PATH files present (%s; new shells/sessions pick it up)\n' "$PREFIX/bin"
    else
        printf 'path:     fcai not on PATH (run scripts/install.sh install)\n'
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
    for rel in .agents/skills .config/agents/skills .pi/agent/skills .claude/skills \
               .codex/skills .gemini/skills .gemini/config/skills .config/opencode/skills \
               .config/crush/skills .config/zed/skills .cursor/skills; do
        if [ -f "$RUN_HOME/$rel/fidocad/SKILL.md" ]; then
            printf '          %s\n' "$RUN_HOME/$rel/fidocad"
        fi
    done
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
