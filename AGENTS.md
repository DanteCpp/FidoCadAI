# FidoCadAI — agent notes

C11 FidoCad interpreter. No third-party deps. Keep it that way.

## What this is

Parse / emit / query / render FidoCad `.fcd` drawings and `.fcl` libraries.
The CLI is the AI interface: JSON + `context` + PNG. The GUI (`gui/`, Qt6,
binary `fcai-gui`) reuses `include/fidocad.h` for everything — parsing,
rendering (`fc_render_rgb`), hit-testing, editing, diffing. Do not fork a
second model or a second renderer in C++.

The GUI talks to the agent that launched it through a **session directory**
(`gui/session.cpp`, `fcai session wait|push|set|get|ack|quit`): the user
presses Return in the window, the agent gets the drawing and the diff, edits
and pushes back, the window reloads live. There is no built-in chat.

Spec: `~/Projects/FidoCadJS/FIDOSPECS.md`. Behaviour should match FidoCadJS
`ParserActions` + primitive `toString`, not invent a dialect.

## Rules

- C11, POSIX, `-Wall -Wextra`. The core and `fcai` link only `-lm` and must
  build and run on Linux and macOS; OS-specific code lives only in `src/sys.c`
  (executable path, data-dir search). The install script is POSIX sh and works
  on both platforms too.
- Qt6 Widgets is allowed **only** in `gui/` and `tests/test_gui.cpp`, and the
  build must still work without it (`HAVE_QT` in the Makefile). No moc: no
  `Q_OBJECT`; use lambdas and `std::function` callbacks. Qt6 is available on
  Linux (`qt6-base-dev`) and macOS (`brew install qt pkg-config`).
- GUI settings are changed by the calling agent through the session
  (`fcai session set DIR key value`) or by editing `gui.conf`. Add new knobs to
  `kKnobs` in `gui/settings.cpp`; do not add settings dialogs.
- The window is the canvas plus an optional bottom bar (`I`, hidden by
  default) with status only: cursor, zoom, layer (tinted with the layer colour),
  selection, ruler measurement. No toolbar, buttons or dropdowns. Single keys
  are in `Canvas::keyPressEvent`, Ctrl
  shortcuts in `MainWindow::buildShortcuts`; list every new one in the K sheet
  (`Canvas::paintEvent`, which is the Omarchy-style searchable overlay; it is
  scrollable, one key per line, search field on top).
- Library: Ctrl+B shows the FidoCadJS hierarchy (library → category → macro,
  from `FcMacro.lib_name`/`category`) with a live macro preview (`fc_render_rgb`).
- Ruler: right-drag or Shift+drag measures (length, mm, angle) like FidoCadJS
  `Ruler.ts`; the measurement stays until the next press.
  Return on the canvas **sends** the drawing to the agent and keeps the window
  open; Ctrl+Return is Done (save + close). Space fits. Library: Ctrl+B.
- Selection shows FidoCadJS-style control-point handles (`fc_prim_handle`,
  drag to resize/reshape); strokes are round-capped/joined like FidoCadJS
  (`line_aa` disks at solid thick-line ends).
- The session protocol is an interface for other programs: JSON lines in
  `DIR/commands.jsonl` and `DIR/events.jsonl`, read offsets in `DIR/.events.pos`.
  The GUI archives both files and the read offset under `DIR/history/` at start,
  then starts fresh logs. Correlated `--wait` replies also use `DIR/replies/`.
  Do not silently change field names —
  agents and the shipped skill (`share/skills/fidocad/SKILL.md`) depend on them;
  keep that skill in sync when the CLI/GUI workflow changes.
- No dialog windows for editing (Hyprland can bury them behind the drawing):
  use `InlineForm` (`gui/props.cpp`). `editProperties` covers every FcPrim field.
- Panels start closed (canvas only; the bottom bar is off). gui.conf stores
  only non-default knobs.
- Hyprland: the window is a **normal tiled window** by default (`float = 0`).
  With `float = 1` it maps floating and centred via a `hyprctl eval` window rule
  (`MainWindow::placeWindow`); Hyprland regexes are full-match.
