#!/usr/bin/env python3
"""Minimal MIPS disassembler for the BE-encoded GE ROM.

Usage: disasm.py <ram_addr_hex> [n_instructions]
Maps game-segment RAM (base 0x7F000000) to file offset 0x34B28.
Also handles lib segment (RAM base = _startSegmentEnd - 0x10000000, ROM 0x1050).
"""
import struct, sys

ROM = open('data/ge007.ntsc-final.z64', 'rb').read()
GAME_RAM_BASE = 0x7F000000
GAME_ROM_OFF = 0x34B28
LIB_ROM_OFF = 0x1050
LIB_RAM_BASE = 0x70000450  # from boot.s comment: code at ROM 0x1050 -> RAM 0x70000450

REGS = ["$zero","$at","$v0","$v1","$a0","$a1","$a2","$a3",
        "$t0","$t1","$t2","$t3","$t4","$t5","$t6","$t7",
        "$s0","$s1","$s2","$s3","$s4","$s5","$s6","$s7",
        "$t8","$t9","$t10","$t11","$t12","$t13","$gp","$sp",
        "$fp","$ra"]

RTYPE = {0x00:"sll",0x02:"srl",0x04:"sra",0x06:"sllv",0x08:"srav",0x0A:"sralv",
         0x0C:"and",0x0E:"or",0x0F:"xor",0x10:"nor",0x12:"slt",0x14:"sltu",
         0x18:"add",0x1A:"addu",0x1C:"sub",0x1E:"subu",
         0x20:"mult",0x21:"multu",0x22:"div",0x23:"divu",
         0x24:"madd",0x25:"maddu",0x26:"msub",0x27:"msubu"}

COP0 = {0x00:"mfc0",0x01:"mfc1",0x02:"mfc2",0x10:"mtc0",0x11:"mtc1",0x12:"mtc2",
        0x40:"bc0t",0x44:"bc0f"}

