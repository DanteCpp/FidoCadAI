# FidoCadAI

Draw circuits by talking to your agent.

FidoCadAI gives you and your AI agent a shared drawing canvas. Ask for a circuit, inspect the result, then answer on the canvas itself: select a component, move a label or adjust a wire. Your selections and edits are shared automatically, so the next request can refer to what you just touched.

The conversation stays in your terminal. The drawing stays open beside it, and you can edit it by hand at any point.

## An example session

In this session, Pi draws a buck converter from the request “Draw a simple 15 V to 12 V buck converter.” The recording is shortened: agent responses are accelerated and waiting periods are removed.

![Real Pi session: type a buck converter request, select COUT in the GUI, and ask Pi to change it from 220u to 470u](docs/images/pi_buck_selection.gif)

Once the circuit is drawn, click the output capacitor and ask, “Set this capacitor to 470u.” The selection tells the agent which component you mean. Pi changes its value from 220u to 470u and leaves it highlighted.

The [drawing, symbol library and session details](playground/buck-demo/) are included. Component values are illustrative; this example demonstrates drawing and editing a circuit.

## Working with the drawing

I come from chip design, and the keyboard shortcuts borrow from Cadence and other editors I like. Language is the main way to ask for work, while direct editing lets you make a small adjustment where you see it. Both are part of the same session.

The drawing window has no permanent toolbars or menus. You can bring up a symbol library or a searchable shortcut guide when you need one. Labels have their own handles, so you can move a component's name or value without moving the component. A ruler measures distances and angles on the canvas.

![FidoCadAI GUI features: sidebar, keybindings cheatsheet, and ruler](docs/images/gui_features.gif)

FidoCadAI ships with a skill: written instructions that teach an agent how to use the tools. It also carries some of my own taste in schematic drawing. You can ask the agent to remember your drawing preferences for future sessions, or enable learning so it saves preferences from your explicit feedback.

## Why it is built this way

A FidoCad drawing is plain text. An agent can read and change it, and you can track those changes in Git. The format is small enough to explain in a few pages, leaving more of the agent's context available for the design itself. The drawings remain editable files that you can keep and share.

The agent works through a command-line tool, `fcai`, while you see the result in `fcai-gui`. It can draw, render the result, inspect it and make corrections. Keeping that cycle quick matters, so the core is written in C11 with only the C standard library as a dependency. The drawing window uses Qt6.

The language model interprets your requests and makes the design decisions, so results depend on the model you use. The tools let it inspect the drawing's syntax, wire geometry and appearance; those checks do not establish that a circuit will work electrically.

## Examples

These examples range from circuit details to a house energy system and a portrait. Each uses the same text-based drawing format. Editable sources live in [`playground/`](playground/); click an image to open the full-resolution render.

The buck-boost pair shows a schematic and a PCB layout. The ADC drawings use repeated circuit elements and labels to describe more detailed structures.

| **Buck-Boost Schematic** | **Buck-Boost PCB Layout** |
|:---:|:---:|
| [![Inverting buck-boost schematic](docs/images/circuit_buck_boost.png)](docs/images/circuit_buck_boost.png?raw=true) | [![Two-layer buck-boost PCB layout](docs/images/circuit_buck_boost_pcb.png)](docs/images/circuit_buck_boost_pcb.png?raw=true) |
| Power-stage topology and component labels. [FidoCad source](playground/buck-boost/buck_boost.fcd) | Copper tracks, pads, and silkscreen. [FidoCad source](playground/buck-boost/buck_boost_pcb.fcd) |

| **VCO-Based Sigma-Delta ADC** | **Capacitive SAR ADC** |
|:---:|:---:|
| [![VCO-based ADC](docs/images/circuit_vco_adc.png)](docs/images/circuit_vco_adc.png?raw=true) | [![Capacitive SAR ADC](docs/images/circuit_cap_adc.png)](docs/images/circuit_cap_adc.png?raw=true) |
| Modulator and current-starved ring oscillator. [FidoCad source](playground/vco-adc/vco_adc.fcd) | Binary-weighted capacitor array and SAR register. [FidoCad source](playground/cap-adc/cap_adc.fcd) |

**Solar + Storage House — Single-Line Drawing**

This drawing brings electrical and thermal systems onto one canvas: rooftop solar, battery storage, a hybrid inverter, house circuits and heat-pump heating. Colours and line styles distinguish power, control signals and heat flow.

[![Dark-mode fcai-gui screenshot of a house with solar PV, battery storage, hybrid inverter, heat pump, thermal storage, and energy management](docs/images/example_unifilar.png)](docs/images/example_unifilar.png?raw=true)

[Editable drawing and symbol library](playground/unifilar/) · Blue: AC power · Gold: PV DC · Green: battery / PE · Violet dashes: logical controls · Orange: thermal flow

**Enrico Fermi Portrait**

The same lines used for circuits can also describe a face. This portrait is made of editable outlines and cross-hatching, with no embedded photograph.

<p align="center">
  <a href="docs/images/example_enrico_fermi.png?raw=true"><img src="docs/images/example_enrico_fermi.png" width="480" alt="Enrico Fermi portrait in dark-mode fcai-gui, drawn with editable outlines and cross-hatching"></a>
</p>

[Editable artwork and reference photograph](playground/enrico-fermi/)

## Try it

FidoCadAI runs on Linux and macOS with a coding agent such as Pi, Codex or Claude Code. Give your agent this repository and ask it to install FidoCadAI, read the included skill and open a drawing. Building requires a C11 compiler, Make and Qt6 for the drawing window; the [installer](scripts/install.sh) and [agent instructions](share/skills/fidocad/SKILL.md) cover setup and use.

A first request can be as simple as “Draw an inverting buck-boost converter.” Once the drawing is open, select something you want to change and continue the conversation.

## Credits and license

* **Core & GUI:** Licensed under [GPLv3](LICENSE).
* **Reference Projects:** [FidoCadJ](https://github.com/DarwinNE/FidoCadJ) (Java) by Davide Bucci and [FidoCadJS](https://github.com/DarwinNE/FidoCadJ) (TypeScript).
* **Rasterizer & Fonts:** Vendors [stb](https://github.com/nothings/stb) (`stb_truetype`, `stb_image_write`) and [Liberation Fonts](https://github.com/liberationfonts/liberation-fonts) (SIL Open Font License 1.1).