- No headers-in-headers beyond `fidocad.h`. Internals live in `src/internal.h`.
- Arena for drawing/library strings and vertex arrays; `realloc` for growable tables.
- Locale-independent numbers (`fc_parse_double` / `fc_fmt_num`). Never `atof`/`printf %f` for FCD.
- Parser never aborts the file on a bad line: skip + `fc_drawing_diag`.
- Round-trip (`parse → emit → parse → emit`) must be idempotent. `make test` checks this.
- Macro keys are lowercased. Stdlib has an **empty** prefix (`080` not `fcdstdlib.080`).
- `fc_expand` parses/flattens each macro body once per call, refuses cycles and caps output at `FC_MAX_EXPANDED_PRIMS` (diagnostics go to the destination drawing). Keep library input untrusted.
- Rasterizer loops must stay clipped to the image; map pixels through `clampi`/`mapx`/`scale_len`, never a raw `(int)` cast.
- Macro bodies are drawn relative to logical origin **(100,100)**; expansion subtracts that then applies orientation/mirror (see `fc_xform_point`, copied from FidoCadJS `MapCoordinates.mapXr/mapYr`).
- Do not add JSON/PNG/MathJax libraries. Emitters are in `src/emit.c` and `src/render.c`.
- `$...$` / `$$...$$` in TY text is typeset by `src/tex.c` (scripts, `\frac`, `\sqrt`, Greek, common symbols). Unknown TeX falls back to the literal source. Toggle with `--tex` / `--no-tex`.
- Text is rasterized from vendored Liberation TTFs via `stb_truetype` (`src/ttf.c`, `vendor/`). `*` / Courier → Mono; Sans-serif / Helvetica → Sans; Constantia / Times → Serif. Stroke font is the fallback if no TTF is found. PNG uses `stb_image_write` (deflate). See `THIRD_PARTY.md`.

## Layout

- `include/fidocad.h` — public types + API (read this first)
- `src/parse.c` — line parser, FCJ/TY attachment state machine
- `src/emit.c` — FCD writer, JSON, describe, validate
- `src/lib.c` — `.fcl` loader, hash lookup, `fc_expand`
- `src/edit.c` — in-place edits, `fc_hit_test`, `fc_prim_bounds`, `fc_diff`
- `src/render.c` — software rasterizer, PNG/PPM
- `src/cli.c` — `fcai` commands and config
- `src/sys.c` — platform glue: executable path, share/ search
- `share/lib/` — English standard libraries
- `scripts/install.sh` — system install/update/uninstall (`make system-install`)
- `scripts/install-skill.sh` — copy the agent skill into every local harness
- `gui/` — `document` (FcDrawing + undo), `canvas` (mouse tools + K overlay),
  `session` (agent channel), `settings`, `theme` (Omarchy), `window`, `main`
- `share/skills/fidocad/SKILL.md` — the agent skill shipped with the project;
  `make install` copies it and registers it with the local agents, and
  `fcai skill install` does the same on its own
- `tests/test_core.c` — no framework, just `EXPECT`
- `tests/test_gui.cpp` — offscreen QTest mouse/key events + session channel

## Working style: iterate fast, harden at push

During normal work, implement first and verify with the smallest thing that can
fail, then report back. Good smoke checks:

- incremental build of what changed (`make -s fcai`, `make -s gui`)
- the touched drawing through `fcai context|describe|render|diff`
- `./build/test_core` (~50 ms) for core edits;
  `QT_QPA_PLATFORM=offscreen ./build/test_gui` (~2 s) for GUI edits

Do **not** run the full suite or sanitizers on every change — that is the push
gate, not the loop. Write the tests alongside the change, but they only have to
be green before a push. If you touch the parser, add a fixture under
`tests/fixtures/` with a round-trip assertion.

Before pushing to a remote (a deploy):

```
make test       # core + GUI
make sanitize   # ASan + UBSan
```

CI (`.github/workflows/ci.yml`) reruns both on Linux and macOS; fix what it
flags. The post-commit hook refreshes `/opt/FidoCadAI` via
`scripts/install.sh update`.

## Install & update

