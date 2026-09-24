---
name: fidocad
description: Read, write, validate and render FidoCad / FidoCadJ circuit drawings (.fcd) and macro libraries (.fcl) with the `fcai` CLI. Use whenever a schematic or PCB drawing in the FidoCad text format must be understood, authored, edited, checked, or turned into a picture - including finding the right library symbol for a component and working out where its pins land once placed.
---

# FidoCad circuits with `fcai`

An `.fcd` drawing is plain text, one command per line, tokens separated by
**single spaces**. Each line either declares a primitive, attaches attributes
to the previous primitive (`FCJ`, `TY`), or sets global config (`FJC`).

```
[FIDOCAD]
MC 60 30 1 0 080          <- a resistor from the standard library
TY 66 34 4 3 0 0 0 * R1   <- its name   (TY lines right after MC are its labels)
TY 66 38 4 3 0 0 0 * 100k <- its value
LI 60 40 60 70 0          <- a wire
SA 60 45 0                <- a junction dot
```

`fcai` (FidoCadAI) does the mechanical work: parsing, library lookup, macro
expansion, validation and PNG rendering. Use it rather than reasoning about
coordinates by hand — **never guess a macro key or a pin location.**

```bash
fcai config            # sanity check: exe path, macros_loaded (~948), fonts = ttf
```

If `fcai` is missing, build and install it from the FidoCadAI repo:
`make && sudo make install`. Always give `fcai` a file or pipe into it;
with no file it reads stdin.

## Working with the user

Drawing is a conversation: the user watches the picture grow and edits it
back. Keep them able to see what you are doing at every step.

**Show before you build.** When asked to draw something, first create the
`.fcd` file (`[FIDOCAD]` alone is enough), render it and open the picture for
the user, and only then start adding parts:

```bash
fcai render amp.fcd -o amp.png && open amp.png     # Linux: xdg-open amp.png
```

Keep the PNG next to the `.fcd`, same name, so the user knows which is which.
After every meaningful step (a block of parts placed, a group of wires routed,
labels added) re-render and run `open` again so the window refreshes, and say
in one line what changed ("placed R1, R2 and Q1; wiring the divider next").
Do not batch the whole drawing into one silent write at the end.

**Snapshot what you hand over.** Each time you stop and give the drawing back,
save its canonical form as the reference for the next round:

```bash
fcai emit amp.fcd -o "${TMPDIR:-/tmp}/amp.fcai-last.fcd"
```

**When the user sends the drawing back modified, acknowledge the changes
first.** Before doing anything else, diff it against the snapshot in
canonical form (so formatting noise disappears) and tell the user what they
changed, in circuit terms:

```bash
fcai emit amp.fcd | diff "${TMPDIR:-/tmp}/amp.fcai-last.fcd" -
fcai context amp.fcd | head -20          # names/values to describe the diff
```

- Say it back concretely: "You moved Q1 from (100,45) to (110,45), changed R2
  from 22k to 47k, and added a wire from (60,80) to (60,95)." Name parts by
  their reference designator, not by line number.
- Point out what the edit broke or left dangling (a wire end that no longer
  meets a pin after the move, a new `UNRESOLVED` macro, a free `TY` that got
  captured as a label) and ask before fixing it — the edit may be deliberate.
- Treat their version as the new truth: never overwrite it from your own
  earlier copy. Re-render, `open` it, then continue from there.
- If there is no snapshot (a new session, or a file you did not write), say
  so, summarize the drawing with `context` instead, and take that as the
  baseline.

## Reading a drawing

```bash
fcai context circuit.fcd          # summary, macros with name/value, diagnostics, JSON
fcai describe -v circuit.fcd      # one line per primitive, human-readable
fcai json circuit.fcd             # full machine-readable dump
fcai render circuit.fcd -o /tmp/c.png   # then look at the PNG
```

`context` resolves every `MC` to its component name and its reference
designator / value, and lists every `UNRESOLVED` macro. Read it before
answering any question about what a circuit contains, and render + look at the
picture before answering what it *does* — `fcai` has no netlist command, so
connectivity is read from coincident coordinates and the image.

Render knobs: `--scale N` (px per unit), `--grid`, `--width/--height` (auto-fit
box, default 1200×900), `--bg RRGGBB`, `--no-tex` (print `$...$` literally
instead of typesetting it).

## Writing a drawing

Work in this order. Skipping step 2 is the main source of broken drawings.

**1. Find the symbols.** The bundled libraries hold ~950 macros.
`fcai lib` prints `key<TAB>category<TAB>name`, one per line:

```bash
fcai lib | grep -i resistor
fcai lib | grep -i -E 'npn|transistor'
fcai lib --json                    # key, name, category, library
```

