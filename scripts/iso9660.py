"""Minimal ISO9660 reader: list files, or extract one file.

usage: iso9660.py <iso> list
       iso9660.py <iso> extract <ISO/PATH> <out_file>
"""
import struct
import sys

SECTOR = 2048


def read(f, lba, size):
    f.seek(lba * SECTOR)
    return f.read(size)


def records(f, lba, size):
    data = read(f, lba, size)
    pos = 0
    while pos < len(data):
        n = data[pos]
        if n == 0:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        rec = data[pos:pos + n]
        ext_lba = struct.unpack_from("<I", rec, 2)[0]
        ext_size = struct.unpack_from("<I", rec, 10)[0]
        flags = rec[25]
        name_len = rec[32]
        name = rec[33:33 + name_len]
        pos += n
        if name in (b"\0", b"\1"):
            continue
        yield name.decode("latin-1").split(";")[0], ext_lba, ext_size, bool(flags & 2)


def walk(f, lba, size, prefix=""):
    for name, l, s, is_dir in records(f, lba, size):
        path = prefix + "/" + name
        if is_dir:
            yield from walk(f, l, s, path)
        else:
            yield path, l, s


def main():
    iso, cmd = sys.argv[1], sys.argv[2]
    with open(iso, "rb") as f:
        pvd = read(f, 16, SECTOR)
        root = pvd[156:190]
        root_lba = struct.unpack_from("<I", root, 2)[0]
        root_size = struct.unpack_from("<I", root, 10)[0]
        files = list(walk(f, root_lba, root_size))
        if cmd == "list":
            for path, lba, size in files:
                print(f"{size:>12} {lba:>8} {path}")
            print(f"{len(files)} files, {sum(s for _, _, s in files) / 1e9:.2f} GB")
        elif cmd == "extract":
            want, out = sys.argv[3].upper(), sys.argv[4]
            path, lba, size = next(x for x in files if x[0].upper() == want)
            with open(out, "wb") as o:
                f.seek(lba * SECTOR)
                remaining = size
                while remaining:
                    chunk = f.read(min(remaining, 1 << 20))
                    o.write(chunk)
                    remaining -= len(chunk)
            print("extracted", path, size, "->", out)


main()
