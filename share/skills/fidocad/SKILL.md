---
name: fidocad
description: Draw, edit, validate and render FidoCad (.fcd) schematics, PCB layouts and other drawings, and create/use FidoCad macro libraries (.fcl), with the `fcai` CLI and the live `fcai-gui` editor. Use whenever a user asks to create, modify, explain or inspect a FidoCad drawing or a FidoCad library, or when any file contains [FIDOCAD]/LI/RV/MC/TY lines.
---

# FidoCad with fcai

Use `fcai` to inspect, edit and render `.fcd` drawings and `.fcl` libraries;
use its live GUI to work with the user.

## User drawing preferences

Read `fcai style show --json` at the start of drawing work and again before
recording feedback. This shared user profile lives in `~/.fcai/style.md`
(`FCAI_HOME` overrides the directory), outside the installed skill. Its scoped
preferences are drawing defaults: follow current user instructions and the
document's established conventions; preserve valid connections. Apply only
preferences whose scope matches the task.

If `learning` is true, save reusable preferences expressed in explicit style
feedback, including a GUI example the user identifies as their desired style.
Do not generalize silent acceptance, an unexplained GUI movement, a one-off
layout request, exact coordinates, device sizing or a bug workaround. State
briefly what you remembered. If learning is off, apply saved preferences but
capture new ones only when the user asks to remember them.

Use stable keys, narrow scopes and a short source (the user's statement or a
report/example reference). Update a matching key when corrected, preserving
unrelated preferences. Snapshot before changes and use the stale-base guard:

```bash
fcai style show > /tmp/style-base.md
fcai style set mos-labels "Place MOS names and values beside the body at mid-height." \
  --scope "MOS schematics" --source "User: this is how I want the labels" \
  --base /tmp/style-base.md
fcai style show --json
```

On a stale-base error, read fresh preferences, reconcile the correction and
retry with a new snapshot. `style forget KEY` removes a rule; `style history`
and `style restore ID` recover previous profiles. `style learning on|off`
controls future capture. No profile means learning is off and no preferences
are saved. The CLI stores the profile; the calling agent interprets feedback.

## First step: open the GUI before drawing

**When asked to create or edit a drawing, open the GUI first**, before adding
any symbols, wires or labels, so the user can watch the drawing take shape.
Unless the user explicitly asks for a headless workflow:

1. For a new drawing, create only an empty file containing `[FIDOCAD]` and a
   newline. For an existing drawing, open it as-is.
2. Start the GUI in the background with a separate session directory (or reuse
   the active session for that drawing):

   ```bash
   fcai gui sketch.fcd --session-dir /tmp/sketch.session &
   ```

3. Once the window is open, build or edit the drawing in meaningful increments.
   Use the feedback workflow below to push each update and check the GUI state.
   Keep the GUI open so the user sees progress from the first drawing edit.

Disk edits reload live **only when the GUI has no unsaved edits**. Otherwise
the GUI keeps the user's version; the warning appears in the optional bottom
bar and may be hidden. `session push` replaces the in-memory drawing, including
unsaved edits, so read and preserve the current GUI state before pushing.
Accepted updates automatically fit if they extend outside the canvas; updates
that already fit keep the current view. Fitting follows window resizing and
tiling until a manual pan or zoom; Space restores it. Return sends the user's
drawing and edits while keeping the window open; Ctrl+Return saves and closes.

```bash
fcai session status /tmp/sketch.session              # pending events/sends; no consumption
fcai session wait /tmp/sketch.session --timeout 1    # consume one event
fcai session get /tmp/sketch.session --wait          # current GUI state as JSON
fcai session push /tmp/sketch.session /tmp/new.fcd --wait
fcai session save /tmp/sketch.session --wait         # persist GUI document
fcai session reload-libs /tmp/sketch.session --wait  # updated .fcl files, same window
fcai session set /tmp/sketch.session grid 10         # settings live
fcai session ack /tmp/sketch.session "got your edit"
fcai session quit /tmp/sketch.session                # Done
```

### Apply feedback and confirm the live update

**Chat feedback is an instruction to act now.** The user does not need to
press Return as well. For either chat feedback or a GUI `send`:

