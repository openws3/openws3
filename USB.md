# USB monitor interface

The firmware is a USB **HID** device (VID `34bf`, PID `1004`, product "OpenWS3") alongside its normal job. No driver is
needed. The report descriptor uses a vendor-defined usage page, so the host does not turn the telemetry into keyboard or other input events. On Linux it shows up as `/dev/hidrawN`. Use `tools/fwmon.py`.

* Telemetry: 64-byte **IN** reports, polled every 10 ms, in two pages sent alternately (byte 3 = page): page 1 is the charger, page 2 the
  limiter and supervisor, so each page is refreshed every 20 ms. It is available from power-up, and the commands work in every state,
  also in a fatal condition (everything stays off; the unit can still be read, stopped and flashed). `fwmon.py` merges the two pages into one snapshot.
* Commands: one 64-byte **OUT** report. The interrupt only stores it. `app_pass` executes it in the main loop with the same rules as
  the buttons, so the power path is never touched from interrupt context. Raw PWM and pin access exists only in the separate
  developer mode (commands 7–9, below), which keeps its own guards.
* ISP is unaffected: hold the button while plugging USB-C to flash as before, or send command 6 (`fwmon.py isp`).
* The 0.5 ms tick interrupt has higher priority than USB. A report costs about 0.4 ms of CPU every 10 ms.

## Units

**Counts** are what the ADC returned: 0..4095, relative to VCC, which is regulated. The charger works on them as they are, in a fixed
unit: **vu** = 4.5 V / 4096 (1.0986 mV) for port voltages and the rail, **iu** = 1/68.25 A (14.65 mA) for port currents
(`src/adc.h`). The bandgap count guards VCC. The voltage counts (ports, input, rail) are corrected in the firmware for a supply that
sags: reading = count × 1071 / bandgap count, then the unit above or cal.h. The currents are corrected the same way.

## Telemetry report (IN, 64 bytes, big-endian)

| Byte | Content |
|---|---|
| 0, 1 | magic `57 33` |
| 2 | sequence (1..255), **head**; a report is valid only if byte 63 equals it |
| 3 | page (1) |
| 4–7 | uptime, ms |
| 8 | flags: b0 running, b1–2 target voltage (0..3: 4.20 / 4.35 / 3.80 / 3.85 V), b3 outputs enabled (P5.2), b4 reserved (0), b5 rail alarm pause, b6 strap P3.4, b7 reserved (0) |
| 9 | current setting (iu: 21 = 0.3 A … 91 = 1.3 A, ×7 per 0.1 A) |
| 10 | firmware release: major in the high nibble, minor in the low (`src/version.h`); 0 on firmware that predates it |
| 11 | stop reason: 0 none, 1 input low, 2 VCC low, 3 rail, 4 user, 5 minimum input voltage (user setting) |
| 12 | low nibble: the condition now, as a stop reason (byte 11: 1 input low, 2 VCC low, 3 rail out of range, 5 minimum input voltage; the first three have a screen of their own); high nibble: screen open on the LCD (0 = main, 1..6 = diagnostics page, 13 = port settings, 14 = developer mode, 15 = menu) |
| 13–15 | last event: sequence (increments), code (1 port full, 2 port fault, 3 stopped), port 0..5 (or the stop reason for code 3) |
| 16, 17 | last command: its sequence number, result (0 ok, 1 refused, 2 unknown) |
| 18–23 | port states 1..6: 0 idle, 1 probe, 2 charge, 3 rest, 4 full, 5 bleed, 6 fault |
| 24 | bit k = port k+1 switch pin high (bleed path) |
| 25 | phase: 1 bringing the rail up, 2 splash, 7 running, 0x80 fatal condition (everything off; the link keeps working) |
| 26–37 | PWM duty 1..6 (0..400), 16 bit |
| 38–49 | port voltage 1..6: the charger's filtered reading, vu (×4.5/4096 V), 16 bit |
| 50–55 | port current 1..6: the charger's filtered reading, iu (÷68.25 A), saturated at 255 |
| 56–57, 58–59, 60–61 | raw counts: rail ADC8, input ADC7, bandgap ADC15 |
| 62 | rail PWM duty ÷ 4 |
| 63 | sequence, **tail** (written last; 0 while the report is being rebuilt) |

### Page 2: limiter and supervisor

Same header (bytes 0–7, byte 3 = 2, head / tail sequence in bytes 2 / 63). Everything below is zero before the run phase.

