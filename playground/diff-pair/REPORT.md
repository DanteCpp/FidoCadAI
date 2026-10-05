# PMOS differential pair, 130 nm — design and revision report

**Deliverable:** `playground/diff-pair/pmos_diffpair_130nm.fcd`
**Built with:** `fcai` + live `fcai-gui` session (`/tmp/diffpair.session`)
**Initial request:** draw a PMOS differential pair with current-mirror biasing and source
degeneration, sized for a 130 nm technology node.

---

## 1. Circuit and sizing (unchanged through every layout revision)

Topology: PMOS input pair (M1/M2) with source degeneration (Rs1/Rs2), PMOS tail current
source (M3) biased by a diode-connected PMOS reference (M4 + Rb), and an NMOS mirror load
(M5 diode-connected, M6 output).

Assumptions: VDD = 1.2 V core devices, |Vth| ≈ 0.4 V, µpCox ≈ 110 µA/V²,
µnCox ≈ 390 µA/V², L = 0.18 µm on every device.

| Device | W/L (µm) | I (µA) | Vov (V) | gm (mS) |
|---|---|---|---|---|
| M1, M2 (PMOS input) | 8/0.18 | 100 | 0.20 | 0.99 |
| M3 (PMOS tail, 2× M4) | 16/0.18 | 200 | 0.20 | — |
| M4 (PMOS bias diode) | 8/0.18 | 100 | 0.20 | — |
| M5, M6 (NMOS mirror load) | 4/0.18 | 100 | 0.15 | 1.3 |

Derived values: Rb = (1.2 − 0.60)/100 µA = 6 kΩ → Vbias = 0.60 V, I_ref ≈ 100 µA,
I_tail ≈ 200 µA. Degeneration Rs = 500 Ω → 50 mV drop, gm,eff = gm/(1+gm·Rs) ≈ 0.66 mS
(1.5× degeneration). Headroom: ICMR ≈ 0.15–0.45 V (nominal Vcm = 0.30 V, M3 saturation
verified at |VDS3| ≈ 0.35 V > Vov = 0.20 V); output swing ≈ 0.15–0.65 V; Av ≈ 30 dB
(λ ≈ 0.1 V⁻¹); input-referred thermal noise ≈ 6 nV/√Hz.

The sizing was fixed in the first draft and was **never changed** by any later feedback —
all revisions were symbolic and geometric.

---

## 2. Library assets created or adopted

| Key | File | Shape | Why |
|---|---|---|---|
| `mos.pmos`, `mos.nmos` | `~/Documents/FidoLib/mos.fcl` (**created**) | 3-terminal MOSFET, gate lead + gate bar + channel bar + source arrow, **no envelope circle**; same pin geometry as the stock symbols (pins at ±15/±10 from the anchor) | user asked for "no shield" transistors |
| `power.vdd` | `~/Documents/FidoLib/power.fcl` (**created**, then trimmed) | stem + horizontal bar, 10-unit stem | microelectronics VDD rail marker; no bundled library has one |
| `power.vss` | same file | down triangle | **created, then deleted** — superseded by the EY library |
| `ey_libraries.refpnt0` | bundled EY library | filled down-pointing triangle ("Ground") | user pointed out the ground symbol already exists in the EY library |
| `080`, `000` | bundled standard library | resistor, terminal circle | unchanged throughout |

Rejected/considered and not used: `power.vss` (custom, deleted), `ey_libraries.refpnt5`
(“Electrical ground”, three slanted bars), `ey_libraries.refpnt4` (“Electrical earth”),
stdlib `045` (hatched ground), `ey_libraries.genpws0/1` (power source/sink boxes),
stdlib `470` (Vcc generator circle).

---

## 3. Revision log (every feedback round)

### D1 — initial drawing
Stock `420`/`410` symbols (with envelope circles), bias branch at the far left with an
L-shaped `Vbias` route (80,70)→(85,70)→(85,80)→(135,80), long rails (VDD 60→155,
GND 60→190), one long reference column (Rb directly under M4, then 110 units of wire),
labels parked in the left margin at x = 58.

