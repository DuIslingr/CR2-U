#!/usr/bin/env python3
"""Edge Animation skeletons (.skel, 'ES02'; MVGL's port of Sony's Edge Animation: edgeAnim*) and
the figure's inverse bind matrices (geom header +0x40: 3x4 row-major on Android / the Vita, 4x4
column-major on the iPad), from Figure::Pose and
edgeAnimLocalJointsToWorldMatrices3x4.

  skeleton  +0x10 u16 joints   +0x12 u16 user channels   +0x14 u16 linkage groups
            +0x18 self-relative offset: the base pose, a JointTransform per joint (0x30 bytes:
                  rotation quaternion x y z w, translation x y z + pad, scale x y z + pad)
            +0x20 self-relative offset: joint name hashes (u32 each)
            +0x40 the hierarchy: groups of 4 (joint, parent) u16 pairs; parent & 0x7fff >= 0x4000:
                  a root (the figure's root transform); bit 0x8000 clear: divide by the parent's
                  scale (scale compensation)
  world = parent world * local; skinning matrix = world * inverse bind (3x4, row-major, the
  translation in the 4th column: what MatrixPalette's rows hold)

  skel.py check <archive> [name]   for every figure, the joints its meshes skin to: how many have
                                   base-pose world * inverse bind = identity (the base pose is the
                                   bind pose but where a figure's is posed otherwise)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mvgl  # noqa: E402


def rel(d, at):
    o = struct.unpack_from('<i', d, at)[0]
    return at + o if o else None


def parse(d):
    if d[:4] != b'20SE':
        raise ValueError('not an ES02 skeleton')
    joints, users, groups = struct.unpack_from('<HHH', d, 0x10)
    bp = rel(d, 0x18)
    pose = []
    for j in range(joints):
        q = struct.unpack_from('<4f', d, bp + 0x30 * j)
        t = struct.unpack_from('<3f', d, bp + 0x30 * j + 0x10)
        s = struct.unpack_from('<3f', d, bp + 0x30 * j + 0x20)
        pose.append((q, t, s))
    links = []
    for k in range(groups * 4):
        j, p = struct.unpack_from('<HH', d, 0x40 + 4 * k)
        links.append((j, p))
    return joints, pose, links


def mat_trs(q, t, s):
    x, y, z, w = q
    m = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[m[r][0] * s[0], m[r][1] * s[1], m[r][2] * s[2], t[r]] for r in range(3)]


def mul(a, b):  # 3x4 * 3x4 (as 4x4 with a last row 0 0 0 1)
    return [[sum(a[r][k] * b[k][c] for k in range(3)) + (a[r][3] if c == 3 else 0) for c in range(4)] for r in range(3)]


def world(joints, pose, links):
    W = [None] * joints
    for j, p in links:
        if j >= joints or W[j] is not None and p == j:
            continue
        q, t, s = pose[j]
        parent = p & 0x7fff
        if parent >= 0x4000:
            W[j] = mat_trs(q, t, s)
            continue
        ps = pose[parent][2]
        local = mat_trs(q, t, s)
        if not (p & 0x8000):  # scale compensation: the parent's scale undone for the child
            local = [[local[r][c] for c in range(3)] + [local[r][3]] for r in range(3)]
            inv = [1 / x if x else 0.0 for x in ps]  # (zero-scaled, hidden joints: the game gets infinities)
            local = [[local[r][c] * inv[r] for c in range(3)] + [local[r][3]] for r in range(3)]
        W[j] = mul(W[parent], local)
    return W


def inverse_bind(g, at, j):
    """joint j's inverse bind matrix as 3 rows of 4: Android / the Vita (geom version 0x50) keep 3x4
    row-major matrices (0x30 bytes), the iPad (0x3c) 4x4 column-major ones (0x40 bytes)"""
    if struct.unpack_from('<I', g, 0)[0] == 0x50:
        return [list(struct.unpack_from('<4f', g, at + 0x30 * j + 16 * r)) for r in range(3)]
    m = struct.unpack_from('<16f', g, at + 0x40 * j)
    return [[m[c * 4 + r] for c in range(4)] for r in range(3)]


def main(argv):
    if len(argv) < 3 or argv[1] != 'check':
        print(__doc__)
        return 1
    a = mvgl.Mvgl(argv[2])
    files = {n: f for n, *f in a.files()}
    names = [argv[3]] if len(argv) > 3 else sorted(n[:-5] for n in files if n.endswith('.skel'))
    figures = joints_used = joints_bind = 0
    off = {}
    for n in names:
        if n + '.geom' not in files:
            continue
        sk = a.decoded(*files[n + '.skel'])
        g = a.decoded(*files[n + '.geom'])
        joints, pose, links = parse(sk)
        W = world(joints, pose, links)
        ib = struct.unpack_from('<I', g, 0x40)[0]
        if not ib:
            continue
        # only the joints some mesh's palette uses (mesh +0 u16 count, +0x2c u32 joint list): helper
        # joints' inverse binds need not match
        used = set()
        mo = struct.unpack_from('<I', g, 0x30)[0]
        for i in range(struct.unpack_from('<H', g, 4)[0]):
            r = mo + 0x54 * i
            cnt, po = struct.unpack_from('<H', g, r)[0], struct.unpack_from('<I', g, r + 0x2c)[0]
            if po:
                used |= set(struct.unpack_from(f'<{cnt}I', g, po))
        figures += 1
        for j in sorted(x for x in used if x < joints):
            inv = inverse_bind(g, ib, j)
            m = mul(W[j], inv)
            err = max(abs(m[r][c] - (1.0 if r == c else 0.0)) for r in range(3) for c in range(4))
            joints_used += 1
            if err <= 1e-3:
                joints_bind += 1
            else:
                off[n] = off.get(n, 0) + 1
    print(f'{figures} figures, {joints_used} skinned joints: {joints_bind} with base pose * inverse bind = identity '
          f'({100.0 * joints_bind / max(joints_used, 1):.1f}%); the others in {len(off)} figures, e.g. '
          + ', '.join(f'{k} ({v})' for k, v in sorted(off.items(), key=lambda kv: -kv[1])[:8]))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
