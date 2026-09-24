# FidoCadAI — agent notes

C11 FidoCad interpreter. No third-party deps. Keep it that way.

## What this is

Parse / emit / query / render FidoCad `.fcd` drawings and `.fcl` libraries.
The CLI is the AI interface: JSON + `context` + PNG. GUI comes later and must
reuse `include/fidocad.h` — do not fork a second model.

Spec: `~/Projects/FidoCadJS/FIDOSPECS.md`. Behaviour should match FidoCadJS
`ParserActions` + primitive `toString`, not invent a dialect.

## Rules

- C11, POSIX, `-Wall -Wextra`. Link only `-lm`.
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
- `src/render.c` — software rasterizer, PNG/PPM
- `src/cli.c` — `fcai` commands and config
- `share/lib/` — English standard libraries
- `tests/test_core.c` — no framework, just `EXPECT`

## Checks after a change

```
make test
make sanitize   # after parser/emitter/rasterizer edits
```

If you touch the parser, add a fixture under `tests/fixtures/` and a round-trip assertion.

## Talking to this program

```
fcai context file.fcd     # what to read first
fcai json file.fcd
fcai render file.fcd -o /tmp/d.png
fcai lib                  # keys an MC may use
fcai config               # every knob currently in effect
```

Every knob is a CLI flag, env var, or `key = value` config entry. Do not hard-code user paths; use `FIDO_LIB_DIR` / `--lib-dir`.
