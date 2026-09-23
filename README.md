# FidoCadAI

A **dependency-free C11** interpreter for the [FidoCad](https://github.com/DarwinNE/FidoCadJ) drawing language (`.fcd` / `.fcl`). Built to be fast, embeddable, and **AI-centric**: every setting is a flag, env var, or config key, and the CLI speaks both human text and JSON so agents can read a drawing without a GUI.

Reference implementations: `~/Projects/FidoCadJS` (TypeScript) and `~/Projects/FidoCadJ` (Java). Language spec: `FidoCadJS/FIDOSPECS.md`.

## Status (v0.1)

| Piece | State |
|-------|--------|
| Parser (all primitives, FCJ/FJC, TY name/value) | done |
| Canonical emitter + round-trip validate | done |
| Macro libraries + expansion | done |
| Agent context / JSON / describe | done |
| Software rasterizer → PNG/PPM | done |
| `$...$` LaTeX in TY (no MathJax) | done |
| CLI (`fcai`) | done |
| GUI editor | not yet |

Core parser/emitter is still libc + libm. Rendering vendors [stb](https://github.com/nothings/stb) (`stb_truetype`, `stb_image_write`) and Liberation fonts (`share/fonts/`) so TY text matches real Sans / Serif / Mono faces. See `THIRD_PARTY.md`.

## Build

```
make            # build/fcai
make test
make sanitize   # ASan + UBSan
make install PREFIX=/usr/local
```

Standard libraries ship in `share/lib/` (English FidoCadJ bundles). `fcai` finds them next to the binary, via `FIDO_STDLIB_DIR`, or under `$PREFIX/share/fidocad/lib`.

## CLI for agents

```
fcai context drawing.fcd          # summary + JSON (default command)
fcai describe -v drawing.fcd      # primitive-by-primitive
fcai json drawing.fcd             # machine-readable dump
fcai validate drawing.fcd         # round-trip + diagnostics
fcai emit drawing.fcd -o out.fcd  # canonical rewrite
fcai expand drawing.fcd           # macros → primitives
fcai render drawing.fcd -o out.png
fcai lib                          # list loaded macros
fcai config                       # dump effective settings
```

Stdin is accepted (`fcai json -` or pipe). Exit status is 0 on success.

### Settings (all overridable by an agent)

Flags, `key = value` config (`--config file`), and environment:

| Key | Flag / env | Default |
|-----|------------|---------|
| `stdlib` | `--stdlib` / `--no-stdlib` / `FIDO_NO_STDLIB` | on |
| `lib_dir` | `--lib-dir` / `FIDO_LIB_DIR` | — |
| `extensions` | `--extensions` / `--no-extensions` | on |
| `scale` | `--scale` / `FIDO_SCALE` | auto |
| `margin` | `--margin` / `FIDO_MARGIN` | 12 |
| `width`/`height` | `--width` `--height` | 1200×900 |
| `grid` | `--grid` | off |
| `ppm` | `--ppm` | off (PNG) |
| `bg` | `--bg RRGGBB` | `FFFFFF` |
| `json` | `--json` / `FIDO_JSON` | off |
| `verbose` | `-v` | off |

`--lib PATH` (repeatable) plus `--prefix NAME` loads extra `.fcl` files. Personal libraries: `FIDO_LIB_DIR=/home/dante/Drive/Resources/FidoLibs`.

## Library (embed)

```c
#include "fidocad.h"

FcDrawing d;
FcLib *lib = fc_lib_new();
fc_drawing_init(&d);
fc_lib_load_stdlib(lib, argv[0]);
fc_parse_file(&d, "board.fcd");
char *ctx = fc_context(&d, lib);
fputs(ctx, stdout);
fc_render_file(&d, lib, NULL, "board.png");
```

See `include/fidocad.h` for the data model (`FcPrim`, layers, config).

## Layout

```
include/fidocad.h   public API
src/                parser, emitter, libs, rasterizer, CLI
share/lib/          bundled .fcl
tests/              fixtures + unit tests
```

## Next

- GUI (X11/Wayland) sharing the same config surface
- In-place edit commands (`fcai add` / `move` / `set`) for agents
- Hit-testing / netlist-ish queries
