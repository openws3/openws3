# OpenWS3 — open firmware for the WhoopStor V3

Open firmware for the WhoopStor V3, a six-port LiPo charger / storage unit built around the STC8H8K64U (8051 core, 64 KB flash).
Six independent charge ports, four target voltages (4.20 V, 4.35 V, 3.80 V storage, 3.85 V storage), CC/CV charging with termination,
storage-voltage bleed, input / rail / VCC supervision, a colour LCD UI with a settings menu, **input current and input voltage limits**
(so it can run from a PC USB port or a weak supply), a USB monitor and button-less flashing.

> **Status: early.** It runs on a real unit (display, buttons, menu, USB monitor, limits, charging at low current from a 5 V supply).
> The full charge cycle to termination, all six ports and higher input voltages have had little or no hardware testing.
> **You are charging lithium cells with unproven firmware. Supervise it, start with one cell and a current limit, and use it at your own risk.**
> It replaces the manufacturer's firmware: flashing is possible only through the chip's ISP loader and there is no vendor support afterwards.
> See `HARDWARE.md` for what is known about the board and what is still assumed.

**Using the charger?** Read `GUIDE.md`: the buttons, the screens (with pictures), every setting and the defaults.

## Build, test, flash

```bash
make                 # build/fw.bin                (needs SDCC 4.x)
make test            # host unit tests: charge logic, limits, menu, settings (plain C, run on the PC)
```

Flash through the STC8H USB loader (Linux, needs read/write access to `/dev/hidraw*`):

```bash
# first time, or when the firmware does not answer: put the unit in ISP mode (hold the button while plugging in USB-C), then
python3 tools/isp.py write build/fw.bin --erase
# afterwards, while OpenWS3 runs (and nothing is charging), no button is needed:
python3 tools/fwmon.py flash build/fw.bin
```

## Monitoring over USB

The firmware is also a USB HID device (`34bf:1004`, product "OpenWS3"): `python3 tools/fwmon.py watch` shows boot phases, port states,
voltages, currents, events and faults live; `start`/`stop`/`current`/`voltage` drive it. Details and report layout: `USB.md`.

`python3 tools/devtool.py` is a terminal UI for bench work: it switches every output by hand (port PWMs and bleed switches,
global enable, fan, input profile, the rail as a setpoint or a raw duty) and shows every input live (port and rail voltages,
port currents, input, VCC, thermistor, buttons, strap). The firmware keeps guards underneath (a 0.5 s link watchdog, a cut above
4.40 V or 2.8 A per port, the rail over-voltage stop); holding both buttons on the unit leaves the mode. Not while charging.
`tools/openws3.html` is the tool for users: open the file in Chrome or Edge (WebHID, no server, no driver) and press Connect. It opens
in a simple view: start / stop, charge current and target voltage, each port's state, voltage and current with a 10 s plot, and a
firmware installer for users (a guided "hold the left button while plugging in USB-C" flow for a charger that does not run OpenWS3 yet; a running unit restarts into the loader by itself). Esc stops charging.
The Advanced view adds the raw inputs, the limiter, setpoints, an event log and developer mode (the manual controls above).
`make` also writes `build/openws3.html`, the same page with `build/fw.bin` inside: that is the one to hand out.


## Menu

Press **B1 and B2 together** (within about 0.2 s of each other) and release. Holding both for 0.8 s (a beep) closes the menu and saves it.

The menu has four categories; **OK** opens one and **BACK** returns to the list of categories (from there, **BACK** closes the menu and saves).

| Category | Item | Setting |
|---|---|---|
| CHARGING | VOLTAGE | 4.20 V / 4.35 V / 3.80 V storage / 3.85 V storage (not while charging) |
| | CURRENT | charge current, 0.3 to 1.3 A in 0.1 A steps (not while charging) |
| LIMITS | MAX IN AMPS | limit on the estimated input current: off, 0.5 to 3.0 A in 0.1 A steps, then 0.5 A steps up to 10 A. The ports are limited smoothly |
| | MIN IN VOLTS | minimum input voltage: off, 4.80 to 5.00 V in 0.05 V steps, to 6.0 V in 0.1 V steps, then 0.5 V steps up to 25 V |
| | BELOW MIN | what happens when the input is below that minimum: `LIMIT` reduces the charge current smoothly until the input recovers; `STOP RUN` stops charging after a second and shows `V LIMIT` |
| | REGULATION | how the limits share the budget over the ports: `EQUAL`, `CH1 FIRST`, `HIGH V FIRST` (below) |
| PREFERENCES | SOUND | on / off |
| | THEME | colours only, no behaviour change: `CLASSIC` (the original look), `CALM` (colour only where it means something), `AMBER` (one hue on black, red for trouble), `HIGH CONTRAST`. Applies at once |
| DIAGNOSTICS | | the debug pages (below) |

Keys: the two buttons sit under the screen, **left = B1, right = B2**. A tap steps once; holding a button repeats, faster the longer it is held.
Both buttons together are **OK**, both held for 0.8 s are **BACK**. In a list **B2** moves down and **B1** up (it wraps around, the position is shown as n/m);
**OK** opens a category or edits the item and **BACK** goes one level up. While editing, **B2** raises the value and **B1** lowers it, **OK** confirms and **BACK** cancels (the old value comes back).
The bottom row of the screen shows these cues in its corners. Values apply at once and are saved when the menu closes; only changed settings are written.

