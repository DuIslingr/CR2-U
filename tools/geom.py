#!/usr/bin/env python3
"""MVGL figure meshes (.geom; MVGL::Draw::Figure::Build / BuildMesh / DrawMesh, docs/FORMATS.md).

  header (version 0x3c on the iPad, 0x50 on Android / Vita: the newer header is 0x10 longer, the
  records are the same)
    +0x00 u32 version     +0x04 u16 meshes   +0x06 u16 materials   +0x10 u16 relocations
    +0x30 u32 meshes' offset   +0x34 materials'   +0x48 the relocation list (Android / Vita)
  mesh (0x54 bytes)
    +0x02 u16 attributes   +0x04 u16 vertex stride   +0x06 u16 index type (0x1403: u16)
    +0x0a u16 primitive (5: triangle strip)   +0x10 u32 material   +0x14 u32 vertices
    +0x18 u32 indices   +0x24 vertex data   +0x28 index data   +0x2c the mesh's node (u32 count +
    list)   +0x34 attribute descriptors
  attribute (8 bytes): u8 semantic, u8 normalised, u16 components, u16 GL type, u16 offset -
    semantics 1 position, 2 normal, 3 tangent, 4 binormal, 5 / 6 texcoord 0 / 1 (shorts / 1024),
    9 colour, 10 blend indices, 11 blend weights
  material: u32 hash, ..., +0x14 u8 parameters, +0x15 u8 render states, then 0x14-byte entries:
    u8 parameter, u8 kind (0 texture, 1 float, 3 vec3, 4 vec4, 100 render state), then a 12-byte
    texture name at +4 or the values (0x32 diffuse map, 0x35 normal map, 0x33 colour)

  geom.py info <archive> <name>            the meshes and materials
  geom.py check <archive>                  every .geom parses, indices within their vertices
  geom.py render <archive> <name> <out.png> [yaw]   a flat-shaded preview (PIL)
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mvgl  # noqa: E402

GL = {0x1400: ('b', 1), 0x1401: ('B', 1), 0x1402: ('h', 2), 0x1403: ('H', 2), 0x1406: ('f', 4)}


class Mesh:
    pass


def parse(d):
    version = struct.unpack_from('<I', d, 0)[0]
    if version not in (0x3c, 0x50):
        raise ValueError(f'geom version {version:#x}')
    nmesh, nmat = struct.unpack_from('<HH', d, 4)
    mo, mato = struct.unpack_from('<II', d, 0x30)
    meshes = []
    for i in range(nmesh):
        r = mo + 0x54 * i
        m = Mesh()
        m.nattr, m.stride, m.itype, _, m.prim = struct.unpack_from('<HHHHH', d, r + 2)
        m.material, m.nvert, m.nidx = struct.unpack_from('<III', d, r + 0x10)
        vo, io, no = struct.unpack_from('<III', d, r + 0x24)
        ao = struct.unpack_from('<I', d, r + 0x34)[0]
        m.node = struct.unpack_from('<I', d, no)[0] if no else None
        m.attrs = {}
        for k in range(m.nattr):
            sem, norm, cnt, typ, ofs = struct.unpack_from('<BBHHH', d, ao + 8 * k)
            m.attrs[sem] = (norm, cnt, typ, ofs)
        m.indices = list(struct.unpack_from(f'<{m.nidx}H', d, io))

        def attr(sem):
            if sem not in m.attrs:
                return None
            norm, cnt, typ, ofs = m.attrs[sem]
            f, size = GL[typ]
            out = []
            for v in range(m.nvert):
                vals = struct.unpack_from(f'<{cnt}{f}', d, vo + v * m.stride + ofs)
                if norm and f in 'hH':
                    vals = tuple(x / 32767.0 for x in vals)
                elif norm and f in 'bB':
                    vals = tuple(x / 255.0 for x in vals)
                out.append(vals)
            return out
        m.positions = attr(1)
        m.normals = attr(2)
        uv = attr(5)
        m.uv = [(u / 1024.0, v / 1024.0) for u, v in uv] if uv else None
        meshes.append(m)
    materials = []
    p = mato
    for i in range(nmat):
        npar, nstate = d[p + 0x14], d[p + 0x15]
        mat = {'hash': struct.unpack_from('<I', d, p)[0], 'textures': {}, 'params': {}}
        for k in range(npar + nstate):
            e = d[p + 0x18 + 0x14 * k: p + 0x18 + 0x14 * (k + 1)]
            pid, kind = e[0], e[1]
            if kind == 0:
                mat['textures'][pid] = e[4:16].split(b'\0')[0].decode('ascii', 'replace')
            elif kind in (1, 3, 4):
                mat['params'][pid] = struct.unpack_from(f'<{kind}f', e, 4)
        materials.append(mat)
        p += 0x18 + (npar + nstate) * 0x14
    return version, meshes, materials


def triangles(m):
    """the mesh's triangles (strips unrolled, degenerate ones dropped)"""
    idx = m.indices
    if m.prim == 4:
        return [tuple(idx[i:i + 3]) for i in range(0, len(idx) - 2, 3)]
    out = []
    for i in range(len(idx) - 2):
        a, b, c = idx[i], idx[i + 1], idx[i + 2]
        if a == b or b == c or a == c:
            continue
        out.append((a, b, c) if i % 2 == 0 else (b, a, c))
    return out


