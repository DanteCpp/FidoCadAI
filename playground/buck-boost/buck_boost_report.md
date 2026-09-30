# Buck-boost converter — session report

**Date:** 2026-09-30
**Workspace:** `playground/`
**Artifacts:** `buck_boost.fcd` (final drawing), `buck_boost.png` (preview render)
**Tooling:** `fcai` (parse/validate/render) and `fcai-gui` (live window at
`/tmp/buckboost.session`, file `/Users/dante/Projects/FidoCadAI/playground/buck_boost.fcd`)
**Origin:** user request — *“draw a buck-bust converter”* (i.e. buck-boost).

The user watched every step in the live GUI and gave nine rounds of
feedback. Each round is quoted below, followed by what was wrong, what was
changed, and the resulting drawing state. Quotes are from voice input,
lightly cleaned of filler words; wording is otherwise untouched.

---

## 1. Final result

Inverting buck-boost, one sheet, layer 0 (black circuit), blue title.

```
+VIN ──┬── Q1 ────┬── D1 ◀──┬──────┬──────┬── −VOUT
      CIN         L1        COUT   RL
GND ──┴───────────┴─────────┴──────┴──────┴── GND
```

| element | macro | position | notes |
|---------|-------|----------|-------|
| +VIN terminal | 010 | (30, 60) | mirrored, points left |
| CIN 100u | 170 | (60, 80) | vertical, plates y=84/86 |
| Q1 NMOS | 410 | (100, 75) | drain 90, source 110; gate pin 75 |
| PWM flag | 000 | (100, 75) | mounted directly on the gate pin, points down |
| L1 100u | 120 | (140, 80) | vertical, SW node at (140, 60) |
| D1 Schottky | 200 | (185, 60) | cathode tip 170, anode 185 |
| COUT 220u | 170 | (215, 80) | vertical |
| RL 1k | 080 | (245, 80) | vertical |
| −VOUT terminal | 020 | (275, 60) | points right |
| GND terminals | 000 | (30, 110), (275, 110) | return rail brought out, aligned with the top pair |
| Electrical ground | 045 | (152, 110) | centered on the return rail |
| junctions | SA | (60, 140, 215, 245) × (60, 110) | 8 dots |

**Geometry rules the drawing ended up following**

- rails y = 60 (power) and y = 110 (return) — 50 units apart;
- vertical component leads = 20 units each (components are 10 tall);
- **every horizontal segment = 30 units**: terminal leads, series-component
  leads, and branch gaps alike;
- series components centered in their span: Q1 drain 90 / source 110 is
  centered between junctions 60 and 140; D1 (170–185) is centered between
  junctions 140 and 215;
- no dangling rail ends anywhere;
- labels are attached properties, not loose text.

**Verification**

```
$ fcai validate buck_boost.fcd
round-trip ok (1295 bytes, 36 primitives)
```

36 primitives: LI=14, MC=12, SA=8, TY=2 (only the title and the formula are
standalone text). `fcai emit` is idempotent (emit → emit byte-identical);
the file on disk is exactly the emitter's canonical output. The GUI document
reported `saved: true`, `modified: false`, identical to disk.

---

## 2. Feedback log

### 2.1 “Can you see what I have selected?”

Not a drawing complaint but part of the conversation: the user selected
two primitives in the GUI and asked whether the agent could see it. Answer
came from `fcai session get` → `state` event, field `selection: [6, 7]`:
the two vertical wires of the CIN branch (index 6 = `LI 60 60 60 110`,
index 7 = `LI 60 120 60 180`). The CIN macro body itself (index 17) was
*not* selected — noted to the user.

### 2.2 Too much wire around the components

> **“Those lines are too long compared with the size of the components.”**

The first draft had the rails 120 units apart; every shunt component
(10 units long) sat between two 50-unit leads. Fixed by compressing the
whole vertical span: rails 120 → 50 apart, every component lead 50 → 20
units, gate wire shortened accordingly, formula moved up. Same for all
four branches (CIN, L1, COUT, RL), not just the selected one, so the
drawing stayed consistent.

### 2.3 The transistor gate lead

> **“Also the gate of the transistor is too long.”**

The PWM flag hung off a 20-unit wire below the gate pin. Removed the wire
entirely and mounted the flag **directly on the gate pin** (`MC 100 75 1 0 000`);
the only remaining gate lead is the 9 units inside the NMOS macro itself.
Rule: control/flag leads are attached at the pin, never stretched with an
extra segment.

