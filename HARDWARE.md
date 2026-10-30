# WhoopStor V3 hardware notes

What the firmware assumes about the board. **V** = checked on the hardware or by measurement, **I** = inferred from behaviour (probably right,
not measured directly). `src/board.h` is the single source of truth for pins.

## Overview

* MCU STC8H8K64U (8051 core, 64 KB flash, 8 KB XRAM), internal RC trimmed to about 40 MHz (V).
* Six charge ports. Each has a current-control PWM output, a switch pin and its own voltage and current sense. One common rail
  feeds all ports; a PWM on P3.3 regulates the rail to a setpoint just above the cell target (higher duty = lower rail) (V).
* One buck stage is visible on the board (I). The input takes roughly 5 V up to about 20 V (a 19.7 V supply has been seen).
* 160x80 ST7735S colour LCD on bit-banged SPI, two buttons, a buzzer, a fan output and a USB device port (used for ISP and for the monitor).

## Pins

| Function | Pin |
|---|---|
| Port 1..6 switch | P4.5, P2.7, P2.5, P4.2, P4.3, P4.4 |
| Port 1..6 current PWM | PWMA/PWMB channels, see `PORT_CCR_INIT` in `board.h` (100 kHz, period 400) |
| Rail PWM | P3.3 (PWM7) |
| Main power enable / global charge enable | P4.7 / P5.2 |
| Input profile enable / input strap | P5.3 / P3.4 (I) |
| Button 1 / button 2 | P3.2 (INT0) / P3.6 (INT2) |
| Buzzer tone / fan-and-alarm level | P4.6 / P0.7 |
| LCD | SDA P3.5, SCL P3.7, CS P4.1, DC P5.1, RST P5.0 |

Switch pin (V): **high** adds the bleed load, **low** removes it. It does not disconnect the charge path: with the PWM running, a high
switch pin only lowers the net current into the cell. `P2` carries power-path switches, which is why the firmware avoids initialised XRAM and the medium memory model (see the README).

## ADC

| Channel | Signal | Scale |
|---|---|---|
| 1..6 | port voltage | 1 count = 4.5 V / 4096 = 1.0986 mV (V: mode targets land exactly on 4.20 / 4.35 / 3.80 / 3.85 V) |
| 12, 14, 13, 9, 10, 11 | port current (ports 1..6) | 1 count = 1/68.25 A = 14.65 mA (V) |
| 7 | input voltage | about 6.6 mV per count, ~26.9 V full scale (I, needs a sweep) |
| 8 | common rail | same scale as the port voltages |
| 15 | internal 1.19 V bandgap | gives VCC |
| 0 | probably a board thermistor (I) | the fan is switched on below 1300 and off above 1451 |

Port current is read directly across a pair of 0.15 ohm resistors in parallel, 75 mohm per port (V). The scale (1 A = 75 mV =
68.25 counts) means there is no amplifier, so the resistor is on the low side, between the cell's negative terminal and ground (I).

Each port has a bleed load: an 18 ohm resistor from the cell's positive terminal through a transistor (the switch pin) to ground (V).
At 4.15 V that is about 0.23 A and 1 W. The bleed current returns to the cell through the sense resistor in the opposite direction, so the
sensed value is the **net** current into the cell (charge minus bleed): with the PWM off and the bleed on, the pin sits about
-17 mV below ground, which the ADC reads as 0. With both on, the reading drops by up to the bleed current and clamps at 0 when the
charge current is lower. The firmware therefore cannot measure a bleed current; the displayed bleed value is an estimate from the voltage.

## Behaviour worth knowing

* Rail PWM gain is about 1 mA per duty count at the port; the soft ramp of 1 count per 100 control passes takes minutes to reach 0.3 A.
* The ADC reads high when VCC sags (the reference is VCC); the firmware watches the bandgap and stops below about 4.0 V.
* A weak input (PC USB port) sags under load: use MAX IN AMPS and MIN IN VOLTS in the menu.
* The input profile pin (P5.3) is a bypass around a diode in the USB-C input path, set at boot when the strap P3.4 reads low (V: with it off
  the input of a USB-powered unit sits about 0.6 V lower, 5.15 to 4.53 V, and VCC about 0.24 V lower, 4.55 to 4.31 V; the drop is a diode's,
  with load too). It also conducts backwards (V): with it on, a supply on the XT60 is fed into the USB-C connector. Not known: what the
  strap is wired to. The menu setting DISABLE USB POWER keeps the pin off: `hal_profile` in `hal.c` is the only place that drives it, and
  it never sets it while the setting is on.
* The global charge enable (P5.2) raises the voltage of an empty port by about 0.3 V (V); read port voltages with it on only when a cell is present.
* Input current is **estimated** (rail voltage x sum of port currents / (90 % x input voltage)); the 90 % converter efficiency is a guess.

## Settings storage

Settings use the chip's IAP (EEPROM area of the flash): one 512-byte sector at offset 0x0800, a layout byte followed by the
fields of `cfg_t` (`src/cfg.h`). A page without the current layout byte loads the defaults.

## Flashing

The STC8H USB loader enumerates as `34bf:1001` when the chip is reset with the ISP button held (or via `fwmon.py isp` from OpenWS3).
The loader accepts plain (unencrypted) images; `tools/isp.py` implements the frames it needs.
