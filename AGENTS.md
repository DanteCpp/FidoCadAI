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

- C11, POSIX, `-Wall -Wextra`. The core and `fcai` link only `-lm`.
- Qt6 Widgets is allowed **only** in `gui/` and `tests/test_gui.cpp`, and the
  build must still work without it (`HAVE_QT` in the Makefile). No moc: no
  `Q_OBJECT`; use lambdas and `std::function` callbacks.
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
  The GUI truncates both files at start. Do not silently change field names —
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
- `share/lib/` — English standard libraries
- `scripts/install.sh` — system install/update/uninstall (`make system-install`)
- `gui/` — `document` (FcDrawing + undo), `canvas` (mouse tools + K overlay),
  `session` (agent channel), `settings`, `theme` (Omarchy), `window`, `main`
- `share/skills/fidocad/SKILL.md` — the agent skill shipped with the project;
  `make install` copies it, `fcai skill install` registers it with local agents
- `tests/test_core.c` — no framework, just `EXPECT`
- `tests/test_gui.cpp` — offscreen QTest mouse/key events + session channel

## Checks after a change

```
make test
make sanitize   # after parser/emitter/rasterizer edits
```

If you touch the parser, add a fixture under `tests/fixtures/` and a round-trip assertion.

## Install & update

`scripts/install.sh install` builds the tree and installs it to
`/opt/FidoCadAI`, symlinks `fcai`/`fcai-gui` into `/usr/local/bin` and adds the
prefix to `PATH` system-wide (`/etc/profile.d/fidocadai.sh` for shells,
`/etc/environment.d/99-fidocadai.conf` for systemd user sessions), creates the
user library directory `~/Documents/FidoLib` and delivers `share/skills/fidocad`
through the Omarchy default-agents channel: one canonical copy, a symlink at
`/usr/share/omarchy/default/agents/skills/fidocad`, and a symlink from every
agent's skills directory (`fcai skill install` uses the same symlink scheme).
It also installs a `post-commit` git hook that runs
`scripts/install.sh update --quiet` after every commit, so the system copy
follows the tree. `scripts/install.sh status` shows what is installed where.

Macros the user (or an agent) creates for repeated patterns belong in
`~/Documents/FidoLib/<prefix>.fcl`; `fcai` and `fcai-gui` load that directory
automatically (`fc_user_lib_dir()`, `FIDO_USER_LIB_DIR` overrides). Never touch
`share/lib/` from a session — that is the shipped standard library.

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
fcai session push   /tmp/sketch.session /tmp/new.fcd    # live replace (or stdin)
fcai session set    /tmp/sketch.session grid 10
fcai session ack    /tmp/sketch.session "got your edit" # acknowledge the user's send
fcai session quit   /tmp/sketch.session                 # Done
```

`wait` prints one JSON event line: `send` (the user's `note`, `fcd`,
`selection`, `diff`), `state` (answer to `get`) or `closed`. Exit 3 = timeout.
**Acknowledging a `send` is part of the protocol:** say what the `diff` shows
before editing, and put a one-liner in the window's bottom bar with
`fcai session ack` so the user knows the agent got it.
Editing the `.fcd` on disk also live-reloads the window. The window shows only
the drawing — never put status messages over it; report in your own interface.
The window blocks
until done/closed, then prints status, note, selection, the diff since opening
and the final FCD (`--json`, `--no-fcd`). Exit status 2 = closed without Done:
treat edits as unconfirmed. Read the diff before touching the file again.

Every knob is a CLI flag, env var, or `key = value` config entry. Do not hard-code user paths; use `FIDO_LIB_DIR` / `--lib-dir`.