1. Check `session status`, consume pending events with short `wait` calls,
   and acknowledge each `send`: describe the user's changes in chat and put
   a one-liner in the bottom bar with `session ack`. Read `diff_prev` for the
   change since the previous event; `diff` retains the existing baseline diff.
2. Run `session get DIR --wait`. Its JSON contains the actual `file`, `fcd`,
   `selection`, `modified` and `disk_changed`. Save `fcd` as a base snapshot
   and make the requested edits in a separate draft, preserving GUI changes.
3. Validate and inspect the draft, then push it with a precondition:

   ```bash
   fcai session push /tmp/sketch.session /tmp/new.fcd --base /tmp/base.fcd --wait
   fcai session save /tmp/sketch.session --wait
   ```

   `--base` rejects the push if the user changed the GUI since the snapshot.
   On rejection, read fresh state and merge the new edits before retrying.
   Check `ok` and the returned `fcd`; only report an update after confirmation.
   Push replaces GUI memory; `save` persists it to the drawing's path. Avoid
   writing the draft over that path before the guarded push, since a disk
   reload could change the GUI and invalidate the base.

`--wait` on get/push/save/reload/reload-libs returns the matching reply,
without consuming queued user events. This requires the updated CLI and GUI
(protocol 2). Reopen a pre-update window after preserving its edits if needed;
guarded pushes probe support before sending any drawing replacement. Do not
work around an unsupported/stale-base error by removing `--base`.
Without `--wait`, commands only queue
work; `get` alone prints nothing. The reply is also in the event log with an
`id`, so do not apply a previously handled reply twice. Exit 3 is a timeout,
not a closed window; the command may still apply. Query state before retrying.

| Event | Meaning |
|-------|---------|
| `send` | Return: user drawing, selection, note and diffs; acknowledge before editing |
| `state` | Reply to `get` |
| `applied` | Reply to push/save/reload/reload-libs; check `ok` and any `error` |
| `conflict` | Disk reload blocked by GUI edits; `fcd` is the GUI version, `disk_fcd` the disk version; reconcile both |
| `closed` | Window closed; stop editing and report |

Events include `seq` (increasing within `session_id`) and `diff_prev` (since
the preceding drawing event, including state replies). `session reload DIR`
refuses unsaved GUI edits unless explicitly given `--force`; use it only
when discarding that version is intended. Restarting archives old logs under
`DIR/history/` and starts a fresh queue. Reload libraries instead of restarting
when a macro changes; it preserves selection, undo history and unsaved edits.

Use one `session wait` reader per session. Use the built-in `--timeout`, not
an external `timeout` command or a detached/background wait. Check events
between drawing increments and on every user turn. While actively awaiting
GUI feedback, use bounded foreground waits. Once the agent turn ends, Return
only queues an event; it cannot wake an idle agent by itself.

The window shows the drawing plus the user's optional bottom bar: report
progress in your own chat, not inside the drawing.

Without a session directory the GUI still works for a human (`Return` sends
nothing, `Ctrl+Return`/close ends it), and `fcai gui` prints a report on exit:
status, note, the primitive diff since opening and the final FCD.

## Inspecting and checking drawings

Query rather than guess. For drawing work, open the GUI first as above:

```bash
fcai context   d.fcd            # stats, macros, diags — inspect this first
fcai json      d.fcd            # every primitive as JSON
fcai describe  d.fcd            # human summary
fcai render    d.fcd -o out.png # PNG for visual checking
fcai validate  d.fcd            # parse → emit → parse, reports problems
fcai emit      d.fcd            # canonical text
fcai expand    d.fcd            # macros expanded to primitives
fcai lib                        # every macro key currently loadable
fcai macro 080 --orient 1        # JSON geometry at anchor (100,100)
fcai hit d.fcd --at 40 20        # primitive index near a point
fcai find d.fcd --bbox 20 10 80 60 # indices whose bounds intersect a region
fcai lint d.fcd --json           # advisory wire/junction geometry
fcai render d.fcd --crop 20 10 80 60 --scale 8 -o /tmp/detail.png
fcai diff a.fcd b.fcd           # primitive-level + layer colour/opacity changes
fcai config                     # effective CLI configuration
```

