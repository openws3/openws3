#!/usr/bin/env python3
"""
Host tool for the OpenWS3 (WhoopStor V3 open firmware)'s USB monitor interface (HID, 34bf:1004, no driver; Linux hidraw).
Layout and commands: USB.md.

  fwmon.py status                  one decoded snapshot
  fwmon.py watch [-n N] [--raw]    live view: a line whenever a port changes state or an event happens, plus a summary every second
  fwmon.py log FILE [--secs S]     JSON lines, one per telemetry report (about every 10 ms), for later analysis
  fwmon.py start | stop | ping
  fwmon.py current 0.5             set the charge current (0.3 ... 1.3 A in 0.1 A steps; refused while charging)
  fwmon.py voltage 4.20            target voltage: 4.20, 4.35, 3.80 or 3.85 (V; the last two are storage); refused while charging
  fwmon.py port 2 0.8              one port's current: auto (the global one), off, or 0.3 ... 1.3 A; refused while charging

Driving the outputs by hand: tools/devtool.py.

Needs read/write access to /dev/hidraw* (udev rule or sudo).  Commands open the device afresh each time (a long-lived
fd wedges after an OUT report on this chip).
"""
import argparse, json, os, select, struct, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import isp

VID, PID = 0x34BF, 0x1004
REPORT = 64

PHASE = {1: 'rail-up', 2: 'splash', 7: 'run', 0x80: 'FATAL'}
STATE = ['IDLE', 'PROBE', 'CHARGE', 'REST', 'FULL', 'BLEED', 'FAULT']
STOP = {0: '-', 1: 'input-low', 2: 'vcc-low', 3: 'rail', 4: 'user', 5: 'min-input-voltage'}
EVENT = {0: '-', 1: 'port-full', 2: 'port-fault', 3: 'stopped'}
VOLTS = ['4.20', '4.35', '3.80', '3.85']
RESULT = {0: 'ok', 1: 'refused', 2: 'unknown command'}
V_PER_COUNT = 4.5 / 4096            # per vu: the charger's voltage readings (port voltages, page 1 bytes 38-49)
A_PER_COUNT = 1 / 68.25             # per iu: its current readings
BG_NOM = 1071                       # bandgap count at the VCC the voltage scales were made at: a voltage count x BG_NOM / bandgap = its reading (src/adc.h)
reading = lambda raw, bandgap: raw * BG_NOM / bandgap if bandgap else raw     # the voltages (ports, input, rail); the currents are not corrected
vin_volts = lambda reading: (reading + 1) * 0.4 / 61           # cal.c cal_vin_mv
vcc_volts = lambda bandgap: 1.19 * 4096 / bandgap if bandgap else 0
vmin_volts = lambda v: v * 0.05 if v <= 200 else 10 + (v - 200) * 0.5        # limits.h VMIN_MV


LMODE = ['EQUAL', 'CH1 FIRST', 'HIGH V FIRST']
P_NONE = 10000                       # budget value meaning "no limit"
_lim = {}                            # the latest page 2 (limits / supervision), merged into every page 1 report
_dev = {}                            # the latest page 3 (developer mode, tools/devtool.py), merged the same way


def parse_limits(r):
    u16 = lambda i: (r[i] << 8) | r[i + 1]
    return dict(lim=dict(
        ilim=r[8], vmin=r[9], vcut=r[10], lmode=r[11], budget=u16(12), p_cur=u16(14), p_vol=u16(16), est_ma=u16(18), bcnt=u16(20),
        trip=r[22], active=r[23], gate=list(r[24:30]), want=list(r[30:36]), pset=list(r[36:42]), stack=r[46], setpoint=u16(42), target=u16(44),
        any_active=r[51], sup_stop=r[52], rail_cut=r[54], rail_alarm=r[55], fan=r[56], build=(u16(57) << 16) | u16(59), dirty=r[61], pre=r[47]))