`scripts/install.sh install` builds the tree and installs it under
`/opt/FidoCadAI` (override with `--prefix DIR`), symlinks `fcai`/`fcai-gui`
into `/usr/local/bin` and puts the prefix on `PATH`: `/etc/profile.d`,
`/etc/environment.d` and `/etc/fish/conf.d` on Linux, `/etc/paths.d` on macOS.
It creates `~/.fcai/lib`, copies legacy Documents/FidoLib libraries without
overwriting new files or removing originals, and delivers
`share/skills/fidocad` to every agent harness found (`fcai skill install`,
with the Omarchy default-agents channel used when present). It also installs a
`post-commit` git hook that runs `scripts/install.sh update --quiet` after
every commit, so the system copy follows the tree. `scripts/install.sh status`
shows what is installed where, and `--no-gui`, `--no-build`, `--no-path`,
`--no-skill`, `--no-hook`, `--no-link` and `--force` are available on install.

`make install` is the plain makefile install (default `PREFIX=/usr/local`):
binaries, libraries, fonts and skill under `$DATADIR`, plus the agent-harness
registration through `scripts/install-skill.sh` (real copies, so it works on
Linux and macOS and never touches `$HOME` on a staged DESTDIR install).

Macros the user (or an agent) creates for repeated patterns belong in
`~/.fcai/lib/<prefix>.fcl`; `fcai` and `fcai-gui` load that directory
automatically (`fc_user_lib_dir()`, `FCAI_HOME` changes the application directory,
`FIDO_USER_LIB_DIR` overrides the library directory). Without overrides, legacy
Documents/FidoLib libraries load first; new definitions win. Never touch
`share/lib/` from a session — that is the shipped standard library.

User drawing preferences live in `~/.fcai/style.md`, shared across agent
harnesses and preserved by application/skill updates. `fcai style show --json`
reads them; `set`, `forget`, `learning`, `history` and `restore` maintain them.
Read the profile for drawing work. When learning is on, record explicit reusable
style feedback with scope/source and `--base`, following the shipped skill.
Keep personal preferences out of the shipped skill and repo-wide defaults.

## Talking to this program

```
fcai context file.fcd     # what to read first
fcai json file.fcd
fcai render file.fcd -o /tmp/d.png
fcai lib                  # keys an MC may use
fcai config               # every knob currently in effect
fcai diff a.fcd b.fcd     # what changed, primitive by primitive
fcai skill                # the fidocad skill text (libraries, GUI workflow)
fcai skill install        # register it with the local agents
```

## Showing the user a drawing

Start the window **before** the first edit so the user watches it happen; a
separate session directory keeps the workspace clean:

```
fcai gui sketch.fcd --session-dir /tmp/sketch.session &
fcai session wait   /tmp/sketch.session --timeout 300   # user pressed Return
fcai session get    /tmp/sketch.session --wait         # current state without consuming sends
fcai session push   /tmp/sketch.session /tmp/new.fcd --wait # confirmed live replace
fcai session save   /tmp/sketch.session --wait         # persist GUI drawing
fcai session reload-libs /tmp/sketch.session --wait    # refresh macros without restart
fcai session set    /tmp/sketch.session grid 10
fcai session ack    /tmp/sketch.session "got your edit" # acknowledge the user's send
fcai session quit   /tmp/sketch.session                 # Done
```

`wait` prints one JSON event line: `send` (the user's `note`, `fcd`,
`selection`, `diff`, `diff_prev`), `state`, `applied`, `conflict` or `closed`.
Events include `seq` and `session_id`. Exit 3 = timeout. `get/push/save/reload/
reload-libs --wait` returns a correlated reply without consuming queued sends;
`session status` reports pending counts. `push --base FILE` rejects stale edits.
**Acknowledging a `send` is part of the protocol:** say what the `diff` shows
before editing, and put a one-liner in the window's bottom bar with
`fcai session ack` so the user knows the agent got it.
Editing the `.fcd` on disk live-reloads a clean window; unsaved GUI edits block
the reload and emit `conflict` with both versions. Read/merge the GUI state,
then `push --base FILE --wait` and `save --wait` to apply feedback safely. The window shows only
the drawing — never put status messages over it; report in your own interface.
The window blocks
until done/closed, then prints status, note, selection, the diff since opening
and the final FCD (`--json`, `--no-fcd`). Exit status 2 = closed without Done:
treat edits as unconfirmed. Read the diff before touching the file again.

Every knob is a CLI flag, env var, or `key = value` config entry. Do not hard-code user paths; use `FIDO_LIB_DIR` / `--lib-dir`.
