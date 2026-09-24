---
name: fidocad
description: Draw, edit, validate and render FidoCad (.fcd) schematics and PCB layouts, and create/use FidoCad macro libraries (.fcl), with the `fcai` CLI and the live `fcai-gui` editor. Use whenever a user asks to create, modify, explain or inspect a FidoCad drawing or a FidoCad library, or when any file contains [FIDOCAD]/LI/RV/MC/TY lines.
---

# FidoCad with fcai

`fcai` (FidoCadAI) is a dependency-free C interpreter for the FidoCad
language. It parses, emits, validates, queries, renders and edits `.fcd`
drawings and `.fcl` macro libraries, and it has an editor GUI that the agent
drives while the user watches.

Everything is a CLI command with JSON output, so prefer it over guessing:

```bash
fcai context   d.fcd            # stats, macros, diags — read this first
fcai json      d.fcd            # every primitive as JSON
fcai describe  d.fcd            # human summary
fcai render    d.fcd -o out.png # PNG for visual checking
fcai validate  d.fcd            # parse → emit → parse, reports problems
fcai emit      d.fcd            # canonical text
fcai expand    d.fcd            # macros expanded to primitives
fcai lib                        # every macro key currently loadable
fcai diff a.fcd b.fcd           # primitive-level changes
fcai config                     # effective CLI configuration
```

## Golden rule: draw with the user watching

**When asked to create or edit a drawing, start the GUI before the first
edit** so the user sees it happen. Run it in the background with a session:

```bash
fcai gui sketch.fcd --session-dir /tmp/sketch.session &
```

Then work normally. The GUI live-reloads the file whenever it changes on disk,
so editing `sketch.fcd` (or `fcai session push`) is visible immediately.

The user sends you the drawing (and their edits) from the GUI with Return; the
window stays open and you get the event.

```bash
fcai session wait /tmp/sketch.session --timeout 120   # blocks for one event
fcai session push /tmp/sketch.session /tmp/new.fcd    # replace live (or stdin)
fcai session set  /tmp/sketch.session grid 10         # settings live
fcai session get  /tmp/sketch.session                 # ask for full state
fcai session ack  /tmp/sketch.session "got it"        # acknowledge the user's send
fcai session quit /tmp/sketch.session                 # same as Ctrl+Return
fcai session path sketch.fcd                          # default session dir
```

`fcai session wait` prints one JSON line and exits (exit 3 on timeout). Events:

| `event` | meaning |
|---------|---------|
| `send`  | user pressed Return: `note` (their message), `fcd`, `selection`, `diff`, `saved` — apply their edits before doing anything else |
| `state` | answer to `session get`: same fields plus the current drawing |
| `closed`| user closed the window; stop editing and report |

**Acknowledge every `send`.** The user pressed Return and is waiting: before
anything else, say what you saw in the `diff` (what they moved, added or
deleted) and what you are going to do. Also put a one-line acknowledgement in
the window's bottom bar so the user sees the agent got it — if the bar is
hidden, ask them to press `I` once:

```bash
fcai session ack /tmp/sketch.session "got your edit: R1 moved; updating the netlist"
```

Loop: `wait` → read the event → acknowledge → edit the file (or `push`) →
`wait` again. The window shows nothing but the drawing (plus the user's own
bottom bar): report progress to the user in your own chat, not inside their
drawing.

Without a session directory the GUI still works for a human (`Return` sends
nothing, `Ctrl+Return`/close ends it), and `fcai gui` prints a report on exit:
status, note, the primitive diff since opening and the final FCD.

## Drawing in the GUI

The user can also edit directly: mouse tools and single keys, `K` for the
keyboard map (type to search), `I` for the bottom bar (cursor, zoom, layer,
selection, tinted with the layer colour). Right-drag (or Shift+drag) measures
with the ruler — length in units and mm plus the angle. Selected elements show
control-point handles they can drag to resize/reshape. `Ctrl+B` opens the
symbol library as library → category → symbol, with a live preview of the
selected macro. Every change they make is reported back
in the next `send` event, including a primitive-level diff — read it before
applying your own edits so you do not undo their work.

## Language quick reference

A drawing is line-oriented text. Tokens are separated by single spaces and the
last numeric token of a primitive is its layer (0-15, 0 = black circuit).

```
[FIDOCAD]
LI x1 y1 x2 y2 l              line
BE x1 y1 x2 y2 x3 y3 x4 y4 l  Bezier
RV|RP x1 y1 x2 y2 l           rectangle outline|filled
EV|EP x1 y1 x2 y2 l           ellipse outline|filled
PV|PP x1 y1 ... xn yn l       polygon outline|filled
CV|CP closed x1 y1 ... l      smooth curve outline|filled
SA x y l                      connection dot
PL x1 y1 x2 y2 width l        PCB track
PA x y rx ry hole style l     PCB pad
MC x y orientation mirror key macro instance (orientation 0-3, mirror 0/1)
TY x y sizeY sizeX rotation style layer font text...
TY 20 40 4 3 0 0 0 * Vout      (a standalone label)
TE x y text                   legacy text — always emit TY
```

