"""Cross-reference PS2 decompiled functions with class names and library markers.

usage: ps2_name_map.py <export_dir> <pc_pe_structure.json> <out.json>
"""
import collections
import json
import re
import sys

export, pc_json, out = sys.argv[1:4]

strings = {}
for line in open(f"{export}/strings.tsv", encoding="utf-8"):
    addr, _, value = line.rstrip("\n").partition("\t")
    strings[addr.lower().lstrip("0")] = value

pc_classes = set(json.load(open(pc_json, encoding="utf-8"))["classes"])
ps2_class_strings = sorted({v for v in strings.values() if re.fullmatch(r"C[A-Z][A-Za-z0-9_]+", v)})
shared = sorted(set(ps2_class_strings) & pc_classes)

# Split decompiled.c into functions and collect the string labels each one references.
src = open(f"{export}/decompiled.c", encoding="utf-8").read()
blocks = re.split(r"^// (\S+) @ ([0-9a-fA-F]+)\n", src, flags=re.M)
funcs = []
for i in range(1, len(blocks) - 2, 3):
    name, addr, body = blocks[i], blocks[i + 1], blocks[i + 2]
    refs = set(re.findall(r"s_[A-Za-z0-9_]*?_([0-9a-fA-F]{6,8})\b", body))
    texts = [strings.get(r.lower().lstrip("0")) for r in refs]
    funcs.append({"addr": addr, "name": name, "lines": body.count("\n"), "strings": [t for t in texts if t]})

libs = {
    "CRI ADX (áudio)": re.compile(r"ADX|adxm|ADXF|ADXT"),
    "CRI Sofdec (vídeo)": re.compile(r"mwSfd|SFD|MPEG|sfd"),
    "Sony libpad (controle)": re.compile(r"scePad|pad", re.I),
    "Sony libmc (memory card)": re.compile(r"libmc|sceMc|MCMAN", re.I),
    "Sony SIF/IOP": re.compile(r"sceSif|IRX|IOP", re.I),
    "Sony CD/DVD": re.compile(r"sceCd|cdrom0", re.I),
    "Cenas do jogo": re.compile(r"^CScene"),
}
by_lib = collections.defaultdict(list)
named = {}
for f in funcs:
    for t in f["strings"]:
        for lib, rx in libs.items():
            if rx.search(t):
                by_lib[lib].append(f["addr"])
        if t in ps2_class_strings:
            named.setdefault(t, []).append(f["addr"])

sizes = sorted(funcs, key=lambda f: -f["lines"])[:15]
result = {
    "function_count": len(funcs),
    "ps2_class_strings": ps2_class_strings,
    "shared_with_pc": shared,
    "functions_by_library": {k: sorted(set(v)) for k, v in by_lib.items()},
    "functions_referencing_class_names": named,
    "largest_functions": [{"addr": f["addr"], "lines": f["lines"], "strings": f["strings"][:5]} for f in sizes],
}
json.dump(result, open(out, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
print("functions", len(funcs), "| class strings", len(ps2_class_strings), "| shared with PC", len(shared))
print("by library:", {k: len(set(v)) for k, v in by_lib.items()})
print("class names with referencing functions:", len(named))
print("PS2-only classes:", [c for c in ps2_class_strings if c not in pc_classes][:40])
print("largest:", [(f["addr"], f["lines"]) for f in sizes[:8]])