### 2.4 Component labels must be properties

> **“You are not using the properties of the components to name them and
> give values — that is not a good practice.”**

The first draft used free-floating `TY` primitives near each component.
All 15 labels were converted to attached name/value properties: after each
`MC`, a bare `FCJ` line followed by up to two `TY` lines (name, value).
Effects:

- primitive count dropped 48 → 33 (labels are no longer primitives);
- `fcai json` reports `name`/`value` on each macro;
- the GUI property form shows them in the Name/Value fields;
- `fc_prim_translate` moves the labels together with the component, so a
  drag no longer leaves orphan text behind;
- the file is saved in the emitter's canonical form, including the empty
  value slot for name-only terminals (`+VIN`, `PWM`, `−VOUT`, `GND`).

### 2.5 Quadrupole / return-rail terminals

> **“You drew the circuit as a quadrupole, but you didn't add terminals to
> the lower rail, the ground essentially. That is not a good practice. You
> either draw the ground as a closed loop circuit without the extensions,
> or you draw a proper quadrupole with terminals aligned with the upper
> rail as well.”**

The return rail ended in empty space on both sides while the top rail
carried `+VIN` and `−VOUT` terminals. Chosen fix: proper quadrupole — the
bottom rail was extended and two `000` terminals named **GND** were added
at x = 30 and x = 275, exactly aligned under the top terminals; the
electrical-ground symbol stays mid-rail as the reference marker. (The
alternative — trimming the rail to the outermost junctions so it is a
closed loop with no overhang — was offered and not taken.)

### 2.6 Label alignment

> **“Make sure that the labels are well aligned with the terminals or the
> components. For example, the output terminals don't have the labels well
> aligned. I like where you position the components labels and names, and
> the input labels.”**

Two defects on the output side: `−VOUT` sat at y = 46 while `+VIN` sat at
y = 50, and the right labels were tucked inward with a gap to their flags.
Rule adopted: the input labels share one left edge (x = 18, i.e. 12 left
of the pin) and sit 10 units above their wire; output labels mirror that
exactly — anchored at their pin, extending outward, right edge on the
mirrored line. `−VOUT` → (275, 50) in the final layout, right `GND` →
(278, 100), both at the same heights as their input counterparts.
Component labels were not touched.

### 2.7 Title styling

> **“Can you make the tile bigger and blue.”** (“tile” read as “title”)

Title `TY 20 10 …` changed from size 5×4 (font 4) to **8×6 (font 6)** and
moved to layer **11**, whose colour is the bright blue `#0180ff` (layer 1
“Bottom copper” was darker navy; offered as an alternative). Note: text
colour in FidoCad comes from the layer, so a coloured title necessarily
lives on a coloured layer.

### 2.8 Center series components between junctions

> **“The diode and the transistor are not centered in between the two
> extremities of the loop. It's good practice to have the component in
> between, having the same length of wire between the two sides.”**

Measured before the fix:

| component | span | wire left / right |
|-----------|------|-------------------|
| Q1 | CIN (60) → SW (130) | 45 / 5 |
| D1 | SW (130) → COUT (220) | 15 / 60 |

Both were jammed against the switching node. Fixed by centering each
component in the span bounded by its two adjacent junctions: Q1 drain 85 /
source 105 (25/25), D1 cathode 168 / anode 183 (38/37 — the 15-long diode
cannot split a 90-unit span perfectly in integers).

### 2.9 Equal lead length across components

> **“Also the wires around D1 are longer than those around Q1.”**

Q1 had 25/25, D1 38/37. To match D1 to Q1, the COUT junction moved
220 → 195 (span 65 = 25 + 15 + 25), giving D1 **25/25**. RL was then
re-centered between COUT and `−VOUT` (270 → 250) so the output section was
also even (55/55).

### 2.10 Output terminal leads = input terminal leads

> **“Output terminals' length of wire is not the same as input length.”**

Measured: `+VIN` lead 30 (30→60) but the `−VOUT` lead 55 (250→305). Final
rule: **every horizontal segment is 30 units**, component leads included.
The input side (`+VIN` 30, CIN 60) was kept exactly as it was; everything
downstream moved to fit: Q1 (90/110), SW/L1 (140), D1 (170–185), COUT
(215), RL (245), `−VOUT` (275), ground symbol re-centered at 152.

