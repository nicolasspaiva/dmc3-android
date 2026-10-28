"""Dump every vtable of the given PS2 classes, marking slots that differ from a reference class.

usage: ps2_vtables.py <SLUS elf> <classes.json> <functions.tsv> <Class> [<Class>...] [--vs <RefClass>]
"""
import json
import struct
import sys

args = sys.argv[1:]
ref = None
if "--vs" in args:
    i = args.index("--vs")
    ref = args[i + 1]
    args = args[:i] + args[i + 2:]
elf, classes_json, functions_tsv, *wanted = args

d = open(elf, "rb").read()
phoff = struct.unpack_from("<I", d, 28)[0]
_, seg_off, seg_va, _, seg_size, _, _, _ = struct.unpack_from("<8I", d, phoff)
word = lambda v: struct.unpack_from("<I", d, v - seg_va + seg_off)[0]

sizes, names = {}, {}
for line in open(functions_tsv, encoding="utf-8").readlines()[1:]:
    a, s, n = line.split("\t")[:3]
    sizes[int(a, 16)] = int(s)
    names[int(a, 16)] = n
code_end = 0x58c000  # first data address after .text in SLUS_213.61

classes = json.load(open(classes_json, encoding="utf-8"))["classes"]


def tables(cls):
    loc = int(classes[cls]["record"], 16)
    out = []
    for x in range(seg_off, seg_off + seg_size - 4, 4):
        if struct.unpack_from("<I", d, x)[0] != loc:
            continue
        ref_va = x - seg_off + seg_va
        if ref_va == loc or word(ref_va - 16) == loc:
            continue
        offset = struct.unpack_from("<i", d, x + 4)[0]
        p, slots = ref_va + 8, []
        while seg_va <= word(p) < code_end and word(p) % 4 == 0:
            slots.append(word(p))
            p += 4
        out.append((ref_va, offset, slots))
    return out


ref_tables = tables(ref) if ref else []
for cls in wanted:
    print(f"## {cls}")
    for n, (va, offset, slots) in enumerate(tables(cls)):
        print(f"  vtable @ {va:#x} this-offset {offset}: {len(slots)} slots")
        rslots = ref_tables[n][2] if n < len(ref_tables) else []
        for i, s in enumerate(slots):
            differs = ref and (i >= len(rslots) or rslots[i] != s)
            mark = "*" if differs else " "
            print(f"   {mark} [{i:3}] {s:#08x} size={sizes.get(s, '?'):>6} {names.get(s, '')}")
