#!/usr/bin/env python3
"""The awakenings', advents' and monsters' special scenes (BtlSpecialCommandList::BuildAwake /
BuildAdvent) and which command shows which (BtlActionCtrl::SetActionTable) as C++:
engine/src/battle/specialtable.inc.

Each scene is built in code - a switch on the awakening's or advent's number, each case a run of
BtlSpecialCommandList::AddXxx(time, ...) calls (effects, cameras, visibility, hits, sounds...).
This runs each case of the two functions through a small ARM interpreter (over llvm-objdump's
listing: mov / movw / movt / add / ldr from the literal pools / str and stm to the stack and the
local Vector3 buffer, b, bl) and records every call to BtlSpecialCommandList, its arguments read
by the mangled signature (softfp: r1.., then the stack; a string from the library, a Vector3
reference's three floats). Each step's op is the number its AddXxx stores (piVar[1] = n in the
decompilation: BtlActionCtrl's handler table, ActionSpecialSetMotion 0 .. SetSkipEnable 34;
AddCommand's its argument), BuildAwakeFromMotionTable's -1 (its figure and motion: the hits,
the dash and the return the viewer reads from the motion table).

Which scene a command shows: SetActionTable's switch on it (by whether its +0x48 is set, the
party's awakenings and advents, or not, the monsters' specials) - a run of ifs and gotos in the
decompilation, compiled here as it stands (cc) and run for each command.

  gen_special_table.py <libChaosRings2.so (armv7)> <decomp.c> [llvm-objdump] > engine/src/battle/specialtable.inc
"""
import os
import tempfile
import re
import struct
import subprocess
import sys

FUNCS = {'BuildAdvent': '_ZN21BtlSpecialCommandList11BuildAdventEP7BtlMaini',
         'BuildAwake': '_ZN21BtlSpecialCommandList10BuildAwakeEP7BtlMaini'}


def file_offset(lib, vaddr):
    phoff, = struct.unpack_from('<I', lib, 0x1c)
    phnum, = struct.unpack_from('<H', lib, 0x2c)
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from('<5I', lib, phoff + i * 32)
        if p_type == 1 and p_vaddr <= vaddr < p_vaddr + p_filesz:
            return vaddr - p_vaddr + p_offset
    return None


def symbols(lib_path, objdump):
    nm = objdump.replace('objdump', 'nm')
    out = subprocess.run([nm, '-D', '-S', '--defined-only', lib_path], capture_output=True, text=True).stdout
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 4:
            syms[p[3]] = (int(p[0], 16), int(p[1], 16))
    return syms


def listing(lib_path, objdump, start, size):
    out = subprocess.run([objdump, '-d', '--no-show-raw-insn', '--triple=armv7', '--start-address=%d' % start,
                          '--stop-address=%d' % (start + size), lib_path], capture_output=True, text=True).stdout
    ins = {}
    order = []
    for line in out.splitlines():
        m = re.match(r'\s+([0-9a-f]+):\s+(\S+)\s*(.*)$', line)
        if m:
            a = int(m.group(1), 16)
            ins[a] = (m.group(2), m.group(3))
            order.append(a)
    return ins, order


def params(mangled):
    """the parameter kinds of BtlSpecialCommandList::Name(...) from its mangled name"""
    m = re.match(r'_ZN21BtlSpecialCommandList(\d+)(.*)$', mangled)
    n = int(m.group(1))
    name, rest = m.group(2)[:n], m.group(2)[n:]
    assert rest.startswith('E')
    rest = rest[1:]
    kinds = []
    while rest:
        if rest.startswith('PKc'):
            kinds.append('s')
            rest = rest[3:]
        elif rest.startswith('RKN10Vectormath3Aos7Vector3E'):
            kinds.append('v')
            rest = rest[len('RKN10Vectormath3Aos7Vector3E'):]
        elif rest.startswith('P7BtlMain'):
            kinds.append('m')
            rest = rest[len('P7BtlMain'):]
        elif rest[0] in 'fibj':
            kinds.append(rest[0])
            rest = rest[1:]
        elif rest[0] == 'v':
            rest = rest[1:]
        else:
            raise ValueError('kind ' + rest)
    return name, kinds


