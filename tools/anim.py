#!/usr/bin/env python3
"""Edge Animation animations (.anim, 'EA04'; _edgeAnimEvaluate and edgeAnimProcessCommandList in
the Android library). Every animation in the game uses the fixed formats (no packing specs at +0x4c):

  header  +0x04 f duration   +0x08 f sample rate   +0x0c u16 header size   +0x0e u16 joints
          +0x10 u16 frames   +0x12 u16 frame sets   +0x16..+0x24 u16 channel counts: constant
          R T S user, animated R T S user   +0x34 / +0x38 self-relative: frame sets' DMA array
          (u16, u16 size, u32 offset) and info (u16 start frame, u16 count)
          +0x3c / +0x40 / +0x44 / +0x48 self-relative: constant R / T / S / user data
  +0x60   the channels' joint indices (u16), in the counts' order: constant R (padded to 8 entries),
          then each further table padded to 4
  R: 6 bytes, the smallest three of a unit quaternion - three 15-bit values (v - 16383.5) /
     23169.768, the dropped one's index in the low 2 bits; T / S: 3 little-endian floats
  frame set (at its DMA offset): u16 sizes of initial R, T, S, user and key R, T, S, user; the
     initial values (local frame 0), a keyframe bit per animated channel per frame (frames 1..count,
     MSB first, channels R, T, S, user), the keys (each channel's in turn; key T / S data aligned to 4,
     user to 4 more), then - aligned to 16 - the next set's header and initial values (local frame
     count + 1). Between keys: t = (i - P + f) / (Q - P); rotations slerp on the short arc (lerp
     from a dot of 0.999), the rest lerp.

  anim.py info <archive> <name>                 the header and channels
  anim.py check <archive> [pattern]             every animation: frame sets as their sizes say,
                                                unit rotations, continuity across frame set boundaries
"""
import fnmatch
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mvgl  # noqa: E402


def rel(d, at):
    o = struct.unpack_from('<i', d, at)[0]
    return at + o if o else None


def quat48(b, at):
    b0, b1, b2, b3, b4, b5 = b[at:at + 6]
    u = b5 | b2 << 24 | b3 << 16
    v0 = (((b0 << 8) | b1) & 0x7fff)
    v1 = u >> 17
    v2 = ((b4 << 8) | (u & 0x1ffff)) >> 2
    c = [(v - 16383.5) / 23169.768 for v in (v0, v1, v2)]
    w = math.sqrt(max(0.0, 1.0 - c[0] * c[0] - c[1] * c[1] - c[2] * c[2]))
    k = b5 & 3
    if k == 0:
        return (w, c[0], c[1], c[2])
    if k == 1:
        return (c[0], w, c[1], c[2])
    if k == 2:
        return (c[0], c[1], w, c[2])
    return (c[0], c[1], c[2], w)


def vec3(b, at):
    return struct.unpack_from('<3f', b, at)