def parse_dev(r):
    u16 = lambda i: (r[i] << 8) | r[i + 1]
    return dict(dev=dict(
        v=[u16(8 + 2 * k) for k in range(6)], i=[u16(20 + 2 * k) for k in range(6)], therm=u16(32), vin=u16(34), rail=u16(36), bandgap=u16(38),
        duty=[u16(40 + 2 * k) for k in range(6)], rail_duty=u16(52), sw=r[54], flags=r[55], b1=r[56] & 1, b2=(r[56] >> 1) & 1, strap=(r[56] >> 2) & 1,
        watchdog=r[57] & 1, supply_stop=(r[57] >> 1) & 1, trip=r[58], setpoint=u16(59)))


def parse(r):
    """decode one 64-byte telemetry report; None if it is not a complete one.  Pages 2 and 3 (byte 3) only update `_lim` / `_dev`
    and return None."""
    if len(r) != REPORT or r[0] != 0x57 or r[1] != 0x33 or r[2] == 0 or r[2] != r[63]:
        return None
    if r[3] == 2:
        _lim.update(parse_limits(r)); _dev.clear()
        return None
    if r[3] == 3:
        _dev.update(parse_dev(r)); _lim.clear()
        return None
    u16 = lambda i: (r[i] << 8) | r[i + 1]
    f = r[8]
    return dict(
        version=r[3], fw=r[10], ms=struct.unpack_from('>I', r, 4)[0],
        run=f & 1, volt=(f >> 1) & 3, outputs=(f >> 3) & 1, rail_hold=(f >> 5) & 1,
        strap=(f >> 6) & 1,
        iset=r[9], stop=r[11], alarm=r[12] & 15, debug_page=r[12] >> 4, ev_seq=r[13], ev_code=r[14], ev_port=r[15],
        cmd_seq=r[16], cmd_res=r[17], state=list(r[18:24]), sw=[(r[24] >> k) & 1 for k in range(6)], phase=r[25],
        duty=[u16(26 + 2 * k) for k in range(6)], v=[u16(38 + 2 * k) for k in range(6)], i=list(r[50:56]),
        rail=u16(56), vin=u16(58), bandgap=u16(60), rail_duty=r[62] * 4, **_lim, **_dev)


def find(flags=os.O_RDWR):
    path = isp.find_hidraw(lambda v, p: (v, p) == (VID, PID), quiet=True)
    if path: return os.open(path, flags)
    sys.exit('no OpenWS3 USB device (%04x:%04x) found - is it running OpenWS3, and are /dev/hidraw* readable?' % (VID, PID))


def read_report(fd, timeout=1.0):
    end = time.time() + timeout
    while time.time() < end:
        if select.select([fd], [], [], max(0, end - time.time()))[0]:
            t = parse(os.read(fd, REPORT))
            if t: return t
    return None


def snapshot():
    fd = find()
    try:
        t = read_report(fd, 2.0)
        if t and 'lim' not in t: t = read_report(fd, 2.0) or t       # the first report can precede the first page 2
    finally:
        os.close(fd)
    if not t: sys.exit('no telemetry')
    return t


def command(cmd, arg=0, ack=True, extra=b''):
    """send one command (extra: bytes 3.. of the report), wait for the acknowledgement in the telemetry; returns the result code"""
    seq = int(time.time() * 10) % 255 + 1
    fd = find()
    try:
        os.write(fd, bytes([0, cmd, arg, seq]) + bytes(extra) + bytes(61 - len(extra)))
        if not ack: return 0              # the device resets: nothing to wait for
        end = time.time() + 3
        while time.time() < end:
            t = read_report(fd, 0.5)
            if t and t['cmd_seq'] == seq: return t['cmd_res']
    finally:
        os.close(fd)
    sys.exit('command not acknowledged')


