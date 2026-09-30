# 5-bit capacitive SAR ADC in FidoCad — iteration report

Workspace: `playground/cap-adc` · session `/tmp/cap-adc.session` (fcai-gui)
Artifacts: `cap_adc.fcd` (83 primitives), `~/Documents/FidoLib/adc.fcl` (2 macros)
State: `fcai validate` round-trips, 0 diagnostics, layers 0 (circuit) + 1 (logic nets)

## 1. What was built

Single-ended charge-redistribution SAR ADC:

- `Vin` → sample switch (CLK) → top-plate rail shared by the capacitor array.
- CDAC: `16C, 8C, 4C, 2C, C` (b4…b0) + a dummy `C` (termination, grounded) = `32C`, LSB = Vref/32.
- Each bottom plate switches between the `Vref` bus and the `GND` bus via a FidoCad `880` changeover switch.
- Comparator (`580`): `+` on the top plate, `−` to ground; output feeds the SAR logic.
- SAR block: COMP input, CLK + START, bit-trial outputs `b4…b0` (dashed, blue layer 1) back to the switches, result `D4…D0`.
- Reuse: `adc.cell` (cap + SPDT + bus stubs + control lead) placed five times, `adc.ssw` (sample switch) — one library edit updates every instance.

## 2. Iteration log

### Round 0 — recon (turn 1, before any drawing)
Read the skill, stdlib and the pre-existing `adc.fcl`; learned the macro geometry by writing a
four-orientation test drawing and expanding it (`170`, `880`, `580`, `000`, `045`); read the layer
palette and the `MC` syntax. Chose the architecture and the cell macro approach. No drawing yet.

### Round 1 — window first ("show how you draw I want the interactive window")
Wrote the library (`adc.ssw`, `adc.cell`), a stub drawing, launched `fcai gui --session-dir`, then
built in visible stages, validating and rendering each one:

1. Vin + sample switch + top rail + comparator + rails/terminals
2. six capacitors + switches + Vref/GND buses
3. SAR block + COMP feedback + D/CLK/START
4. polish: logic nets on layer 1 (blue), bit labels at the block, `Ctot`/LSB note

### Round 2 — your GUI edits
You moved the title block `(40,92/104/114) → (210,72/84/94)` and pulled the comparator feedback wire
from `x=560` to `x=520`; you also fixed the `Vin` terminal yourself: `MC 60 140 0 0 000` →
`MC 65 140 2 0 000` (rotation 180°, as in your first review point).

My failure: six `send` events sat unread in `events.jsonl` because I never ran `session wait` in a
loop (my only earlier attempt used `timeout`, which does not exist on macOS). Nothing told me events
were pending. I drained them, read the diffs and acknowledged.

### Round 3 — five-point review

| # | Your point | Action |
|---|------------|--------|
| 1 | terminals misplaced; input terminal needs 180° | kept your `Vin` fix (`65 140 2 0 000`) |
| 2 | inputs: wire leaves on the right; outputs: wire enters from the left | `Vref` → `MC 120 160 3 0 000` (circle above, wire leaves downward) |
| 3 | digital pins have no terminals | removed the `CLK`/`START` terminals at the time (later corrected — see round 6) |
| 4 | corners must not have connection dots | removed `SA 365 172`, `SA 435 184` (the only two corner dots) |
| 5 | `Cd` not vertically aligned in the circuit branch | could not reproduce: pixel-measured all six caps (plates + leads centred on their branch, identical); fixed instead the real inconsistency I found — the six name/value label pairs were hand-offset, now centred by one rule |

### Round 4 — "did you update the drawing, I did not see any update"
Diagnosis: the window held unsaved in-memory edits (an undo of the terminal rotation: memory had
`MC 65 140 0 0 000`, disk had `2 0 000`). `gui/window.cpp` refuses disk reloads while `modified()`
is true and only reports *"file changed on disk, but you have unsaved edits — kept yours"* to the
bottom bar (hidden by default) — so all four of my edits were silently dropped.

Fix: `fcai session push` the reviewed file; verified the window document ≡ file with `session get`
+ `fcai diff` ("no changes").

