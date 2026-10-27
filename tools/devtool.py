#!/usr/bin/env python3
"""
OpenWS3 developer tool: switch every output by hand and watch every input, live (developer mode, USB commands 7 / 8 / 9, USB.md).

  devtool.py              full-screen terminal UI
  tools/openws3.html      the same in a browser (Chrome / Edge, WebHID; open the file, no server): its Advanced view, plus a monitor and a firmware updater

Keys:  up/down  pick an output     left/right  -1 / +1     [ ]  -10 / +10     { }  -50 / +50
       space    toggle (switches, enables) or set to 0 (values)              x  everything off      b  beep      q  quit

The firmware keeps its guards: the outputs go off if this tool stops sending (0.5 s), a port above 4.40 V or 2.8 A is cut
until it is asked for 0 / off, the fan also runs when the board is warm, and holding both buttons on the unit leaves the mode.
Leaving (q, Ctrl-C) switches everything off and returns the unit to its normal screen.  Not while charging.
"""
import curses, os, random, select, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fwmon

DF_GLOBAL, DF_FAN, DF_PROFILE, DF_BEEP, DF_RAIL_RAW = 1, 2, 4, 8, 16
PWM_PERIOD = 400
S_MAX = 3972                          # the highest rail setpoint the firmware accepts (4.35 V)
SEND_EVERY = 0.1                      # s; the firmware's watchdog is 0.5 s
SESSION = random.randint(1, 255)      # developer mode obeys only the host that entered it


class Outputs:
    """what we ask the unit for; rows of the editor"""
    def __init__(self, setpoint):
        self.duty = [0] * 6; self.sw = [0] * 6
        self.flags = 0; self.setpoint = setpoint; self.raw = 0
        self.rows = [('P%d PWM' % (k + 1), 'duty', k) for k in range(6)] + [('P%d BLEED SW' % (k + 1), 'sw', k) for k in range(6)] + [
            ('GLOBAL ENABLE P5.2', 'flag', DF_GLOBAL), ('FAN P0.7', 'flag', DF_FAN), ('PROFILE P5.3', 'flag', DF_PROFILE),
            ('RAIL MODE', 'flag', DF_RAIL_RAW), ('RAIL SETPOINT', 'setpoint', 0), ('RAIL RAW DUTY', 'raw', 0)]

    def value(self, row):
        _, kind, k = self.rows[row]
        if kind == 'duty': return '%3d / %d' % (self.duty[k], PWM_PERIOD)
        if kind == 'sw': return 'BLEED' if self.sw[k] else 'off'
        if kind == 'flag' and k == DF_RAIL_RAW: return 'RAW DUTY' if self.flags & k else 'REGULATED (PI)'
        if kind == 'flag': return 'ON' if self.flags & k else 'off'
        if kind == 'setpoint': return '%4d  = %.3f V' % (self.setpoint, self.setpoint * fwmon.V_PER_COUNT)
        return '%3d / %d  (higher = lower rail)' % (self.raw, PWM_PERIOD)

    def change(self, row, step):
        """step = +-n, or None to toggle / zero"""
        _, kind, k = self.rows[row]
        clamp = lambda v, hi: max(0, min(hi, v))
        if kind == 'duty': self.duty[k] = 0 if step is None else clamp(self.duty[k] + step, PWM_PERIOD)
        elif kind == 'sw': self.sw[k] = (self.sw[k] ^ 1) if step is None else int(step > 0)
        elif kind == 'flag':
            on = not (self.flags & k) if step is None else step > 0
            self.flags = (self.flags | k) if on else (self.flags & ~k)
        elif kind == 'setpoint' and step is not None: self.setpoint = clamp(self.setpoint + step, S_MAX)
        elif kind == 'raw': self.raw = 0 if step is None else clamp(self.raw + step, PWM_PERIOD)

    def all_off(self):
        self.duty = [0] * 6; self.sw = [0] * 6; self.flags &= DF_RAIL_RAW | DF_PROFILE

    def report(self, beep):
        rail = self.raw if self.flags & DF_RAIL_RAW else self.setpoint
        b = [8, self.flags | (DF_BEEP if beep else 0), 0, sum(s << k for k, s in enumerate(self.sw)), rail >> 8, rail & 0xFF]
        for d in self.duty: b += [d >> 8, d & 0xFF]
        return b + [SESSION]


def send(cmd_bytes):
    """one OUT report on a fresh descriptor (a long-lived fd wedges after an OUT report on this chip)"""
    fd = fwmon.find(os.O_WRONLY)
    try: os.write(fd, bytes([0] + cmd_bytes) + bytes(64 - len(cmd_bytes)))
    finally: os.close(fd)


def read_dev(fd, timeout):
    """the newest page 1 + page 3 until timeout; None if nothing arrived"""
    end, got = time.time() + timeout, None
    while True:
        left = end - time.time()
        if left <= 0 or not select.select([fd], [], [], left)[0]: return got
        t = fwmon.parse(os.read(fd, fwmon.REPORT))
        if t and 'dev' in t: got = t


