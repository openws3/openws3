# USB monitor interface

The firmware is a USB **HID** device (VID `34bf`, PID `1004`, product "OpenWS3") alongside its normal job. No driver is
needed. The report descriptor uses a vendor-defined usage page, so the host does not turn the telemetry into keyboard or other input events. On Linux it shows up as `/dev/hidrawN`. Use `tools/fwmon.py`.

* Telemetry: 64-byte **IN** reports, polled every 10 ms, in two pages sent alternately (byte 3 = page): page 1 is the charger, page 2 the
  limiter and supervisor, so each page is refreshed every 20 ms. It is available from power-up, including during boot and on fatal
  errors, so a failing boot can be watched. `fwmon.py` merges the two pages into one snapshot.
* Commands: one 64-byte **OUT** report. The interrupt only stores it. `app_pass` executes it in the main loop with the same rules as
  the buttons, so the power path is never touched from interrupt context. **There is no raw PWM or GPIO access.**
* ISP is unaffected: hold the button while plugging USB-C to flash as before, or send command 6 (`fwmon.py isp`).
* The 0.5 ms tick interrupt has higher priority than USB. A report costs about 0.4 ms of CPU every 10 ms.

## Telemetry report (IN, 64 bytes, big-endian)

| Byte | Content |
|---|---|
| 0, 1 | magic `57 33` |
| 2 | sequence (1..255), **head**; a report is valid only if byte 63 equals it |
| 3 | page (1) |
| 4–7 | uptime, ms |
| 8 | flags: b0 running, b1–2 target voltage (0..3: 4.20 / 4.35 / 3.80 / 3.85 V), b3 outputs enabled (P5.2), b4 reserved (0), b5 rail alarm pause, b6 strap (variable input), b7 DC input chosen |
| 9 | current setting (counts: 21 = 0.3 A … 91 = 1.3 A, ×7 per 0.1 A) |
| 10 | reserved (0) |
| 11 | stop reason: 0 none, 1 input low, 2 VCC low, 3 rail, 4 user, 5 minimum input voltage (user setting) |
| 12 | low nibble: alarm shown on the display (1 rail, 2 input low, 3 VCC, 4 minimum input voltage stop); high nibble: screen open on the LCD (0 = main, 1..6 = diagnostics page, 15 = menu) |
| 13–15 | last event: sequence (increments), code (1 port full, 2 port fault, 3 stopped), port 0..5 (or the stop reason for code 3) |
| 16, 17 | last command: its sequence number, result (0 ok, 1 refused, 2 unknown) |
| 18–23 | port states 1..6: 0 idle, 1 probe, 2 charge, 3 rest, 4 full, 5 bleed, 6 fault |
| 24 | bit k = port k+1 switch pin high (bleed path) |
| 25 | phase: 1 rail wait, 2 splash, 3 VCC gate, 4 self-test, 5 input, 6 DC/Lipo choice, 7 running, 0x80 fatal error |
| 26–37 | PWM duty 1..6 (0..400), 16 bit |
| 38–49 | port voltage 1..6, counts (×4.5/4096 V), 16 bit |
| 50–55 | port current 1..6, counts (÷68.25 A), saturated at 255 |
| 56–57, 58–59, 60–61 | rail ADC8, input ADC7, bandgap ADC15 |
| 62 | rail PWM duty ÷ 4 |
| 63 | sequence, **tail** (written last; 0 while the report is being rebuilt) |

### Page 2: limiter and supervisor

Same header (bytes 0–7, byte 3 = 2, head / tail sequence in bytes 2 / 63). Everything below is zero before the run phase.

| Byte | Content |
|---|---|
| 8–11 | settings: MAX IN AMPS (0.1 A, 0 = off), MIN IN VOLTS (0.05 V, 0 = off), BELOW MIN (1 = stop the run), REGULATION (0 equal, 1 ch1 first, 2 high V first) |
| 12, 13 | power budget, 0.1 W (10000 = no limit) |
| 14, 15 / 16, 17 | its two parts: from the current limit / from the voltage floor (10000 = not active) |
| 18, 19 | estimated input current, mA |
| 20, 21 | the budget as a total port current, counts (1/68.25 A); 0 = no limit active |
| 22 | trip: 1 = the minimum input voltage stops the run |
| 23 | the limiter ran this pass (running and no rail hold) |
| 24–29 | cap per port: 0 none, 1 parked (output off), else a current in counts |
| 30–35 | the current each port wants before the limits, counts (charging ports only) |
| 36–41 | 1 = the port's cap is below the current it wants |
| 42–50 | reserved (0): input, rail and the current setting are on page 1, setpoint and target follow from the target voltage |
| 51 | some port is charging or bleeding (global enable) |
| 52–56 | supervisor: stop reason, power-off, rail cut (fatal), rail alarm, fan |
| 57 | input cell count read at boot; 58–59: stop-charging threshold, 60–61: power-down threshold, ADC counts |

Voltages, currents and the rail are zero before phase 7 except the three ADC words, which are live from phase 1.

The screen open on the LCD (menu or a diagnostics page) is in byte 12's high nibble. Stop reason 5 in byte 11 is the minimum input voltage (user setting).

## Commands (OUT, byte 0 = command, 1 = argument, 2 = sequence echoed in telemetry byte 16)

| Cmd | Argument | Effect |
|---|---|---|
| 1 | – | stop charging (staggered, like the button). Always allowed |
| 2 | – | start charging. Refused if already running or if a supervisor stop is active |
| 3 | current in counts (21, 28 … 91) | set the charge current, stored in EEPROM. Refused while running or if not a valid step |
| 4 | voltage 0..3 | set the target voltage (0 = 4.20 V, 1 = 4.35 V, 2 = 3.80 V storage, 3 = 3.85 V storage), stored in EEPROM. Refused while running |
| 5 | – | ping |
| 6 | – | reboot into the USB ISP loader, so the next flash needs no button. Refused while running. `fwmon.py flash FILE` does this and then writes the image |