### Structured edits

Prefer `fcai edit` for targeted changes; indices come from `fcai json`, `hit`,
`find` or GUI selection. Output goes to stdout or `-o`; the input is unchanged
unless deliberately chosen as the output. Re-query indices after add/remove.

```bash
fcai edit move draft.fcd --index 3 --by 10 0 -o next.fcd
fcai edit set draft.fcd --index 3 --name R1 --value 10k -o next.fcd
fcai edit set draft.fcd --index 5 --dash 1 --layer 0 -o next.fcd
fcai edit set draft.fcd --index 5 --stroke 1 -o next.fcd   # own line width; 0 = default
fcai edit rm draft.fcd --index 5 -o next.fcd
fcai edit add draft.fcd --from additions.fcd -o next.fcd
```

Moving a primitive carries its attached name/value labels. Separate wires or
stubs are independent primitives: move or reroute them deliberately. `--text`
edits a standalone TY; `--name`/`--value` edit attached labels. `find --near X Y R`
uses a bounding square, not electrical connectivity.

### Layer colours and transparency

Each of the 16 layers has a colour, an opacity and a name, stored per drawing
as `FJC L layer rgb alpha` and `FJC N layer name` (FidoCadJS reads both).
`fcai json`/`context` list the effective values under `layers`. Change them
with `edit layer` instead of recolouring primitives:

```bash
fcai edit layer draft.fcd --layer 2 --color 00AAFF -o next.fcd      # colour (RRGGBB)
fcai edit layer draft.fcd --layer 2 --alpha 0.4 -o next.fcd         # opacity 0..1
fcai edit layer draft.fcd --layer 3 --name "Silk top" -o next.fcd   # display name
fcai edit layer draft.fcd --layer 2 --reset -o next.fcd             # back to default
```

Options combine in one call. Opacity applies to everything on the layer
(fills overlap visibly below 1). Layer 0 is the black circuit layer; in a dark
GUI theme near-black layers are drawn in the theme foreground, but the file
keeps the real colour. `fcai diff` and the GUI `diff`/`diff_prev` report layer
changes as `L [n] before -> after` (JSON: `layers_changed` and a `layers`
array of `{layer, before, after}` with `rgb`, `alpha`, `name`); `changes`
counts them too. With a GUI open, write the result and `session push` it (or
edit the open file) so the window reloads with the new colours.

## Drawing in the GUI

