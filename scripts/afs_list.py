"""List the table of contents of CRI AFS archives stored inside an ISO.

usage: afs_list.py <iso> <name>:<lba> [<name>:<lba> ...]
"""
import collections
import os
import struct
import sys

SECTOR = 2048
iso = sys.argv[1]

with open(iso, "rb") as f:
    for arg in sys.argv[2:]:
        name, lba = arg.split(":")
        base = int(lba) * SECTOR
        f.seek(base)
        magic, count = struct.unpack("<4sI", f.read(8))
        if magic[:3] != b"AFS":
            print(name, "not AFS:", magic)
            continue
        toc = [struct.unpack("<II", f.read(8)) for _ in range(count)]
        name_off, name_size = struct.unpack("<II", f.read(8))
        names = []
        if name_off:
            f.seek(base + name_off)
            for _ in range(count):
                rec = f.read(48)
                names.append(rec[:32].split(b"\0")[0].decode("latin-1"))
        exts, sizes, magics = collections.Counter(), collections.Counter(), collections.Counter()
        for i, (off, size) in enumerate(toc):
            n = names[i] if names else f"#{i}"
            e = os.path.splitext(n)[1].lower() or "(none)"
            exts[e] += 1
            sizes[e] += size
            f.seek(base + off)
            m = f.read(4)
            magics[m.decode("latin-1").rstrip("\0") if all(32 <= c < 127 or c == 0 for c in m) else m.hex()] += 1
        print(f"== {name}: {count} entries, {sum(s for _, s in toc) / 1e6:.0f} MB")
        print("   by ext:", [(e, exts[e], round(sizes[e] / 1e6)) for e, _ in sizes.most_common(8)])
        print("   magics:", magics.most_common(6))
        print("   sample:", names[:12] if names else "(no name table)")