### F1 — “use transistor symbols with no shield; there are long stretches of wires and the components are not well distributed, they are too close to each other compared to the empty space”
* Created `mos.fcl` (naked PMOS/NMOS) and re-laid out the whole drawing:
  * `Vbias` became **one straight horizontal segment** between M4's and M3's gate tips;
  * rails trimmed to their taps (+5 units), both starting at the same x;
  * rows evenly spaced (~20 units between tail source/drain → tail node → Rs → pair →
    mirror → ground), branches widened 70 → **90** units;
  * input terminals attached directly at the gate tips (no stub wires), Vout straight
    off M6/M2's drain node.

### F2 — “use the triangle downfacing version of the ground symbol, and also the standard Vdd and Vss symbols used in microelectronics”
* Created `power.fcl` with `power.vdd` (bar) and `power.vss` (down triangle); replaced the
  stdlib `045` earth symbol and the plain `$V_{DD}$` text.

### F3 — “there should be a symbol in the library for the ground I think it is in the EY library”
* Found and adopted **`ey_libraries.refpnt0`**; deleted `power.vss`; `power.fcl` keeps only
  the VDD bar (no bundled equivalent). Bar trimmed to 10×5 to match the EY marker's weight.

### F4 — four points
1. *“Vss and Vdd symbols should have the same wire length”* → both are now exactly **10 units**
   from the rail (VDD stem lengthened to 10; VSS = 8-unit wire + the EY triangle's 2-unit stem).
2. *“Rb is not centered vertically in the wire, it is very close to the connection”* → Rb moved
   to (90, 125)–(90, 135): **55 units of wire above, 55 below**.
3. *“the current mirror is wider than the diff pair … reduce the width of the diff pair to the one of the mirror”* → **first attempt was wrong**: I mirrored M1/M2 inward (M1 mirror 0,
   M2 mirror 1) and put the terminals above the gate tips. The pair's bodies then aligned over
   M5/M6, but the user rejected the mirroring (F5).
4. *“still the labels and values are not close enough to the corresponding device”* → all
   labels pulled in to 1–6 units of their symbol (M4 (70,52), M3 (164,50), Rs1 (112,98),
   Rs2 (199,98), Rb (76,128), M1/M2 (102/200,138), M5/M6 (127/175,172)).

### F5 — two points
1. *“you did not change the width of the Diffpair (i.e. the distance between M1 and M2)”* →
   the real fix: the **branch columns themselves** were moved from x = 115/205 to
   **x = 125/195** (90 → 70 units apart). Every measure shrank by 20 units: gate tips
   110/210, bodies 120/200, pins 125/195. The mirror, Rs, tail line and drain wires moved
   with the branches, so all vertical connections stay straight.
2. *“you should not have mirrored M1 and M2, they were fine in the previous position, same for their gate terminals”* → reverted to `M1` mirror 1 / `M2` mirror 0 with the terminals
   attached directly at the outward-facing gate tips (Vin+ at (110,120), Vin− at (210,119)).

### F6 — “this is how I want the labels do you see”
The user edited the labels directly in the GUI and pressed Return (a `send` event with 9
modified primitives). Their layout was adopted as-is; I acknowledged it with
`fcai session ack` and verified the GUI had saved it. Changes:

| primitive | before | after (user's placement) |
|---|---|---|
| M1 name/value | (102, 138)/(102, 143) | **(129, 116)/(129, 120)** — right of the symbol |
| M2 + name/value | (210, 120) · (200, 138)/(200, 143) | **(210, 119)** · **(177, 114)/(177, 119)** — left of the symbol |
| Vin− terminal + label | (210, 120) · (206, 112) | (210, 119) · (206, 111) |
| Vout wire + junction dot | y = 130 | y = 137 |
| Vout terminal + label | (225, 130) · (222, 140) | (225, 137) · **(224, 141)** — hugging the marker |
| M5 name/value | (127, 172)/(127, 177) | **(105, 155)/(105, 160)** — left of the symbol |
| M6 name/value | (175, 172)/(175, 177) | **(202, 155)/(202, 159)** — right of the symbol |

Resulting rule, now used everywhere: **name/value sit beside the symbol at its mid-height
(never above/below), 2–4 units of clearance; terminal labels hug their marker.**

### F7 / F8 — GUI reopen requests
The window was reopened twice (`fcai gui pmos_diffpair_130nm.fcd --session-dir
/tmp/diffpair.session`); both times the old session log was archived automatically under
`/tmp/diffpair.session/history/` and the canvas verified identical to the file on disk.
Current session: pid 40507, `session_id 008a957f-68e8-4df4-9375-2576bf722f8c`.

---

## 4. Final layout map (coordinates)

```
VDD rail        LI 85 40 → 165 40           taps: 90 (M4), 125 (power.vdd), 160 (M3)
bias branch     M4 (105,60) m0, diode wire (105,60)-(105,70)-(90,70)
                Rb (90,125) m1 = 080, wire above (90,70)-(90,125), below (90,135)-(90,190)
                Vbias wire (105,60)-(145,60), note "$V_{bias}$ = 0.60 V" at (108,72)
tail            M3 (145,60) m1; source (160,50)-(160,40); drain (160,70)-(160,90)
tail node       LI 125 90 → 195 90          dot at (160,90)
degeneration    stubs (125,90)-(125,100) and (195,90)-(195,100); Rs1/Rs2 = 080 at (125,100)/(195,100)
input pair      M1 (110,120) m1 → pins x=125; Vin+ = 000 m1 at (110,120), circle (104..108, 118..122)
                M2 (210,119) m0 → pins x=195; Vin− = 000 m0 at (210,119), circle (212..216, 117..121)
                (branch columns 125 / 195 = 70 units apart)
mirror load     drain wires (125,130)-(125,150) and (195,129)-(195,150)
                diode link (125,150)-(160,150)-(160,160), dots (125,150) and (160,160)
                M5 (140,160) m1, M6 (180,160) m0; tie (140,160)-(180,160)
                sources (125,170)-(125,190), (195,170)-(195,190)
ground rail     LI 85 190 → 200 190         dots 90, 125, 160, 195
VSS              wire (160,190)-(160,198) + ey_libraries.refpnt0 at (160,198), label (167,201)
output          LI 195 137 → 225 137, dot (195,137), terminal 000 at (225,137), label (224,141)
notes block     (70,214)/(70,221)/(70,228)
```

Label positions in the final file: M4 (70,52), M3 (164,50), Rb (76,128), Rs1 (112,98),
Rs2 (199,98), M1 (129,116), M2 (177,114), M5 (105,155), M6 (202,155), Vin+ (98,112),
Vin− (206,111), Vout (224,141); the three supply/value notes at x = 70.

---

## 5. Verification performed

* `fcai validate` — round-trip idempotent at every push (final: 55 primitives, 1945 bytes).
* `fcai lint --json` — no findings (junction dots, corners, overlaps) in any accepted revision.
* `fcai diff` — each `send`/push reviewed primitive-by-primitive before adopting.
* `fcai session push --wait` / `save --wait` — every accepted revision confirmed applied
   (`ok: true`); `session get --wait` verified canvas == file on disk after each round.
* `fcai render` (full view plus crops of bias, pair, mirror, terminals) inspected visually
   after every revision.

---

## 6. Open nits

* M2 was nudged up 1 unit (210,120 → 210,119) in the user's label pass, so its source pin
  sits at (195,109) while Rs2's bottom pin is at (195,110). The two drawn segments still
  overlap by 1 unit, so it reads and renders as connected — but it is a 1-unit misalignment.
  Snapping M2 back to (210,120) (and restoring the drain wire to y = 130) would make it
  exact if ever desired.
* `power.vss` was intentionally removed from `~/Documents/FidoLib/power.fcl`; the file now
  contains only the VDD bar.
* The notes line “gm_eff = 0.66 mS” and “Av ~ 30 dB” remain estimates (λ ≈ 0.1 V⁻¹);
  no simulation was run.
