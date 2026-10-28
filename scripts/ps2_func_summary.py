"""Summarize decompiled PS2 functions: size, switch cases, callees, strings and field offsets used.

usage: ps2_func_summary.py <export_dir> <addr> [<addr>...] [--body N]
"""
import collections
import re
import sys

args = sys.argv[1:]
body_lines = 0
if "--body" in args:
    i = args.index("--body")
    body_lines = int(args[i + 1])
    args = args[:i] + args[i + 2:]
export, *addrs = args

strings = {}
for line in open(f"{export}/strings.tsv", encoding="utf-8"):
    a, _, v = line.rstrip("\n").partition("\t")
    if re.fullmatch(r"[0-9a-fA-F]+", a):
        strings[int(a, 16)] = v

src = open(f"{export}/decompiled.c", encoding="utf-8").read()
parts = re.split(r"^// (\S+) @ ([0-9a-fA-F]+)\n", src, flags=re.M)
funcs = {int(parts[i + 1], 16): (parts[i], parts[i + 2]) for i in range(1, len(parts) - 2, 3)}

for a in addrs:
    name, body = funcs[int(a, 16)]
    calls = collections.Counter(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", body))
    for kw in ("if", "while", "for", "switch", "return", "sizeof", name):
        calls.pop(kw, None)
    cases = re.findall(r"case (0x[0-9a-f]+|\d+):", body)
    fields = collections.Counter(re.findall(r"\(param_1 \+ (0x[0-9a-f]+)\)", body))
    strs = [strings[int(m, 16)] for m in re.findall(r"\b0x([0-9a-fA-F]{6})\b", body) if int(m, 16) in strings]
    floats = sorted(set(re.findall(r"\b\d+\.\d+\b", body)))[:15]
    print(f"=== {name} @ {a}: {body.count(chr(10))} lines, {len(cases)} switch cases")
    print("  callees:", ", ".join(f"{k}x{v}" if v > 1 else k for k, v in calls.most_common(25)))
    print("  most used fields of param_1:", ", ".join(f"{k}x{v}" for k, v in fields.most_common(12)))
    if strs:
        print("  strings:", strs[:10])
    if floats:
        print("  float constants:", floats)
    if body_lines:
        kept = [l for l in body.splitlines()
                if l.strip() and not re.match(r"\s*(undefined\d*|u?int|float|char|short|long|bool) \**[a-zA-Z]+Var\d+;", l)]
        print("\n".join(kept[:body_lines]))
    print()
