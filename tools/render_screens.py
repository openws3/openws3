#!/usr/bin/env python3
"""Turn the PPM screens written by build/render_screens into scaled PNGs (the pictures in GUIDE.md).
   python3 tools/render_screens.py build/render_screens docs/screens"""
import os, struct, sys, tempfile, zlib, subprocess

SCALE = 5      # the panel is 160 x 80; 5x makes it about the size it has in the hand


def read_ppm(path):
    with open(path, 'rb') as f:
        f.readline()                                  # P6
        w, h = map(int, f.readline().split())
        f.readline()                                  # 255
        return w, h, f.read()


def png(w, h, rgb, path):
    raw = bytearray()
    row = w * 3
    for y in range(h):
        line = b''.join(rgb[y * row + x * 3:y * row + x * 3 + 3] * SCALE for x in range(w))
        raw += (b'\x00' + line) * SCALE
    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * SCALE, h * SCALE, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    exe, out = sys.argv[1:]
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([exe, tmp], check=True)
        for name in sorted(os.listdir(tmp)):
            w, h, rgb = read_ppm(os.path.join(tmp, name))
            png(w, h, rgb, os.path.join(out, name[:-4] + '.png'))
            print(name[:-4])


main()