### Round 5 — corner/dot practice
I had extended the buses a few units past the outer taps so the outermost branches became T-taps
with dots (following `examples/cdac6_binary.fcd`). You corrected this: **corners are fine; do not
extend a wire that is not meant to continue — just do not put a dot there.** Reverted:
GND bus `120→435` (corner at `Cd`), Vref bus `120→365` (corner at C0's stub), both without dots.
Dots remain only on true T-junctions.

### Round 6 — CLK/START terminals
I had misread point 3. Added them back as **input** terminals:
`MC 150 345 1 0 000`, `MC 210 345 1 0 000` (orientation 1: circle below the pin, wire leaves upward
into the block), labels at `(158,346)` / `(218,346)`. `D4…D0` stay as plain wires.

### Round 7 — sample-switch clock
Added the missing terminal on the sample switch's control lead: `MC 88 126 0 0 000`
(circle on the outer right end, wire enters from the lever), label moved to `(96,123)`.

### Round 8 — dashed bit-control lines
`FCJ 0 0 3 2 1 0` (dash style 1 = `[5,5]`) after the control lead **in the `adc.cell` macro**, so one
change covers `b4…b0` everywhere. The drawing file itself is unchanged. Since the GUI loads
libraries at startup, I restarted `fcai-gui` on the same session dir (the session files are
truncated on start) so the window shows the dashed lines.

## 3. Final state

```
[FIDOCAD] … 83 primitives, bbox [42,72]–[520,351]
  LI=17  RV=1  SA=21  MC=15  TY=29   layers 0,1   diagnostics: none
```

Library `~/Documents/FidoLib/adc.fcl`:

```
[ssw  Sample switch with control]
LI 100 100 103 100 / LI 107 100 110 100
LI 103 100 108 94  / LI 108 94 108 86 / LI 108 86 118 86

[cell 5-bit CDAC bit cell (C + SPDT to Vref/GND)]
MC 100 100 1 0 170        vertical capacitor at the rail
MC 100 110 1 0 880        SPDT, common at the cap bottom
LI 85 115 85 132 0        throw → Vref bus
LI 100 120 100 144 0      throw → GND bus
LI 93 113 93 215 1        control lead to the SAR block
FCJ 0 0 3 2 1 0           dashed
```

## 4. How I would like fcai to work (agent feedback)

Ordered by how much time each issue cost this session.

### 4.1 Session protocol

1. **Pending-event visibility.** An agent turn ending leaves `send` events queued with no signal
   anywhere. I lost six of them. Add `fcai session status DIR` →
   `{"pending":6,"oldest":"send","last_send":"…"}`, and/or a `pending` count in every `state`.
2. **Incremental diffs.** `send.diff` is cumulative since the window opened, so each new event
   repeats everything; reacting means manually diffing event *n* against *n−1*. Add a monotonic
   `seq` to every event and a `diff_prev` (changes since the previous event).
3. **Conflicts must be visible to the agent.** When the watcher refuses to reload because the GUI
   has unsaved edits, that is silently dropped today (round 4). Emit a `conflict` event, or add
   `"disk_changed":true,"stale":true` to `state`/`send`, so the agent knows its disk edits were
   lost and can `push` deliberately.
4. **`session save` and `session reload`,** and make `push` answer with a `state` (or `applied`)
   event. I verified pushes by hand (`get` + `diff`) every time.
5. **`session get` must not be blocked by queued sends.** It timed out (empty stdout) while the
   response state event was appended anyway; correlate responses with an id and return the state
   even with pending events.
6. **Do not truncate the session history on GUI start** (or rotate it): restarting the window to
   reload a library wiped the conversation history.

### 4.2 Library and macro introspection

7. **Macro geometry dump:** `fcai macro KEY` (or `lib --json --geometry`) listing primitives with
   anchor-relative *connection points/pins*. I derived the orientation mapping `(dx,dy)→(−dy,dx)`
   and the mirror axis by writing a four-orientation test file and expanding it — that should be
   one command, and the mapping belongs in the skill too.
8. **Reload libraries at runtime.** `FIDO_USER_LIB_DIR` / `--lib` files are read once at startup;
   the dash change forced a window restart. Watch those files, or add `session reload-libs`.

### 4.3 Structured editing (the CLI is supposed to be the AI interface)

9. **`fcai edit` verbs** instead of text patching: `add/rm/move/set` primitives with `--layer`,
   `--dash`, `--attach-label`; a `move` on an `MC` must carry its `FCJ` + two `TY` labels (and
   optionally attached stubs) the way the GUI does. Hand-editing coordinates in the `.fcd` works
   but is exactly how label offsets drift.
10. **`fcai hit X Y`** (core already has `fc_hit_test`) and **`fcai find --near/--bbox`** to get
    primitive indices for targeted edits.
11. **Connectivity query** (`fcai net` / `fcai connect`): endpoints, junctions, dangling ends,
    which wires meet a rail mid-span. This is the data a lint pass needs (below) and it also lets
    the agent reason about "is this dot correct" itself.

### 4.4 Style lint — the highest-value addition

12. `fcai lint` (or `validate --style`) with the rules you reviewed by hand:
    - `SA` on an L-corner (two wires sharing one endpoint) — warn;
    - wire endpoint on another wire's mid-span without `SA`, or `SA` where nothing junctions;
    - dangling wire ends / overhangs past the last tap with no label, terminal or pin;
    - terminal macros whose pin end is not on a wire endpoint (misplaced terminals), wires starting
      inside a terminal circle;
    - label text overlapping wires, plates or pins (clearance) — this alone would have caught the
      `Vref`/`b4` crowding before you did;
    - collinear overlapping segments (my `Vin` wire overlapped the terminal's own lead by a unit).
    With even the corner/dot/dangling subset I could self-review to your standard before asking.

### 4.5 Rendering and inspection

13. **`fcai render --crop x0,y0,x1,y1` / `--focus`** — I cropped PNGs with `sips` and pixel-measured
    PPMs by hand to verify centring and clearances.
14. **Scale-aware dashes:** `dash_on()` counts pixels (`[5,5]` in px), while spec §3.2 documents
    unit patterns — so a dashed line looks dotted at low zoom and the editor/PNG disagree. Make the
    pattern scale-aware so what you review is what ships.

### 4.6 Docs and skill

15. **Put the reviewed conventions in `share/skills/fidocad/SKILL.md`**: terminal side rules
    (input: circle on the outside, wire toward the circuit; output: wire entering the pin), corner
    vs junction-dot policy (never a dot on a corner, never extend a wire that ends), no rail
    overhangs, label placement/centring, logic-net layer convention, and the `(termination)` dummy
    annotation. These are exactly the points you had to re-teach me; a first draft would pass review.
16. **Point the skill at `examples/*.fcd` as canonical style references.** I only found
    `examples/cdac6_binary.fcd` mid-review; it resolved the termination-cap convention instantly
    (though its tiny rail overhangs are the opposite of your stated practice — worth fixing or
    annotating in the example).