class Run:
    def __init__(self, lib, ins, case, entry):
        self.lib, self.ins = lib, ins
        self.r = {'r%d' % k: None for k in range(13)}
        self.r.update(r0='THIS', r1='MAIN', r2=case, sp=0x800000, lr=None, r11=None)
        self.mem = {}
        self.pc = entry
        self.calls = []

    def val(self, tok, addr):
        tok = tok.strip()
        if tok.startswith('#'):
            return int(tok[1:], 0) & 0xffffffff
        if tok == 'pc':
            return addr + 8
        return self.r[tok]

    def string(self, va):
        o = file_offset(self.lib, va)
        return self.lib[o:o + 96].split(b'\0')[0].decode('latin-1') if o is not None else None

    def word(self, va):
        if va in self.mem:
            return self.mem[va]
        o = file_offset(self.lib, va)
        return struct.unpack_from('<I', self.lib, o)[0] if o is not None else None

    def step(self):
        addr = self.pc
        if addr not in self.ins:
            raise RuntimeError('no instruction at %x' % addr)
        op, args = self.ins[addr]
        args = args.split('@')[0].strip()
        nxt = addr + 4
        regs = self.r
        def ops():
            return [a.strip() for a in re.split(r',(?![^\[]*\])(?![^{]*})', args)]
        if op in ('push',):
            pass
        elif op == 'pop':
            return None
        elif op in ('mov', 'mvn', 'movw'):
            d, s = ops()
            v = self.val(s, addr)
            regs[d] = (~v & 0xffffffff) if op == 'mvn' and v is not None else v
        elif op == 'movt':
            d, s = ops()
            regs[d] = ((regs[d] or 0) & 0xffff) | (self.val(s, addr) << 16)
        elif op in ('add', 'sub', 'bic', 'addls'):
            o = ops()
            d, n, m = o[0], o[1], o[2]
            a, b = self.val(n, addr), self.val(m, addr)
            if len(o) > 3 and o[3].startswith('lsl'):
                b = (b << int(o[3].split('#')[1])) & 0xffffffff
            if op == 'addls':  # (the switch: case <= 39 jumps into its table of branches)
                if regs['r2'] <= self.limit:
                    return a + b
                return nxt
            if a is None or b is None:
                regs[d] = None
            else:
                regs[d] = (a + b if op == 'add' else a - b if op == 'sub' else a & ~b) & 0xffffffff
        elif op == 'cmp':
            self.limit = self.val(ops()[1], addr)
        elif op in ('ldr', 'str'):
            d, m = ops()[0], ops()[1]
            mm = re.match(r'\[(\w+)(?:,\s*#(-?0x[0-9a-f]+|-?\d+))?\]', m)
            base = self.val(mm.group(1), addr)
            ea = (base + int(mm.group(2) or '0', 0)) & 0xffffffff if base is not None else None
            if op == 'ldr':
                regs[d] = self.word(ea) if ea is not None else None
            elif ea is not None:
                self.mem[ea] = regs[d]
        elif op in ('stm', 'stmib'):
            n, lst = args.split(',', 1)
            base = self.val(n, addr)
            names = [x.strip() for x in lst.strip(' {}').split(',')]
            off = 4 if op == 'stmib' else 0
            for k, nm in enumerate(names):
                self.mem[base + off + 4 * k] = regs[nm]
        elif op == 'b':
            return int(args.split()[0], 16)
        elif op == 'bl':
            target = re.search(r'<([^>]+)>', self.ins[addr][1]).group(1)
            self.call(target)
            for k in ('r0', 'r1', 'r2', 'r3', 'r12'):
                regs[k] = None
            regs['r0'] = 'RET'
        else:
            raise RuntimeError('%x: %s %s' % (addr, op, args))
        return nxt

    def call(self, target):
        if not target.startswith('_ZN21BtlSpecialCommandList'):
            self.calls.append(('?' + target, []))
            return
        name, kinds = params(target)
        slots = [self.r['r1'], self.r['r2'], self.r['r3']] + [self.mem.get(self.r['sp'] + 4 * k) for k in range(8)]
        out = []
        for k, kind in enumerate(kinds):
            v = slots[k]
            if kind == 'f':
                out.append(struct.unpack('<f', struct.pack('<I', v or 0))[0] if v is not None else None)
            elif kind in 'ijb':
                out.append(None if v is None else (v - (1 << 32) if v >= 1 << 31 else v))
            elif kind == 's':
                out.append(self.string(v) if v else None)
            elif kind == 'v':
                out.append(tuple(struct.unpack('<f', struct.pack('<I', self.mem.get(v + 4 * j) or 0))[0] for j in range(3)) if v else None)
            elif kind == 'm':
                out.append('main')
        self.calls.append((name, out))


