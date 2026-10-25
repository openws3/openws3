# USB monitor interface

The firmware is a USB **HID** device (VID `34bf`, PID `1004`, product "OpenWS3") alongside its normal job. No driver is
needed. The report descriptor uses a vendor-defined usage page, so the host does not turn the telemetry into keyboard or other input events. On Linux it shows up as `/dev/hidrawN`. Use `tools/fwmon.py`.

* Telemetry: one 64-byte **IN** report, polled every 10 ms. It is available from power-up, including during boot and on fatal
  errors, so a failing boot can be watched.
* Commands: one 64-byte **OUT** report. The interrupt only stores it. `app_pass` executes it in the main loop with the same rules as
  the buttons, so the power path is never touched from interrupt context. **There is no raw PWM or GPIO access.**
* ISP is unaffected: hold the button while plugging USB-C to flash as before, or send command 6 (`fwmon.py isp`).
* The 1 ms tick interrupt has higher priority than USB. A report costs about 0.4 ms of CPU every 10 ms.

## Telemetry report (IN, 64 bytes, big-endian)

| Byte | Content |
|---|---|
| 0, 1 | magic `57 33` |
| 2 | sequence (1..255), **head**; a report is valid only if byte 63 equals it |
| 3 | layout version (1) |
| 4–7 | uptime, ms |
| 8 | flags: b0 running, b1–2 mode, b3 outputs enabled (P5.2), b4 charging compiled in, b5 rail alarm pause, b6 strap (variable input), b7 DC input chosen |
| 9 | effective current setting (counts: 21 = 0.3 A … 91 = 1.3 A, ×7 per 0.1 A) |
| 10 | user current setting (before the input caps) |
| 11 | stop reason: 0 none, 1 input low, 2 VCC low, 3 rail, 4 user, 5 minimum input voltage (user setting) |
| 12 | low nibble: alarm shown on the display (6 = minimum input voltage stop); high nibble: screen open on the LCD (0 = main, 1..6 = diagnostics page, 15 = menu) |
| 13–15 | last event: sequence (increments), code (1 port full, 2 port fault, 3 stopped), port 0..5 (or the stop reason for code 3) |
| 16, 17 | last command: its sequence number, result (0 ok, 1 refused, 2 unknown) |
| 18–23 | port states 1..6: 0 idle, 1 probe, 2 charge, 3 rest, 4 pause, 5 full, 6 bleed, 7 fault |
| 24 | bit k = port k+1 switch pin high (bleed path) |
| 25 | phase: 1 rail wait, 2 splash, 3 VCC gate, 4 self-test, 5 input, 6 DC/Lipo choice, 7 running, 0x80 fatal error |
| 26–37 | PWM duty 1..6 (0..400), 16 bit |
| 38–49 | port voltage 1..6, counts (×4.5/4096 V), 16 bit |
| 50–55 | port current 1..6, counts (÷68.25 A), saturated at 255 |
| 56–57, 58–59, 60–61 | rail ADC8, input ADC7, bandgap ADC15 |
| 62 | rail PWM duty ÷ 4 |
| 63 | sequence, **tail** (written last; 0 while the report is being rebuilt) |

Voltages, currents and the rail are zero before phase 7 except the three ADC words, which are live from phase 1.

The screen open on the LCD (menu or a diagnostics page, B1+B2 chord) is in byte 12's high nibble. Stop reasons in byte 11 now include 5 (minimum input voltage, user setting).

## Commands (OUT, byte 0 = command, 1 = argument, 2 = sequence echoed in telemetry byte 16)

| Cmd | Argument | Effect |
|---|---|---|
| 1 | – | stop charging (staggered, like the button). Always allowed |
| 2 | – | start charging. Refused if already running, if a supervisor stop is active, or in a `CHARGE=0` image |
| 3 | current in counts (21, 28 … 91) | set the charge current, stored in EEPROM. Refused while running or if not a valid step |
| 4 | mode 0..3 | set the mode, stored in EEPROM. Refused while running |
| 5 | – | ping |
| 6 | – | reboot into the USB ISP loader, so the next flash needs no button. Refused while running. `fwmon.py flash FILE` does this and then writes the image |
