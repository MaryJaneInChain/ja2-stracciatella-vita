#!/usr/bin/env python3
"""Minimal Vita psp2core reader (format per xyzz/vita-parse-core).
Prints crashed threads and module-relative offsets of PC/LR/stack words in segment 1
of our module, as `seg1off` lines to feed to arm-vita-eabi-addr2line -e build-vita/ja2.
   python3 tools/coreinfo.py dump.psp2dmp [module-name-substring]"""
import gzip, struct, sys
from elftools.elf.elffile import ELFFile

u32 = lambda d, o: struct.unpack_from("<I", d, o)[0]
cstr = lambda d, o: d[o:d.index(b"\0", o)].decode(errors="replace")
REASONS = {0x30002: "undefined instr", 0x30003: "prefetch abort", 0x30004: "data abort", 0x60080: "div by zero"}

elf = ELFFile(gzip.open(sys.argv[1], "rb"))
want = sys.argv[2] if len(sys.argv) > 2 else "ja2"
notes, loads = {}, []
for seg in elf.iter_segments():
    if seg.header.p_type == "PT_NOTE":
        for n in seg.iter_notes():
            notes[n["n_name"]] = n["n_desc"].encode("latin1") if isinstance(n["n_desc"], str) else n["n_desc"]
    elif seg.header.p_type == "PT_LOAD":
        loads.append((seg.header.p_vaddr, seg.data()))

mods, d, off = [], notes["MODULE_INFO"], 8
for _ in range(u32(d, 4)):
    name, nseg = cstr(d, off + 0x24), u32(d, off + 0x4C)
    off += 0x50
    segs = [(u32(d, off + i*0x14 + 8), u32(d, off + i*0x14 + 12)) for i in range(nseg)]
    off += nseg * 0x14 + 0x10
    mods.append((name, segs))

def where(a):
    for name, segs in mods:
        for i, (s, n) in enumerate(segs):
            if s <= a < s + n:
                tag = "OURS seg%d+0x%x" % (i + 1, a - s) if want in name else "%s seg%d+0x%x" % (name, i + 1, a - s)
                return tag
    return ""

def read(a):
    for v, data in loads:
        if v <= a < v + len(data) - 3:
            return u32(data, a - v)

print("modules:", ", ".join(m[0] for m in mods))
threads, d, off = {}, notes["THREAD_INFO"], 8
for _ in range(u32(d, 4)):
    sz = u32(d, off)
    threads[u32(d, off + 4)] = (cstr(d, off + 8), u32(d, off + 0x74), u32(d, off + 0x9C))
    off += sz
regs, d, off = {}, notes["THREAD_REG_INFO"], 8
for _ in range(u32(d, 4)):
    sz = u32(d, off)
    regs[u32(d, off + 4)] = [u32(d, off + 8 + 4*i) for i in range(16)]
    off += sz

for tid, (name, reason, pc) in threads.items():
    r = regs.get(tid, [0]*16)
    print("\nthread %s stop=0x%x %s  PC=0x%x %s  LR=0x%x %s" % (name, reason, REASONS.get(reason, ""), pc, where(pc), r[14], where(r[14])))
    if reason:
        print("  regs:", " ".join("r%d=%x" % (i, v) for i, v in enumerate(r[:13])), "sp=%x" % r[13])
        for i in range(64):
            v = read(r[13] + 4*i)
            if v is not None and "OURS seg1" in where(v):
                print("  stack[%d] 0x%x %s" % (i, v, where(v)))
