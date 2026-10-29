#!/usr/bin/env python3
"""
STC8H USB-ISP client (Linux hidraw): flashes an image through the chip's built-in USB loader.

Frame format (both directions), as used by the loader:

    46 B9 6A|68  <len:2 BE>  <cmd>  <data...>  <sum:2 BE>  16

    len  = total_frame_len - 2          (i.e. 3 + cmd + data + sum + term)
    sum  = sum of every byte from the 0x6A/0x68 magic byte to the last data
           byte, mod 0x10000   (the leading 46 B9 are excluded)

Program command: 0x32 first block, 0x12 subsequent.

HID: 64-byte reports; host writes are 65 bytes with a leading report id 0.

Usage:
    isp.py info                     handshake only (read-only)
    isp.py write FILE [--addr A] [--erase]
    any command accepts --dry-run
"""
import os, sys, time, select, struct, fcntl, argparse

MAGIC_HOST = b'\x46\xb9\x6a'
MAGIC_DEV  = b'\x46\xb9\x68'
TERM       = 0x16

REPORT = 64

# ---- HIDIOCGRAWINFO to confirm we opened the right thing ----
def hidraw_info(fd):
    buf = bytearray(12)
    try:
        fcntl.ioctl(fd, 0x80084803, buf, True)   # HIDIOCGRAWINFO
        bustype, vendor, product = struct.unpack_from('<IHH', buf, 0)
        return vendor, product
    except OSError:
        return None, None

def build(cmd, data=b''):
    body = bytes([cmd]) + data
    total = 3 + len(body) + 2 + 1          # magic + body + checksum + terminator
    chk = (0x6a + (total >> 8) + (total & 0xff) + sum(body)) & 0xffff
    return (MAGIC_HOST + struct.pack('>H', total) + body +
            struct.pack('>H', chk) + bytes([TERM]))

def program_frame(cmd, addr, payload):
    return build(cmd, struct.pack('>H', addr) + b'\x5a\xa5' + payload)

VID = 0x34bf
FW_PID = 0x1004                          # the running OpenWS3 firmware (same vendor id): not the loader

def find_hidraw(match, quiet=False):
    """the first /dev/hidraw* whose (vendor, product) satisfies match(v, p), or None"""
    for i in range(32):
        p = f'/dev/hidraw{i}'
        if not os.path.exists(p):
            continue
        try:
            fd = os.open(p, os.O_RDWR)
        except PermissionError:
            if not quiet:
                print(f'[!] {p} exists but is not readable by this user')
                print('    fix once:  sudo chmod 666 ' + p)
                print('    or permanently:')
                print('      echo \'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="34bf", MODE="0666"\' \\')
                print('        | sudo tee /etc/udev/rules.d/99-stc-isp.rules')
                print('      sudo udevadm control --reload-rules && sudo udevadm trigger')
            continue
        except OSError:
            continue
        v, pid = hidraw_info(fd)
        os.close(fd)
        if match(v, pid):
            return p
    return None

def find_stc(quiet=False):
    return find_hidraw(lambda v, p: v == VID and p != FW_PID, quiet)

class Isp:
    def __init__(self, path, verbose=True):
        self.fd = os.open(path, os.O_RDWR)
        self.verbose = verbose
        v, p = hidraw_info(self.fd)
        if verbose:
            print(f'[i] {path}: vendor={v and hex(v)} product={p and hex(p)}')

    def send_frame(self, frame):
        # pad to a whole number of 64-byte reports, then prefix report id 0
        pad = (-len(frame)) % REPORT
        frame = frame + b'\x00' * pad
        for i in range(0, len(frame), REPORT):
            os.write(self.fd, b'\x00' + frame[i:i+REPORT])

    def recv_frame(self, timeout=2.0):
        deadline = time.time() + timeout
        buf = b''
        while time.time() < deadline:
            r, _, _ = select.select([self.fd], [], [], max(0, deadline - time.time()))
            if not r:
                break
            try:
                buf += os.read(self.fd, REPORT)
            except OSError as e:
                print(f'[!] read failed: {e} (device reset?)')
                return None
            j = buf.find(MAGIC_DEV)
            if j >= 0 and len(buf) >= j + 5:
                ln = struct.unpack_from('>H', buf, j + 3)[0]
                end = j + 5 + (ln - 3)          # payload after length field
                if len(buf) >= end:
                    return buf[j:end]
        return None

    def cmd(self, cmd, data=b'', timeout=2.0):
        self.send_frame(build(cmd, data))
        return self.recv_frame(timeout)

def parse(resp):
    if not resp or resp[:3] != MAGIC_DEV:
        return None, None
    ln = struct.unpack_from('>H', resp, 3)[0]
    body = resp[5:5 + (ln - 3) - 3]          # strip sum(2) + term(1)
    if not body:
        return None, None
    return body[0], body[1:]

