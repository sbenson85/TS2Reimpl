"""Progress minimap: the exe image as a picture, one pixel per --bytes bytes, coloured by what we know of the code.

  .venv/Scripts/python -I tools/minimap.py [--out local/minimap.png] [--layout hilbert|rows] [--width 512] [--bytes N]

Reads db/ (function extents, status, library and third-party matches) and the PE section table of the unpacked exe.
Each pixel is the byte-weighted blend of its categories. The hilbert layout (default) runs the image along a Hilbert curve in
a --width square (a power of two), so nearby addresses stay in nearby blobs; rows is plain left-to-right lines. Just a picture: nothing reads its output.
"""
import argparse, glob, json, re, struct, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "local/original/Sims2EP9.exe.unpacked.exe"

# category: (colour, legend)
CATS = {
    "verified":       ((46, 204, 113), "verified"),
    "trace-verified": ((26, 188, 156), "trace-verified"),
    "reimplemented":  ((241, 196, 15), "reimplemented / replaced"),
    "mapped":         ((230, 126, 34), "mapped"),
    "library":        ((52, 73, 140), "library (CRT, STL, concrt)"),
    "third-party":    ((110, 90, 160), "third-party (D3DX, OpenSSL, Lua, image libs...)"),
    "todo":           ((150, 60, 60), "EA code not done"),
    "uncovered":      ((70, 70, 70), "code outside our function list (EH funclets, missed)"),
    "rdata":          ((28, 34, 44), "read-only data"),
    "data":           ((20, 24, 30), "data / resources / relocations"),
    "none":           ((10, 10, 10), "header / padding"),
}
ORDER = list(CATS)


def sections():
    b = EXE.read_bytes()[:4096]
    pe = struct.unpack_from("<I", b, 0x3c)[0]
    n = struct.unpack_from("<H", b, pe + 6)[0]
    opt = struct.unpack_from("<H", b, pe + 20)[0]
    out = []
    for i in range(n):
        o = pe + 24 + opt + 40 * i
        name = b[o:o + 8].rstrip(b"\0").decode()
        vsize, rva, _, _, _, _, _, _, chars = struct.unpack_from("<IIIIIIHHI", b, o + 8)
        out.append((name, rva, vsize, chars))
    return out


def hilbert(n, d):
    """Distance d along a Hilbert curve filling an n x n square (n a power of two) -> (x, y)."""
    x = y = 0
    s = 1
    while s < n:
        rx = 1 & (d // 2)
        ry = 1 & (d ^ rx)
        if ry == 0:
            if rx == 1: x, y = s - 1 - x, s - 1 - y
            x, y = y, x
        x += s * rx
        y += s * ry
        d //= 4
        s *= 2
    return x, y


def jsonl(path):
    with open(path, encoding="utf-8") as f:
        return [json.loads(l) for l in f if l.strip()]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=str(ROOT / "local/minimap.png"))
    ap.add_argument("--layout", choices=("hilbert", "rows"), default="hilbert")
    ap.add_argument("--width", type=int, default=512, help="hilbert: the square's side, rounded up to a power of two")
    ap.add_argument("--bytes", type=int, help="bytes per pixel (default: hilbert fills the square, rows 64)")
    a = ap.parse_args()

    secs = sections()
    end = max(r + v for _, r, v, _ in secs)
    cat = bytearray(end)  # index into ORDER, 'none' by default
    idx = {k: i for i, k in enumerate(ORDER)}
    cat[:] = bytes([idx["none"]]) * end
    for name, rva, vsize, chars in secs:
        c = "uncovered" if chars & 0x20000000 else "rdata" if name == ".rdata" else "data"
        cat[rva:rva + vsize] = bytes([idx[c]]) * vsize

    size = {}
    for r in jsonl(ROOT / "db/functions.jsonl") + jsonl(ROOT / "db/missed-functions.jsonl"):
        size[int(r["rva"], 16)] = r["size"]

    def paint(rva, c):
        n = size.get(rva)
        if n: cat[rva:rva + n] = bytes([idx[c]]) * n

    for rva in size: paint(rva, "todo")
    for r in jsonl(ROOT / "db/library-matches.jsonl"):
        lib = (r.get("lib") or ["?"])[0]
        third = not (lib.endswith(".lib") or lib.startswith("probe"))
        paint(int(r["rva"], 16), "third-party" if third else "library")
    for r in jsonl(ROOT / "db/third-party.jsonl"):
        for lo, hi in r.get("rvas", []):
            lo, hi = int(lo, 16), int(hi, 16)
            for rva in size:
                if lo <= rva < hi and cat[rva] == idx["todo"]: paint(rva, "third-party")
    rank = {"mapped": 0, "reimplemented": 1, "replaced": 1, "trace-verified": 2, "verified": 3}
    best = {}
    for f in glob.glob(str(ROOT / "db/status/*.jsonl")):
        for r in jsonl(f):
            s = r.get("status")
            if s in rank:
                rva = int(r["rva"], 16)
                if rank[s] >= rank.get(best.get(rva), -1): best[rva] = s
    for rva, s in best.items():
        paint(rva, "reimplemented" if s == "replaced" else s)

    cols = [CATS[k][0] for k in ORDER]
    W = a.width
    if a.layout == "hilbert":
        W = 1 << max(0, (W - 1).bit_length())
        H = W
        B = a.bytes or -(-end // (W * W))
        if -(-end // B) > W * W: raise SystemExit(f"--bytes {B} doesn't fit a {W}x{W} square")
    else:
        B = a.bytes or 64
        H = -(-(-(-end // B)) // W)
    img = bytearray(W * H * 3)
    for i in range(-(-end // B)):
        chunk = cat[i * B:(i + 1) * B]
        counts = [chunk.count(c) for c in range(len(ORDER))]
        rgb = bytes(sum(counts[c] * cols[c][ch] for c in range(len(ORDER))) // len(chunk) for ch in range(3))
        x, y = hilbert(W, i) if a.layout == "hilbert" else (i % W, i // W)
        o = (y * W + x) * 3
        img[o:o + 3] = rgb
    rows = bytearray()
    for y in range(H):
        rows.append(0)
        rows += img[y * W * 3:(y + 1) * W * 3]

    def chunkp(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = b"\x89PNG\r\n\x1a\n" + chunkp(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) \
        + chunkp(b"IDAT", zlib.compress(bytes(rows), 9)) + chunkp(b"IEND", b"")
    Path(a.out).write_bytes(png)

    print(f"{a.out}: {W}x{H}, {B} bytes per pixel, image 0x{end:x} bytes; sections "
          + ", ".join(f"{n} 0x{r:x}" for n, r, _, _ in secs))
    for i, k in enumerate(ORDER):
        n = cat.count(i)
        print(f"  rgb{CATS[k][0]!s:16} {n:>10,} B  {CATS[k][1]}")


if __name__ == "__main__":
    main()