def fmt_status(t):
    bg = t['bandgap']
    head = '%8.1fs %-9s %s %s  set %.1fA  %s  rail %.3fV  vin %.2fV  vcc %.2fV  (counts %d %d %d)' % (
        t['ms'] / 1000, PHASE.get(t['phase'], t['phase']), 'RUN' if t['run'] else 'idle', VOLTS[t['volt']] + ' V',
        0.1 * t['iset'] / 7.0, 'hold' if t['rail_hold'] else '', reading(t['rail'], bg) * V_PER_COUNT, vin_volts(reading(t['vin'], bg)),
        vcc_volts(bg), t['rail'], t['vin'], bg)
    cells = '  '.join('%d:%-6s %.3fV %3dmA%s' % (k + 1, STATE[t['state'][k]], t['v'][k] * V_PER_COUNT, round(t['i'][k] * A_PER_COUNT * 1000),
                                                  ' sw' if t['sw'][k] else '') for k in range(6) if t['state'][k] or t['v'][k] > 900)
    extra = ''
    if t['stop']: extra += '  stop=%s' % STOP.get(t['stop'], t['stop'])
    if t['alarm']: extra += '  alarm=%d' % t['alarm']
    if t['debug_page']: extra += '  screen=%s' % {15: 'menu', 14: 'developer mode'}.get(t['debug_page'], 'diag%d' % t['debug_page'])
    return head + extra + ('\n         ' + cells if cells else '') + fmt_psets(t) + fmt_limits(t)


def pset_name(v):
    return 'off' if v == 0 else 'auto' if v == 1 else '%.1fA' % (v / 70)


def fmt_psets(t):
    """each port's own value (page 2), when any port has one"""
    L = t.get('lim')
    if not L or t['phase'] != 7 or all(v == 1 for v in L['pset']): return ''          # zero before the run phase
    return '\n         port settings  ' + '  '.join('%d: %s' % (k + 1, pset_name(v)) for k, v in enumerate(L['pset']))


def fmt_limits(t):
    """the limiter / supervisor lines (page 2), empty if the firmware does not send it"""
    L = t.get('lim')
    if not L or not t['run']: return ''
    amps = lambda c: '%.2fA' % (c * A_PER_COUNT)
    gate = lambda g: ('waiting' if (g & 3) == 2 else 'held' if g & 3 else 'free') + (' wants more' if g & 4 else '') + \
        (' rising' if g & 8 else '') + (' brought down' if g & 16 else '')
    watts = lambda p: 'none' if p >= P_NONE else '%.1fW' % (p / 10)
    out = '\n         limits  in %s  min %s  below-min %s  regulation %s  | %s' % (
        '%.1fA' % (L['ilim'] / 10) if L['ilim'] else 'off', '%.2fV' % vmin_volts(L['vmin']) if L['vmin'] else 'off',
        'STOP RUN' if L['vcut'] else 'LIMIT', LMODE[L['lmode']] if L['lmode'] < len(LMODE) else L['lmode'], 'evaluated' if L['active'] else 'idle')
    out += '\n         budget  %s (current %s, voltage %s) = %s over the ports (%d counts)  est input %.2fA  trip %d' % (
        watts(L['budget']), watts(L['p_cur']), watts(L['p_vol']), amps(L['bcnt']) if L['bcnt'] else 'no limit', L['bcnt'], L['est_ma'] / 1000, L['trip'])
    ports = [k for k in range(6) if t['state'][k] == 2]
    if ports:
        out += '\n         ports   ' + '  '.join('%d: set %s %s' % (k + 1, amps(L['want'][k]), gate(L['gate'][k])) for k in ports)
    out += '\n         setpoints  target %.3fV  rail %.3fV  sup stop=%s' % (
        L['target'] * V_PER_COUNT, L['setpoint'] * V_PER_COUNT, STOP.get(L['sup_stop'], L['sup_stop']))
    return out