class Anim:
    def __init__(self, d):
        if d[:4] != b'40AE':
            raise ValueError('not an EA04 animation')
        if struct.unpack_from('<i', d, 0x4c)[0]:
            raise ValueError('packed animation (packing specs): not used by the game, not read')
        self.d = d
        self.duration, self.rate = struct.unpack_from('<ff', d, 4)
        self.hsize, self.joints, self.frames, self.nsets = struct.unpack_from('<4H', d, 0xc)
        self.cR, self.cT, self.cS, self.cU, self.aR, self.aT, self.aS, self.aU = struct.unpack_from('<8H', d, 0x16)
        at = 0x60
        self.tables = []
        for k, n in enumerate((self.cR, self.cT, self.cS, self.cU, self.aR, self.aT, self.aS, self.aU)):
            self.tables.append(list(struct.unpack_from(f'<{n}H', d, at)))
            at += ((n + 7) & ~7 if k == 0 else (n + 3) & ~3) * 2
        self.constR = rel(d, 0x3c)
        self.constT = rel(d, 0x40)
        self.constS = rel(d, 0x44)
        dma, info = rel(d, 0x34), rel(d, 0x38)
        self.sets = []
        for k in range(self.nsets):
            _, size, off = struct.unpack_from('<HHI', d, dma + 8 * k)
            start, count = struct.unpack_from('<HH', d, info + 4 * k)
            self.sets.append((start, count, off, size))

    def set_layout(self, k):
        """the frame set's parts (offsets in the file): initial R / T / S, bits, keys R / T / S, next"""
        start, count, off, size = self.sets[k]
        d = self.d
        sz = struct.unpack_from('<8H', d, off)
        p = off + 16
        initR = p
        initT = initR + sz[0]
        initS = initT + sz[1]
        initU = initS + sz[2]
        bits = initU + sz[3]
        keyR = bits + ((self.aR + self.aT + self.aS + self.aU) * count + 7 >> 3)
        keyT = keyR + sz[4]
        keyS = keyT + sz[5]
        keyU = (keyS + sz[6] + 3) & ~3
        nxt = (keyU + sz[7] + 15) & ~15
        return dict(start=start, count=count, sizes=sz, initR=initR, initT=initT, initS=initS, bits=bits,
                    keyR=keyR, keyT=keyT, keyS=keyS, next=nxt)

    def evaluate(self, frame, base):
        """the pose at `frame` (a float, in frames): per joint (rotation, translation, scale), from
        the skeleton's base pose `base` (the same list form)"""
        d = self.d
        pose = [list(map(list, j)) for j in base]
        for i, j in enumerate(self.tables[0]):
            pose[j][0] = list(quat48(d, self.constR + 6 * i))
        for i, j in enumerate(self.tables[1]):
            pose[j][1] = list(vec3(d, self.constT + 12 * i))
        for i, j in enumerate(self.tables[2]):
            pose[j][2] = list(vec3(d, self.constS + 12 * i))
        if not self.sets or not (self.aR or self.aT or self.aS):
            return pose
        frame = max(0.0, min(frame, float(self.frames - 1)))
        k = 0
        while k + 1 < len(self.sets) and self.sets[k + 1][0] <= frame:
            k += 1
        L = self.set_layout(k)
        n = L['count']
        local = frame - L['start']
        i = int(local)
        f = local - i
        if i > n:
            i, f = n, 1.0
        nxt = None
        if k + 1 < len(self.sets):
            nL = self.set_layout(k + 1)
            nxt = (nL['initR'], nL['initT'], nL['initS'])
        bitsAt = L['bits']

        def keys(ch):  # this channel's key frames (local, 1..n) in the set
            out = []
            for b in range(n):
                bit = ch * n + b
                if d[bitsAt + (bit >> 3)] & (0x80 >> (bit & 7)):
                    out.append(b + 1)
            return out

        def bracket(ch, initAt, keyAt, size, nextAt):
            ks = keys(ch)
            before = [x for x in ks if x <= i]
            after = [x for x in ks if x > i]
            P = before[-1] if before else 0
            Q = after[0] if after else n + 1
            a = keyAt + size * (len(before) - 1) if before else initAt
            if after:
                b = keyAt + size * len(before)
            else:
                b = nextAt if nextAt is not None else a
            t = (i - P + f) / (Q - P)
            return a, b, t, len(ks)

        ch = 0
        kR, kT, kS = L['keyR'], L['keyT'], L['keyS']
        for c, j in enumerate(self.tables[4]):
            a, b, t, nk = bracket(ch, L['initR'] + 6 * c, kR, 6, nxt[0] + 6 * c if nxt else None)
            q0, q1 = quat48(d, a), quat48(d, b)
            dot = sum(x * y for x, y in zip(q0, q1))
            if dot < 0:
                q1, dot = tuple(-x for x in q1), -dot
            if dot >= 0.999:
                w0, w1 = 1 - t, t
            else:
                th = math.acos(min(1.0, dot))
                w0, w1 = math.sin((1 - t) * th) / math.sin(th), math.sin(t * th) / math.sin(th)
            pose[j][0] = [w0 * x + w1 * y for x, y in zip(q0, q1)]
            kR += 6 * nk
            ch += 1
        for c, j in enumerate(self.tables[5]):
            a, b, t, nk = bracket(ch, L['initT'] + 12 * c, kT, 12, nxt[1] + 12 * c if nxt else None)
            v0, v1 = vec3(d, a), vec3(d, b)
            pose[j][1] = [x + t * (y - x) for x, y in zip(v0, v1)]
            kT += 12 * nk
            ch += 1
        for c, j in enumerate(self.tables[6]):
            a, b, t, nk = bracket(ch, L['initS'] + 12 * c, kS, 12, nxt[2] + 12 * c if nxt else None)
            v0, v1 = vec3(d, a), vec3(d, b)
            pose[j][2] = [x + t * (y - x) for x, y in zip(v0, v1)]
            kS += 12 * nk
            ch += 1
        return pose

    def check_layout(self):
        """each set's computed parts end where its own sizes and the next set say"""
        problems = []
        for k in range(len(self.sets)):
            L = self.set_layout(k)
            n = L['count']
            nkR = nkT = nkS = 0
            bitsAt = L['bits']
            for ch in range(self.aR + self.aT + self.aS):
                cnt = sum(1 for b in range(n) if self.d[bitsAt + ((ch * n + b) >> 3)] & (0x80 >> ((ch * n + b) & 7)))
                if ch < self.aR:
                    nkR += cnt
                elif ch < self.aR + self.aT:
                    nkT += cnt
                else:
                    nkS += cnt
            sz = L['sizes']
            want = (6 * self.aR, 12 * self.aT, 12 * self.aS, 6 * nkR, 12 * nkT, 12 * nkS)
            have = (sz[0], sz[1], sz[2], sz[4], sz[5], sz[6])
            # (the stored sizes include padding to 4 bytes: what follows is aligned)
            if any(not (w <= h < w + 4) for w, h in zip(want, have)):
                problems.append(f'set {k}: sizes {have}, computed {want}')
            if k + 1 < len(self.sets) and L['next'] != self.sets[k + 1][2] + 0 and L['next'] - self.sets[k][2] != 0:
                # the next set's data starts at its own DMA offset
                if L['next'] != self.sets[k + 1][2]:
                    problems.append(f'set {k}: next set at {L["next"]:#x}, DMA says {self.sets[k + 1][2]:#x}')
        return problems


