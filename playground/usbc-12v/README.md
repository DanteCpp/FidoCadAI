# USB-C PD to +12 V demo

Recorded on machy workspace 6 in Ghostty, using interactive Pi with
`ollama-cloud/deepseek-v4.1-flash` as the default model. Pi built and refined
`usbc_12v.fcd` through the live `fcai-gui` session.

Open from the repository root:

```sh
fcai --lib-dir playground/usbc-12v gui playground/usbc-12v/usbc_12v.fcd
```

The included libraries provide the circleless NMOS and triangle ground symbols.
Other symbols come from FidoCadAI's standard library.

View the [finished render](../../docs/images/circuit_usbc_12v.png).

The non-inverting power stage has two half bridges and an inductor between
their switch nodes. A USB-C PD sink block requests 15 V, and the controller
receives output feedback for +12 V regulation. At 15 V input it operates in
buck mode, with ideal duty ratio 12/15 = 0.8. See TI's
[four-switch buck-boost controller description](https://www.ti.com/product/LM5176)
for this topology; the drawn controller is a generic functional block.

This is a conceptual schematic for the drawing demonstration. MOSFET and
inductor ratings, capacitor values, the actual PD sink and controller,
high-side drive details, current limits, switching frequency, feedback values,
compensation, protection, and PCB layout still need to be specified. No load
rating or simulated hardware performance is claimed.

Pi checked FCD round trips, wire and junction geometry, expanded symbol pin
connections, rendered images, and agreement between the GUI and saved file.
These checks cover the drawing, not converter hardware validation.

The [README GIF](../../docs/images/pi_usbc_12v.gif) uses actual screen and
application-window recordings, with waiting periods removed. The recording
shows the initial power stage, interactive layout feedback, the PD/controller
additions, and the final wiring correction.
