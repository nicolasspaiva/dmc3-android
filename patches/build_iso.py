"""Build a copy of the DMC3 SE (SLUS-21361) ISO with the mods built into the executable.

The original ISO is never modified: it is copied, and the patched SLUS_213.61 is written at the end
of the copy, with the ISO9660 directory entry and volume size updated to point at it.

Inside the executable:
  - the empty PT_LOAD segment (program header 1, 0 bytes) is reused to load the mod blob at
    style_switch.CAVE (0x000FF000): the style-switch routine, then the sound flag word;
  - the `jal frame_Begin` in main (0x001EC468) is redirected to the routine.

usage: build_iso.py <original.iso> <output.iso> [--no-sound]
"""
import importlib.util
import os
import shutil
import struct
import sys

SECTOR = 2048
HERE = os.path.dirname(os.path.abspath(__file__))
ELF_NAME = "SLUS_213.61"


def load_style_switch():
    spec = importlib.util.spec_from_file_location("style_switch", os.path.join(HERE, "style_switch", "build_pnach.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def find_file(f, name):
    """Return (directory record file offset, extent LBA, size) of a file in the ISO root directory."""
    f.seek(16 * SECTOR)
    pvd = f.read(SECTOR)
    root_lba, root_size = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
    f.seek(root_lba * SECTOR)
    data = f.read(root_size)
    pos = 0
    while pos < len(data):
        n = data[pos]
        if n == 0:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        rec = data[pos:pos + n]
        rec_name = rec[33:33 + rec[32]].decode("latin-1").split(";")[0]
        if rec_name.upper() == name.upper():
            return root_lba * SECTOR + pos, struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
        pos += n
    raise SystemExit(f"{name} not found in the ISO root directory")


def patch_elf(elf, ss, with_sound):
    phoff = struct.unpack_from("<I", elf, 28)[0]
    phnum = struct.unpack_from("<H", elf, 44)[0]
    seg0 = struct.unpack_from("<8I", elf, phoff)
    seg1 = struct.unpack_from("<8I", elf, phoff + 32)
    if phnum != 2 or seg1[0] != 1 or seg1[4] != 0 or seg1[5] != 0:
        raise SystemExit("unexpected program headers: is this the original SLUS_213.61?")

    elf = bytearray(elf)
    word_off = lambda va: va - seg0[2] + seg0[1]
    for va, target in ((ss.HOOK, ss.FRAME_BEGIN), (ss.OLD_HOOK, ss.FRAME_END)):
        if struct.unpack_from("<I", elf, word_off(va))[0] != ss.jal(target):
            raise SystemExit(f"call site {va:#x} is not jal {target:#x}: wrong game version?")

    code = ss.assemble(ss.PROGRAM, ss.CAVE)
    blob = bytearray(ss.SOUND_FLAG + 4 - ss.CAVE)
    for pc, w, _ in code:
        struct.pack_into("<I", blob, pc - ss.CAVE, w)
    struct.pack_into("<I", blob, ss.SOUND_FLAG - ss.CAVE, 1 if with_sound else 0)

    struct.pack_into("<I", elf, word_off(ss.HOOK), ss.jal(ss.CAVE))
    blob_off = (len(elf) + 15) // 16 * 16
    elf += bytes(blob_off - len(elf)) + blob
    struct.pack_into("<8I", elf, phoff + 32, 1, blob_off, ss.CAVE, ss.CAVE, len(blob), len(blob), 7, 0x10)
    return bytes(elf), len(code)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    with_sound = "--no-sound" not in sys.argv
    src, dst = args
    if os.path.abspath(src) == os.path.abspath(dst):
        raise SystemExit("output must be a different file from the original ISO")
    ss = load_style_switch()

    with open(src, "rb") as f:
        rec_off, lba, size = find_file(f, ELF_NAME)
        f.seek(lba * SECTOR)
        elf = f.read(size)
        f.seek(16 * SECTOR)
        volume_sectors = struct.unpack_from("<I", f.read(SECTOR), 80)[0]
    if os.path.getsize(src) != volume_sectors * SECTOR:
        raise SystemExit("ISO size does not match its volume size")

    new_elf, n_insn = patch_elf(elf, ss, with_sound)
    new_lba = volume_sectors
    new_sectors = (len(new_elf) + SECTOR - 1) // SECTOR

    print(f"copying {src} -> {dst} ...")
    shutil.copyfile(src, dst)
    with open(dst, "r+b") as f:
        f.seek(new_lba * SECTOR)
        f.write(new_elf + bytes(new_sectors * SECTOR - len(new_elf)))
        f.seek(rec_off + 2)
        f.write(struct.pack("<I", new_lba) + struct.pack(">I", new_lba))
        f.write(struct.pack("<I", len(new_elf)) + struct.pack(">I", len(new_elf)))
        total = volume_sectors + new_sectors
        f.seek(16 * SECTOR + 80)
        f.write(struct.pack("<I", total) + struct.pack(">I", total))

    print(f"{ELF_NAME}: {len(elf)} -> {len(new_elf)} bytes, moved from LBA {lba} to {new_lba}")
    print(f"mod: {n_insn} instructions at {ss.CAVE:#x}, hook at {ss.HOOK:#x}, sound {'on' if with_sound else 'off'}")
    print(f"new executable CRC (PCSX2): {ss.elf_crc(new_elf):08X}")


if __name__ == "__main__":
    main()
