"""Extract members from a PCSX2 .p2s save state (zip with zstd-compressed members).

usage: p2s_extract.py <state.p2s> <out_dir> [member ...]   (default: eeMemory.bin)
"""
import os
import struct
import sys
import zipfile

import zstandard

src, out_dir, *members = sys.argv[1:]
members = members or ["eeMemory.bin"]
os.makedirs(out_dir, exist_ok=True)
z = zipfile.ZipFile(src)
with open(src, "rb") as f:
    for info in z.infolist():
        if info.filename not in members:
            continue
        f.seek(info.header_offset)
        hdr = f.read(30)
        name_len, extra_len = struct.unpack_from("<HH", hdr, 26)
        f.seek(info.header_offset + 30 + name_len + extra_len)
        raw = f.read(info.compress_size)
        if info.compress_type == 0:
            data = raw
        else:
            data = zstandard.ZstdDecompressor().decompress(raw, max_output_size=info.file_size)
        open(os.path.join(out_dir, info.filename), "wb").write(data)
        print(info.filename, len(data))