def watch(n, raw):
    fd = find(os.O_RDONLY)
    last = None; last_print = 0; count = 0
    try:
        while n is None or count < n:
            t = read_report(fd, 2.0)
            if not t: print('(no telemetry)'); continue
            changed = last is None or t['state'] != last['state'] or t['ev_seq'] != last['ev_seq'] or t['run'] != last['run'] \
                or t['stop'] != last['stop'] or t['phase'] != last['phase'] or t['alarm'] != last['alarm'] or t['rail_hold'] != last['rail_hold']
            if changed or time.time() - last_print >= 1.0:
                if raw: print(json.dumps(t))
                else:
                    print(fmt_status(t))
                    if last and t['ev_seq'] != last['ev_seq']:
                        print('         EVENT %s port %s' % (EVENT.get(t['ev_code'], t['ev_code']),
                                                             t['ev_port'] + 1 if t['ev_code'] < 3 else STOP.get(t['ev_port'], t['ev_port'])))
                last_print = time.time(); count += 1
            last = t
    except KeyboardInterrupt:
        pass


def log(path, secs):
    fd = find(os.O_RDONLY)
    end = time.time() + secs if secs else None
    prev = None; n = 0
    with open(path, 'w') as f:
        try:
            while end is None or time.time() < end:
                t = read_report(fd, 2.0)
                if t and (prev is None or t['ms'] != prev):
                    f.write(json.dumps(t) + '\n'); prev = t['ms']; n += 1
        except KeyboardInterrupt:
            pass
    print('%d reports -> %s' % (n, path))


def flash(path):
    """Reboot into the ISP loader (command 6) and write the image with tools/isp.py."""
    data = open(path, 'rb').read()
    try:
        command(6, 0, ack=False)
    except SystemExit:
        print('(no firmware USB device: assuming it is already in ISP)')
    end = time.time() + 12
    dev = None
    while not dev and time.time() < end:
        time.sleep(0.2)
        dev = isp.find_stc(quiet=True)
    if not dev: sys.exit('the ISP loader did not appear')
    sys.exit(isp.write(dev, data, erase=True))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('status')
    w = sub.add_parser('watch'); w.add_argument('-n', type=int); w.add_argument('--raw', action='store_true')
    l = sub.add_parser('log'); l.add_argument('file'); l.add_argument('--secs', type=float)
    for name in ('start', 'stop', 'ping', 'isp'): sub.add_parser(name)
    f = sub.add_parser('flash'); f.add_argument('file')
    c = sub.add_parser('current'); c.add_argument('amps', type=float)
    m = sub.add_parser('voltage'); m.add_argument('volts', choices=VOLTS)
    p = sub.add_parser('port', help='one port\'s current: auto (the global one), off, or 0.3 .. 1.3 A')
    p.add_argument('n', type=int, choices=range(1, 7)); p.add_argument('value')
    a = ap.parse_args()

    if a.cmd == 'status':
        t = snapshot()
        print(fmt_status(t))
        if t['fw'] and 'lim' in t:
            print('firmware %d.%d%s build %08x%s' % (t['fw'] >> 4, t['fw'] & 15, '-beta.%d' % t['lim']['pre'] if t['lim']['pre'] else '', t['lim']['build'], '+' if t['lim']['dirty'] else '') +
                  ('  stack free %d bytes' % t['lim']['stack'] if t['phase'] == 7 else ''))
    elif a.cmd == 'watch': watch(a.n, a.raw)
    elif a.cmd == 'log': log(a.file, a.secs)
    elif a.cmd == 'flash': flash(a.file)
    else:
        code, arg = {'stop': (1, 0), 'start': (2, 0), 'ping': (5, 0), 'isp': (6, 0)}.get(a.cmd, (None, 0))
        if a.cmd == 'current': code, arg = 3, 7 * round(a.amps * 10)
        if a.cmd == 'voltage': code, arg = 4, VOLTS.index(a.volts)
        extra = b''
        if a.cmd == 'port':
            v = {'off': 0, 'auto': 1}.get(a.value.lower())
            if v is None: v = 7 * round(float(a.value.rstrip('aA')) * 10)
            code, arg, extra = 10, a.n - 1, bytes([v])
        res = command(code, arg, ack=a.cmd != 'isp', extra=extra)
        print(RESULT.get(res, res))
        sys.exit(0 if res == 0 else 1)


if __name__ == '__main__':
    main()
