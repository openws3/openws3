# OpenWS3 — open firmware for the WhoopStor V3

Open firmware for the WhoopStor V3, a six-port LiPo charger / storage unit built around the STC8H8K64U (8051 core, 64 KB flash).
Six independent charge ports, four modes (4.20 V, 4.35 V, 3.80 V storage, 3.85 V storage), CC/CV charging with termination,
storage-mode bleed, input / rail / VCC supervision, a colour LCD UI with a settings menu, **input current and input voltage limits**
(so it can run from a PC USB port or a weak supply), a USB monitor and button-less flashing.

> **Status: early.** It runs on a real unit (display, buttons, menu, USB monitor, limits, charging at low current from a 5 V supply).
> The full charge cycle to termination, all six ports and higher input voltages have had little or no hardware testing.
> **You are charging lithium cells with unproven firmware. Supervise it, start with one cell and a current limit, and use it at your own risk.**
> It replaces the manufacturer's firmware: flashing is possible only through the chip's ISP loader and there is no vendor support afterwards.
> See `HARDWARE.md` for what is known about the board and what is still assumed.

## Build, test, flash

```bash
make                 # build/fw.bin                (needs SDCC 4.x)
make CHARGE=0        # image that does everything except energise a port (UI, supervision, buttons work)
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
voltages, currents, events and faults live; `start`/`stop`/`current`/`mode` drive it. Details and report layout: `USB.md`.


## Menu

Press **B1 and B2 together** (within about 0.3 s of each other) and hold for 1.5 s. The same chord closes the menu and saves it.

| Item | Setting |
|---|---|
| MODE | 4.20 V / 4.35 V / 3.80 V storage / 3.85 V storage (not while charging) |
| CURRENT | per-port current, 0.3 A and up, within what the present input allows (not while charging) |
| SOUND | on / off |
| THEME | colours only, no behaviour change: `CLASSIC` (the original look), `CALM` (colour only where it means something), `AMBER` (one hue on black, red for trouble), `HIGH CONTRAST`. Applies at once |
| MAX IN AMPS | limit on the estimated input current: off, 0.5 to 3.0 A in 0.1 A steps, then 0.5 A steps up to 10 A. The ports are limited smoothly |
| MIN IN VOLTS | minimum input voltage: off, 4.8 to 6.0 V in 0.1 V steps, then 0.5 V steps up to 25.0 V |
| BELOW MIN | what happens when the input is below that minimum: `LIMIT` reduces the charge current smoothly until the input recovers; `STOP RUN` stops charging after a second and shows `V LIMIT` |
| REGULATION | how the limits share the budget over the ports: `EQUAL`, `CH1 FIRST`, `LOW V FIRST`, `HIGH V FIRST` (below) |
| DIAGNOSTICS | the debug pages (below) |
| EXIT & SAVE | close the menu and save |

Keys: the two buttons sit under the screen, **left = B1, right = B2**. Right is forward, left is back. In the list the **right** button moves
down and the **left** one up (no wrap-around, the position is shown as n/10); a **long press of the right button** edits the item or runs the action.
While editing, **right** raises the value and **left** lowers it, a **long right press** confirms and a **long left press** cancels (the old value comes back).
The bottom row of the screen shows these cues in its corners. Values apply at once and are saved when the menu closes; only changed settings are written.

**The limits.** Both limits work on one power budget, the most the charging ports may draw together (shown on diagnostics page 6). The input
current is estimated as rail voltage x the sum of the measured port currents / (90 % x input voltage).
The current limit sets the budget directly (limit x input voltage). With BELOW MIN on `LIMIT`, the voltage floor keeps a stored budget that shrinks
while the input is below the floor (a weak source recovers when it is loaded less) and grows slowly once it is back 0.3 V above it; the smaller of the
two budgets rules. With `STOP RUN` the floor does not regulate: charging stops when the input stays below it for a second. REGULATION decides how
the budget is shared:

| REGULATION | What it does |
|---|---|
| EQUAL | every charging port gets the same share (a port that wants less frees the rest) |
| CH1 FIRST | one by one: port 1 keeps its current longest, the highest port number is reduced first, down to switched off |
| LOW V FIRST | one by one: the cell with the lowest voltage keeps its current longest |
| HIGH V FIRST | one by one: the cell with the highest voltage keeps its current longest |

Higher-priority ports reserve what they really draw, so a port in its taper makes room for the next one. A port that is switched off by
a limit shows `LIMIT` and restarts at the duty it had. The input rules of the supervisor (cell-count limits, power-off thresholds) still apply underneath.

### Diagnostics pages

Opened from the menu. The right button is the next page, the left one the previous page, a long left press goes back to the menu, the chord closes everything.

| Page | Shows |
|---|---|
| 1 power | bandgap count and the VCC it implies, VIN, rail and its PWM, ADC0 (probably a thermistor), fan, strap, uptime |
| 2 port V | raw count per port, and the voltage under two scales: 4.5 V / 4096 (what the firmware uses) and VCC / 4096 |
| 3 port I | raw count, mA, PWM duty, state, bleed switch |
| 4 status | run, mode, current settings, targets, stop/alarm, strap, USB, build flags |
| 5 ADC raw | all 16 channels |
| 6 limits | estimated input current, the two limits and their modes, the budget, the cap on each port |

## Using it

| Input | Action |
|---|---|
| Button 1, short | current setting +0.1 A (0.3 … 1.3 A, wraps); ignored while charging |
| Button 1, long (1.3 s) | start / stop charging |
| Button 2, short | mode: 4.20 V → 4.35 V → 3.80 V storage → 3.85 V storage |
| Button 2, long (1.8 s) | sound on / off |

Header: set current, input voltage, target voltage (red while charging). Cells: port voltage, and below it the measured
current while charging (`LIMIT` when a user limit holds the port off), or `TEST` / `REST` / `WAIT` / `FULL` / `BLEED` / `ERR`; the dot shows the state.

## Layout

```
src/board.h        pins, ADC channels, PWM — single source of truth
src/hal.c          the only code that touches registers          src/lcd.c   ST7735S driver (asm byte shifter)
src/chg.c          per-port charge state machine (pure)           src/sup.c   rail / input / VCC supervision (pure)
src/limits.c       input current / voltage limits (pure)          src/menu.c  settings menu (pure), rowbuf.c text rows
src/ctl.c          rail PI, cal.c units, fmt.c text, input_pure.c buttons, buzz_pure.c patterns, cfg*.c settings
src/usb.c          USB HID monitor (telemetry + commands)         src/ui.c    screens
src/app.c          boot sequence, 4.5 ms control pass             src/font.c  GENERATED from tools/font5x7.txt
test/              host tests (C)                                 tools/      fwmon.py (USB monitor / flasher), isp.py (ISP client)
```

"Pure" files have no hardware access and are what the host tests compile.

## Build rules

1. No initialised XRAM variables (SDCC's startup would drive `P2` high, which is a power-path port on this board).
2. Large memory model (not small: out of direct RAM; not medium: `MOVX @Ri` uses `P2`).
3. The timer ISR lives in `main.c` (SDCC only emits the vector there).
4. `int` is 16 bits here: any sum that can pass 65535 is done in `uint32_t`.

## License

MIT, see `LICENSE`.
