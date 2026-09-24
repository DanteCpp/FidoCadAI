# Authoring FidoCad drawings

Conventions and recipes for producing drawings that look right and that stock
FidoCadJ can open. For the formal grammar see `format.md`.

## The grid

One logical unit is 5 mils (0.127 mm). The library is built on a **5-unit
pitch**: symbol leads are 5 or 10 units long, DIL packages space their pins 5
units apart, and a standard 2-pin part such as the resistor `080` spans exactly
10 units pin to pin.

Consequences for layout:

- Place every component and route every wire on **multiples of 5**. The drawing
  then lines up with itself, and no coordinate is ever fractional.
- Leave 20–40 units between parallel component columns, and at least 10 units
  between a symbol and any text you put beside it.
- Coordinates cannot be negative, so start the drawing at a comfortable margin
  (x, y ≥ 20) rather than at the origin. Symbols extend *up* from their anchor
  as well as down — an `MC` at y = 5 with a symbol reaching `dy -10` will be
  clamped and deformed.

## Symbol geometry

Every macro body is drawn around the anchor `(100,100)`, which is the point an
`MC` line positions. `echo 'MC 100 100 0 0 <key>' | fcai expand -` prints the
body in its own frame (subtract 100 for the offset from the anchor), and
expanding the `MC` line you actually intend applies the rotation and
mirroring for you.

Rotation is `0`–`3` in 90° steps, applied about the anchor. With `orient = o`
and body offset `(u,v) = (bx-100, by-100)`, a pin lands at:

| `o` | mirror 0 | mirror 1 |
|-----|----------|----------|
| 0 | `(x+u, y+v)` | `(x-u, y+v)` |
| 1 | `(x-v, y+u)` | `(x+v, y+u)` |
| 2 | `(x-u, y-v)` | `(x+u, y-v)` |
| 3 | `(x+v, y-u)` | `(x-v, y-u)` |

Most two-terminal symbols are drawn horizontally with pin 1 *at* the anchor and
pin 2 ten units to the right. `--rot 1` turns them vertical, running downward
from the anchor.

### Standard library quick reference

Bare keys, no prefix. `fcai lib | grep -i <word>` finds the rest.

| Key | Symbol | Key | Symbol |
|-----|--------|-----|--------|
| `000` | terminal | `380` | SCR |
| `010` / `020` | terminal + / − | `390` / `400` | P / N channel JFET |
| `040` | earth | `410` / `420` | NMOS / PMOS |
| `045` | electrical ground | `470` / `480` | Vcc / Vac generator |
| `080` | resistor | `500` | transformer, 1 secondary |
| `090` | variable resistor | `540` | DC motor |
| `120` | inductance | `560` | three-phase motor |
| `170` | capacitor | `580` | operational amplifier |
| `180` | electrolytic capacitor | `590`–`620` | 6/8/10/12-pin DIL outline |
| `200` | diode | `690` / `700` | 2-input AND / NAND |
| `220` | LED | `710` / `720` | 2-input OR / NOR |
| `230` | zener diode | `730` / `740` | 2-input EXOR / EXNOR |
| `280` / `290` | NPN / PNP transistor | `750`–`770` | switches |

Other libraries, all lowercase `prefix.key`: `elettrotecnica.*` (IEC power and
installation symbols), `ey_libraries.*` (a second general set), `ihram.*`
(RF, op-amp blocks, tubes, 555), `pcb.*` (footprints: pads, DIL, SOIC, boards).

## Routing

A schematic wire is an ordinary line on layer 0:

```
LI <x1> <y1> <x2> <y2> 0
```

- Keep every segment **horizontal or vertical**. An L-shaped run is two `LI`
  lines sharing a corner point; do not emit a diagonal.
- Wires connect by *coincident coordinates*, so a wire must end exactly on the
  pin coordinate `fcai expand` reported. Off-by-one means an open circuit that
  still looks connected at a glance.
- At a T or cross junction of three or more wires, add a connection dot:

  ```
  SA <x> <y> 0
  ```

  Crossing wires **without** an `SA` are read as not connected — that is the
  convention, and how a reader distinguishes a crossover from a node.
- `fcai` has no netlist command. After routing, compare every `LI` endpoint in
  `fcai describe -v` against the pin list from `expand`, and render with
  `--grid` to eyeball the joins: a wire that misses its pin by one unit is an
  open circuit.

## Labels and text

A component's reference designator and value are the two `TY` lines directly
after its `MC` line (the same holds for `SA`, `PL` and `PA`):

```
MC 60 30 1 0 080
TY 66 34 4 3 0 0 0 * R1
TY 66 38 4 3 0 0 0 * 100k
```

Write both lines, even when one is empty. The reverse also holds: a `TY`
meant as free text must not directly follow one of those primitives, or it is
captured as a label. `LI`/`BE`/`RV`/`EV`/`PV`/`CV` capture labels only when
their `FCJ` line ends in `1`.

