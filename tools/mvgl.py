#!/usr/bin/env python3
"""MVGL archives (MDB1): CRDB.ios.mvgl (iPad), the Android OBB, CRDB.psp2.mvgl (Vita).

  header   'MDB1', u16 tree entries, u16 names, u32 data entries, u32 data start, u32 total size
  tree     [entries] i16 compare bit, u16 data id, u16 left, u16 right (a Patricia trie on the
           names; entry 0 is the root, data id 0xffff)
  names    [names] 4-byte extension (NUL or space padded) + 60-byte name
  data     [data entries] u32 offset (from data start), u32 size, u32 stored size
           (stored < size: compressed)

  A compressed file is doboz (Attila Afra's LZ77 variant): a header byte (bits 3..5: the size
  fields' byte count - 1; bit 7: stored), the size, the compressed size, then 32-bit control
  words (bit 0 first, 31 flags each: the top set bit is the end marker), each flag a literal byte
  (0) or a match (1): 1..4 bytes, low 3 bits pick the form (offset back from the output's end,
  length 3 + n).

  mvgl.py list <archive> [pattern]          names, sizes, stored sizes
  mvgl.py stats <archive>                   counts by extension, compressed or not
  mvgl.py check <archive> [pattern]         every file decodes to its size
  mvgl.py extract <archive> <out dir> [pattern]   decoded files (--raw: as stored)
"""
import fnmatch
import os
import struct
import sys
from collections import Counter


# doboz match forms by the word's low 3 bits: mask, offset shift, length mask, length shift, bytes
_MATCH = [(0xff, 2, 0, 0, 1), (0xffff, 2, 0, 0, 2), (0xffff, 6, 15, 2, 2), (0xffffff, 8, 31, 3, 3),
          (0xff, 2, 0, 0, 1), (0xffff, 2, 0, 0, 2), (0xffff, 6, 15, 2, 2), (0xffffffff, 11, 255, 3, 4)]


def undoboz(b):
    attr = b[0]
    n = ((attr >> 3) & 7) + 1
    size = int.from_bytes(b[1:1 + n], 'little')
    p = 1 + 2 * n
    if attr & 0x80:
        return bytes(b[p:p + size])
    out = bytearray()
    cw = 1
    while len(out) < size:
        if cw == 1:
            cw = struct.unpack_from('<I', b, p)[0]
            p += 4
        if cw & 1 == 0:
            out.append(b[p])
            p += 1
        else:
            w = int.from_bytes(bytes(b[p:p + 4]).ljust(4, b'\0'), 'little')
            mask, oshift, lmask, lshift, nbytes = _MATCH[w & 7]
            src = len(out) - ((w & mask) >> oshift)
            length = ((w >> lshift) & lmask) + 3
            p += nbytes
            if src < 0:
                raise ValueError('match before the start')
            for k in range(length):
                out.append(out[src + k])
        cw >>= 1
    return bytes(out[:size])


class Mvgl:
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        h = self.f.read(0x14)
        if h[:4] != b'MDB1':
            raise ValueError(f'{path}: not an MDB1 archive')
        self.treeCount, self.nameCount, self.dataCount, self.dataStart, self.total = struct.unpack('<HHIII', h[4:])
        tree = self.f.read(8 * self.treeCount)
        self.tree = [struct.unpack_from('<hHHH', tree, 8 * i) for i in range(self.treeCount)]
        names = self.f.read(64 * self.nameCount)
        self.names = []
        for i in range(self.nameCount):
            ext = names[64 * i:64 * i + 4].split(b'\0')[0].decode('ascii', 'replace').rstrip(' ')
            name = names[64 * i + 4:64 * i + 64].split(b'\0')[0].decode('ascii', 'replace')
            self.names.append((name, ext))
        data = self.f.read(12 * self.dataCount)
        self.data = [struct.unpack_from('<III', data, 12 * i) for i in range(self.dataCount)]

    def files(self):
        """(full name, offset, size, stored size) per tree entry with data"""
        out = []
        for i, (bit, did, left, right) in enumerate(self.tree):
            if did == 0xffff or i >= len(self.names):
                continue
            name, ext = self.names[i]
            off, size, stored = self.data[did]
            out.append((name + ('.' + ext if ext else ''), self.dataStart + off, size, stored))
        return out

    def read(self, off, stored):
        self.f.seek(off)
        return self.f.read(stored)

    def decoded(self, off, size, stored):
        b = self.read(off, stored)
        return b if stored == size else undoboz(b)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd, arc = argv[1], Mvgl(argv[2])
    pat = None
    if cmd == 'list':
        pat = argv[3] if len(argv) > 3 else None
        for n, off, size, stored in arc.files():
            if pat and not fnmatch.fnmatch(n, pat):
                continue
            print(f'{off:10x} {size:10d} {stored:10d} {n}')
    elif cmd == 'stats':
        files = arc.files()
        print(f'{arc.path}: {arc.treeCount} tree entries, {arc.nameCount} names, {arc.dataCount} data, '
              f'data from 0x{arc.dataStart:x}, {arc.total} bytes; {len(files)} files')
        by = Counter()
        comp = Counter()
        size = Counter()
        for n, off, s, st in files:
            e = n.rsplit('.', 1)[-1] if '.' in n else ''
            by[e] += 1
            comp[e] += st < s
            size[e] += s
        for e, c in by.most_common():
            print(f'  {e or "(none)":8s} {c:6d} files, {comp[e]:6d} compressed, {size[e] / 1e6:9.1f} MB')
    elif cmd == 'check':
        pat = argv[3] if len(argv) > 3 else None
        ok = bad = 0
        for n, off, size, stored in arc.files():
            if pat and not fnmatch.fnmatch(n, pat):
                continue
            try:
                good = len(arc.decoded(off, size, stored)) == size
            except (ValueError, IndexError, struct.error):
                good = False
            ok += good
            bad += not good
            if not good:
                print('bad', n)
        print(f'{ok} decode, {bad} bad')
        return 1 if bad else 0
    elif cmd == 'extract':
        raw = '--raw' in argv
        args = [a for a in argv[3:] if a != '--raw']
        out = args[0]
        pat = args[1] if len(args) > 1 else None
        for n, off, size, stored in arc.files():
            if pat and not fnmatch.fnmatch(n, pat):
                continue
            p = os.path.join(out, n.replace('\\', '/'))
            os.makedirs(os.path.dirname(p) or '.', exist_ok=True)
            with open(p, 'wb') as w:
                w.write(arc.read(off, stored) if raw else arc.decoded(off, size, stored))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