def bcd(v):
    return (v >> 4) * 10 + (v & 0x0F)

def chip_info(d):
    """what the loader reports about the chip: clock, boot loader, bandgap, date and the option bytes"""
    if len(d) < 40:
        return
    clock = int.from_bytes(d[0:4], 'big')
    print(f'[i] clock {clock / 1e6:.2f} MHz, boot loader {d[16] >> 4}.{d[16] & 0x0F}.{d[21] & 0x0F}{chr(d[17])}')
    print(f'[i] bandgap {int.from_bytes(d[34:36], "big")} mV, made 20{bcd(d[36]):02d}-{bcd(d[37]):02d}-{bcd(d[38]):02d}')
    base = d[15] * 256                                  # option byte: where the EEPROM area starts in the 64 KB flash (0: none)
    size = 0x10000 - base if base else 0
    if size >= 512:
        print(f'[i] EEPROM area {size} bytes at flash {base:#06x} (IAP offsets 0x0000..{size - 1:#06x})')
    else:
        print('[!] no EEPROM area: the charger has nowhere to keep its settings')
        print('    set the EEPROM size when flashing (STC-ISP: 1K), then flash again')

def handshake(isp):
    r = isp.cmd(0x00, b'')
    c, d = parse(r)
    print(f'[i] handshake: cmd={c} payload={len(d) if d else 0} bytes')
    if d:
        print('    ' + d[:40].hex())
        chip_info(d)
    return d if c == 0x00 else None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('action', choices=['info', 'write'])
    ap.add_argument('file', nargs='?')
    ap.add_argument('--dev', default=None)
    ap.add_argument('--addr', type=lambda x: int(x, 0), default=0)
    ap.add_argument('--erase', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()

    dev = a.dev
    if dev is None:
        dev = find_stc()
        if dev is None:
            print('[-] no STC USB-ISP device (vendor 34bf) found.')
            print('    Put the charger in ISP mode: hold the button while plugging USB in.')
            return 1
        print(f'[i] using {dev}')

    if a.action == 'info':
        ok = handshake(Isp(dev))
        print('[+] handshake ok' if ok else '[-] handshake failed')
        return 0 if ok else 1

    if not a.file:
        print('write needs a file'); return 1
    return write(dev, open(a.file, 'rb').read(), a.addr, a.erase, a.dry_run)

def write(dev, data, addr=0, erase=False, dry_run=False):
    """program `data` at `addr` through the loader at `dev`; returns 0 on success"""
    print(f'[i] image {len(data)} bytes at {addr:#06x}')
    blocks = [data[i:i+128] for i in range(0, len(data), 128)]
    if len(blocks[-1]) < 128:
        blocks[-1] = blocks[-1] + b'\xff' * (128 - len(blocks[-1]))
    if erase:                            # erased flash already reads 0xFF: the padding at the end need not be sent
        while len(blocks) > 1 and blocks[-1] == b'\xff' * 128:
            blocks.pop()
    print(f'[i] {len(blocks)} blocks')

    if dry_run:
        f = program_frame(0x32, addr, blocks[0])
        print('[dry] first frame:', f[:16].hex(), '...', f[-4:].hex(), f'({len(f)} B)')
        print('[dry] nothing sent')
        return 0

    isp = Isp(dev)
    for _ in range(10):                  # a loader that has only just enumerated may not answer yet
        if handshake(isp): break
        time.sleep(0.5)
    else:
        print('[-] handshake failed'); return 1

    if erase:
        print('[i] erase/setup sequence')
        isp.cmd(0x01, bytes([0,0,0,0,0,0,0x80]))
        isp.cmd(0x05, b'\x00\x00\x5a\xa5')
        isp.cmd(0x03, b'\x00\x00\x5a\xa5')
        time.sleep(0.3)

    for i, blk in enumerate(blocks):
        cmd = 0x32 if i == 0 else 0x12
        isp.send_frame(program_frame(cmd, addr, blk))
        r = isp.recv_frame(2.0)
        c, d = parse(r)
        ok = (c == 0x02 and d and d[0] == 0x54)
        if not ok:
            print(f'[-] block {i} addr {addr:#06x}: reply cmd={c} data={d.hex() if d else None}')
            return 1
        addr += 128
        time.sleep(0.002)
        if i % 8 == 0:
            print(f'    {i+1}/{len(blocks)}', end='\r', flush=True)
    print(f'[+] {len(blocks)} blocks written')

    if erase:
        print('[i] reset into application')
        isp.cmd(0xff, b'\x01', 1.0)
    return 0

if __name__ == '__main__':
    sys.exit(main())
