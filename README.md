# FidoCadAI

A **dependency-free C11** interpreter for the [FidoCad](https://github.com/DarwinNE/FidoCadJ) drawing language (`.fcd` / `.fcl`). Built to be fast, embeddable, and **AI-centric**: every setting is a flag, env var, or config key, and the CLI speaks both human text and JSON so agents can read a drawing without a GUI.

Reference implementations: `~/Projects/FidoCadJS` (TypeScript) and `~/Projects/FidoCadJ` (Java). Language spec: `FidoCadJS/FIDOSPECS.md`.

## Status (v0.1)

| Piece | State |
|-------|--------|
| Parser (all primitives, FCJ/FJC, TY name/value) | done |
| Canonical emitter + round-trip validate | done |
| Macro libraries + expansion | done |
| Agent context / JSON / describe | done |
| Software rasterizer → PNG/PPM | done |
| `$...$` LaTeX in TY (no MathJax) | done |
| CLI (`fcai`) | done |
| GUI editor + agent session (`fcai-gui`, Qt6) | done |
| Primitive diff (`fcai diff`) | done |

Core parser/emitter is still libc + libm. Rendering vendors [stb](https://github.com/nothings/stb) (`stb_truetype`, `stb_image_write`) and Liberation fonts (`share/fonts/`) so TY text matches real Sans / Serif / Mono faces. See `THIRD_PARTY.md`.

## Build

```
make            # build/fcai (+ build/fcai-gui when Qt6 is installed)
make gui        # just the GUI
make test       # core tests (+ offscreen GUI tests when Qt6 is installed)
make sanitize   # ASan + UBSan
make install PREFIX=/usr/local
make system-install   # /opt/FidoCadAI + agent skill + update hook (scripts/install.sh)
```

`scripts/install.sh` installs the whole program into `/opt/FidoCadAI`, symlinks
`fcai` and `fcai-gui` into `/usr/local/bin`, adds `/opt/FidoCadAI/bin` to `PATH`
system-wide (`/etc/profile.d/fidocadai.sh`, `/etc/environment.d/99-fidocadai.conf`),
creates the user library directory `~/Documents/FidoLib` and delivers the agent
skill through the Omarchy default-agents channel (one canonical copy plus
symlinks into every agent's skills directory). It also installs a git hook that
runs `scripts/install.sh update` after every commit, so the system copy follows
the source tree.

```bash
scripts/install.sh install     # first install
scripts/install.sh update      # refresh after a change (the git hook does this)
scripts/install.sh status      # what is installed where
scripts/install.sh uninstall
```

Standard libraries ship in `share/lib/` (English FidoCadJ bundles). `fcai` finds them next to the binary, via `FIDO_STDLIB_DIR`, or under `$PREFIX/share/fidocad/lib`. Your own macro libraries live in `~/Documents/FidoLib/` (override with `FIDO_USER_LIB_DIR`) and are loaded automatically; see `fcai config` for the effective `user_lib` path.

## CLI for agents

```
fcai context drawing.fcd          # summary + JSON (default command)
fcai describe -v drawing.fcd      # primitive-by-primitive
fcai json drawing.fcd             # machine-readable dump
fcai validate drawing.fcd         # round-trip + diagnostics
fcai emit drawing.fcd -o out.fcd  # canonical rewrite
fcai expand drawing.fcd           # macros → primitives
fcai render drawing.fcd -o out.png
fcai lib                          # list loaded macros
fcai config                       # dump effective settings
fcai diff before.fcd after.fcd    # primitive-level changes (--json for ops)
fcai session wait DIR             # next event from a running fcai-gui
fcai skill                        # the shipped agent skill; `skill install`

# show a drawing while you work on it (see the GUI section below)
fcai gui drawing.fcd --session-dir /tmp/d.session -m "note" &
```

Stdin is accepted (`fcai json -` or pipe). Exit status is 0 on success.

### Settings (all overridable by an agent)

Flags, `key = value` config (`--config file`), and environment:

| Key | Flag / env | Default |
|-----|------------|---------|
| `stdlib` | `--stdlib` / `--no-stdlib` / `FIDO_NO_STDLIB` | on |
| `lib_dir` | `--lib-dir` / `FIDO_LIB_DIR` | — |
| `extensions` | `--extensions` / `--no-extensions` | on |
| `scale` | `--scale` / `FIDO_SCALE` | auto |
| `margin` | `--margin` / `FIDO_MARGIN` | 12 |
| `width`/`height` | `--width` `--height` | 1200×900 |
| `grid` | `--grid` | off |
| `ppm` | `--ppm` | off (PNG) |
| `bg` | `--bg RRGGBB` | `FFFFFF` |
| `json` | `--json` / `FIDO_JSON` | off |
| `verbose` | `-v` | off |

`--lib PATH` (repeatable) plus `--prefix NAME` loads extra `.fcl` files. Personal libraries: `FIDO_LIB_DIR=/home/dante/Drive/Resources/FidoLibs`.

## GUI: draw with the user watching

`fcai-gui` (also `fcai gui`) is a small Qt6 editor with **no chat panel**: the
agent that launched it is the assistant. The two talk through a *session
directory* of JSON lines, so the agent can push edits while the user watches
and the user can send what they did back.

```bash
fcai gui sketch.fcd --session-dir /tmp/sketch.session &
fcai session wait   /tmp/sketch.session --timeout 300   # blocks for one event
fcai session push   /tmp/sketch.session /tmp/new.fcd    # live replace (or stdin)
fcai session set    /tmp/sketch.session grid 10
fcai session get    /tmp/sketch.session                 # full state as an event
fcai session ack    /tmp/sketch.session "got your edit" # show an acknowledgement in the GUI
fcai session quit   /tmp/sketch.session                 # Done
fcai session path   sketch.fcd                          # default session dir
```

`wait` prints one JSON event and exits (`--timeout SECS`, exit 3 on timeout):

| `event` | fields |
|---------|--------|
| `send`  | user pressed Return: `note`, `fcd`, `selection`, `diff`, `saved` |
| `state` | answer to `get`: same fields plus the current drawing |
| `closed`| window closed (`saved`, `note`) |

After a `send`, acknowledge it before editing: reply to the user with what the
`diff` shows, and optionally put a one-liner in the window's status bar with
`fcai session ack DIR TEXT`.

Editing the `.fcd` on disk reloads the window live as well, so an agent can
just keep editing the file. Closing without Done (exit status 2) means the
edits are unconfirmed.

**The window is the drawing** plus an optional bottom bar (hidden by default):
no toolbar, buttons or menus. **K** opens the keymap — an Omarchy-style,
scrollable overlay with the search field on top and one line per key, Esc
closes. **I** toggles the bottom bar (cursor, zoom, tinted layer, selection,
ruler measurement). **Return** sends the drawing to the agent and
keeps the window open; **Ctrl+Return** is Done (save + close).
Other keys: S select · L line · B rectangle · E ellipse · J dot · T text ·
Esc back to select · R rotate · M mirror · Del delete · arrows nudge · `[` `]`
layer · F filled shapes · G grid · Shift+G snap · 0 or Space fit · wheel zoom ·
middle-drag pan · Ctrl+Z/Ctrl+Shift+Z undo · Ctrl+C/X/V/D copy/cut/paste/
duplicate · Ctrl+B symbol library · Ctrl+S save · Ctrl+W close.
**Right-drag** (or **Shift+drag**) measures with the ruler: length in units and
millimetres plus the angle; the measurement stays until the next click.
**Ctrl+click** adds to the selection.

Selecting an element shows its **control-point handles** (line endpoints,
rectangle/ellipse corners, polygon/curve vertices, Bezier control points, the
position of dots, pads, macros and text): drag a handle to resize or reshape,
like FidoCadJS. Strokes are drawn with round caps and joins. **Ctrl+B** opens
the symbol library as *library → category → symbol* (the FidoCadJS hierarchy),
with a live preview of the selected macro under the tree.

**P** (or F2, or double-click) opens the properties of the selected element
inside the drawing: layer, coordinates, name and value, and whatever that kind
has: dash, arrows (start/end, style, length, width), fill, closed curve, text
height/width/rotation/bold/italic/mirrored/font, symbol key/rotation/mirror, PCB
track width, pad size/hole/shape. Enter applies, Esc cancels, one undo step.
The window shows only the drawing: the agent puts no messages over it — it just
edits or pushes, and reports in its own interface.

When the window ends (`Done`, `quit` or close), it prints a report to stdout:
status, note, selection, the primitive-level diff since opening, and the final
FCD (`--json` for JSON, `--no-fcd` to omit the drawing).

### Settings

There is no settings dialog either: knobs live in
`$XDG_CONFIG_HOME/fidocad/gui.conf` and are changed live by the agent with
`fcai session set DIR key value`. The knobs: `grid`, `snap`, `show_grid`,
`layer`, `filled`, `theme` (`omarchy`|`light`|`dark`), `paper`, `font`,
`font_size`, `tex`, `palette`, `statusbar`, `float`, `window_size`, `autosave`.

On Omarchy the colours and font follow the current theme
(`~/.local/state/omarchy/current/theme/colors.toml`), live on theme switches.
The window is a **normal tiled window** by default; `float = 1` makes it open
floating, centred and focused above the tiled windows, sized by `window_size`
(`85%` of the monitor, or `WxH`).

### Agent skill

The project ships a FidoCad skill for coding agents
(`share/skills/fidocad/SKILL.md`): the CLI tour, the drawing language,
creating and loading `.fcl` libraries, and the rule to start this GUI before
the first edit when the user asks for a drawing.

```bash
fcai skill             # print it
fcai skill path        # where it was found
fcai skill install     # symlink it into ~/.agents/skills + every agent skills dir
fcai skill install DIR # or a specific agent's skills directory
```

`fcai skill install` links the cross-client `~/.agents/skills/fidocad` and,
when the agent is present, `~/.claude/skills`, `~/.pi/agent/skills`,
`~/.config/agents/skills`, `~/.codex/skills`, `~/.gemini/skills`,
`~/.config/opencode/skills` and friends — symlinks to one canonical copy (the
Omarchy default-agents channel when installed, else the installed skill
directory). `make install` puts that canonical copy under
`$PREFIX/share/fidocad/skills/fidocad/` for `fcai skill` to find.

## Library (embed)

```c
#include "fidocad.h"

FcDrawing d;
FcLib *lib = fc_lib_new();
fc_drawing_init(&d);
fc_lib_load_stdlib(lib, argv[0]);
fc_parse_file(&d, "board.fcd");
char *ctx = fc_context(&d, lib);
fputs(ctx, stdout);
fc_render_file(&d, lib, NULL, "board.png");
```

See `include/fidocad.h` for the data model (`FcPrim`, layers, config).

## Layout

```
include/fidocad.h   public API
src/                parser, emitter, edit/diff/hit-test, libs, rasterizer, CLI
gui/                Qt6 editor (fcai-gui): canvas, session, settings, theme
share/lib/          bundled .fcl
share/skills/       fidocad agent skill (installed by `fcai skill install`)
scripts/install.sh  /opt/FidoCadAI installer + updater + git update hook
tests/              fixtures + unit tests
```

## Next

- In-place edit commands (`fcai add` / `move` / `set`) for agents
- Netlist-ish queries
