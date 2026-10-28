"""Recover the PS2 class tree from DMC3's class records and compare it with the PC version.

Record layout found in SLUS_213.61 (L = address of the name pointer):
  L-16: pointer to the parent's L (0 for a root class)
  L+0 : pointer to the class name string
  L+8…: method pointers into the code segment

usage: ps2_classes.py <SLUS elf> <pc_pe_structure.json> <functions.tsv> <out.json>
"""
import json
import re
import struct
import sys

elf, pc_json, functions_tsv, out = sys.argv[1:5]
d = open(elf, "rb").read()
phoff = struct.unpack_from("<I", d, 28)[0]
_, seg_off, seg_va, _, seg_size, _, _, _ = struct.unpack_from("<8I", d, phoff)


def in_seg(v):
    return seg_va <= v < seg_va + seg_size


def word(v):
    return struct.unpack_from("<I", d, v - seg_va + seg_off)[0]


def cstr(v):
    if not in_seg(v):
        return None
    x = v - seg_va + seg_off
    end = d.find(b"\0", x, x + 64)
    s = d[x:end]
    return s.decode() if s and all(32 <= c < 127 for c in s) else None


func_starts = set()
for line in open(functions_tsv, encoding="utf-8").readlines()[1:]:
    func_starts.add(int(line.split("\t")[0], 16))
code_end = max(func_starts) + 4


def is_code_ptr(v):
    return seg_va <= v < code_end and v % 4 == 0

records = {}
for x in range(seg_off + 16, seg_off + seg_size - 8, 4):
    v = x - seg_off + seg_va
    name = cstr(struct.unpack_from("<I", d, x)[0])
    if name and re.fullmatch(r"C[A-Z][A-Za-z0-9_]+", name):
        records[v] = name

record_refs = {}
for x in range(seg_off, seg_off + seg_size - 4, 4):
    w = struct.unpack_from("<I", d, x)[0]
    if w in records:
        record_refs.setdefault(w, []).append(x - seg_off + seg_va)


def vtable_methods(loc):
    """Vtables look like [type record][offset-to-top][method pointers...]; the first one is primary."""
    tables = []
    for ref in record_refs.get(loc, []):
        if ref == loc or word(ref - 16) == loc:
            continue
        p, methods = ref + 8, []
        while in_seg(p) and is_code_ptr(word(p)):
            methods.append(hex(word(p)))
            p += 4
        if methods:
            tables.append(methods)
    return tables


classes = {}
for loc, name in records.items():
    parent_loc = word(loc - 16)
    tables = vtable_methods(loc)
    classes[name] = {"record": hex(loc), "parent": records.get(parent_loc),
                     "methods": tables[0] if tables else [], "secondary_vtables": len(tables[1:])}


def chain(n):
    out, seen = [], set()
    while n and n in classes and n not in seen:
        seen.add(n)
        n = classes[n]["parent"]
        if n:
            out.append(n)
    return out


pc = json.load(open(pc_json, encoding="utf-8"))["classes"]
agree = disagree = 0
mismatches = []
for n, c in classes.items():
    if n in pc and pc[n]["bases"]:
        pc_parent = pc[n]["bases"][0]
        if c["parent"] == pc_parent:
            agree += 1
        else:
            disagree += 1
            mismatches.append((n, c["parent"], pc_parent))
for n in classes:
    classes[n]["ancestors"] = chain(n)

# A method is "own" when no ancestor's vtable has the same pointer in the same slot.
owner = {}
for n, c in classes.items():
    own = []
    for slot, m in enumerate(c["methods"]):
        inherited = any(slot < len(classes[a]["methods"]) and classes[a]["methods"][slot] == m
                        for a in c["ancestors"] if a in classes)
        if not inherited:
            own.append(m)
            owner.setdefault(m, f"{n}::vf{slot}")
    c["own_methods"] = own

json.dump({"class_count": len(classes), "parent_agrees_with_pc": agree, "parent_differs_from_pc": disagree,
           "mismatches": mismatches, "named_functions": owner, "classes": dict(sorted(classes.items()))},
          open(out, "w", encoding="utf-8"), indent=1)
roots = sorted(n for n, c in classes.items() if not c["parent"])
print("classes", len(classes), "| roots", len(roots), roots[:20])
print("parent matches PC:", agree, "| differs:", disagree, mismatches[:8])
with_methods = {n: len(c["methods"]) for n, c in classes.items() if c["methods"]}
print("classes with vtable:", len(with_methods), "| distinct functions named by vtable slot:", len(owner))
for k in ["CPlDante", "CPlVergil", "CEm000", "CSceneGame", "CCameraPlayer", "CDraw"]:
    if k in classes:
        print(k, "<-", " <- ".join(classes[k]["ancestors"]), "| vtable:", len(classes[k]["methods"]),
              "| own:", len(classes[k]["own_methods"]))