def base_pose(skel):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import skel as sk
    joints, pose, links = sk.parse(skel)
    return [(list(q), list(t), list(s)) for q, t, s in pose], links


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    a = mvgl.Mvgl(argv[2])
    files = {n: f for n, *f in a.files()}
    if argv[1] == 'info':
        an = Anim(a.decoded(*files[argv[3] if argv[3].endswith('.anim') else argv[3] + '.anim']))
        print(f'{an.duration:.3f} s at {an.rate:g} Hz, {an.frames} frames in {an.nsets} sets, {an.joints} joints; '
              f'constant R {an.cR} T {an.cT} S {an.cS} U {an.cU}, animated R {an.aR} T {an.aT} S {an.aS} U {an.aU}')
        for k, s in enumerate(an.sets):
            print(f'  set {k}: frames {s[0]}..{s[0] + s[1]}, at {s[2]:#x} ({s[3]} bytes)')
        return 0
    if argv[1] == 'check':
        pat = argv[3] if len(argv) > 3 else '*.anim'
        n = layout_bad = unit_bad = boundaries = 0
        worst = (0.0, '')
        for name in sorted(files):
            if not name.endswith('.anim') or not fnmatch.fnmatch(name, pat):
                continue
            an = Anim(a.decoded(*files[name]))
            n += 1
            probs = an.check_layout()
            if probs:
                layout_bad += 1
                if layout_bad <= 5:
                    print(name, probs[:2])
            # the figure's skeleton: the name up to the motion (c217e080 -> c217)
            base = None
            for cut in range(len(name) - 5, 0, -1):
                if name[:cut] + '.skel' in files:
                    base, _ = base_pose(a.decoded(*files[name[:cut] + '.skel']))
                    break
            if base is None or len(base) != an.joints:
                base = [([0, 0, 0, 1], [0, 0, 0], [1, 1, 1]) for _ in range(an.joints)]
            # (only the joints this animation drives: some UI skeletons' base poses aren't unit quaternions)
            driven = set(an.tables[0] + an.tables[4])
            for fr in range(0, an.frames, max(1, an.frames // 16)):
                for j, (q, _, _) in enumerate(an.evaluate(float(fr), base)):
                    if j in driven and abs(math.sqrt(sum(x * x for x in q)) - 1) > 1e-3:
                        unit_bad += 1
                        break
            # at each frame set's start, the previous set (reaching the next set's initial values,
            # which it carries) and the new one must agree
            for k in range(1, len(an.sets)):
                b = an.sets[k][0]
                boundaries += 1
                for j, ((q0, t0, s0), (q1, t1, s1)) in enumerate(zip(an.evaluate(b - 1e-9, base), an.evaluate(float(b), base))):
                    dq = 1 - abs(sum(x * y for x, y in zip(q0, q1))) if j in driven else 0.0
                    e = max(dq,
                            max(abs(x - y) / max(1.0, abs(y)) for x, y in zip(t0 + s0, t1 + s1)))
                    if e > worst[0]:
                        worst = (e, f'{name} frame {b}')
        print(f'{n} animations: {n - layout_bad} with every frame set as its sizes say; rotations off unit '
              f'length in {unit_bad} sampled frames; {boundaries} frame set boundaries, the largest relative '
              f'difference across one {worst[0]:.2g} ({worst[1]})')
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
