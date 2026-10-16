#!/usr/bin/env python3
"""Compiled Squirrel 2.2.4 scripts (.nut in the MDB1 archives): parse and disassemble.

The stream (SQClosure::Save / SQFunctionProto::Save, Squirrel 2.2): u16 0xfafa, 'SQIR' (u32 tags,
little-endian), sizeof(SQChar) = 1, then a function prototype -
  PART  source name, name                                   (objects: u32 type, then the value)
  PART  u32 counts: literals, parameters, outer values, local variables, line infos,
        default parameters, instructions, functions
  PART  literals          PART  parameters (names)
  PART  outer values (u32 type, object, name)
  PART  local variables (name, u32 position, start op, end op)
  PART  line infos (u32 line, u32 op)          PART  default parameters (u32 each)
  PART  instructions (8 bytes: i32 arg1, u8 op, u8 arg0, u8 arg2, u8 arg3)
  PART  functions (prototypes, recursively)
  u32 stack size, u8 generator, u8 varparams
and 'TAIL' after the outermost. The opcode numbers are 2.2's, checked against the game's own
SQVM::Execute (the case bodies' helpers: 0x06 StartCall, 0x11 ARITH_OP, 0x30 CLOSURE_OP, ...).

  nut.py dis <file.nut>            the whole script, function by function
  nut.py dis <archive.mvgl> <name> the same, from an archive (tools/mvgl.py)
  nut.py names <file.nut | archive.mvgl [pattern]>   the literal strings used as keys of
                                   GETK / PREPCALLK (table.field / calls), counted
"""
import fnmatch
import os
import struct
import sys
from collections import Counter

OPS = ['LINE', 'LOAD', 'LOADINT', 'LOADFLOAT', 'DLOAD', 'TAILCALL', 'CALL', 'PREPCALL', 'PREPCALLK',
       'GETK', 'MOVE', 'NEWSLOT', 'DELETE', 'SET', 'GET', 'EQ', 'NE', 'ARITH', 'BITW', 'RETURN',
       'LOADNULLS', 'LOADROOTTABLE', 'LOADBOOL', 'DMOVE', 'JMP', 'JNZ', 'JZ', 'LOADFREEVAR', 'VARGC',
       'GETVARGV', 'NEWTABLE', 'NEWARRAY', 'APPENDARRAY', 'GETPARENT', 'COMPARITH', 'COMPARITHL',
       'INC', 'INCL', 'PINC', 'PINCL', 'CMP', 'EXISTS', 'INSTANCEOF', 'AND', 'OR', 'NEG', 'NOT',
       'BWNOT', 'CLOSURE', 'YIELD', 'RESUME', 'FOREACH', 'POSTFOREACH', 'DELEGATE', 'CLONE',
       'TYPEOF', 'PUSHTRAP', 'POPTRAP', 'THROW', 'CLASS', 'NEWSLOTA']
CMP = {0: '>', 2: '>=', 3: '<', 4: '<='}

OT_NULL, OT_INTEGER, OT_FLOAT, OT_STRING, OT_BOOL = 0x01000001, 0x05000002, 0x05000004, 0x08000010, 0x01000008


