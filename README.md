# FidoCadAI

> **AI-native pair programming for electronic schematics and PCB layouts.**  
> Fast, dependency-free C11 engine with a distraction-free collaborative GUI.

[![Build & Test](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS-blue.svg)]()
[![Language](https://img.shields.io/badge/C11-zero--dependency-orange.svg)]()
[![License](https://img.shields.io/badge/license-GPLv3-lightgrey.svg)]()

---

## Why FidoCadAI?

Traditional EDA suites are heavy, closed, and full of friction. They store circuits in opaque binary blobs that cannot be diffed, version-controlled, or inspected by AI.

**FidoCadAI** brings electronics design into the modern developer workflow:

* **Schematics as Code:** Every drawing (`.fcd`) and library (`.fcl`) is pure, human-readable plain text. Git diffs are readable, merges make sense, and parsers run in microseconds.
* **The Window is the Drawing, Your Terminal is the Copilot:** `fcai-gui` has **no toolbars, no menus, and no chat window**. Your coding assistant ([Pi](https://github.com/pi-agent), Antigravity, Codex, Claude Code) stays in your terminal while you watch the schematic evolve live on canvas.
* **Live Bidirectional Collaboration:** The AI draws while you watch. See something you dislike? Tweak a component handle, drag a label, or give spoken feedback in your terminal—the agent incorporates your changes on the fly.
* **Persistent Style Memory (`~/.fcai/style.md`):** Remembers your drawing preferences across sessions and models (such as lead lengths, MOS label conventions, and rail margins).
* **Instant C11 Speed:** Zero Java runtime, zero Electron bloat. Starts up in under 10 ms and renders vector schematics in sub-milliseconds.

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

## Built in the Playground

Real circuits designed, iterated, and verified with AI in the [`playground/`](file:///Users/dante/Projects/fidocad/FidoCadAI/playground):

| **130 nm PMOS Differential Pair** (`playground/diff-pair`) | **Inverting Buck-Boost Converter** (`playground/buck-boost`) |
|:---:|:---:|
| [![130nm PMOS Differential Pair](docs/images/circuit_diffpair.png)](file:///Users/dante/Projects/fidocad/FidoCadAI/playground/diff-pair/pmos_diffpair_130nm.fcd) | [![Buck-Boost Schematic](docs/images/circuit_buck_boost.png)](file:///Users/dante/Projects/fidocad/FidoCadAI/playground/buck-boost/buck_boost.fcd) |
| *Active current-mirror bias, source degeneration, and native LaTeX math labels ($V_{DD}, V_{bias}, g_m, V_{out}$).* | *Clean power-stage topology with consistent lead lengths and verified junction dots.* |

| **Buck-Boost 2-Layer PCB Layout** (`playground/buck-boost`) | **2nd-Order VCO-Based Sigma-Delta ADC** (`playground/vco-adc`) |
|:---:|:---:|
| [![Buck-Boost PCB Layout](docs/images/circuit_buck_boost_pcb.png)](file:///Users/dante/Projects/fidocad/FidoCadAI/playground/buck-boost/buck_boost_pcb.fcd) | [![VCO ADC](docs/images/circuit_vco_adc.png)](file:///Users/dante/Projects/fidocad/FidoCadAI/playground/vco-adc/vco_adc.fcd) |
| *PCB tracks, solder pads, thermal reliefs, and silkscreen footprints.* | *System-level modulator block diagram + transistor-level 3-stage current-starved ring oscillator.* |

| **5-Bit Capacitive SAR ADC** (`playground/cap-adc`) | **DeepSeek-V4 Neural Architecture** (`playground/ds4-flash`) |
|:---:|:---:|
| [![Capacitive SAR ADC](docs/images/circuit_cap_adc.png)](file:///Users/dante/Projects/fidocad/FidoCadAI/playground/cap-adc/cap_adc.fcd) | [![Neural Architecture Diagram](playground/ds4-flash/v4_flash.png)](file:///Users/dante/Projects/fidocad/FidoCadAI/playground/ds4-flash/v4_flash.fcd) |
| *Binary-weighted capacitor array, custom CMOS switch macros, and SAR register.* | *Hierarchical machine-learning block diagram with matrix-multiplication callouts.* |

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

FidoCadAI integrates with local agent harnesses. The installed skill works automatically across your favorite tools:

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
