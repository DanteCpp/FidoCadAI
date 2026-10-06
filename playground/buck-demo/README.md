# Interactive buck converter demo

The [short README GIF](../../docs/images/pi_buck_selection.gif) comes from a
real Pi session in Ghostty on machy workspace 6, using
`ollama-cloud/deepseek-v4.1-flash`.

The prompts were entered one character at a time. Their typing is shown at
recorded speed; agent responses are accelerated and waiting periods removed.

Pi drew a compact asynchronous buck converter in the live GUI. The driver then
clicked COUT and pressed Return. The GUI sent `selection: [27]` to Pi, followed
by the typed request "Set this capacitor to 470u." Pi read that selection,
changed only COUT's attached value from `220u` to `470u`, and saved the drawing.
The selected component remained highlighted after the update.

Open the [drawing](buck.fcd) from the repository root:

```sh
fcai --lib playground/buck-demo/mos.fcl gui playground/buck-demo/buck.fcd
```

The included library supplies the circleless NMOS. The Schottky diode and
remaining symbols are in FidoCadAI's bundled libraries. A
[finished render](../../docs/images/circuit_buck_demo.png) is also included.

The circuit is a drawing example with illustrative component values, not a
validated hardware design. Pi checked syntax, wire geometry, diode polarity,
and rendered layout. The before/after drawing diff contains exactly one
modified primitive: COUT's value.