**The limits.** Both limits work on one power budget, the most the charging ports may draw together (shown on diagnostics page 6). The input
current is estimated as rail voltage x the sum of the measured port currents / (90 % x input voltage).
The current limit sets the budget directly (limit x input voltage). With BELOW MIN on `LIMIT`, the voltage floor keeps a stored budget that shrinks
while the input is below the floor (a weak source recovers when it is loaded less) and grows slowly once it is back 0.1 V above it; the smaller of the
two budgets rules. With `STOP RUN` the floor does not regulate: charging stops when the input stays below it for a second.

The ports only grow into the budget, never past it. Every control pass the measured port currents are compared with it: a port may raise its
current a step only while there is room for that step, and the room goes to the ports in priority order. Above the budget (it shrinks when the
input sags), the lowest-priority port is brought down; a higher-priority port that wants more takes its room from the lowest-priority one the
same way. Room is handed out only once it is free as measured, so a newly connected cell cannot draw more before the others have given it up.
REGULATION is the priority order:

| REGULATION | Who gets the room first |
|---|---|
| EQUAL | the port with the lowest current: the currents even out (a port that wants less leaves the rest to the others) |
| CH1 FIRST | port 1, then port 2 ...; the highest port number is reduced first, down to switched off |
| HIGH V FIRST | the cell with the highest voltage |

A port in its taper draws less than it is set to, and the others get what it does not use. A port the limits hold with no current shows
`LIMIT` and starts as soon as there is room. The supervisor's input gate (about 4.6 V) still applies underneath.

**Running from a LiPo pack:** nothing detects the pack or its cell count. Set MIN IN VOLTS to the pack's cut-off (about 3.3 V per
cell: 6.6 V for 2S, 13.2 V for 4S, 19.8 V for 6S) with BELOW MIN on `STOP RUN`, so charging stops before the pack is
over-discharged. The unit itself stays on and draws a little; unplug it from the pack when done.

### Diagnostics pages

Opened from the menu. B2 is the next page, B1 the previous page, BACK (both held) goes back to the menu.

| Page | Shows |
|---|---|
| 1 power | bandgap count and the VCC it implies, VIN, rail and its PWM, ADC0 (probably a thermistor), fan, strap, stack bytes never used since boot, uptime |
| 2 port V | raw ADC count and voltage (corrected for VCC) per port |
| 3 port I | raw ADC count, mA (corrected for VCC), PWM duty, state, bleed switch |
| 4 status | run, target voltage, current setting, targets, stop/alarm, strap, USB |
| 5 ADC raw | all 16 channels |
| 6 limits | estimated input current, the two limits and their modes, the budget (as power and as total port current), which ports the limits hold (H) or keep waiting (W) |

## Using it

| Input | Action |
|---|---|
| Button 1, tap | current setting +0.1 A (0.3 … 1.3 A, wraps); ignored while charging. Taps are saved 3 s after the last one, or when charging starts |
| Button 1, hold (0.35 s) | start / stop charging |
| Button 2, tap | target voltage: 4.20 V → 4.35 V → 3.80 V storage → 3.85 V storage; ignored while charging |
| Button 2, hold (0.5 s) | port settings (below) |
| Both buttons, tap | open the menu |

Header: set current (with a `*` when some port has its own current or is off), input voltage, target voltage (red while charging).
Cells: port voltage, and below it the measured current while charging, for one second in four the current the port charges at
(`>0.8A`), and `LIMIT` when a user limit keeps the port waiting with no current. An idle port shows the current it will charge at
(`>0.5A`), or `OFF` (dimmed) if it sits the run out; otherwise `TEST` / `REST` / `FULL` / `BLEED` / `ERR`. The dot shows the state.

**Port settings.** Each port can have its own current, or be left out of the run. Hold button 2 on the main screen: the cells
show each port's value (`AUTO` = the set current in the header, `SET` = its own, `OFF`) and port 1 is framed.

| Input | Selecting a port | Editing its value |
|---|---|---|
| Button 1 / 2, tap | previous / next port | value down / up: `OFF` < `AUTO` < 0.3 … 1.3 A (holding repeats) |
| Both buttons, tap | edit this port | confirm |
| Both buttons, hold | done (saves) | cancel (the old value comes back) |
| Nothing for 10 s | done (saves) | cancel, done |

While charging the values are shown but cannot be changed (`LOCKED`). Button 1 on the main screen still sets the current that the
`AUTO` ports use.

## Layout

```
src/board.h        pins, ADC channels, PWM — single source of truth
src/hal.c          the only code that touches registers          src/lcd.c   ST7735S driver (asm byte shifter)
src/chg.c          per-port charge state machine (pure)           src/sup.c   rail / input / VCC supervision (pure)
src/limits.c       input current / voltage limits (pure)          src/menu.c  settings menu (pure), rowbuf.c text rows
src/ctl.c          rail PI, cal.c units, fmt.c text, input_pure.c buttons, buzz_pure.c patterns, cfg*.c settings
src/usb.c          USB HID monitor (telemetry + commands)         src/ui.c    screens
src/dev.c          developer mode: outputs set by the host, guards  src/portset.c  port settings mode (pure)
src/app.c          boot sequence, 4.5 ms control pass             src/font.c  GENERATED from tools/font5x7.txt
test/              host tests (C)                                 tools/      fwmon.py (USB monitor / flasher), devtool.py, openws3.html (web tool), bundle.py, isp.py, render_screens.* (the pictures in GUIDE.md)
```

"Pure" files have no hardware access and are what the host tests compile.

## Build rules

1. No initialised XRAM variables (SDCC's startup would drive `P2` high, which is a power-path port on this board).
2. Large memory model (not small: out of direct RAM; not medium: `MOVX @Ri` uses `P2`).
3. The timer ISR lives in `main.c` (SDCC only emits the vector there).
4. `int` is 16 bits here: any sum that can pass 65535 is done in `uint32_t`.

## License

MIT, see `LICENSE`.