def add_ops(src):
    """each AddXxx's op (the number it stores in the step's second word)"""
    ops = {}
    for m in re.finditer(r'/\* BtlSpecialCommandList::(Add\w+)\(', src):
        body = src[m.end():src.index('\n// ---- ', m.end())]
        n = re.search(r'\w+\[1\] = (0x[0-9a-f]+|\d+);', body)
        if n:
            ops[m.group(1)] = int(n.group(1), 0)
    return ops


def scene_map(src):
    """SetActionTable's switch: [(command, overbreak, advent, scene)]"""
    start = src.index('/* BtlActionCtrl::SetActionTable() */')
    fn = src[start:src.index('\n// ---- ', start)]
    lines = fn.splitlines()
    first = next(k for k, l in enumerate(lines) if re.search(r'0x58 \+ 0x58\) = 0xffff;', l))
    last = max(k for k, l in enumerate(lines) if l == '}')
    out = []
    kinds = ['empty', 'sopia', 'awake', 'advent']
    for l in lines[first + 1:last]:
        t = l.strip()
        if t.startswith(('iVar4 = IsOverbreak', 'iVar5 = (int)', 'sVar2 = *(short', 'puVar6 =', 'pBVar8 =', 'iVar4 = -0x')):
            continue
        if re.search(r'\+ 0x20\) = ', t):
            continue
        m = re.search(r'\+ 0x1c\) = BtlActionTable::action_table_(\w+);', t)
        if m:
            out.append('table = %d;' % kinds.index(m.group(1)))
            continue
        m = re.search(r'\+ 0x58\) = (\w+);', t)
        if m:
            out.append('scene = (short)%s;' % m.group(1))
            continue
        out.append(l.replace('return;', 'goto done;'))
    c = ['#include <stdio.h>', 'static void f(int iVar4, short sVar2) {', 'int table = -1, scene = -1;'] + out + [
        'done: if (table >= 2 && scene >= 0) printf("%d %d %d %d\\n", sVar2, iVar4, table == 3, scene); }',
        'int main(void) { for (int o = 0; o < 2; ++o) for (int c = 0; c < 2000; ++c) f(o, (short)c); return 0; }']
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, 'm.c'), 'w').write('\n'.join(c))
        subprocess.run([os.environ.get('CC', 'cc'), '-w', '-o', os.path.join(d, 'm'), os.path.join(d, 'm.c')], check=True)
        res = subprocess.run([os.path.join(d, 'm')], capture_output=True, text=True, check=True).stdout
    return sorted(tuple(int(x) for x in l.split()) for l in res.splitlines())


def run(lib, ins, entry, case):
    r = Run(lib, ins, case, entry)
    r.limit = -1
    pc = entry
    for _ in range(20000):
        pc = r.step()
        if pc is None:
            return r.calls
        r.pc = pc
    raise RuntimeError('runaway')


