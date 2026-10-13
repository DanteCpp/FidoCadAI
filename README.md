# FidoCadAI

> **Draw circuits by talking to your agent.**  
> Natural language in, a live canvas out. No buttons; the agent sees what you see and every edit you make.  
> Fast, self-contained C11 engine built around the plain-text FidoCad format.

[![Build & Test](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-blue.svg)]()
[![Language](https://img.shields.io/badge/C11-zero--dependency-orange.svg)]()
[![License](https://img.shields.io/badge/license-GPLv3-lightgrey.svg)]()

---

## A New Way to Work With a Machine

I believe the recent progress in language models calls for a paradigm shift in human–machine interfaces. FidoCadAI is my take on that shift, applied to circuit design.

You don't drive FidoCadAI through menus and toolbars. **You say what you want**, by typing or dictation, and your agent draws it. Then you look at the result and answer on the canvas itself:

* **Natural language comes first.** Ask for a circuit, a change or a cleanup, in plain words.
* **Inspect visually, give feedback by clicking.** The drawing updates live in `fcai-gui`. Select the wire you don't like, drag a label, add a note, press `Return`: the agent gets the drawing, your selection, your note and a diff of what you changed by hand.
* **No buttons.** The window *is* the drawing: no toolbars, no menus, no chat pane. The conversation stays in your terminal and the canvas stays clean.
* **The agent knows the canvas.** It reads what is drawn and what you touched, so "make *this* shorter" means what you meant.
* **Keybindings are there when you want them.** Every tool has a single-key shortcut. The bindings borrow from Cadence (I come from chip design) and from other editors I like. They are the second way to work; language is the first.

### Why the FidoCad language

The FidoCad format suits this job well. A drawing is plain, human-readable ASCII text, and LLMs are built to read and write text. It also diffs and merges cleanly in git. The specification is small enough to describe in a few pages, so teaching it to an agent costs little context and leaves room for your actual design.

### Why C

The agent draws, renders, looks and corrects, over and over. Each pass has to be quick, so FidoCadAI is a compiled program that starts in milliseconds and renders almost instantly. That fast loop is what lets the agent check its own drawings visually.

I chose C over Rust because C has a huge, mature body of code for LLMs to learn style from, which makes it a good language for agents to read and extend. Dependencies are kept to a minimum so the project stays self-contained: the core and the CLI need only a C compiler and libc.

### The skill: how your agent learns FidoCadAI

FidoCadAI ships with a **skill**: a predefined prompt, written in natural language, that you attach to your agent's context so it knows how to use the tools. On Unix-like systems the install script registers it with your agents automatically. You can also print it with `fcai skill` and paste it into any agent you like.

The skill carries some of my own taste in schematic drawing. On top of that, FidoCadAI learns *yours*: during a session the agent distills your feedback into drawing preferences, stores them in `~/.fcai/style.md` and applies them next time. The more you use it, the more it draws the way you do.

### A CLI made for agents

`fcai` is how the agent talks to the drawing. It was designed for LLMs to use: compact text summaries, JSON on request, structured edits, primitive-level diffs, hit-testing and a session channel to the live GUI.

### The model makes the difference

FidoCadAI is a set of tools; the language model does the thinking. The quality of what you get depends mostly on the model behind your agent, and a frontier model such as Opus 5.5 or GPT 6.1 will give you far cleaner schematics than a weaker one.

---

## Live in Action: Session with Pi

Watch a **real interactive Pi session**, under **15 seconds**, drawing a simple **buck converter** in `fcai-gui`. Pi runs `ollama-cloud/deepseek-v4.1-flash` in **Ghostty** on machy workspace 6:

![Real Pi session: type a buck converter request, select COUT in the GUI, and ask Pi to change it from 220u to 470u](docs/images/pi_buck_selection.gif)

1. **Type the Request:** *"Draw a simple 15 V to 12 V buck converter."* Each character is entered individually in the live agent.
2. **Watch It Draw:** Pi builds the rails, switching MOSFET, freewheel diode, inductor, capacitors, and load in stages.
3. **Select and Send:** Click **COUT** in the GUI and press **Return** to send the selected component to Pi.
4. **Ask for a Change:** Type *"Set this capacitor to 470u."* Pi reads the selection, changes **COUT from 220u to 470u**, and saves it while keeping it highlighted.

Typing plays at its recorded speed; agent work is accelerated and waiting periods are cut. The [drawing and symbol library](playground/buck-demo/) are included. Component values are illustrative.

---

## Interactive GUI: Zero Clutter, Pure Circuit

`fcai-gui` is crafted to be distraction-free. The window **is** the drawing, with instant keyboard access to libraries, shortcuts, and precision measurement:

![FidoCadAI GUI features: sidebar, keybindings cheatsheet, and ruler](docs/images/gui_features.gif)

* **Symbol Library Sidebar (`Ctrl+B`):** Search and browse standard and custom symbol libraries with instant macro preview.
* **Keybindings Cheatsheet (`K`):** Searchable overlay covering every tool and shortcut (`Esc` closes).
* **Precision Ruler (`Shift+drag` or `Right-drag`):** Live measurement line with ticks showing distance in logical units, millimeters, and angle.
* **Smart Label Handles:** Component names and values have independent handles. Move labels anywhere without moving the symbol body.
* **Single-Key Tools:** `S` select · `L` line · `B` rectangle · `E` ellipse · `J` connection dot · `T` text · `R` rotate · `M` mirror · `Space` fit-to-view · `Return` send to agent · `Ctrl+Return` save & finish.
* **Desktop Integration:** Seamlessly follows your system dark/light theme, font, and window tiling.

---

## Examples

Schematics, PCB layouts, home energy systems, and art drawn with `fcai`. Editable sources live in [`playground/`](playground/). Click an image to open the full-resolution render.

| **Buck-Boost Schematic** | **Buck-Boost PCB Layout** |
|:---:|:---:|
| [![Inverting buck-boost schematic](docs/images/circuit_buck_boost.png)](docs/images/circuit_buck_boost.png?raw=true) | [![Two-layer buck-boost PCB layout](docs/images/circuit_buck_boost_pcb.png)](docs/images/circuit_buck_boost_pcb.png?raw=true) |
| Power-stage topology and component labels. [FidoCad source](playground/buck-boost/buck_boost.fcd) | Copper tracks, pads, and silkscreen. [FidoCad source](playground/buck-boost/buck_boost_pcb.fcd) |

| **VCO-Based Sigma-Delta ADC** | **Capacitive SAR ADC** |
|:---:|:---:|
| [![VCO-based ADC](docs/images/circuit_vco_adc.png)](docs/images/circuit_vco_adc.png?raw=true) | [![Capacitive SAR ADC](docs/images/circuit_cap_adc.png)](docs/images/circuit_cap_adc.png?raw=true) |
| Modulator and current-starved ring oscillator. [FidoCad source](playground/vco-adc/vco_adc.fcd) | Binary-weighted capacitor array and SAR register. [FidoCad source](playground/cap-adc/cap_adc.fcd) |

**Solar + Storage House — Single-Line Drawing**

Rooftop solar, battery accumulation, a hybrid inverter, grid metering, protected house circuits, and heat-pump heating. The energy manager coordinates PV surplus, battery limits, room temperature, and hot-water demand.

[![Dark-mode fcai-gui screenshot of a house with solar PV, battery storage, hybrid inverter, heat pump, thermal storage, and energy management](docs/images/example_unifilar.png)](docs/images/example_unifilar.png?raw=true)

[Editable drawing and symbol library](playground/unifilar/) · Blue: AC power · Gold: PV DC · Green: battery / PE · Violet dashes: logical controls · Orange: thermal flow

**Enrico Fermi Portrait**

<p align="center">
  <a href="docs/images/example_enrico_fermi.png?raw=true"><img src="docs/images/example_enrico_fermi.png" width="480" alt="Enrico Fermi portrait in dark-mode fcai-gui, drawn with editable outlines and cross-hatching"></a>
</p>

[Editable artwork and reference photograph](playground/enrico-fermi/)

---

## Quick Start

### 1. Build & Install

```bash
# Clone and build core CLI + GUI
git clone https://github.com/DanteCpp/FidoCadAI.git
cd FidoCadAI
make

# System-wide install (/opt/FidoCadAI, symlinks in /usr/local/bin, auto-registers AI skill)
sudo scripts/install.sh install
```

> **Requirements:** C11 compiler (`clang` or `gcc`) and `make`.  
> For the GUI (`fcai-gui`): Qt6 (`brew install qt` on macOS, `apt install qt6-base-dev` on Linux). Core CLI builds with zero dependencies.

### 2. Pair with Your AI Agent

On Unix-like systems the installer registers the skill with your local agent harnesses. For any other agent, attach the output of `fcai skill` to its prompt.

```bash
# With Pi (interactive terminal):
pi --model deepseek-v4.1-flash "draw an inverting buck-boost converter"

# With Antigravity (AGY):
agy "open fcai gui and sketch a CMOS bandgap reference circuit"

# With OpenAI Codex:
codex "draw a 130 nm PMOS differential pair with degeneration"

# With Claude Code:
claude "sketch a 2nd-order active low-pass filter in fcai"
```

The agent opens `fcai-gui`, builds the schematic in real time, and saves the verified `.fcd` drawing when you are done.

---

## Credits & License

* **Core & GUI:** Licensed under [GPLv3](LICENSE).
* **Reference Projects:** [FidoCadJ](https://github.com/DarwinNE/FidoCadJ) (Java) by Davide Bucci and [FidoCadJS](https://github.com/DarwinNE/FidoCadJ) (TypeScript).
* **Rasterizer & Fonts:** Vendors [stb](https://github.com/nothings/stb) (`stb_truetype`, `stb_image_write`) and [Liberation Fonts](https://github.com/liberationfonts/liberation-fonts) (SIL Open Font License 1.1).