---

## 3. Conventions distilled (candidate for the skill)

1. **Properties, never orphan labels.** Component name/value go in the
   `FCJ` + `TY` pair after the `MC`; terminals too (`+VIN`, `GND`, `PWM`
   are names, with an empty value slot). Loose `TY` is only for sheet text
   like the title and formula.
2. **Wires proportional to components.** A lead should not dwarf the part
   it connects (target ≈ 2× the component length); no long unused runs.
3. **No dangling rails.** Every rail end is a terminal or a junction. If
   the drawing presents ports on one rail, either bring the return rail out
   to terminals aligned with them (quadrupole) or close the loop by
   trimming the rail to the outermost junctions.
4. **Aligned terminals, mirrored labels.** Top/bottom terminals of a port
   share the same x. Input labels share an outer edge at a fixed offset
   above the wire; output labels mirror that placement.
5. **Centered series components.** Each series element sits halfway
   between its two adjacent junctions, with equal wire on both sides.
6. **Uniform wire segments.** Where possible every wire segment on a rail
   is the same length (30 units here), so nothing reads as an accident.
7. **Short control leads.** Flags/terminals attach at the pin; do not add
   an extra segment to place them.
8. **Styling.** Title at font 6 (8×6), colour through the layer (layer 11
   blue here); the formula stays plain text (the typeset `\frac` rendered
   cramped and was reverted).

## 4. Open items

- The vertical component leads are 20 units while horizontal segments are
  30. The user was offered a fully uniform grid (rails 70 apart) and did
  not take it up.
- The GUI session at `/tmp/buckboost.session` is still open and saved.
  Close it with Ctrl+Return in the window or `fcai session quit /tmp/buckboost.session`.

---

## Appendix — final `buck_boost.fcd`

```
[FIDOCAD]
TY 20 10 8 6 0 0 11 * Buck-boost converter (inverting)
TY 20 130 4 3 0 0 0 * Vout = -Vin * D / (1 - D)
LI 30 60 60 60 0
LI 60 60 90 60 0
LI 110 60 140 60 0
LI 140 60 170 60 0
LI 185 60 275 60 0
LI 60 60 60 80 0
LI 60 90 60 110 0
LI 140 60 140 80 0
LI 140 90 140 110 0
LI 215 60 215 80 0
LI 215 90 215 110 0
LI 245 60 245 80 0
LI 245 90 245 110 0
LI 30 110 275 110 0
MC 30 60 0 1 010
FCJ
TY 18 50 4 3 0 0 0 * +VIN
TY 18 50 4 3 0 0 0 *
MC 60 80 1 0 170
FCJ
TY 66 74 4 3 0 0 0 * CIN
TY 66 79 4 3 0 0 0 * 100u
MC 100 75 3 0 410
FCJ
TY 83 46 4 3 0 0 0 * Q1
TY 83 51 4 3 0 0 0 * NMOS
MC 140 80 1 0 120
FCJ
TY 146 72 4 3 0 0 0 * L1
TY 146 77 4 3 0 0 0 * 100u
MC 185 60 2 0 200
FCJ
TY 173 46 4 3 0 0 0 * D1
TY 173 51 4 3 0 0 0 * Schottky
MC 215 80 1 0 170
FCJ
TY 221 74 4 3 0 0 0 * COUT
TY 221 79 4 3 0 0 0 * 220u
MC 245 80 1 0 080
FCJ
TY 251 74 4 3 0 0 0 * RL
TY 251 79 4 3 0 0 0 * 1k
MC 100 75 1 0 000
FCJ
TY 104 77 4 3 0 0 0 * PWM
TY 104 77 4 3 0 0 0 *
MC 275 60 0 0 020
FCJ
TY 275 50 4 3 0 0 0 * -VOUT
TY 275 50 4 3 0 0 0 *
MC 152 110 0 0 045
MC 30 110 0 1 000
FCJ
TY 18 100 4 3 0 0 0 * GND
TY 18 100 4 3 0 0 0 *
MC 275 110 0 0 000
FCJ
TY 278 100 4 3 0 0 0 * GND
TY 278 100 4 3 0 0 0 *
SA 60 60 0
SA 140 60 0
SA 60 110 0
SA 140 110 0
SA 215 60 0
SA 215 110 0
SA 245 60 0
SA 245 110 0
```