* `FCJ` on the line after a primitive carries arrows/dash and, with
  `textFlag=1`, the next one or two `TY` lines are the name and value labels.
* `$...$` / `$$...$$` in TY text is typeset (scripts, `\frac`, `\sqrt`, Greek,
  `\sum`, ...); plain text is fine too.
* Coordinates are non-negative integers on a grid; origin is top-left, x right,
  y down. Keep everything in the positive quadrant with margin.
* `MC` keys are **lowercased**; the standard library uses the bare key
  (`080` resistor, `170` capacitor, `300` NPN, `880` changeover switch...).
  Other libraries are `prefix.key` (`pcb.r01`, `imram.ma09`). `fcai lib` lists
  keys, `fcai expand` shows what a macro becomes.

Minimal drawing:

```
[FIDOCAD]
LI 10 20 40 20 0
MC 40 20 0 0 080
TY 42 22 4 3 0 0 0 * R1
TY 42 26 4 3 0 0 0 * 10k
SA 40 20 0
```

### Libraries (.fcl)

A library is a sequence of macro definitions; `[KEY Long name]` starts one
(the key is the first token, the rest is the human name) and `{Category}`
lines group them. A macro body is drawn **relative to the logical origin
(100,100)**; placement subtracts that and then applies orientation/mirror.

```
{My parts}
[900 LED bridge]
LI 100 90 100 110
PP 100 95 106 100 100 105
LI 106 100 110 100
```

Loading rules:

* bundled standard libraries: keys unprefixed; `FIDO_STDLIB_DIR` overrides
  their location, `--stdlib`/`--no-stdlib` toggle them;
* **your own library directory `~/Documents/FidoLib/`** (overridable with
  `FIDO_USER_LIB_DIR`) is loaded automatically: every `*.fcl` in it is
  available in every drawing;
* `FIDO_LIB_DIR` and `--lib-dir DIR` load extra directories;
* keys in all of these are `prefix.key`, prefix from the file name lowercased
  unless the file itself declares one (`FCDstdlib*` is unprefixed);
* one file: `--lib FILE` (or `--prefix NAME --lib FILE`).

#### Reuse: extract repeated patterns into the library

When the same pattern or element repeats in a drawing — a connector, mounting
hole, decoupling capacitor, power flag, logo, repeated sub-circuit — do **not**
copy the primitives by hand. Write it once as a macro in
`~/Documents/FidoLib/<name>.fcl` and place `MC` instances. A reusable macro
means a single edit updates every occurrence and the drawing stays small.

Create a library:

1. Write `~/Documents/FidoLib/my_parts.fcl` with `[key Long name]` sections
   (keep keys unique); draw bodies around the (100,100) origin.
2. Test it: `fcai lib | grep my_parts` and `fcai render test.fcd`, where
   `test.fcd` places each macro with `MC 100 100 0 0 my_parts.key`.
3. It is loaded from then on — `fcai` and `fcai-gui` read the directory at
   startup, no flag needed. `--lib FILE` loads a single file instead.
4. Place instances with `MC x y orient mirror my_parts.key`; orientation 0-3
   rotates 90° per step, mirror flips. Labels use `TY` after the `MC` line (or
   an `FCJ 1` name/value pair — see the full spec).

## Settings

The GUI reads `$XDG_CONFIG_HOME/fidocad/gui.conf`; change values live with
`fcai session set DIR key value`. Knobs include: `grid`, `snap`, `show_grid`,
`layer`, `filled`, `theme` (omarchy|light|dark), `paper`, `font`, `font_size`,
`tex`, `palette`, `statusbar`, `float`, `window_size`, `autosave`.

## Verifying your work

Always, before telling the user a drawing is done:

```bash
fcai validate d.fcd                  # round-trip must be ok
fcai render d.fcd -o /tmp/d.png && <look at the PNG with a vision model>
```

If the user is watching in the GUI, a `fcai session push` is usually better
than rendering a private PNG: the window updates live and they keep working.

## If something is missing

FidoCadAI installs system-wide to `/opt/FidoCadAI` with the project's install
script:

```bash
scripts/install.sh install     # build, install, create ~/Documents/FidoLib,
                               # register this skill, install the update hook
scripts/install.sh update      # refresh after a source change
scripts/install.sh status      # what is installed where
scripts/install.sh uninstall
```

`fcai skill install` (re)delivers this skill on its own: one canonical copy
(the installed skill directory, or the Omarchy default-agents channel at
`/usr/share/omarchy/default/agents/skills` when present) with symlinks into
`~/.agents/skills` and the skills directory of every coding agent found on the
machine (`~/.claude/skills`, `~/.pi/agent/skills`, `~/.codex/skills`,
`~/.gemini/skills`, `~/.config/opencode/skills`, …). `fcai skill install DIR`
links it into a specific agent's skills directory. Symlinks keep a single
skill on disk, so agents that scan several directories at once do not report
a duplicate.
