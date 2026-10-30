# Embedded mimeTeX

John Forkosh's mimeTeX 1.76, GPL-3.0-or-later (see COPYING). The bundled
Computer Modern, symbol and extension bitmap fonts are in texfonts.h.

Source archive (its top directory is misleadingly named mimetex-1.75):
https://deb.debian.org/debian/pool/main/m/mimetex/mimetex_1.76.orig.tar.gz

SHA256: 37edf717cc8bf3ba78cf53080ec3d5fff576653c8c7d762e05055d183582814f

Applied Debian patch:
https://deb.debian.org/debian/pool/main/m/mimetex/mimetex_1.76-6.debian.tar.xz
`debian/patches/CVE-2024-40446.diff` (security gating for mathtex).

Only mimetex.c, mimetex.h, texfonts.h and COPYING come from the archive.
embedded.c/.h are FidoCadAI's bridge; the legacy implementation compiles in its
own translation unit with private symbols. No CGI main, GIF encoder, external
TeX program, JavaScript engine or runtime library is required.

Local changes to the upstream sources:

- Embedded builds omit input, counter, environment and mathtex from the
  command table, so drawing a formula cannot access files or run commands.
- Unknown symbols/environments mark the whole run unsupported, letting FidoCadAI
  preserve the literal source including its delimiters. Bound recursion and
  raster allocation before multiplying dimensions.
- Fix out-of-bounds string comparisons in limits, array rules and environment
  lookup, and enlarge script buffers to the parser's declared subexpression
  capacity. Free the temporary nth-root index raster, handle failed script
  concatenations and release their inputs on allocation failure.
- Accept AMS aligned/split and gathered environment aliases. Support tfrac
  and give dfrac/tfrac explicit display/text styles.
- Scale fraction rules, spacing, axis placement and radical strokes with the
  actual font resolution, including operator spacing, explicit TeX spaces and
  array row/column spacing and script baselines. Display fractions have full-size text-style children;
  nested text fractions shrink. Disable mimeTeX's aggressive raster smashing.

The integration in src/tex.c normalizes common Unicode math
characters, caches reference-counted high-resolution images and summed coverage
(up to 64 entries / 16 MiB), and rejects unbalanced groups, excessive nesting
and sources longer than 4096 bytes. The renderer filters that coverage at the
current zoom, applies the label's transform, colour and opacity, and shares
actual layout bounds with selection, hit testing, fit and PNG export.

This is a native raster math renderer, not a full TeX distribution. MathJax
package parity remains future work: custom macro definitions, additional AMS
and extension commands, arbitrary Unicode text and per-expression colours are
not all implemented. At extreme enlargement, bitmap font resolution is
visible. Unknown expressions fall back to editable literal source; unsupported
features should be added here with regression examples.

Validation: `make test` and `make sanitize` (AddressSanitizer,
UndefinedBehaviorSanitizer and leak detection). The focused C tests
cover the noise equations, scripts, nested fractions/roots, limits, matrices,
cases, AMS aliases, Unicode, fallback, cache eviction, rotated/mirrored labels,
and malformed/oversized inputs. The focused Qt tests check source editing,
apply/cancel/undo/redo and opaque overlays in light and dark themes.
