"""Build a PCSX2/NetherSX2 .pnach that adds on-the-fly style switching to DMC3 SE (NTSC-U, SLUS-21361).

D-pad during a mission (Dante only), like the Switch version's Free Style:
  Up    -> Trickster      (internal 2)
  Right -> Swordmaster    (internal 0)
  Left  -> Gunslinger     (internal 1)
  Down  -> Royal Guard    (internal 3)

How it works: the `jal frame_Begin` in main's game loop (0x1ec468) is redirected to a routine at
CAVE. frame_Begin reads the controller; right after it, and before the frame's game logic, the
routine checks that a mission is running (CSceneGameMain state 1) and the player is Dante, then:
  - after a switch, until the new style's packs are loaded: fills Dante's style-animation table
    (player+0x243c + 4*pack) with the current style's packs only, by the same rule FUN_0025bb00
    uses when Dante is created (Swordmaster/Gunslinger: pack 13 + equipped weapon id; other styles:
    pack 21 + style), then stops writing. Without this Dante stays in T-pose after a switch;
    refreshing every pack every frame instead displaced the stylish-rank gauge;
  - erases the D-pad from controller 1 (so the game's D-pad status-screen shortcuts never fire);
  - if a direction was pressed and no style action is running, switches style like the game does:
  1. stores the old style's level/EXP in the save data and loads the new style's into Dante;
  2. restores the active move flags (player+0x29d8, 8 words) from the purchased-moves copy
     (player+0x29f8), then FUN_0025b8c0(player) (Dante vtable +0x148) adds the new style's
     level moves; FUN_0025b8c0 only ORs bits in, so without the restore every style's level
     moves would pile up (the game only calls it once, on a freshly created Dante);
  3. FUN_00203f20(0, save + 0x104)   the Divinity Statue path: loads the new style's animation
                                     package(s) and unloads the old one (asynchronous disc read);
     then clears table entries 13-26 (no pointer into an unloaded pack) and sets PENDING_FLAG;
  4. HUD icon: release the icon model buffer (FUN_0013d660), rebind it to the new style's icon
     (CDraw::vf9, as FUN_0030e9c0 does when the HUD is created) and enable it (CDraw::vf13);
  5. optional: plays the menu "style confirmed" sound (separate cheat).

Addresses (all found in the decompiled SLUS_213.61, see RELATORIO_REA_DMC3.md):
  0x01d0d70c  pointer G = CSceneGameMain + 0x140; G+0x0c = player, G+0x24 = scene
  scene +0x18 state (1 = mission running; 3 = pause, 4 = status screen); +0x18e90 HUD
  HUD   +0x38 HUD package, +0x554a cached style icon index, +0x30c0 style icon (CDraw)
  0x005f5bb8  style -> icon index table
  0x01d0d710  pointer -> save data: +0xc5[6] style levels, +0xcc[6] style exp (float),
              +0x104 player setup record, +0x116 current style, +0x117 level, +0x138 exp
  player +0x0000 vtable (CPlDante = 0x00602650)
         +0x3c6c style, +0x3c6e style-action state, +0x3c74 level, +0x3c7c exp (float)
  0x006c3740/44/46  controller 1 held / pressed / released (u16, PS2 libpad bits)
  0x0033e630  play sound (a0 = 0x01e58e80, a1 = bank 0, a2 = 0x19 style-confirm, a3 = 0, t0 = 0)

usage: build_pnach.py <SLUS_213.61> <out_dir>
"""
import os
import struct
import sys

CAVE = 0x000FF000
NO_PACKS_FLAG = 0x000FF7E0   # diagnostic: non-zero skips filling Dante's animation table
NO_HUD_FLAG = 0x000FF7E4     # diagnostic: non-zero skips the HUD icon rebind on a switch
PENDING_FLAG = 0x000FF7E8    # set on a switch; cleared once the new style's packs are in Dante's table
SOUND_FLAG = 0x000FF7F0
FRAME_BEGIN = 0x001EDF20
FRAME_END = 0x001EDFB0
HOOK = 0x001EC468          # jal frame_Begin in main
OLD_HOOK = 0x001EC488      # jal frame_End, hooked by the first version: always restored
PLAY_SOUND = 0x0033E630
STYLE_MOVES = 0x0025B8C0
LOAD_STYLE_PACKS = 0x00203F20
HEAP_RELEASE = 0x0013D660
DRAW_BIND = 0x00281210
DRAW_ENABLE = 0x00281630
PACK_DATA = 0x001AEFC0       # FUN_001aefc0(mgr, 1, charId*0x22 + pack): data pointer, or 0 if not loaded
FIRST_STYLE_PACK = 0x0D      # 13-17 Swordmaster per melee weapon, 18-22 Gunslinger per gun,
LAST_STYLE_PACK = 0x1A       # 23 Trickster, 24 Royal Guard, 25 Quicksilver, 26 Doppelganger