def fl(x):
    t = '%.7g' % x
    return t + ('f' if ('.' in t or 'e' in t) else '.0f')


def cstr(s):
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"') if s is not None else 'nullptr'


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    objdump = argv[3] if len(argv) > 3 else 'llvm-objdump'
    lib = open(argv[1], 'rb').read()
    src = open(argv[2], encoding='utf-8', errors='replace').read()
    ops = add_ops(src)
    syms = symbols(argv[1], objdump)
    out = ['// Generated by tools/gen_special_table.py from BtlSpecialCommandList::BuildAwake and',
           '// BuildAdvent, BtlActionCtrl::SetActionTable - do not edit (the types: battle/special.hpp).',
           '// A scene: its steps in the order added, each an AddXxx call - the op (the handler\'s number;',
           '// -1 BuildAwakeFromMotionTable), its time (seconds) and its other arguments as the call had',
           '// them (ints and bools, floats, a string, a Vector3).']
    tables = []
    for fn, mangled in FUNCS.items():
        start, size = syms[mangled]
        ins, order = listing(argv[1], objdump, start, size)
        cases = 40 if fn == 'BuildAwake' else 6
        scenes = []
        for case in range(cases):
            try:
                calls = run(lib, ins, start, case)
            except Exception as e:  # (a case past the table's end)
                print('// %s case %d: %s' % (fn, case, e), file=sys.stderr)
                calls = []
            steps = []
            for name, args in calls:
                if name.startswith('?'):
                    continue
                ints, floats, s, v = [], [], None, None
                kinds = None
                for a in args[1:] if name not in ('Create',) else args:
                    if isinstance(a, str):
                        s = a if a != 'main' else s
                    elif isinstance(a, tuple):
                        v = a
                    elif isinstance(a, float):
                        floats.append(a)
                    elif a is not None:
                        ints.append(a)
                time = args[0] if args and isinstance(args[0], float) else 0.0
                if name == 'BuildAwakeFromMotionTable':
                    time = 0.0
                if name in ('Create', 'Dump'):
                    continue
                steps.append((name, time, ints[:6], floats[:4], s, v))
            scenes.append(steps)
        tables.append((fn, scenes))
    for fn, scenes in tables:
        for k, steps in enumerate(scenes):
            if not steps:
                continue
            out.append('static const SpecialStep k%s%d[%d] = {' % (fn, k, len(steps)))
            for name, time, ints, floats, s, v in steps:
                f4 = floats + [0.0] * (4 - len(floats))
                v3 = list(v) if v else [0.0, 0.0, 0.0]
                op = -1 if name == 'BuildAwakeFromMotionTable' else ints.pop(0) if name == 'AddCommand' else ops[name]
                i4 = ints + [0] * (6 - len(ints))
                out.append('    {%d, %s, {%s}, {%s}, %s, {%s}},  // %s' % (op, fl(time), ', '.join(str(x) for x in i4),
                           ', '.join(fl(x) for x in f4), cstr(s), ', '.join(fl(x) for x in v3), name[3:] if name.startswith('Add') else name))
            out.append('};')
        out.append('static const SpecialScene k%sScenes[%d] = {' % (fn, len(scenes)))
        for k, steps in enumerate(scenes):
            out.append('    {%d, %s},' % (len(steps), 'k%s%d' % (fn, k) if steps else 'nullptr'))
        out.append('};')
    rows = scene_map(src)
    out.append('// SetActionTable: a command\'s scene - {command, whether its +0x48 is set (the party\'s), an')
    out.append('// advent\'s (BuildAdvent; else BuildAwake), the scene}')
    out.append('static const struct { short command; bool own, advent; short scene; } kSpecialOf[%d] = {' % len(rows))
    for k in range(0, len(rows), 6):
        out.append('    ' + ' '.join('{%d, %s, %s, %d},' % (c, 'true' if o else 'false', 'true' if a else 'false', n) for c, o, a, n in rows[k:k + 6]))
    out.append('};')
    print('\n'.join(out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