| Byte | Content |
|---|---|
| 8–11 | settings: MAX IN AMPS (0.1 A, 0 = off), MIN IN VOLTS (0 = off; up to 200: ×0.05 V, above: 10 V + 0.5 V per step), BELOW MIN (1 = stop the run), REGULATION (0 equal, 1 ch1 first, 2 high V first) |
| 12, 13 | power budget, 0.1 W (10000 = no limit) |
| 14, 15 / 16, 17 | its two parts: from the current limit / from the voltage floor (10000 = not active) |
| 18, 19 | estimated input current, mA |
| 20, 21 | the budget as a total port current, iu (1/68.25 A); 0 = no limit active |
| 22 | trip: 1 = the minimum input voltage stops the run |
| 23 | the limiter ran this pass (running and no rail hold) |
| 24–29 | the limiter on each port: bits 0–1 held (1 = below its setting, 2 = waiting, no current), bit 2 wants more current, bit 3 may raise its duty this pass, bit 4 brought down this pass |
| 30–35 | the current each port is set to, iu (charging ports only) |
| 36–41 | each port's value: 0 = off (sits the run out), 1 = auto (the global current), else its own current in iu (21, 28 … 91) |
| 42, 43 / 44, 45 | the rail setpoint and the cell target voltage of the chosen target, vu |
| 46 | stack bytes never used since boot (internal RAM; the margin left at the deepest point so far) |
| 47 | pre-release: the beta number of the firmware release (`src/version.h`), 0 for a final release or firmware that predates it |
| 48–50 | reserved (0) |
| 51 | some port is charging or bleeding (global enable) |
| 52–56 | supervisor: stop reason (waiting condition: 1 input low, 2 VCC low, 3 rail out of range), reserved (0), fatal condition (0 none, 0x10 VCC over 5.0 V, 0x11 rail overvoltage, 0x12 no rail), rail alarm (stop reason 3), fan |
| 57–60 | build id: the first 8 hex digits of the git commit the firmware was built from, 32 bit (the page is not sent in developer mode; use byte 10 of page 1 there) |
| 61 | 1 if the source tree had uncommitted changes when it was built |

Voltages, currents and the rail are zero before phase 7 except the three ADC words, which are live from phase 1.

### Page 3: developer mode

Sent instead of page 2 while developer mode is on (byte 12's high nibble = 14). Page 1 then carries no port data. ADC values are raw counts (the mean of the samples, before any filtering or correction); the bandgap is in bytes 38–39.

| Byte | Content |
|---|---|
| 8–19 | port voltage 1..6, raw counts (ADC1..6) |
| 20–31 | port current 1..6, raw counts (ADC 12, 14, 13, 9, 10, 11) |
| 32–39 | ADC0 (thermistor), ADC7 (input), ADC8 (rail), ADC15 (bandgap) |
| 40–51 | PWM duty applied to ports 1..6 (0..400) |
| 52, 53 | rail PWM duty applied |
| 54 | switch pins applied, bit k = port k+1 high (bleed path) |
| 55 | applied: b0 global enable P5.2, b1 fan P0.7, b2 profile P5.3, b3 beep request, b4 rail on raw duty (else regulated) |
| 56 | inputs: b0 button 1 down, b1 button 2 down, b2 strap P3.4 |
| 57 | b0 watchdog: no command 8 for 0.5 s, port outputs off and the rail back on its entry setpoint; b1 an input / VCC stop holds the port outputs off |
| 58 | bit k: port k+1 cut by the guard (above 4.40 V or 2.8 A); released by asking for duty 0 and switch low on it |
| 59, 60 | rail setpoint in force, vu |

The screen open on the LCD (menu, port settings or a diagnostics page) is in byte 12's high nibble. Stop reason 5 in byte 11 is the minimum input voltage (user setting).

## Commands (OUT, byte 0 = command, 1 = argument, 2 = sequence echoed in telemetry byte 16)

| Cmd | Argument | Effect |
|---|---|---|
| 1 | – | stop charging (staggered, like the button). Always allowed |
| 2 | – | start charging. Refused if already running or if a supervisor stop is active |
| 3 | current in iu (21, 28 … 91) | set the charge current, stored in EEPROM. Refused while running or if not a valid step |
| 4 | voltage 0..3 | set the target voltage (0 = 4.20 V, 1 = 4.35 V, 2 = 3.80 V storage, 3 = 3.85 V storage), stored in EEPROM. Refused while running |
| 5 | – | ping |
| 6 | – | reboot into the USB ISP loader, so the next flash needs no button. Refused while running. `fwmon.py flash FILE` does this and then writes the image |
| 7 | session (1..255) | enter developer mode (`tools/devtool.py`): every port output off, the rail stays regulated at its setpoint, the input profile P5.3 stays as it is (the input depends on it), the LCD shows DEV MODE. Refused while running, and while another session is in control and still sending |
| 8 | bytes 1, 3–18 (below) | developer mode: set the outputs. Must be repeated at least every 0.5 s. Refused outside developer mode or from another session |
| 9 | – | leave developer mode: everything off, back to the main screen. Holding both buttons on the unit does the same |
| 10 | port 0..5, value in byte 3 | set one port's value, as in page 2 bytes 36–41 (0 off, 1 auto, else a current step in iu), stored in EEPROM. Refused while running, while that port settings mode on the unit is editing, or if not a valid value |

Command 8: byte 1 flags (b0 global enable, b1 fan, b2 profile, b3 beep (a 0→1 change beeps once), b4 rail on raw duty), byte 3 switch
pins (bit k = port k+1 to the bleed path), bytes 4–5 the rail: a setpoint in vu (at most 3972 = 4.35 V) or, with b4, a raw duty
(0..400, higher = lower rail), bytes 6–17 PWM duty of ports 1..6 (0..400), all 16 bit big-endian, byte 18 the session given
with command 7: only that host is obeyed, so two tools cannot fight over the outputs. A new session can enter once the old one has gone
quiet for 0.5 s. Guards that stay on: a port above
4.40 V or 2.8 A is cut, an input or VCC stop holds the ports off, the fan also runs when the board is warm, a rail over-voltage is
the usual fatal error. The main power enable P4.7 is not offered (it would cut the unit's own supply).