class Reader:
    def __init__(self, b):
        self.b, self.p = b, 0

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.b, self.p)[0]
        self.p += 4
        return v

    def u8(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def tag(self, want):
        t = self.b[self.p:self.p + 4]
        self.p += 4
        if t != want:
            raise ValueError(f'expected {want!r} at {self.p - 4:#x}, found {t!r}')

    def obj(self):
        t = self.u32()
        if t == OT_STRING:
            n = self.u32()
            s = self.b[self.p:self.p + n]
            self.p += n
            return s.decode('utf-8', 'replace')
        if t == OT_INTEGER:
            return self.i32()
        if t == OT_FLOAT:
            v = struct.unpack_from('<f', self.b, self.p)[0]
            self.p += 4
            return v
        if t == OT_BOOL:
            return self.u32() != 0
        if t == OT_NULL:
            return None
        raise ValueError(f'object type {t:#x} at {self.p - 4:#x}')


class Proto:
    pass


def read_proto(r):
    f = Proto()
    r.tag(b'TRAP')
    f.source, f.name = r.obj(), r.obj()
    r.tag(b'TRAP')
    nlit, npar, nout, nloc, nline, ndef, nins, nfun = (r.u32() for _ in range(8))
    r.tag(b'TRAP')
    f.literals = [r.obj() for _ in range(nlit)]
    r.tag(b'TRAP')
    f.params = [r.obj() for _ in range(npar)]
    r.tag(b'TRAP')
    f.outers = [(r.u32(), r.obj(), r.obj()) for _ in range(nout)]
    r.tag(b'TRAP')
    f.locals = [(r.obj(), r.u32(), r.u32(), r.u32()) for _ in range(nloc)]
    r.tag(b'TRAP')
    f.lines = [(r.u32(), r.u32()) for _ in range(nline)]
    r.tag(b'TRAP')
    f.defaults = [r.u32() for _ in range(ndef)]
    r.tag(b'TRAP')
    f.code = []
    for _ in range(nins):
        a1 = r.i32()
        op, a0, a2, a3 = r.u8(), r.u8(), r.u8(), r.u8()
        f.code.append((op, a0, a1, a2, a3))
    r.tag(b'TRAP')
    f.functions = [read_proto(r) for _ in range(nfun)]
    f.stack = r.u32()
    f.generator, f.varparams = r.u8(), r.u8()
    return f


def parse(b):
    if b[:2] != b'\xfa\xfa' or b[2:6] != b'RIQS':
        raise ValueError('not a compiled Squirrel script')
    r = Reader(b)
    r.p = 6
    if r.u32() != 1:
        raise ValueError('SQChar is not 1 byte')
    f = read_proto(r)
    r.tag(b'LIAT')
    return f


def lit(f, i):
    v = f.literals[i] if 0 <= i < len(f.literals) else f'?lit{i}'
    return repr(v)


def operands(f, pc, ins):
    op, a0, a1, a2, a3 = ins
    n = OPS[op] if op < len(OPS) else f'op{op:#x}'
    if n in ('LOAD',):
        return f'r{a0} = {lit(f, a1)}'
    if n == 'LOADINT':
        return f'r{a0} = {a1}'
    if n == 'LOADFLOAT':
        return f'r{a0} = {struct.unpack("<f", struct.pack("<i", a1))[0]!r}'
    if n == 'DLOAD':
        return f'r{a0} = {lit(f, a1)}, r{a2} = {lit(f, a3)}'
    if n in ('GETK',):
        return f'r{a0} = r{a2}[{lit(f, a1)}]'
    if n == 'PREPCALLK':
        return f'r{a0} = r{a2}[{lit(f, a1)}], r{a3} = r{a2}'
    if n == 'PREPCALL':
        return f'r{a0} = r{a2}[r{a1}], r{a3} = r{a2}'
    if n in ('CALL', 'TAILCALL'):
        return f'r{a0} = r{a1}(r{a2} .. {a3} args)'
    if n == 'GET':
        return f'r{a0} = r{a1}[r{a2}]'
    if n in ('SET', 'NEWSLOT'):
        return f'r{a1}[r{a2}] = r{a3}' + (f' -> r{a0}' if a0 != 0xff else '')
    if n == 'MOVE':
        return f'r{a0} = r{a1}'
    if n in ('JMP',):
        return f'-> {pc + 1 + a1}'
    if n in ('JZ', 'JNZ'):
        return f'r{a0} -> {pc + 1 + a1}'
    if n == 'CMP':
        return f'r{a0} = r{a2} {CMP.get(a3, a3)} r{a1}'
    if n in ('EQ', 'NE'):
        return f'r{a0} = r{a2} {"==" if n == "EQ" else "!="} ' + (lit(f, a1) if a3 else f'r{a1}')
    if n == 'ARITH':
        return f'r{a0} = r{a2} {chr(a3)} r{a1}'
    if n == 'LOADBOOL':
        return f'r{a0} = {bool(a1)}'
    if n == 'LOADNULLS':
        return f'r{a0} .. +{a1} = null'
    if n == 'LOADROOTTABLE':
        return f'r{a0} = roottable'
    if n == 'RETURN':
        return f'r{a1}' if a0 != 0xff else ''
    if n == 'CLOSURE':
        return f'r{a0} = closure {a1}' + (f' ({f.functions[a1].name})' if a1 < len(f.functions) else '')
    if n == 'LINE':
        return f'line {a1}'
    return f'{a0} {a1} {a2} {a3}'


def dis(f, out, depth=0):
    pad = '  ' * depth
    out.append(f'{pad}function {f.name}({", ".join(map(str, f.params))})  [{f.source}; {len(f.code)} ops, '
               f'{len(f.literals)} literals, stack {f.stack}]')
    for pc, ins in enumerate(f.code):
        op = ins[0]
        if OPS[op] == 'LINE' if op < len(OPS) else False:
            continue
        out.append(f'{pad}  {pc:5d}  {OPS[op] if op < len(OPS) else hex(op):13s} {operands(f, pc, ins)}')
    for g in f.functions:
        dis(g, out, depth + 1)


def keys(f, c):
    for ins in f.code:
        if ins[0] < len(OPS) and OPS[ins[0]] in ('GETK', 'PREPCALLK') and 0 <= ins[2] < len(f.literals):
            c[f.literals[ins[2]]] += 1
    for g in f.functions:
        keys(g, c)


def scripts(argv):
    """(name, bytes) for a .nut file or an archive's scripts matching a pattern"""
    path = argv[0]
    if open(path, 'rb').read(4) == b'MDB1':
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import mvgl
        a = mvgl.Mvgl(path)
        pat = argv[1] if len(argv) > 1 else '*.nut'
        for n, off, size, stored in a.files():
            if n.endswith('.nut') and (fnmatch.fnmatch(n, pat) or fnmatch.fnmatch(n.split('\\')[-1], pat)):
                yield n, a.decoded(off, size, stored)
    else:
        yield path, open(path, 'rb').read()


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == 'dis':
        for name, b in scripts(argv[2:]):
            out = [f'== {name}']
            dis(parse(b), out)
            print('\n'.join(out))
    elif cmd == 'names':
        c = Counter()
        n = 0
        for name, b in scripts(argv[2:]):
            keys(parse(b), c)
            n += 1
        for k, v in c.most_common():
            print(f'{v:7d}  {k}')
        print(f'{n} scripts, {len(c)} names', file=sys.stderr)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
