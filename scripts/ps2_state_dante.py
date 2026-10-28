"""Print Dante's animation/style state from PS2 EE memory dumps (eeMemory.bin from a PCSX2 save state).

usage: ps2_state_dante.py <eeMemory.bin> [<eeMemory.bin> ...]
"""
import struct
import sys

STYLE = {0: "Swordmaster", 1: "Gunslinger", 2: "Trickster", 3: "Royal Guard", 4: "Quicksilver", 5: "Doppelganger"}


def report(path):
    m = open(path, "rb").read()
    u8 = lambda a: m[a]
    u16 = lambda a: struct.unpack_from("<H", m, a)[0]
    u32 = lambda a: struct.unpack_from("<I", m, a)[0]
    f32 = lambda a: struct.unpack_from("<f", m, a)[0]

    print(f"===== {path}")
    print(f"mod code at 0xFF000: {u32(0xFF000):08x} (27bdffa0 = mod present); hook 0x1ec468 = {u32(0x1EC468):08x}")
    print(f"flags: no_packs={u32(0xFF7E0)} no_hud={u32(0xFF7E4)} pending={u32(0xFF7E8)} sound={u32(0xFF7F0)}")
    g = u32(0x1D0D70C)
    if not g:
        print("no game object")
        return
    scene, p = u32(g + 0x24), u32(g + 0xC)
    print(f"G={g:#x} scene={scene:#x} state={u8(scene + 0x18)} player={p:#x} vtable={u32(p):#x}")
    style = u8(p + 0x3C6C)
    print(f"style={style} ({STYLE.get(style, '?')}) action_state={u8(p + 0x3C6E)} level={u8(p + 0x3C74)} "
          f"exp={f32(p + 0x3C7C):.0f}")
    print(f"weapons slots (0x3d4f..53)={[u8(p + 0x3D4F + i) for i in range(5)]} current slot idx (0x3d4e)={u8(p + 0x3D4E)}")
    up, low = u16(p + 0x24C6), u16(p + 0x24C4)
    print(f"motion upper(0x24c6)={up:#06x} bank {up >> 8} #{up & 0xff} | lower(0x24c4)={low:#06x} bank {low >> 8} #{low & 0xff}")
    print(f"motion flags 0x24ce={u8(p + 0x24CE)} 0x24cf={u8(p + 0x24CF)} 0x28a0={u8(p + 0x28A0)} 0x28b0={u8(p + 0x28B0)}")
    table = [u32(p + 0x243C + 4 * i) for i in range(34)]
    print("anim table (bank: ptr):", " ".join(f"{i}:{v:x}" for i, v in enumerate(table) if v))
    print("move flags 0x29d8:", " ".join(f"{u32(p + 0x29D8 + 4 * i):08x}" for i in range(8)))
    print("purchased  0x29f8:", " ".join(f"{u32(p + 0x29F8 + 4 * i):08x}" for i in range(8)))
    print(f"pad0 held={u16(0x6C3740):04x} prev={u16(0x6C3742):04x} pressed={u16(0x6C3744):04x} "
          f"released={u16(0x6C3746):04x} sub0={u16(0x6C374C):04x} sub1={u16(0x6C375C):04x}")
    save = u32(0x1D0D710)
    if save:
        print(f"save: style={u8(save + 0x116)} level={u8(save + 0x117)} levels={[u8(save + 0xC5 + i) for i in range(6)]} "
              f"record weapons={[u8(save + 0x104 + i) for i in range(5)]}")


for path in sys.argv[1:]:
    report(path)
