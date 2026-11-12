# Minimal SLF extractor + STI (STCI) decoder -> PNG. Usage: sti.py file.slf name.sti out.png [subimage]
import struct, sys
from PIL import Image

def slf_get(slf, name):
    d = open(slf, 'rb').read()
    n = struct.unpack_from('<i', d, 512)[0]
    base = len(d) - n * 280
    for i in range(n):
        o = base + i * 280
        fn = d[o:o+256].split(b'\0')[0].decode('latin1')
        off, ln, state = struct.unpack_from('<IIB', d, o + 256)
        if state == 0 and fn.lower().replace('\\', '/').endswith(name.lower()):
            return d[off:off+ln]
    raise SystemExit('not found: ' + name)

def sti_to_image(b, sub=0):
    assert b[:4] == b'STCI'
    _, _, transp, flags, h, w = struct.unpack_from('<IIIIHH', b, 4)
    if flags & 0x04:  # 16-bit RGB565, uncompressed
        data = b[64:64 + w * h * 2]
        px = struct.unpack('<%dH' % (w * h), data)
        img = Image.new('RGB', (w, h))
        img.putdata([(((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31) for p in px])
        return img
    # 8-bit indexed: header has num colours, num subimages at 24/28
    ncol, nsub = struct.unpack_from('<IH', b, 24)
    pal = b[64:64 + 768]
    subs = 64 + 768
    off, ln, ox, oy, sh, sw = struct.unpack_from('<IIhhHH', b, subs + sub * 16)
    data = b[subs + nsub * 16 + off: subs + nsub * 16 + off + ln]
    out = bytearray()
    i = 0
    while i < len(data):  # ETRLE
        c = data[i]; i += 1
        if c == 0: continue  # row end
        if c & 0x80: out += b'\0' * (c & 0x7f)
        else: out += data[i:i+c]; i += c
    img = Image.frombytes('P', (sw, sh), bytes(out[:sw * sh]).ljust(sw * sh, b'\0'))
    img.putpalette(pal)
    return img.convert('RGB')

slf, name, outp = sys.argv[1:4]
sub = int(sys.argv[4]) if len(sys.argv) > 4 else 0
sti_to_image(slf_get(slf, name), sub).save(outp)
print(outp)
