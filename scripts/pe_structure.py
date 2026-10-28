"""Recover class hierarchy, vtables and imports from the unencrypted parts of an x64 MSVC PE."""
import json
import struct
import sys

path, out = sys.argv[1], sys.argv[2]
data = open(path, "rb").read()

pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
opt = pe + 24
image_base = struct.unpack_from("<Q", data, opt + 24)[0]
datadirs = opt + 112

sections = []
for i in range(nsec):
    o = opt + optsz + 40 * i
    name = data[o:o + 8].rstrip(b"\0").decode()
    vsize, va, rawsz, rawptr = struct.unpack_from("<IIII", data, o + 8)
    sections.append((name, va, max(vsize, rawsz), rawptr, rawsz))


def section_of(rva):
    for s in sections:
        if s[1] <= rva < s[1] + s[2]:
            return s
    return None


def off(rva):
    s = section_of(rva)
    if not s or rva - s[1] >= s[4]:
        return None
    return s[3] + rva - s[1]


def u32(rva):
    o = off(rva)
    return None if o is None else struct.unpack_from("<I", data, o)[0]


def cstr(rva, limit=256):
    o = off(rva)
    if o is None:
        return None
    end = data.find(b"\0", o, o + limit)
    return data[o:end].decode("latin-1")


text = next(s for s in sections if s[0] == ".text")
rdata = next(s for s in sections if s[0] == ".rdata")

# .pdata -> function sizes
pdata = next(s for s in sections if s[0] == ".pdata")
func_size = {}
for o in range(pdata[3], pdata[3] + pdata[4] - 11, 12):
    b, e, _ = struct.unpack_from("<III", data, o)
    if b == 0:
        break
    func_size.setdefault(b, 0)
    func_size[b] = max(func_size[b], e - b)


def demangle(td_name):
    n = td_name[4:].rstrip("@")
    parts = n.split("@")
    return "::".join(reversed([p for p in parts if p]))


def type_name(td_rva):
    n = cstr(td_rva + 16)
    return demangle(n) if n and n.startswith(".?A") else None


# Scan .rdata for vtables: qword pointing to a valid CompleteObjectLocator, followed by .text pointers.
classes = {}
ro, rs = rdata[3], rdata[4]
for o in range(ro, ro + rs - 16, 8):
    col_va = struct.unpack_from("<Q", data, o)[0]
    col_rva = col_va - image_base
    if not (rdata[1] <= col_rva < rdata[1] + rdata[2]):
        continue
    co = off(col_rva)
    if co is None:
        continue
    sig, offset, cd_off, td_rva, chd_rva, self_rva = struct.unpack_from("<IIIIII", data, co)
    if sig != 1 or self_rva != col_rva:
        continue
    name = type_name(td_rva)
    if not name:
        continue
    vt_rva = rdata[1] + (o - ro) + 8
    methods = []
    p = o + 8
    while p < ro + rs:
        fva = struct.unpack_from("<Q", data, p)[0]
        frva = fva - image_base
        if not (text[1] <= frva < text[1] + text[2]):
            break
        methods.append({"rva": hex(frva), "size": func_size.get(frva)})
        p += 8
    # base classes from the hierarchy descriptor
    bases = []
    if chd_rva:
        n_bases = u32(chd_rva + 8) or 0
        arr = u32(chd_rva + 12)
        for i in range(1, min(n_bases, 32)):
            bcd = u32(arr + 4 * i) if arr else None
            if bcd:
                bn = type_name(u32(bcd))
                if bn:
                    bases.append(bn)
    entry = classes.setdefault(name, {"vtables": [], "bases": bases})
    entry["vtables"].append({"rva": hex(vt_rva), "subobject_offset": offset, "methods": methods})

# Imports
imports = {}
imp_rva, imp_size = struct.unpack_from("<II", data, datadirs + 8)
if imp_rva:
    d = imp_rva
    while True:
        ilt, _, _, name_rva, iat = (u32(d + 4 * k) for k in range(5))
        if not name_rva:
            break
        dll = cstr(name_rva)
        funcs = []
        t = ilt or iat
        while t:
            o = off(t)
            if o is None:
                break
            v = struct.unpack_from("<Q", data, o)[0]
            if v == 0:
                break
            funcs.append(f"#{v & 0xFFFF}" if v >> 63 else cstr((v & 0x7FFFFFFF) + 2))
            t += 8
        imports[dll] = funcs
        d += 20

summary = {
    "image_base": hex(image_base),
    "function_count": len(func_size),
    "function_bytes": sum(func_size.values()),
    "class_count": len(classes),
    "classes": dict(sorted(classes.items())),
    "imports": imports,
}
json.dump(summary, open(out, "w", encoding="utf-8"), indent=1)
print("functions:", len(func_size), "classes with vtables:", len(classes), "import dlls:", len(imports))