Free text is a `TY` that is not captured that way:

```
TY <x> <y> <sizeY> <sizeX> <orientation> <style> <layer> <font> <text...>
```

- Size is **Y before X**; `4 3` is the normal small label, `5 3` a heading.
  Keep the ratio near 10:7.
- `orientation` is degrees, counter-clockwise. `style` is a bitmask:
  `1` bold, `2` italic, `4` mirrored.
- `*` in the font field means the default `Courier New`; spaces inside a font
  name are written `++`.
- The text is every remaining token rejoined with single spaces, so embedded
  spaces need no escaping. `$...$` marks LaTeX math.

## Line style, arrows, fills

`RV`/`EV`/`PV` are outlines; `RP`/`EP`/`PP` are the filled counterparts.

A dash style or an arrow is attached with an `FCJ` line after the primitive:

```
LI 5 50 90 50 0
FCJ 2 0 3 1 0 0        arrows=2 (at end), style=0 (filled), len 3, halfwidth 1
RV 50 20 60 30 0
FCJ 2 0                dash style 2 (fine dotted), no label
```

Arrow bits: `1` = at start, `2` = at end, `3` = both. Arrow-style bits: `1` =
bar/limiter, `2` = open head. Dash styles are `0` solid, `1` dashed, `2` fine
dotted, `3` dotted, `4` dash-dot.

Note the two shapes of `FCJ`: `LI`, `BE`, `CV`/`CP` take the six-field arrow
form, while `RV`/`RP`, `EV`/`EP`, `PV`/`PP` take `FCJ <dash> <textFlag>`.

## PCB drawings

Copper and silkscreen live on their own layers: 1 = bottom copper, 2 = top
copper, 3 = silkscreen.

```
PL <x1> <y1> <x2> <y2> <width> <layer>          track
PA <x> <y> <rx> <ry> <hole> <style> <layer>     pad: style 0 round, 1 rect, 2 rounded
```

Footprints live in the `pcb.*` library; `fcai lib | grep '^pcb\.' | grep -i dil`
lists them.
Set a non-default track width once with `FJC A <w>` rather than repeating it.

## Global configuration

`FJC` lines go directly after `[FIDOCAD]` and are only emitted when a value
differs from its default:

```
FJC C 1.5              connection-dot diameter   (default 2.0)
FJC A 0.35             default line width        (default 0.5)
FJC B 0.25             circle/oval line width    (default 0.35)
FJC L 2 16711680 1     layer 2 colour (packed RGB) and alpha
FJC N 2 Top Copper     layer 2 display name
```

A drawing written without any `FCJ`/`FJC` lines is maximally compatible with
the original FidoCAD reader; `fcai --no-extensions emit in.fcd -o out.fcd`
produces that form, at the cost of dropping arrows, dashes and labels.

## Worked example

A biased NPN stage. Place the parts, read the pin coordinates, then route.

```bash
echo 'MC 60 30 1 0 080' | fcai expand -     # R1: leads end at (60,30) and (60,40)
echo 'MC 60 70 1 0 080' | fcai expand -     # R2: (60,70) and (60,80)
echo 'MC 100 45 0 0 280' | fcai expand -    # Q1: base (100,45), collector (115,35),
                                            #     emitter (115,55)
```

```
[FIDOCAD]
MC 60 30 1 0 080
TY 66 34 4 3 0 0 0 * R1
TY 66 38 4 3 0 0 0 * 100k
MC 60 70 1 0 080
TY 66 74 4 3 0 0 0 * R2
TY 66 78 4 3 0 0 0 * 22k
MC 100 45 0 0 280
TY 118 40 4 3 0 0 0 * Q1
TY 118 44 4 3 0 0 0 * BC547
LI 60 20 60 30 0
LI 60 40 60 70 0
LI 60 80 60 90 0
LI 60 45 100 45 0
SA 60 45 0
MC 60 90 0 0 040
MC 60 20 0 0 000
TY 40 14 4 3 0 0 0 * +Vcc
TY 40 18 4 3 0 0 0 *
```

The divider is one vertical run from the `+Vcc` terminal at `(60,20)` to earth
at `(60,90)`, broken by the two resistors; the base tap leaves the midpoint at
`(60,45)` and is marked with `SA`. `+Vcc` is the terminal's name label, so it
is followed by an empty value line. Confirm with:

```bash
fcai validate amp.fcd
fcai context amp.fcd | head -12      # 5 macros, none unresolved
fcai render amp.fcd -o /tmp/amp.png
```

## Finding pins by eye

FidoCad symbols do not declare their pins; they are the free ends of the
leads in the expanded geometry, plus any explicit `SA`/`PA` points. That is
unambiguous for most of the library, but symbols drawn at 45° (`210`, the
diode at 45 degrees) or with decorative spikes (`570`, antenna) have line ends
that are not pins. When unsure, render the lone part with `--grid` and a large
`--scale` and read the pin positions off the picture.
