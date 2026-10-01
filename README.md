# FidoCadAI

A **dependency-free C11** interpreter for the [FidoCad](https://github.com/DarwinNE/FidoCadJ) drawing language (`.fcd` / `.fcl`). Built to be fast, embeddable, and **AI-centric**: every setting is a flag, env var, or config key, and the CLI speaks both human text and JSON so agents can read a drawing without a GUI.

Reference implementations: `~/Projects/FidoCadJS` (TypeScript) and `~/Projects/FidoCadJ` (Java). Language spec: `FidoCadJS/FIDOSPECS.md`.

Builds and runs on **Linux and macOS** (Apple clang or gcc); CI runs `make test` and `make sanitize` on both.

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

Qt6 is optional for the GUI: `qt6-base-dev` on Linux, `brew install qt pkg-config`
on macOS. Without it everything except `fcai-gui` builds and tests normally.

`scripts/install.sh` installs the whole program into `/opt/FidoCadAI`, symlinks
`fcai` and `fcai-gui` into `/usr/local/bin`, puts `/opt/FidoCadAI/bin` on `PATH`
(`/etc/profile.d/fidocadai.sh` and `/etc/environment.d/99-fidocadai.conf` on
Linux, `/etc/paths.d/fidocadai` on macOS), creates the user library directory
`~/.fcai/lib` (copying legacy Documents/FidoLib libraries without overwriting
new files or removing originals) and registers the agent skill with every local harness
(one canonical copy; the Omarchy default-agents channel is used when present).
It also installs a git hook that runs `scripts/install.sh update` after every
commit, so the system copy follows the source tree.

```bash
scripts/install.sh install     # first install
scripts/install.sh update      # refresh after a change (the git hook does this)
scripts/install.sh status      # what is installed where
scripts/install.sh uninstall
```

`make install` also installs the agent skill (`share/skills/fidocad/`): a copy
goes to `$(PREFIX)/share/fidocad/skills/`, and — unless `DESTDIR` is set — a
copy into each agent harness found in your home (Claude Code, Codex,
Antigravity, Pi, Hermes, `~/.agents`) through `scripts/install-skill.sh`. Run
under `sudo`, the copies are made as the invoking user. `make install-skill`
refreshes just the skill; `FIDO_SKILL_DIRS=dir1:dir2` overrides the targets.

Standard libraries ship in `share/lib/` (English FidoCadJ bundles) and fonts in
`share/fonts/`. `fcai` finds them via `FIDO_STDLIB_DIR` / `FIDO_FONT_DIR`, next
to the binary (it resolves its own path through `/proc/self/exe` on Linux,
`_NSGetExecutablePath` on macOS, else `argv[0]` + `$PATH`), or under
`/usr/local`, `/opt/homebrew` or `/usr` `share/fidocad/`. Your own macro
libraries live in `~/.fcai/lib/` (`FCAI_HOME` changes the application directory;
`FIDO_USER_LIB_DIR` overrides just the library directory) and
are loaded automatically; `fcai config` prints the resolved executable, the
effective `user_lib` and `style_profile` paths and whether TTF fonts were found.
Without overrides, old Documents/FidoLib libraries still load first; new
definitions take precedence.

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
fcai style show --json             # shared user drawing preferences

# show a drawing while you work on it (see the GUI section below)
fcai gui drawing.fcd --session-dir /tmp/d.session -m "note" &
```

Stdin is accepted (`fcai json -` or pipe), and bare `fcai` at a terminal prints
the help and exits. Exit status is 0 on success.

### User drawing style

The installed skill reads `~/.fcai/style.md` before drawing. This is user-owned
data, shared by all local agents and preserved across application/skill updates.
`FCAI_HOME` overrides `~/.fcai` for both the profile and default `lib/` directory.

```bash
fcai style path
fcai style show                    # readable Markdown; no file creation
fcai style show --json             # path, learning flag, keyed preferences
fcai style learning on            # enable agent capture of explicit style feedback
fcai style show > /tmp/style-base.md
fcai style set mos-labels "Place MOS labels beside the body." \
  --scope "MOS schematics" --source "User's label example" --base /tmp/style-base.md
fcai style forget mos-labels
fcai style learning off           # retain/apply saved preferences, stop capture
fcai style history                # previous-profile IDs
fcai style restore ID             # restore one of those profiles
```

Learning is off when no profile exists. The agent interprets feedback and saves
reusable, explicitly stated style preferences when enabled. The application
does not infer preferences from mouse movements or acceptance. Current requests
override saved defaults; each rule has a scope, source and update timestamp.
Keep exact coordinates, circuit sizing and bug workarounds with the drawing.

`set`/`forget` are explicit edits even with learning off. Updating a key preserves
its scope/source unless supplied again. Keys are lowercase, at most 64 bytes;
text, scope and source are nonempty single lines (limits 4096/1024/1024 bytes).
The readable Markdown format can be edited directly while preserving its header,
metadata and paragraph structure. CLI updates validate the whole profile, use a
writer lock and atomic replacement, and save the previous file verbatim under
`~/.fcai/history/style/`. `--base` on any mutation rejects stale snapshots; read
fresh state and merge before retrying. The profile is limited to 64 KiB/256 rules.
No-op updates do not add history. Restoring also saves the current version.

Text `fcai context` includes the profile; drawing JSON and the core `fc_context`
API retain their existing format. Use `style show --json` alongside JSON workflows.
Style is guidance for the calling agent, rather than automatic rendering settings.

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

`--lib PATH` (repeatable) plus `--prefix NAME` loads extra `.fcl` files.
`FIDO_FONT_DIR` points at a directory with the Liberation TTFs when they are not
next to the binary.

## Agent editing and inspection

```bash
fcai macro 080 --orient 1                   # expanded JSON at (100,100)
fcai macro 080 --mirror --at 200 200         # inspect an actual placement
fcai hit sketch.fcd --at 40 20 --tol 2       # nearest hit, -1 if none
fcai find sketch.fcd --bbox 20 10 80 60      # intersecting primitive bounds
fcai find sketch.fcd --near 40 20 10        # same query in a square
fcai edit move sketch.fcd --index 3 --by 10 0 -o draft.fcd
fcai edit set sketch.fcd --index 3 --name R1 --value 10k -o draft.fcd
fcai edit set sketch.fcd --index 5 --dash 1 --layer 0 -o draft.fcd
fcai edit set sketch.fcd --index 5 --stroke 1 -o draft.fcd   # per-primitive line width
fcai edit rm sketch.fcd --index 5 -o draft.fcd
fcai edit add sketch.fcd --from additions.fcd -o draft.fcd
fcai lint sketch.fcd --json
fcai render sketch.fcd --crop 20 10 80 60 --scale 8 -o detail.png
```

Indices refer to the current drawing (`json`, GUI selection, `hit`, or `find`).
Moves carry attached labels; separate wires remain independent. Edits emit
FCD to stdout or `-o`; they never implicitly modify the input. `set --text`
changes standalone text, while `--name` and `--value` change attached labels.
`macro` exposes actual drawing geometry, not inferred electrical pin metadata.
Keep placements away from coordinate limits to avoid clipping.

`lint` is advisory: it checks top-level solid `LI`/`SA` geometry on each layer,
including neighbouring macro lines, for corner/unnecessary dots, missing
branch dots and overlapping wires. It does not infer electrical nets, semantic
pins, or label clearance; check findings against the drawing. Work is limited
to 4096 primitives (also after expansion) and 128 findings, reported with
`limited: true`. Existing examples illustrate syntax and layouts, not mandatory
style: in particular, do not copy incidental rail overhangs or add dots at bends.

Crops use logical coordinates and the normal rasterizer. Dash patterns now use
logical units, including their phase along connected segments, so their size
tracks render scale and GUI zoom.

## GUI: draw with the user watching

`fcai-gui` (also `fcai gui`) is a small Qt6 editor with **no chat panel**: the
agent that launched it is the assistant. The two talk through a *session
directory* of JSON lines, so the agent can push edits while the user watches
and the user can send what they did back.

```bash
fcai gui sketch.fcd --session-dir /tmp/sketch.session &
fcai session status /tmp/sketch.session              # pending events and user sends
fcai session wait /tmp/sketch.session --timeout 1     # consume one event
fcai session get /tmp/sketch.session --wait           # correlated state JSON
fcai session push /tmp/sketch.session /tmp/new.fcd --base /tmp/base.fcd --wait
fcai session save /tmp/sketch.session --wait          # save GUI memory to disk
fcai session reload-libs /tmp/sketch.session --wait   # refresh libraries in place
fcai session set /tmp/sketch.session grid 10
fcai session ack /tmp/sketch.session "got your edit"
fcai session quit /tmp/sketch.session                 # Done
```

`wait` consumes one event (`--timeout SECS`, exit 3 on timeout). `status`
reports unread counts without consuming anything. Check it between drawing
updates and on each user turn: Return queues feedback but cannot wake an
agent whose turn has ended.

| Event | Purpose |
|-------|---------|
| `send` | Return: user `note`, `fcd`, `selection`, `diff`, `saved`, `modified` |
| `state` | Reply to `get` |
| `applied` | Reply to push/save/reload/reload-libs, including `ok` and optional `error` |
| `conflict` | Disk update blocked by unsaved GUI edits; includes both `fcd` and `disk_fcd` |
| `closed` | Window closed |

Events have a `session_id`, increasing `seq` within that run, UTC `time`, and
`diff_prev` since the preceding drawing event. Existing `diff` and other
fields retain their meanings. A restart archives the previous command/event
logs and read offset under `DIR/history/`, then starts a fresh queue without
replaying commands.

`get`, `push`, `save`, `reload`, and `reload-libs` accept `--wait` with a
5-second default timeout (`--timeout SECS` overrides it). State events advertise
`protocol: 2`; guarded pushes verify protocol support with a read-only probe
before sending the replacement, so an older GUI cannot silently ignore the
base check. Reopen pre-update windows after preserving their edits.
The CLI attaches an
`id` and reads the matching atomic reply from `DIR/replies/ID.json`, removing
that reply file after reading. The same event remains in `events.jsonl`;
queued user sends and `.events.pos` are untouched. Without `--wait`, commands
retain their asynchronous behavior (`get` prints nothing). A timeout does
not cancel a command; inspect state before retrying. Exit 1 indicates a
rejected/failed command, with `ok: false` and `error` in a received reply.

After every `send`, acknowledge what changed in chat and with `session ack`.
For chat feedback, act directly; no extra Return in the GUI is required.
Read current state with `get --wait`, save its `fcd` as a base snapshot, edit a
separate draft, then `push --base SNAPSHOT --wait`. The GUI rejects a stale
base so newer user edits can be merged. Verify the returned drawing, then
`save --wait`. A push changes GUI memory; it does not save unless autosave is on.

Disk edits reload only when the GUI has no unsaved edits. A blocked reload
preserves the user's version and emits `conflict`; `disk_changed` is also
included in state. `reload --wait` refuses unsaved edits; `--force` explicitly
discards them. `reload-libs --wait` refreshes the configured libraries, canvas
and palette without changing the drawing, selection or undo history.

Disk reloads and session pushes automatically fit
the drawing when it extends outside the canvas, preserving the current view
when it already fits. Fit-to-view also follows window resizing and tiling until
you manually pan or zoom; Space restores it. Closing without Done (exit status 2)
means the edits are unconfirmed.

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
like FidoCadJS. Nonempty attached **names and values have separate handles** at
their text origins: drag either label independently, without moving the symbol
or its other label. Label dragging uses the grid snap and supports undo/redo.
Dragging the component body moves it together with its labels.
To move several labels together, **drag a selection box around their handles**
without enclosing a whole component, or **Ctrl+click handles** to toggle them.
Then drag any selected handle or use the arrow keys; their relative spacing
stays fixed, including at drawing boundaries. Ctrl+drag a box adds handles to
the group. This also works across components and with other control points.
Selected handles are larger and filled; the other handles are hollow.
Strokes are drawn with round caps and joins. **Ctrl+B** opens
the symbol library as *library → category → symbol* (the FidoCadJS hierarchy),
with a live preview of the selected macro under the tree.

**P** (or F2, or double-click) opens the properties of the selected element
inside the drawing: layer, coordinates, name and value, and whatever that kind
has: dash, line width (empty = drawing default), arrows (start/end, style, length, width), fill, closed curve, text
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
`$PREFIX/share/fidocad/skills/fidocad/` for `fcai skill` to find, and (on a
plain `PREFIX` install) also copies it into the harnesses through
`scripts/install-skill.sh`.

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
share/skills/       fidocad agent skill (installed by make install / fcai skill install)
scripts/install.sh        system installer + updater + git update hook
scripts/install-skill.sh  copy the skill into local agent harnesses
tests/              fixtures + unit tests
```

## Next

- In-place edit commands (`fcai add` / `move` / `set`) for agents
- Netlist-ish queries