Standard-library macros use bare keys (`080`, `280`). Every other library uses
a lowercase `prefix.key` (`ihram.ma20`, `pcb.dil08`). Always copy the key
exactly from `fcai lib`; an unknown key does not fail anything, it just
leaves a missing component.

**2. Place each part and read where its pins land.** Expand a single `MC` line
at the position and orientation you intend:

```bash
echo 'MC 60 30 1 0 080' | fcai expand -
# LI 60 30 60 32 0      <- lead: pin at (60,30)
# LI 60 38 60 40 0      <- lead: pin at (60,40)
# RV 62 32 58 38 0      <- body
```

The output is the symbol's geometry in drawing coordinates, rotation and
mirroring already applied. The pins are the **free outer ends of the leads**
(plus any `SA`/`PA` points). Route wires to those numbers. `MC x y o m key`:
`o` = rotation 0–3 in 90° steps, `m` = mirror 0/1.

To inspect a symbol in its own frame, expand it at the anchor:
`echo 'MC 100 100 0 0 280' | fcai expand -` — coordinates minus 100 are the
offsets from the `MC` point.

**3. Wire it.** A wire is a plain `LI` on layer 0 between two pin coordinates.
Keep runs orthogonal: go horizontal then vertical, never diagonal. Where three
or more wires meet, add `SA x y 0` — without it the crossing reads as two
unconnected wires.

**4. Validate, then look at it.**

```bash
fcai validate circuit.fcd              # round-trip + diagnostics; non-zero on failure
fcai context circuit.fcd | head -12    # check: "(N unresolved)", diagnostics
fcai render circuit.fcd -o /tmp/c.png
```

`validate` exits 0 even when a macro is **unresolved** — that is only a
warning. Check `context` for `UNRESOLVED` / `unresolved_macros`. Always render
and actually look at the result before calling a drawing finished — a file
can be perfectly valid and still be a nonsense schematic.

## The rules that bite

- **Labels attach to the previous primitive.** Up to two `TY` lines directly
  after `MC`, `SA`, `PL` or `PA` become that primitive's *name* and *value* —
  no `FCJ` needed. So a free-standing text line must **not** sit right after
  one of those; put it after an `LI`, or at the top of the file. (One stray
  `TY` after an `MC` becomes its name, and `emit` then writes an empty value.)
  `LI`/`BE`/`RV`/`EV`/`PV`/`CV` take labels only when their own `FCJ` line
  ends in `1` (`FCJ 0 0 3 1 0 1`).
- **Coordinates are non-negative.** Negatives are clamped to `0` on read,
  silently and with no diagnostic. Origin is top-left, y grows *down*.
- **Only `U+0020` separates tokens.** A tab silently breaks the line.
- **`MC x y` puts the macro's anchor there.** Inside a macro body the anchor is
  `(100,100)`. `expand` does the transform for you.
- **Layer 0 is the schematic layer.** 1 = bottom copper, 2 = top copper,
  3 = silkscreen. Use `PL` (track) and `PA` (pad) for PCB work, `LI` for
  schematic wires.
- Snap everything to integers on a 5-unit grid. Fractional coordinates are a
  FidoCadJS extension that stock FidoCadJ cannot read.

## Editing an existing drawing

Append or edit lines directly — the format is line-oriented and order is
document order. `fcai emit file.fcd -o file.fcd` rewrites it in canonical
form in place (legacy `TE` → `TY`, number formatting, redundant `FCJ` markers
dropped). Add `--no-extensions` for legacy FidoCAD output without
`FCJ`/`FJC`, at the cost of arrows, dashes and line labels. Re-run
`validate` after any hand edit.

## Libraries

`fcai` loads the bundled standard libraries automatically (`--no-stdlib` to
skip). Extra libraries:

```bash
fcai --lib my_parts.fcl lib             # keys become my_parts.<key>
fcai --prefix foo --lib my_parts.fcl …  # keys become foo.<key>
fcai --lib-dir ~/fcl …                  # every *.fcl; also FIDO_LIB_DIR
```

The prefix is the lowercased file stem. Put the same `--lib`/`--lib-dir`
options on every command (`context`, `render`, `expand`) that touches a
drawing using those macros.

## Reference

- `references/format.md` — the complete FCD/FCL specification: every
  instruction code, argument shape, the `FCJ`/`FJC` extensions, macro library
  syntax, and the writer's serialization rules. Read it when you need a
  primitive this file does not cover.
- `references/authoring.md` — drawing conventions: grid sizing, symbol
  geometry, the standard-library quick reference, routing, text styling,
  arrows and dashes, PCB layers, and a worked example.
- `fcai help` — every command, flag, environment variable and config key.
