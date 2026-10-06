# Solar + storage house

[distribution.fcd](distribution.fcd) is an editable single-line drawing of a
house with solar generation, electrical storage, and heat-pump heating. The
README image is an actual dark-mode `fcai-gui` screenshot.

The system shows:

- Two rooftop PV strings, DC isolation, and a shunt surge protector.
- Two battery modules, a DC fuse and isolator, and battery management.
- A hybrid inverter with separate MPPT and battery conversion stages, an AC
  breaker, grid synchronisation, and anti-islanding.
- A bidirectional grid meter, main breaker, AC surge protector, and house bus.
- Individual RCBO feeders for lighting, sockets, and the air-to-water heat pump.
- Hot-water/buffer storage, floor heating, and room and tank temperature sensors.
- An energy manager for metering, charge/discharge limits, battery telemetry,
  PV-surplus heating commands, and comfort control.

The example capacities are 6 kWp PV, 10 kWh LFP storage at 48 V, and a 5 kVA
inverter. They illustrate the drawing; the sheet is not an installation design
or a component-sizing calculation. Protection choices and interfaces depend on
the actual equipment and installation.

| Appearance | Meaning |
|---|---|
| Solid blue | AC power, with one line representing L+N |
| Solid gold | PV DC circuit |
| Solid green | Battery DC, or separately labelled protective earth |
| Dashed violet | Logical control integration |
| Solid orange | Thermal flow to storage and heating |

The violet network groups controller relationships. Modbus, CAN, SG Ready, and
temperature-sensor interfaces are distinct physical interfaces; it does not
represent one shared electrical cable. Orange links show thermal functions,
not a complete hydraulic circuit. Earth symbols represent the common bonding
system; individual PE conductors are not expanded.

Open from the repository root:

```sh
fcai-gui --config playground/unifilar/preview.conf --lib playground/unifilar/houseenergy.fcl playground/unifilar/distribution.fcd
```

`houseenergy.fcl` supplies the PV and battery icons. Protection, meter, and
converter symbols use the bundled `elettrotecnica` library. `preview.conf`
sets the dark theme and hides the grid for the appearance shown in the README.
A clean render with the same background can be generated with:

```sh
fcai --lib playground/unifilar/houseenergy.fcl render playground/unifilar/distribution.fcd --bg 1a1b26 --scale 2 --margin 28 -o /tmp/house-energy.png
```

The drawing passes FCD round-trip validation and has no wire/junction lint
findings. Its conceptual topology follows the general approach in Victron's
[ESS system design guide](https://www.victronenergy.com/media/pg/Energy_Storage_System/en/ess-system-design.html).
The heat-pump integration is illustrated by STIEBEL ELTRON's
[energy management and SG Ready description](https://www.stiebel-eltron.com/en/home/products-solutions/renewables/controller_energymanagement.html).