R = {n: i for i, n in enumerate(
    "zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split())}

PROGRAM = f"""
    addiu sp, sp, -0x60
    sd    ra, 0(sp)
    sq    s0, 0x10(sp)
    sq    s1, 0x20(sp)
    sq    s2, 0x30(sp)
    sq    s3, 0x40(sp)
    jal   {FRAME_BEGIN:#x}
    nop
    lui   s2, 0x01d1
    lw    s2, -0x28f4(s2)
    beq   s2, zero, done
    nop
    lw    t1, 0x24(s2)
    beq   t1, zero, done
    nop
    lbu   t1, 0x18(t1)
    addiu t2, zero, 1
    bne   t1, t2, done
    nop
    lw    s0, 0xc(s2)
    beq   s0, zero, done
    nop
    lw    t1, 0(s0)
    lui   t2, 0x0060
    ori   t2, t2, 0x2650
    bne   t1, t2, done
    nop
    lui   t1, 0x0010
    lw    t1, {PENDING_FLAG - 0x100000}(t1)
    beq   t1, zero, after_packs
    nop
    lui   t1, 0x0010
    lw    t1, {NO_PACKS_FLAG - 0x100000}(t1)
    bne   t1, zero, after_packs
    nop
    lbu   t1, 0x46(s0)
    sll   t2, t1, 5
    sll   t1, t1, 1
    addu  s1, t1, t2
    sw    zero, 0x50(sp)
    lbu   t1, 0x3c6c(s0)
    sltiu t2, t1, 2
    beq   t2, zero, single_pack
    addiu s3, zero, 0
    beq   t1, zero, slot_loop
    nop
    addiu s3, zero, 2
slot_loop:
    addu  t1, s0, s3
    lbu   t1, 0x3d4f(t1)
    addiu t2, zero, 0xff
    beq   t1, t2, slot_next
    nop
    addiu t1, t1, {FIRST_STYLE_PACK:#x}
    sw    t1, 0x54(sp)
    lui   a0, 0x006c
    ori   a0, a0, 0x3810
    addiu a1, zero, 1
    jal   {PACK_DATA:#x}
    addu  a2, s1, t1
    lw    t1, 0x54(sp)
    sll   t1, t1, 2
    addu  t1, s0, t1
    sw    v0, 0x243c(t1)
    bne   v0, zero, slot_next
    nop
    lw    t1, 0x50(sp)
    addiu t1, t1, 1
    sw    t1, 0x50(sp)
slot_next:
    addiu s3, s3, 1
    lbu   t1, 0x3c6c(s0)
    beq   t1, zero, sm_end
    sltiu t2, s3, 5
    beq   zero, zero, slot_cond
    nop
sm_end:
    sltiu t2, s3, 2
slot_cond:
    bne   t2, zero, slot_loop
    nop
    beq   zero, zero, fill_done
    nop
single_pack:
    sltiu t2, t1, 6
    beq   t2, zero, fill_done
    addiu t1, t1, 0x15
    sw    t1, 0x54(sp)
    lui   a0, 0x006c
    ori   a0, a0, 0x3810
    addiu a1, zero, 1
    jal   {PACK_DATA:#x}
    addu  a2, s1, t1
    lw    t1, 0x54(sp)
    sll   t1, t1, 2
    addu  t1, s0, t1
    sw    v0, 0x243c(t1)
    bne   v0, zero, fill_done
    nop
    lw    t1, 0x50(sp)
    addiu t1, t1, 1
    sw    t1, 0x50(sp)
fill_done:
    lw    t1, 0x50(sp)
    bne   t1, zero, after_packs
    nop
    lui   t1, 0x0010
    sw    zero, {PENDING_FLAG - 0x100000}(t1)
after_packs:
    lui   t9, 0x006c
    lhu   t3, 0x3744(t9)
    lhu   t1, 0x3740(t9)
    andi  t1, t1, 0x0fff
    sh    t1, 0x3740(t9)
    andi  t1, t3, 0x0fff
    sh    t1, 0x3744(t9)
    lhu   t1, 0x3746(t9)
    andi  t1, t1, 0x0fff
    sh    t1, 0x3746(t9)
    andi  t1, t3, 0x1000
    bne   t1, zero, up
    andi  t1, t3, 0x2000
    bne   t1, zero, right
    andi  t1, t3, 0x8000
    bne   t1, zero, left
    andi  t1, t3, 0x4000
    bne   t1, zero, down
    nop
    beq   zero, zero, done
    nop
up:
    beq   zero, zero, set
    addiu t4, zero, 2
right:
    beq   zero, zero, set
    addiu t4, zero, 0
left:
    beq   zero, zero, set
    addiu t4, zero, 1
down:
    addiu t4, zero, 3
set:
    lbu   t5, 0x3c6c(s0)
    beq   t5, t4, done
    nop
    lbu   t1, 0x3c6e(s0)
    bne   t1, zero, done
    nop
    sltiu t1, t5, 6
    beq   t1, zero, done
    nop
    lui   s1, 0x01d1
    lw    s1, -0x28f0(s1)
    beq   s1, zero, done
    nop
    lbu   t1, 0x3c74(s0)
    addu  t7, s1, t5
    sb    t1, 0xc5(t7)
    lw    t1, 0x3c7c(s0)
    sll   t7, t5, 2
    addu  t7, s1, t7
    sw    t1, 0xcc(t7)
    sb    t4, 0x3c6c(s0)
    sb    t4, 0x116(s1)
    addu  t7, s1, t4
    lbu   t1, 0xc5(t7)
    sb    t1, 0x3c74(s0)
    sb    t1, 0x117(s1)
    sll   t7, t4, 2
    addu  t7, s1, t7
    lw    t1, 0xcc(t7)
    sw    t1, 0x3c7c(s0)
    sw    t1, 0x138(s1)
    addu  s3, t4, zero
    lw    t1, 0x29f8(s0)
    sw    t1, 0x29d8(s0)
    lw    t1, 0x29fc(s0)
    sw    t1, 0x29dc(s0)
    lw    t1, 0x2a00(s0)
    sw    t1, 0x29e0(s0)
    lw    t1, 0x2a04(s0)
    sw    t1, 0x29e4(s0)
    lw    t1, 0x2a08(s0)
    sw    t1, 0x29e8(s0)
    lw    t1, 0x2a0c(s0)
    sw    t1, 0x29ec(s0)
    lw    t1, 0x2a10(s0)
    sw    t1, 0x29f0(s0)
    lw    t1, 0x2a14(s0)
    sw    t1, 0x29f4(s0)
    jal   {STYLE_MOVES:#x}
    addu  a0, s0, zero
    addiu a0, zero, 0
    jal   {LOAD_STYLE_PACKS:#x}
    addiu a1, s1, 0x104
    addiu t1, s0, {0x243C + 4 * FIRST_STYLE_PACK:#x}
    addiu t2, s0, {0x243C + 4 * (LAST_STYLE_PACK + 1):#x}
clear_packs:
    sw    zero, 0(t1)
    addiu t1, t1, 4
    bne   t1, t2, clear_packs
    nop
    lui   t1, 0x0010
    addiu t2, zero, 1
    sw    t2, {PENDING_FLAG - 0x100000}(t1)
    lui   t1, 0x0010
    lw    t1, {NO_HUD_FLAG - 0x100000}(t1)
    bne   t1, zero, hud_done
    nop
    lw    t1, 0x24(s2)
    lui   t2, 0x0002
    addu  t1, t1, t2
    lw    s2, -0x7170(t1)
    beq   s2, zero, hud_done
    nop
    lui   t1, 0x005f
    addu  t1, t1, s3
    lbu   s3, 0x5bb8(t1)
    sb    s3, 0x554a(s2)
    lw    t0, 0x38(s2)
    lw    t1, 8(t0)
    lh    t1, 0(t1)
    bne   t1, zero, single
    lw    t2, 0xc(t0)
    lw    t3, 4(t2)
    sll   t5, s3, 1
    addiu t6, t5, 0x14
    sltu  t7, t3, t6
    bne   t7, zero, hud_done
    nop
    sll   t6, t5, 2
    addu  t6, t2, t6
    lw    a2, 0x50(t6)
    lw    a1, 0x54(t6)
    beq   a2, zero, hud_done
    nop
    beq   a1, zero, hud_done
    nop
    addu  a2, t2, a2
    beq   zero, zero, bind
    addu  a1, t2, a1
single:
    addu  a1, t2, zero
    addu  a2, t2, zero
bind:
    addu  s0, a1, zero
    addu  s1, a2, zero
    jal   {HEAP_RELEASE:#x}
    addiu a0, s2, 0x30d4
    addiu a0, s2, 0x30c0
    addu  a1, s0, zero
    jal   {DRAW_BIND:#x}
    addu  a2, s1, zero
    beq   v0, zero, hud_done
    nop
    addiu a0, s2, 0x30c0
    jal   {DRAW_ENABLE:#x}
    addiu a1, zero, 1
hud_done:
    lui   t1, 0x0010
    lw    t1, {SOUND_FLAG - 0x100000}(t1)
    beq   t1, zero, done
    nop
    lui   a0, 0x01e6
    addiu a0, a0, -0x7180
    addiu a1, zero, 0
    addiu a2, zero, 0x19
    addiu a3, zero, 0
    jal   {PLAY_SOUND:#x}
    addiu t0, zero, 0
done:
    ld    ra, 0(sp)
    lq    s0, 0x10(sp)
    lq    s1, 0x20(sp)
    lq    s2, 0x30(sp)
    lq    s3, 0x40(sp)
    jr    ra
    addiu sp, sp, 0x60
"""

I_OPS = {"addiu": 9, "sltiu": 11, "andi": 12, "ori": 13, "lui": 15, "beq": 4, "bne": 5,
         "lh": 33, "lw": 35, "lbu": 36, "lhu": 37, "sb": 40, "sh": 41, "sw": 43,
         "ld": 55, "sd": 63, "lq": 30, "sq": 31}
R_FUNCT = {"addu": 0x21, "sltu": 0x2B}


def assemble(text, base):
    lines = [l.split("#")[0].strip() for l in text.strip().splitlines()]
    labels, insns, pc = {}, [], base
    for l in lines:
        if not l:
            continue
        if l.endswith(":"):
            labels[l[:-1]] = pc
            continue
        insns.append((pc, l))
        pc += 4
    words = []
    for pc, l in insns:
        op, _, rest = l.partition(" ")
        a = [x.strip() for x in rest.split(",")] if rest else []
        if op == "nop":
            w = 0
        elif op == "jal":
            w = (3 << 26) | ((int(a[0], 0) >> 2) & 0x3FFFFFF)
        elif op == "jr":
            w = (R[a[0]] << 21) | 8
        elif op in R_FUNCT:
            w = (R[a[1]] << 21) | (R[a[2]] << 16) | (R[a[0]] << 11) | R_FUNCT[op]
        elif op == "sll":
            w = (R[a[1]] << 16) | (R[a[0]] << 11) | (int(a[2], 0) << 6)
        elif op in ("beq", "bne"):
            off = (labels[a[2]] - (pc + 4)) >> 2
            w = (I_OPS[op] << 26) | (R[a[0]] << 21) | (R[a[1]] << 16) | (off & 0xFFFF)
        elif op == "lui":
            w = (I_OPS[op] << 26) | (R[a[0]] << 16) | (int(a[1], 0) & 0xFFFF)
        elif op in ("addiu", "sltiu", "andi", "ori"):
            w = (I_OPS[op] << 26) | (R[a[1]] << 21) | (R[a[0]] << 16) | (int(a[2], 0) & 0xFFFF)
        elif op in ("lh", "lw", "lbu", "lhu", "sb", "sh", "sw", "ld", "sd", "lq", "sq"):
            off, base_reg = a[1].rstrip(")").split("(")
            w = (I_OPS[op] << 26) | (R[base_reg] << 21) | (R[a[0]] << 16) | (int(off, 0) & 0xFFFF)
        else:
            raise ValueError(f"unknown instruction: {l}")
        words.append((pc, w, l))
    return words


def elf_crc(data):
    crc = 0
    for (v,) in struct.iter_unpack("<I", data[: len(data) // 4 * 4]):
        crc ^= v
    return crc


def jal(target):
    return (3 << 26) | (target >> 2)


def main():
    elf, out_dir = sys.argv[1], sys.argv[2]
    data = open(elf, "rb").read()
    phoff = struct.unpack_from("<I", data, 28)[0]
    _, seg_off, seg_va, *_ = struct.unpack_from("<8I", data, phoff)
    word_at = lambda va: struct.unpack_from("<I", data, va - seg_va + seg_off)[0]
    for va, target in ((HOOK, FRAME_BEGIN), (OLD_HOOK, FRAME_END)):
        if word_at(va) != jal(target):
            sys.exit(f"call site {va:#x} is {word_at(va):#010x}, expected jal {target:#x} (wrong game version?)")

    code = assemble(PROGRAM, CAVE)
    if CAVE + 4 * len(code) > min(NO_PACKS_FLAG, NO_HUD_FLAG, PENDING_FLAG, SOUND_FLAG):
        sys.exit("routine overlaps the flag words")
    crc = elf_crc(data)

    os.makedirs(out_dir, exist_ok=True)
    name = f"SLUS-21361_{crc:08X}.pnach"
    with open(os.path.join(out_dir, name), "w", newline="\n") as f:
        f.write("gametitle=Devil May Cry 3 - Dante's Awakening Special Edition [SLUS-21361] (U)\n\n")
        f.write("[Style Switch (D-pad)]\n")
        f.write("author=DMC3_port research\n")
        f.write("description=Free Style: D-pad switches style mid-mission (Up Trickster, Right Swordmaster, "
                "Left Gunslinger, Down Royal Guard); loads the style's animations and updates the HUD icon\n")
        for pc, w, _ in code:
            f.write(f"patch=1,EE,{pc:08X},word,{w:08X}\n")
        for flag in (NO_PACKS_FLAG, NO_HUD_FLAG, SOUND_FLAG):
            f.write(f"patch=1,EE,{flag:08X},word,00000000\n")
        f.write(f"patch=1,EE,{OLD_HOOK:08X},word,{jal(FRAME_END):08X}\n")
        f.write(f"patch=1,EE,{HOOK:08X},word,{jal(CAVE):08X}\n\n")
        f.write("[Style Switch - sound]\n")
        f.write("author=DMC3_port research\n")
        f.write("description=Plays the style-confirm sound when switching (needs Style Switch (D-pad))\n")
        f.write(f"patch=1,EE,{SOUND_FLAG:08X},word,00000001\n\n")
        f.write("[Style Switch - debug: no animation table refresh]\n")
        f.write("description=Diagnostic only: skips the per-frame refresh of Dante's style animation table\n")
        f.write(f"patch=1,EE,{NO_PACKS_FLAG:08X},word,00000001\n\n")
        f.write("[Style Switch - debug: no HUD icon update]\n")
        f.write("description=Diagnostic only: skips the HUD style icon rebind when switching\n")
        f.write(f"patch=1,EE,{NO_HUD_FLAG:08X},word,00000001\n")
    with open(os.path.join(out_dir, "style_switch.lst"), "w", newline="\n") as f:
        for pc, w, l in code:
            f.write(f"{pc:08x}: {w:08x}    {l}\n")
        f.write(f"{OLD_HOOK:08x}: {jal(FRAME_END):08x}    jal {FRAME_END:#x}   (original, restored)\n")
        f.write(f"{HOOK:08x}: {jal(CAVE):08x}    jal {CAVE:#x}   (hook, replaces jal {FRAME_BEGIN:#x})\n")
    print(f"{name}: {len(code)} instructions at {CAVE:#x}-{CAVE + 4 * len(code):#x}, hook at {HOOK:#x}, ELF CRC {crc:08X}")


if __name__ == "__main__":
    main()