`K`: searchable keyboard map; `I`: status bar (cursor, zoom, layer, selection).
Right-drag or Shift+drag measures length, mm and angle. Drag selected handles
to reshape; nonempty attached names and values have their own handles to move
each label without moving its component. Box-select just the label handles
(without enclosing a whole component), or Ctrl+click handles to toggle them,
then drag any selected handle or use arrow keys to move the group together.
Ctrl+drag a box adds handles; groups can span multiple components.
`Ctrl+B`: library → category → symbol, with live preview.
Read each `send` diff before editing to preserve the user's changes.

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
FJC L layer rgb alpha         layer colour (R<<16|G<<8|B, decimal) + opacity 0..1
FJC N layer name...           layer display name
```

* Component and terminal names/values are **attached properties**: `MC`, bare
  `FCJ`, then two `TY` lines (name, value; empty text for an unused slot).
  Loose `TY` is for sheet annotations. Parameterized `FCJ` carries arrows/dash
  and a `textFlag` for labels.
* Line width is per primitive (FidoCadAI extension): an optional width in
  logical units sits **between the dash style and the text flag** of the `FCJ`
  after `LI`/`BE`/`CV`/`CP` (`FCJ arrows style len halfw dash [width] text`) or
  `RV`/`RP`/`EV`/`EP`/`PV`/`PP` (`FCJ dash [width] text`). Omitted or `0` = the
  drawing default (`FJC B` lines, `FJC C` ellipses; 0.5 / 0.35).
  FidoCadJS/FidoCadJ ignore the token and draw the default width.
  `LI 10 10 60 10 0` + `FCJ 0 0 3 1 0 2 0` is a line 2 units thick.
* `$...$` / `$$...$$` in TY text is typeset (scripts, `\frac`, `\sqrt`, Greek,
  `\sum`, ...); plain text is fine too.
* Coordinates are non-negative integers on a grid; origin is top-left, x right,
  y down. Keep everything in the positive quadrant with margin.
* `MC` keys are **lowercased**; the standard library uses the bare key
  (`080` resistor, `170` capacitor, `300` NPN, `880` changeover switch...).
  Other libraries are `prefix.key` (`pcb.r01`, `imram.ma09`). `fcai lib` lists
  keys, `fcai expand` shows what a macro becomes.

Attached-label example:

```
[FIDOCAD]
LI 20 20 40 20 0
MC 40 20 0 0 080
FCJ
TY 42 8 4 3 0 0 0 * R1
TY 42 13 4 3 0 0 0 * 10k
LI 50 20 70 20 0
```

### Schematic layout

Before placing parts, inspect unfamiliar macros with `fcai macro KEY` or
`fcai expand`. `macro` returns expanded geometry at (100,100), with optional
`--orient 0..3`, `--mirror`, and `--at X Y`. These are drawing primitives,
not declared electrical pins: identify leads from the geometry. The `MC`
anchor is not necessarily the symbol centre. Keep placements far enough from
the coordinate boundary to avoid clipping rotated/mirrored geometry.

* Choose compact lead lengths relative to symbol size (about twice the body
  length is a starting point). Center series parts between adjacent junctions
  using their pins, with equal leads; reuse that length across comparable parts
  and input/output terminals. Derive junction spacing from pin span + two leads.
* Keep branch spacing regular and wires orthogonal. Attach control flags
  directly at pins when clear; avoid unnecessary extension wires.
  Preserve deliberate device and terminal orientation when changing spacing.
* End rails deliberately: align return terminals beneath their matching power
  terminals, or trim the return rail to the outermost branches. No unexplained
  overhangs. A wire that ends at a corner needs neither an extension nor a dot.
  Use `SA` for actual connected branches (T-junctions or connected crossings),
  not ordinary bends or unconnected crossings. Do not extend a finished wire
  merely to justify a dot.
* Align name/value pairs with consistent offsets and clearance. Mirror input/
  output label placement outward, with matching heights. Allow room for text
  before fixing spacing; preserve an existing drawing's style. Centre labels
  consistently where appropriate without crowding neighbouring wires or symbols.
* Orient terminals with their exposed marker outside and their lead toward the
  circuit. For left inputs, the lead goes right; for right outputs it goes left;
  rotate the same rule for top/bottom ports. Choose terminals by the port's role
  and the drawing's conventions, not just whether a signal is analog or digital.
* Use layers, colours and dash patterns consistently with the document's purpose.
  PCB copper/silkscreen layers have physical meanings; a blue logic net is not a
  universal convention. Explain unusual functions with brief annotations only
  when useful; no circuit-specific label is mandatory.
* Existing `examples/*.fcd` in the project checkout can illustrate syntax and
  layouts. Inspect the relevant example, but current user instructions take
  precedence over incidental choices or mistakes in an example. For mechanical
  drawings, PCB layouts or other graphics, use their conventions rather than
  applying schematic terminal and junction rules.

### Libraries (.fcl)

A library is a sequence of macro definitions; `[KEY Long name]` starts one
(the key is the first token, the rest is the human name) and `{Category}`
lines group them. A macro body is drawn **relative to the logical origin
(100,100)**; placement subtracts that and then applies orientation/mirror.
For an offset `(dx,dy)`, orientation 0/1/2/3 maps to `(dx,dy)`, `(-dy,dx)`,
`(-dx,-dy)`, `(dy,-dx)` respectively (clockwise on the downward-y canvas).
Mirror negates the resulting x offset, then the instance anchor is added.

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
* **your own library directory `~/.fcai/lib/`** (`$FCAI_HOME/lib`, overridable
  with `FIDO_USER_LIB_DIR`) is loaded automatically: every `*.fcl` in it is
  available in every drawing. Read `fcai config` for the effective `user_lib`
  path before writing. Without either override, legacy Documents/FidoLib
  libraries load first for compatibility; definitions in the new directory win;
* `FIDO_LIB_DIR` and `--lib-dir DIR` load extra directories;
* keys in all of these are `prefix.key`, prefix from the file name lowercased
  unless the file itself declares one (`FCDstdlib*` is unprefixed);
* one file: `--lib FILE` (or `--prefix NAME --lib FILE`).

#### Reuse: extract repeated patterns into the library

Reuse existing macros for repeated symbols. For repeated groups of primitives
such as connectors, footprints, logos or subcircuits, define a reusable macro
in the effective user library directory (`~/.fcai/lib/<name>.fcl` by default)
and place `MC` instances. Preserve unrelated
macros already in that file; inspect before editing. A library change affects
every drawing using the key, so use a new key when the change is not meant to
apply to existing instances.

Create a library:

1. Write `my_parts.fcl` in the effective user library directory with `[key Long name]` sections
   (keep keys unique); draw bodies around the (100,100) origin.
2. Test it: `fcai lib | grep my_parts` and `fcai render test.fcd`, where
   `test.fcd` places each macro with `MC 100 100 0 0 my_parts.key`.
3. CLI invocations load current files automatically. For an already open GUI,
   run `fcai session reload-libs DIR --wait` after creating or changing a library.
   It reloads the startup library sources and refreshes the canvas and palette.
   Check `ok`; no window restart or session-history reset is needed.
4. Place instances with `MC x y orient mirror my_parts.key`; orientation 0-3
   rotates 90° per step, mirror flips. Attach labels as above.

## Settings

The GUI reads `$XDG_CONFIG_HOME/fidocad/gui.conf`; change values live with
`fcai session set DIR key value`. Knobs include: `grid`, `snap`, `show_grid`,
`layer`, `filled`, `theme` (omarchy|light|dark), `paper`, `bg_image`,
`bg_image_x`, `bg_image_y`, `bg_image_width`, `bg_image_opacity`,
`show_bg_image`, `font`, `font_size`, `tex`, `palette`, `statusbar`, `float`,
`window_size`, `autosave`.

### Tracing picture

To draw over a photo, scan or datasheet figure, show it under the drawing:

```bash
fcai session set DIR bg_image photo.jpg      # relative to your cwd; "" clears it
fcai session set DIR bg_image_x 0            # top-left corner, FidoCad units
fcai session set DIR bg_image_y 0
fcai session set DIR bg_image_width 400      # height keeps the aspect; 0 = 1 unit/pixel
fcai session set DIR bg_image_opacity 40     # percent
```

The picture is view-only: it is never written to the `.fcd`, and `fcai render`
and `session get` do not include it, so look at the image file itself when you
need its content. `H` hides or shows it, and Space fits drawing and picture. It
is a GUI setting, so clear it when the tracing is done or it appears under the
next drawing too.

## Verifying your work

Before presenting the first draft and after layout changes:

```bash
fcai validate d.fcd                  # syntax/round-trip, not electrical correctness
fcai lint d.fcd --json               # advisory geometry; inspect findings in context
fcai render d.fcd -o /tmp/d.png && <look at the PNG with a vision model>
```

Inspect pin connections/polarity, lead symmetry, rail ends and label clearance
at readable scale. Check `fcai json` for attached `name`/`value` fields.
Use `render --crop X0 Y0 X1 Y1` for close-ups in logical drawing coordinates.
Dashes use drawing units and scale with zoom. A live push does not replace
visual inspection. `lint` checks top-level solid wires and dots, including
neighbouring macro lines on the same layer: corner/unnecessary dots, missing
branch dots, and overlapping wire segments. It does not establish electrical
connectivity, identify semantic pins, or check text clearance. Its findings
are advisory; `limited: true` means the geometry/finding limit was reached.
Do not auto-remove dots at symbol pins based only on a lint warning.

## If something is missing

FidoCadAI installs system-wide to `/opt/FidoCadAI` with the project's install
script:

```bash
scripts/install.sh install     # build, install, create/migrate ~/.fcai/lib,
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
