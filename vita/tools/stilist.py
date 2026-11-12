# list STI files in an SLF with their subimage sizes (indexed ones)
import struct, sys
d = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<i', d, 512)[0]
base = len(d) - n * 280
for i in range(n):
    o = base + i * 280
    fn = d[o:o+256].split(b'\0')[0].decode('latin1')
    off, ln, state = struct.unpack_from('<IIB', d, o + 256)
    if state or not fn.lower().endswith('.sti'): continue
    b = d[off:off+ln]
    if b[:4] != b'STCI': continue
    flags, h, w = struct.unpack_from('<IHH', b, 16)
    if flags & 0x04:
        print(fn, 'rgb', w, h); continue
    nsub = struct.unpack_from('<H', b, 28)[0]
    subs = 64 + 768
    sizes = []
    for k in range(min(nsub, 12)):
        _, _, _, _, sh, sw = struct.unpack_from('<IIhhHH', b, subs + k * 16)
        sizes.append(f'{sw}x{sh}')
    print(fn, nsub, ' '.join(sizes))
