"""Extract RTTI class names and categorized strings from a PE's unencrypted sections."""
import collections
import json
import re
import struct
import sys

path, out = sys.argv[1], sys.argv[2]
data = open(path, "rb").read()

pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
optsz = struct.unpack_from("<H", data, pe + 20)[0]
sections = {}
for i in range(nsec):
    o = pe + 24 + optsz + 40 * i
    name = data[o:o + 8].rstrip(b"\0").decode()
    rawsz, rawptr = struct.unpack_from("<II", data, o + 16)
    sections[name] = data[rawptr:rawptr + rawsz]

blob = sections[".rdata"] + sections[".data"]

rtti = sorted(set(m.decode() for m in re.findall(rb"\.\?A[VU]([A-Za-z0-9_@?$]+?)@@", blob)))

strings = sorted(set(m.decode() for m in re.findall(rb"[\x20-\x7e]{6,}", blob)))
wstrings = sorted(set(m.decode("utf-16le") for m in re.findall(rb"(?:[\x20-\x7e]\x00){6,}", blob)))

cats = {
    "file_extensions": re.compile(r"\.(pac|ptx|arc|sdt|mod|lmt|scm|ogg|wav|fsb|bank|dds|tga|bin|dat|ini|txt|cfg|wmv|mp4|usm)\b", re.I),
    "paths": re.compile(r"[\\/][A-Za-z0-9_]+[\\/]|^[a-z]+:[\\/]", re.I),
    "d3d_render": re.compile(r"d3d|dxgi|shader|texture|render|swap|vertex|pixel|hlsl|sampler", re.I),
    "audio": re.compile(r"fmod|xaudio|sound|audio|bgm|\bse\b|voice|bank", re.I),
    "input": re.compile(r"dinput|xinput|gamepad|joystick|keyboard|button|controller", re.I),
    "steam": re.compile(r"steam|achievement|cloud", re.I),
    "gameplay": re.compile(r"dante|vergil|enemy|stage|mission|style|weapon|devil|trigger|boss|player", re.I),
    "source_files": re.compile(r"\.(cpp|c|h|hpp)\b", re.I),
}
categorized = collections.defaultdict(list)
for s in strings + wstrings:
    for k, rx in cats.items():
        if rx.search(s):
            categorized[k].append(s)

json.dump(
    {"rtti_classes": rtti, "counts": {k: len(v) for k, v in categorized.items()}, "categorized": categorized},
    open(out, "w", encoding="utf-8"),
    indent=1,
)
print("rtti:", len(rtti), "strings:", len(strings), "wstrings:", len(wstrings))
print({k: len(v) for k, v in categorized.items()})
