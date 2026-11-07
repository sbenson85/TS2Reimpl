"""Compares two PNG screenshots pixel by pixel (standard library only).

    python -I tools/imgdiff.py a.png b.png [--tolerance N] [--ignore x0,y0,x1,y1 ...]

Prints the number of differing pixels, their bounding box and the largest channel difference, and
exits 1 when any pixel differs by more than the tolerance outside the ignored rectangles. Handles
the 8-bit RGB/RGBA, non-interlaced PNGs tools/screenshot.ps1 writes.
"""
import argparse
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat = 8, []
    width = height = channels = None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace or colour not in (2, 6):
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, colour {colour}, interlace {interlace})")
            channels = 3 if colour == 2 else 4
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(height):
        kind = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + a) & 0xFF
            elif kind == 2:
                line[x] = (line[x] + b) & 0xFF
            elif kind == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        rows.append(bytes(line))
        prev = line
    return width, height, channels, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tolerance", type=int, default=0, help="largest channel difference still counted equal")
    ap.add_argument("--ignore", action="append", default=[], help="x0,y0,x1,y1 rectangle to skip (exclusive end)")
    args = ap.parse_args()
    wa, ha, ca, ra = read_png(args.a)
    wb, hb, cb, rb = read_png(args.b)
    if (wa, ha) != (wb, hb):
        print(f"size differs: {wa}x{ha} vs {wb}x{hb}")
        return 1
    ignore = [tuple(int(v) for v in r.split(",")) for r in args.ignore]
    count, worst, box = 0, 0, None
    for y in range(ha):
        la, lb = ra[y], rb[y]
        if la == lb and ca == cb:
            continue
        for x in range(wa):
            if any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in ignore):
                continue
            d = max(abs(la[x * ca + k] - lb[x * cb + k]) for k in range(3))
            if d > args.tolerance:
                count += 1
                worst = max(worst, d)
                box = (x, y, x, y) if box is None else (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
    if count:
        print(f"{count} pixels differ (largest channel difference {worst}), box {box[0]},{box[1]}-{box[2]},{box[3]}")
        return 1
    print(f"identical ({wa}x{ha}, tolerance {args.tolerance})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
