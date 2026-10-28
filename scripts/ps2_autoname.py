"""Propose names for PS2 functions from string evidence, class vtables and call patterns.

usage: ps2_autoname.py <export_dir> <out_names.tsv>
Reads decompiled.c, strings.tsv, functions.tsv and classes.json from <export_dir>.
"""
import collections
import json
import re
import sys

export, out = sys.argv[1:3]

strings = {}
for line in open(f"{export}/strings.tsv", encoding="utf-8"):
    addr, _, value = line.rstrip("\n").partition("\t")
    if re.fullmatch(r"[0-9a-fA-F]+", addr):
        strings[int(addr, 16)] = value

src = open(f"{export}/decompiled.c", encoding="utf-8").read()
parts = re.split(r"^// (\S+) @ ([0-9a-fA-F]+)\n", src, flags=re.M)
funcs = {}
for i in range(1, len(parts) - 2, 3):
    funcs[int(parts[i + 1], 16)] = {"ghidra": parts[i], "body": parts[i + 2]}

CONST = re.compile(r"\b0x([0-9a-fA-F]{6})\b")
LABEL = re.compile(r"\bs_\w*?_([0-9a-fA-F]{8})\b")
CALL = re.compile(r"\b(FUN_[0-9a-f]{8})\s*\(([^;]*)\);")


def string_refs(body):
    refs = []
    for m in CONST.finditer(body):
        v = int(m.group(1), 16)
        if v in strings:
            refs.append(strings[v])
    for m in LABEL.finditer(body):
        v = int(m.group(1), 16)
        if v in strings:
            refs.append(strings[v])
    return refs


names = {}  # addr -> (name, reason)


def propose(addr, name, reason):
    name = re.sub(r"[^A-Za-z0-9_:]", "_", name)
    if addr in funcs and addr not in names:
        names[addr] = (name, reason)


# 1. Class methods from vtables.
classes = json.load(open(f"{export}/classes.json", encoding="utf-8"))
for addr_hex, label in classes["named_functions"].items():
    propose(int(addr_hex, 16), label, "vtable")

# 2. Library functions that name themselves in their error strings.
SELF = [
    re.compile(r"^E[0-9]+\s*:?\s*([A-Za-z_][A-Za-z0-9_]{3,})\s*:"),       # E02080804 ADXT_Create: ...
    re.compile(r"\(([A-Za-z_][A-Za-z0-9_]{3,})\)\s*(\\n)?$"),              # ...(ADXF_GetPtStat)
    re.compile(r"^([A-Za-z_][A-Za-z0-9_]{3,})\s*(\(\))?\s*:\s"),           # sceSifCallRpc : error
]
for addr, f in funcs.items():
    found = collections.Counter()
    for s in string_refs(f["body"]):
        for rx in SELF:
            m = rx.search(s)
            if m:
                found[m.group(1)] += 1
                break
    if found:
        name, _ = found.most_common(1)[0]
        propose(addr, name, "self-named error string")

# 3. Logging helpers: callees whose first argument is very often a format string.
fmt_calls = collections.Counter()
for f in funcs.values():
    for m in CALL.finditer(f["body"]):
        first = m.group(2).split(",")[0].strip()
        c = CONST.fullmatch(first)
        if c and int(c.group(1), 16) in strings and "%" in strings[int(c.group(1), 16)] + "\\n":
            fmt_calls[m.group(1)] += 1
for callee, n in fmt_calls.most_common(5):
    if n >= 20:
        propose(int(callee[4:], 16), f"log_printf_{callee[4:]}", f"called with format strings {n}x")

# 4. Data loaders: functions that reference asset file names.
ASSET = re.compile(r"\.(afs|sfd|adx|pac|bin|irx|IMG|BIN)\b|^/(eff|demo)/", re.I)
for addr, f in funcs.items():
    refs = [s for s in string_refs(f["body"]) if ASSET.search(s)]
    if refs:
        stem = re.sub(r"[^A-Za-z0-9]+", "_", refs[0].split("/")[-1].split("\\")[-1].split(";")[0]).strip("_")
        propose(addr, f"load_{stem}" + (f"_and_{len(refs) - 1}_more" if len(refs) > 1 else ""), "references asset path")

# 5. Functions that are almost only a call to one named function (thin wrappers).
for addr, f in funcs.items():
    if addr in names:
        continue
    calls = CALL.findall(f["body"])
    if len(calls) == 1 and f["body"].count("\n") < 15:
        target = int(calls[0][0][4:], 16)
        if target in names and names[target][1] != "wrapper":
            propose(addr, f"{names[target][0]}_wrapper", "wrapper")

# Clean up: bare error codes and placeholders get a generic prefix; duplicates get the address appended.
for addr, (n, r) in list(names.items()):
    if re.fullmatch(r"E[0-9]+|null|s|[a-z]", n) or n.startswith("load_s"):
        names[addr] = (f"lib_{n}_{addr:08x}" if r != "references asset path" else f"load_asset_{addr:08x}", r)
counts = collections.Counter(n for n, _ in names.values())
for addr, (n, r) in list(names.items()):
    if counts[n] > 1:
        names[addr] = (f"{n}_{addr:08x}", r)

with open(out, "w", encoding="utf-8") as o:
    o.write("address\tname\treason\n")
    for addr in sorted(names):
        n, r = names[addr]
        o.write(f"{addr:08x}\t{n}\t{r}\n")

by_reason = collections.Counter(r for _, r in names.values())
print("named", len(names), "of", len(funcs), dict(by_reason))
print("logging helpers:", fmt_calls.most_common(5))
samples = [n for n, r in names.values() if r == "self-named error string"][:40]
print("library samples:", samples)
print("loader samples:", [n for n, r in names.values() if r == "references asset path"][:20])