def render(meshes, out, yaw=30.0, size=512):
    from PIL import Image
    pts = [p for m in meshes for p in m.positions]
    lo = [min(p[k] for p in pts) for k in range(3)]
    hi = [max(p[k] for p in pts) for k in range(3)]
    c = [(lo[k] + hi[k]) / 2 for k in range(3)]
    r = max(hi[k] - lo[k] for k in range(3)) / 2 or 1
    ya, pa = math.radians(yaw), math.radians(20)
    img = Image.new('RGB', (size, size), (40, 40, 48))
    z = [[-1e9] * size for _ in range(size)]
    px = img.load()

    def xf(p):
        x, y, zz = p[0] - c[0], p[1] - c[1], p[2] - c[2]
        x, zz = x * math.cos(ya) + zz * math.sin(ya), -x * math.sin(ya) + zz * math.cos(ya)
        y, zz = y * math.cos(pa) - zz * math.sin(pa), y * math.sin(pa) + zz * math.cos(pa)
        s = size * 0.45 / r
        return (size / 2 + x * s, size / 2 - y * s, zz)
    light = (0.4, 0.8, 0.45)
    for m in meshes:
        P = [xf(p) for p in m.positions]
        for a, b, cc in triangles(m):
            A, B, C = P[a], P[b], P[cc]
            ux, uy, uz = B[0] - A[0], B[1] - A[1], B[2] - A[2]
            vx, vy, vz = C[0] - A[0], C[1] - A[1], C[2] - A[2]
            nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
            ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1
            shade = abs(nx * light[0] - ny * light[1] + nz * light[2]) / ln
            col = tuple(int(60 + 180 * shade) for _ in range(3))
            x0, x1 = int(max(0, min(A[0], B[0], C[0]))), int(min(size - 1, max(A[0], B[0], C[0])))
            y0, y1 = int(max(0, min(A[1], B[1], C[1]))), int(min(size - 1, max(A[1], B[1], C[1])))
            den = (B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1])
            if abs(den) < 1e-9:
                continue
            for y in range(y0, y1 + 1):
                for x in range(x0, x1 + 1):
                    w0 = ((B[1] - C[1]) * (x - C[0]) + (C[0] - B[0]) * (y - C[1])) / den
                    w1 = ((C[1] - A[1]) * (x - C[0]) + (A[0] - C[0]) * (y - C[1])) / den
                    w2 = 1 - w0 - w1
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    zz = w0 * A[2] + w1 * B[2] + w2 * C[2]
                    if zz > z[y][x]:
                        z[y][x] = zz
                        px[x, y] = col
    img.save(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    a = mvgl.Mvgl(argv[2])
    files = {n: (o, s, st) for n, o, s, st in a.files()}
    if argv[1] == 'check':
        ok = bad = 0
        for n, f in files.items():
            if not n.endswith('.geom'):
                continue
            try:
                _, meshes, _ = parse(a.decoded(*f))
                good = all(max(m.indices, default=0) < m.nvert for m in meshes)
            except (ValueError, struct.error, KeyError) as e:
                good = False
                print(n, e)
            ok += good
            bad += not good
        print(f'{ok} parse, {bad} bad')
        return 1 if bad else 0
    name = argv[3] if argv[3].endswith('.geom') else argv[3] + '.geom'
    version, meshes, materials = parse(a.decoded(*files[name]))
    if argv[1] == 'info':
        print(f'{name}: version {version:#x}, {len(meshes)} meshes, {len(materials)} materials')
        for i, m in enumerate(meshes):
            print(f'  mesh {i}: material {m.material}, node {m.node}, {m.nvert} vertices, {m.nidx} indices, '
                  f'stride {m.stride}, attributes {sorted(m.attrs)}')
        for i, mat in enumerate(materials):
            print(f'  material {i}: textures {mat["textures"]}')
    elif argv[1] == 'render':
        render(meshes, argv[4], float(argv[5]) if len(argv) > 5 else 30.0)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
