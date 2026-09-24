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

Builds on Linux and macOS (Apple clang or gcc, no extra packages); CI runs `make test` and `make sanitize` on both.

Standard libraries ship in `share/lib/` (English FidoCadJ bundles) and fonts in `share/fonts/`. `fcai` finds them via `FIDO_STDLIB_DIR` / `FIDO_FONT_DIR`, next to the binary (it resolves its own path through `/proc/self/exe` on Linux, `_NSGetExecutablePath` on macOS, else `argv[0]` + `$PATH`), or under `/usr/local`, `/opt/homebrew` or `/usr` `share/fidocad/`. `fcai config` prints the resolved executable and whether TTF fonts were found.

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

Stdin is accepted (`fcai json -` or pipe). Exit status is 0 on success; non-zero on I/O errors, a `--lib` that cannot be loaded, or a failed `validate`.

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
| `tex` | `--tex` / `--no-tex` / `FIDO_NO_TEX` | on |
| fonts | `FIDO_FONT_DIR` | next to binary |
| `verbose` | `-v` | off |

`--lib PATH` (repeatable) plus `--prefix NAME` loads extra `.fcl` files. Personal libraries: point `FIDO_LIB_DIR` (or `--lib-dir`) at your own `.fcl` folder.

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