def draw(scr, out, cur, t, msg):
    scr.erase()
    h, w = scr.getmaxyx()
    line = [0]
    def put(text, attr=0):
        if line[0] < h - 1: scr.addnstr(line[0], 0, text, w - 1, attr)
        line[0] += 1
    put('OpenWS3 devtool  (q quit, x all off, b beep)', curses.A_BOLD)
    if not t:
        put('waiting for telemetry ...'); scr.refresh(); return
    d = t['dev']
    state = []
    if d['watchdog']: state.append('WATCHDOG: no command for 0.5 s, outputs off')
    if d['supply_stop']: state.append('SUPPLY STOP (%s): port outputs off' % fwmon.STOP.get(t['stop'], 'input/vcc'))
    put('uptime %.1f s   %s' % (t['ms'] / 1000, '  '.join(state) or 'link ok'), curses.A_REVERSE if state else 0)
    put('')
    put('INPUTS', curses.A_BOLD)
    bg = d['bandgap']
    volts = lambda raw: fwmon.reading(raw, bg) * fwmon.V_PER_COUNT          # page 3 carries raw counts: correct them with the bandgap
    amps = lambda raw: fwmon.reading(raw, bg) * fwmon.A_PER_COUNT
    put('  VIN   %6.2f V (%4d)    RAIL %6.3f V (%4d, duty %3d, setpoint %4d = %.3f V)' % (
        fwmon.vin_volts(fwmon.reading(d['vin'], bg)), d['vin'], volts(d['rail']), d['rail'], d['rail_duty'], d['setpoint'], d['setpoint'] * fwmon.V_PER_COUNT))
    put('  VCC   %6.2f V (bandgap %4d)    THERMISTOR ADC0 %4d    B1 %s  B2 %s  STRAP %d' % (
        fwmon.vcc_volts(bg), d['bandgap'], d['therm'], 'DOWN' if d['b1'] else 'up', 'DOWN' if d['b2'] else 'up', d['strap']))
    put('  applied: GLOBAL %s  FAN %s  PROFILE %s  RAIL %s' % (tuple(
        'on' if d['flags'] & f else 'off' for f in (DF_GLOBAL, DF_FAN, DF_PROFILE)) + ('RAW' if d['flags'] & DF_RAIL_RAW else 'PI',)))
    put('')
    put('  PORT     VOLTS  (cnt)     AMPS  (cnt)    PWM   SW     ', curses.A_UNDERLINE)
    for k in range(6):
        cut = (d['trip'] >> k) & 1
        put('  %d     %6.3f  (%4d)   %6.3f  (%4d)    %3d   %-5s  %s' % (
            k + 1, volts(d['v'][k]), d['v'][k], amps(d['i'][k]), d['i'][k], d['duty'][k],
            'BLEED' if (d['sw'] >> k) & 1 else '-', 'CUT (over 4.40 V / 2.8 A): set 0 / off to release' if cut else ''),
            curses.A_BOLD if cut else 0)
    put('')
    put('OUTPUTS', curses.A_BOLD)
    for r, (name, _, _) in enumerate(out.rows):
        put('%s %-20s %s' % ('>' if r == cur else ' ', name, out.value(r)), curses.A_REVERSE if r == cur else 0)
    put('')
    put(msg)
    scr.refresh()


def run(scr):
    curses.curs_set(0); scr.nodelay(True); scr.keypad(True)
    if fwmon.command(7, SESSION) != 0:
        return 'refused: the unit is charging, or another program drives developer mode'
    fd = fwmon.find(os.O_RDONLY)
    try:
        t = read_dev(fd, 1.0)
        if not t: return 'no developer-mode telemetry (is the firmware up to date?)'
        out = Outputs(t['dev']['setpoint'])
        out.flags = t['dev']['flags'] & DF_PROFILE           # the profile pin as it is: the input depends on it
        out.raw = t['dev']['rail_duty']                  # a switch to raw duty starts where the regulator is
        cur, beep, msg, last_send, last_rx = 0, 0, '', 0, time.time()
        keys = {curses.KEY_LEFT: -1, curses.KEY_RIGHT: 1, ord('['): -10, ord(']'): 10, ord('{'): -50, ord('}'): 50}
        while True:
            k = scr.getch()
            while k != -1:
                if k in (ord('q'), 27): return None
                if k == curses.KEY_UP: cur = (cur - 1) % len(out.rows)
                elif k == curses.KEY_DOWN: cur = (cur + 1) % len(out.rows)
                elif k in keys: out.change(cur, keys[k])
                elif k == ord(' '): out.change(cur, None)
                elif k == ord('x'): out.all_off(); msg = 'everything off'
                elif k == ord('b'): beep = 3
                last_send = 0                            # a change goes out at once
                k = scr.getch()
            if time.time() - last_send >= SEND_EVERY:
                send(out.report(beep > 0)); beep = max(0, beep - 1); last_send = time.time()
            n = read_dev(fd, 0.03)
            if n:
                t, last_rx = n, time.time()
                if t['debug_page'] != 14: return 'the unit left developer mode (both buttons held?)'
            elif time.time() - last_rx > 1.0:            # recover a descriptor that stopped delivering
                os.close(fd); fd = fwmon.find(os.O_RDONLY); last_rx = time.time()
            draw(scr, out, cur, t, msg)
    finally:
        os.close(fd)


def main():
    try:
        msg = curses.wrapper(run)
    except KeyboardInterrupt:
        msg = None
    finally:
        try: fwmon.command(9)                            # everything off, back to the normal screen
        except SystemExit: pass
    if msg: sys.exit(msg)


if __name__ == '__main__':
    main()
