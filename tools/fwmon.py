#!/usr/bin/env python3
"""
Host tool for the OpenWS3 (WhoopStor V3 open firmware)'s USB monitor interface (HID, 34bf:1004, no driver; Linux hidraw).
Layout and commands: fw/USB.md.

  fwmon.py status                  one decoded snapshot
  fwmon.py watch [-n N] [--raw]    live view: a line whenever a port changes state or an event happens, plus a summary every second
  fwmon.py log FILE [--secs S]     JSON lines, one per telemetry report (about every 10 ms), for later analysis
  fwmon.py start | stop | ping
  fwmon.py current 0.5             set the charge current (0.3 ... 1.3 A in 0.1 A steps; refused while charging)
  fwmon.py mode 0..3               4.20 / 4.35 / 3.80 / 3.85 V; refused while charging

Needs read/write access to /dev/hidraw* (udev rule or sudo).  Commands open the device afresh each time (a long-lived
fd wedges after an OUT report on this chip - see tools/bench/RESULTS.md).
"""
import argparse, fcntl, json, os, select, struct, sys, time

VID, PID = 0x34BF, 0x1004
REPORT = 64

PHASE = {1: 'rail-wait', 2: 'splash', 3: 'vcc-gate', 4: 'self-test', 5: 'input', 6: 'choose', 7: 'run', 0x80: 'FATAL'}
STATE = ['IDLE', 'PROBE', 'CHARGE', 'REST', 'PAUSE', 'FULL', 'BLEED', 'FAULT']
STOP = {0: '-', 1: 'input-low', 2: 'vcc-low', 3: 'rail', 4: 'user', 5: 'min-input-voltage'}
EVENT = {0: '-', 1: 'port-full', 2: 'port-fault', 3: 'stopped'}
MODE_V = ['4.20 V', '4.35 V', '3.80 V', '3.85 V']
RESULT = {0: 'ok', 1: 'refused', 2: 'unknown command'}
V_PER_COUNT = 4.5 / 4096
A_PER_COUNT = 1 / 68.25


def parse(r):
    """decode one 64-byte telemetry report; None if it is not a complete one"""
    if len(r) != REPORT or r[0] != 0x57 or r[1] != 0x33 or r[2] == 0 or r[2] != r[63]:
        return None
    u16 = lambda i: (r[i] << 8) | r[i + 1]
    f = r[8]
    return dict(
        version=r[3], ms=struct.unpack_from('>I', r, 4)[0],
        run=f & 1, mode=(f >> 1) & 3, outputs=(f >> 3) & 1, charge_enabled=(f >> 4) & 1, rail_hold=(f >> 5) & 1,
        strap=(f >> 6) & 1, dc=(f >> 7) & 1,
        iset=r[9], iset_user=r[10], stop=r[11], alarm=r[12] & 15, debug_page=r[12] >> 4, ev_seq=r[13], ev_code=r[14], ev_port=r[15],
        cmd_seq=r[16], cmd_res=r[17], state=list(r[18:24]), sw=[(r[24] >> k) & 1 for k in range(6)], phase=r[25],
        duty=[u16(26 + 2 * k) for k in range(6)], v=[u16(38 + 2 * k) for k in range(6)], i=list(r[50:56]),
        rail=u16(56), vin=u16(58), bandgap=u16(60), rail_duty=r[62] * 4)


def find(flags=os.O_RDWR):
    for n in range(32):
        path = '/dev/hidraw%d' % n
        if not os.path.exists(path): continue
        try:
            fd = os.open(path, flags)
        except OSError:
            continue
        buf = bytearray(12)
        try:
            fcntl.ioctl(fd, 0x80084803, buf, True)             # HIDIOCGRAWINFO
            _, vid, pid = struct.unpack_from('<IHH', buf, 0)
        except OSError:
            vid = pid = 0
        if (vid, pid) == (VID, PID): return fd
        os.close(fd)
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
    finally:
        os.close(fd)
    if not t: sys.exit('no telemetry')
    return t


def command(cmd, arg=0, ack=True):
    """send one command, wait for the acknowledgement in the telemetry; returns the result code"""
    seq = int(time.time() * 10) % 255 + 1
    fd = find()
    try:
        os.write(fd, bytes([0, cmd, arg, seq]) + bytes(61))
        if not ack: return 0              # the device resets: nothing to wait for
        end = time.time() + 3
        while time.time() < end:
            t = read_report(fd, 0.5)
            if t and t['cmd_seq'] == seq: return t['cmd_res']
    finally:
        os.close(fd)
    sys.exit('command not acknowledged')


def fmt_status(t):
    head = '%8.1fs %-9s %s %s  set %.1fA  %s  rail %d  vin %d  vcc %d' % (
        t['ms'] / 1000, PHASE.get(t['phase'], t['phase']), 'RUN' if t['run'] else 'idle', MODE_V[t['mode']],
        0.1 * t['iset'] / 7.0, 'hold' if t['rail_hold'] else '', t['rail'], t['vin'], t['bandgap'])
    cells = '  '.join('%d:%-6s %.3fV %3dmA%s' % (k + 1, STATE[t['state'][k]], t['v'][k] * V_PER_COUNT, round(t['i'][k] * A_PER_COUNT * 1000),
                                                  ' sw' if t['sw'][k] else '') for k in range(6) if t['state'][k] or t['v'][k] > 900)
    extra = ''
    if t['stop']: extra += '  stop=%s' % STOP.get(t['stop'], t['stop'])
    if t['alarm']: extra += '  alarm=%d' % t['alarm']
    if t['debug_page']: extra += '  screen=%s' % ('menu' if t['debug_page'] == 15 else 'diag%d' % t['debug_page'])
    return head + extra + ('\n         ' + cells if cells else '')


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
    """Reboot into the ISP loader (command 6) and write the image with tools/dump/isp.py."""
    import subprocess
    isp = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'isp.py')
    try:
        command(6, 0, ack=False)
    except SystemExit:
        print('(no firmware USB device: assuming it is already in ISP)')
    time.sleep(2.0)
    for _ in range(10):
        if subprocess.run([sys.executable, '-I', isp, 'info'], capture_output=True).returncode == 0: break
        time.sleep(1.0)
    sys.exit(subprocess.run([sys.executable, '-I', isp, 'write', path, '--erase']).returncode)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('status')
    w = sub.add_parser('watch'); w.add_argument('-n', type=int); w.add_argument('--raw', action='store_true')
    l = sub.add_parser('log'); l.add_argument('file'); l.add_argument('--secs', type=float)
    for name in ('start', 'stop', 'ping', 'isp'): sub.add_parser(name)
    f = sub.add_parser('flash'); f.add_argument('file')
    c = sub.add_parser('current'); c.add_argument('amps', type=float)
    m = sub.add_parser('mode'); m.add_argument('mode', type=int, choices=range(4))
    a = ap.parse_args()

    if a.cmd == 'status': print(fmt_status(snapshot()))
    elif a.cmd == 'watch': watch(a.n, a.raw)
    elif a.cmd == 'log': log(a.file, a.secs)
    elif a.cmd == 'flash': flash(a.file)
    else:
        code, arg = {'stop': (1, 0), 'start': (2, 0), 'ping': (5, 0), 'isp': (6, 0)}.get(a.cmd, (None, 0))
        if a.cmd == 'current': code, arg = 3, 7 * round(a.amps * 10)
        if a.cmd == 'mode': code, arg = 4, a.mode
        res = command(code, arg, ack=a.cmd != 'isp')
        print(RESULT.get(res, res))
        sys.exit(0 if res == 0 else 1)


if __name__ == '__main__':
    main()