def disasm_word(w):
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    rd = (w >> 11) & 31
    sa = (w >> 6) & 31
    fn = w & 63
    imm = w & 0xFFFF
    def s16(v): return v - 0x10000 if v >= 0x8000 else v

    if op == 0x00:
        if fn in RTYPE:
            if fn in (0x00,0x02,0x04,0x06,0x08,0x0A):
                return "%s %s,%s,%d" % (RTYPE[fn], REGS[rd], REGS[rt], sa)
            if fn in (0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27):
                return "%s %s,%s" % (RTYPE[fn], REGS[rs], REGS[rt])
            return "%s %s,%s,%s" % (RTYPE[fn], REGS[rd], REGS[rs], REGS[rt])
        if fn == 0x19:  # tgei etc rare
            return "tgeiu? %s,%s,0x%04x" % (REGS[rd], REGS[rs], imm)
        return ".word 0x%08x" % w
    if op == 0x01:
        c = (w >> 21) & 3
        if c == 0:
            if fn in COP0:
                if fn in (0x00,0x10):
                    return "%s %s,0x%02x" % (COP0[fn], REGS[rt], rs)
                return "%s %s,0x%02x" % (COP0[fn], REGS[rs], rt)
            if fn == 0x00: return "mfc0? %s" % REGS[rt]
            return ".word 0x%08x" % w
        if c == 1:  # cop1
            if fn in (0x00,0x01,0x02,0x03):
                names = ["mfc1","cfc1","mtc1","ctc1"]
                return "%s %s,%s" % (names[fn], REGS[rt], REGS[rs])
            if fn == 0x46: return "bc1t +0x%04x" % s16(imm)
            if fn == 0x47: return "bc1f +0x%04x" % s16(imm)
            if fn == 0x4E: return "bc1t? (cond)"
            if fn == 0x4F: return "bc1f? (cond)"
            # fp ops
            fops = {0x10:"abs.s",0x11:"add.s",0x12:"div.s",0x13:"cmp.s",0x14:"cvt.s.w",
                    0x15:"cvt.w.s",0x16:"max.s",0x17:"min.s",0x18:"mov.s",0x19:"neg.s",
                    0x1A:"recip.s",0x1B:"rsq.s",0x1C:"sqrt.s",0x20:"abs.d",0x21:"add.d",
                    0x22:"div.d",0x23:"cmp.d",0x26:"max.d",0x27:"min.d",0x28:"mov.d",
                    0x29:"neg.d",0x41:"and.b",0x42:"or.b",0x45:"cvt.s.w"}
            if fn in fops:
                return "%s %f%d,%f%d,%f%d" % (fops[fn], rd, rs, rt, 0)
            return ".word 0x%08x" % w
        if c == 2:
            return "cop2? .word 0x%08x" % w
        return ".word 0x%08x" % w
    if op == 0x02: return "jalr %s%s" % (REGS[rs], ", "+REGS[rt] if rt else "")
    if op == 0x03: return "syscall"
    if op == 0x04: return "beq %s,%s,+0x%04x" % (REGS[rs], REGS[rt], s16(imm))
    if op == 0x05: return "bne %s,%s,+0x%04x" % (REGS[rs], REGS[rt], s16(imm))
    if op == 0x06: return "blez %s,+0x%04x" % (REGS[rs], s16(imm))
    if op == 0x07: return "bgtz %s,+0x%04x" % (REGS[rs], s16(imm))
    if op == 0x08: return "j 0x%08x" % ((w & 0x3FFFFFF) << 2)
    if op == 0x09: return "jal 0x%08x" % ((w & 0x3FFFFFF) << 2)
    if op == 0x0A: return "addi %s,%s,0x%04x" % (REGS[rt], REGS[rs], s16(imm))
    if op == 0x0B: return "addiu %s,%s,0x%04x" % (REGS[rt], REGS[rs], s16(imm))
    if op == 0x0C: return "andi %s,%s,0x%04x" % (REGS[rt], REGS[rs], imm)
    if op == 0x0D: return "slti %s,%s,0x%04x" % (REGS[rt], REGS[rs], s16(imm))
    if op == 0x0E: return "sltiu %s,%s,0x%04x" % (REGS[rt], REGS[rs], imm)
    if op == 0x0F: return "ori %s,%s,0x%04x" % (REGS[rt], REGS[rs], imm)
    if op == 0x10: return "xori %s,%s,0x%04x" % (REGS[rt], REGS[rs], imm)
    if op == 0x11: return "lui %s,0x%04x" % (REGS[rt], imm)
    if op in range(0x20, 0x3C):
        name = {0x20:"lb",0x21:"lhu",0x22:"lw",0x23:"lbu",0x24:"lwl",0x25:"lwr",
                0x28:"sb",0x29:"sh",0x2A:"sw",0x2B:"swl",0x2D:"swr",0x2F:"ld"}.get(op, ".word")
        if op in (0x28,0x29,0x2A):
            return "%s 0x%04x(%s)" % (name, s16(imm), REGS[rs])
        if op == 0x2F:
            return "ld %s,0x%04x(%s)" % (REGS[rt], s16(imm), REGS[rs])
        return "%s %s,0x%04x(%s)" % (name, REGS[rt], s16(imm), REGS[rs])
    if op == 0x3C: return "lui %s,0x%04x" % (REGS[rt], imm)
    if op == 0x3D: return "ld %s,0x%04x(%s)" % (REGS[rt], s16(imm), REGS[rs])
    return ".word 0x%08x" % w

def ram_to_file(ram):
    if GAME_RAM_BASE <= ram < GAME_RAM_BASE + 0x200000:
        return GAME_ROM_OFF + (ram - GAME_RAM_BASE)
    if LIB_RAM_BASE <= ram < LIB_RAM_BASE + 0x40000:
        return LIB_ROM_OFF + (ram - LIB_RAM_BASE)
    # bss/data in lib segment: RAM 0x8002xxxx etc — not in ROM; skip
    return None

def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    ram = int(sys.argv[1], 16)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    off = ram_to_file(ram)
    if off is None or off < 0 or off + 4*n > len(ROM):
        print("cannot map RAM %x" % ram); sys.exit(1)
    for i in range(n):
        w = struct.unpack_from('>I', ROM, off + i*4)[0]
        addr = ram + i*4
        print("  %08x:  %08x   %s" % (addr, w, disasm_word(w)))

if __name__ == '__main__':
    main()
