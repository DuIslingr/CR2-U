#include "assets/gltf_figure.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

#include "cgltf/cgltf.h"
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace cr2 {

namespace {

bool fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return false;
}

// ---- 4x4 column-major (glTF's) matrices ----

struct M4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    M4 operator*(const M4& b) const {
        M4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
    void point(const float* p, float* o) const {
        for (int r = 0; r < 3; ++r) o[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
    }
    void vector(const float* p, float* o) const {
        for (int r = 0; r < 3; ++r) o[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2];
    }
    float det3() const {
        return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
    }
    bool invertible() const { return std::fabs(det3()) > 1e-12f; }
    M4 inverse() const { // (affine)
        const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
        const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
        const float det = a * A + b * B + c * C;
        M4 r;
        if (std::fabs(det) < 1e-12f) return r;
        const float inv = 1.0f / det;
        r.m[0] = A * inv, r.m[4] = -(b * i - c * h) * inv, r.m[8] = (b * f - c * e) * inv;
        r.m[1] = B * inv, r.m[5] = (a * i - c * g) * inv, r.m[9] = -(a * f - c * d) * inv;
        r.m[2] = C * inv, r.m[6] = -(a * h - b * g) * inv, r.m[10] = (a * e - b * d) * inv;
        const float tx = m[12], ty = m[13], tz = m[14];
        r.m[12] = -(r.m[0] * tx + r.m[4] * ty + r.m[8] * tz);
        r.m[13] = -(r.m[1] * tx + r.m[5] * ty + r.m[9] * tz);
        r.m[14] = -(r.m[2] * tx + r.m[6] * ty + r.m[10] * tz);
        return r;
    }
};

M4 fromMat34(const Mat34& a) { // (rows -> columns)
    M4 r;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col) r.m[col * 4 + row] = a.m[row * 4 + col];
    return r;
}

M4 fromArray(const float* m) {
    M4 r;
    std::memcpy(r.m, m, sizeof r.m);
    return r;
}

// An affine matrix as glTF's TRS (a shear, should there be one, left out)
struct Trs {
    float t[3], r[4], s[3];
};
Trs trsOf(const M4& a) {
    Trs o;
    float c[3][3];
    for (int k = 0; k < 3; ++k) {
        o.t[k] = a.m[12 + k];
        const float l = std::sqrt(a.m[k * 4] * a.m[k * 4] + a.m[k * 4 + 1] * a.m[k * 4 + 1] + a.m[k * 4 + 2] * a.m[k * 4 + 2]);
        o.s[k] = l;
        for (int r = 0; r < 3; ++r) c[k][r] = l > 1e-12f ? a.m[k * 4 + r] / l : (r == k ? 1.0f : 0.0f);
    }
    const float det = c[0][0] * (c[1][1] * c[2][2] - c[2][1] * c[1][2]) - c[1][0] * (c[0][1] * c[2][2] - c[2][1] * c[0][2]) +
                      c[2][0] * (c[0][1] * c[1][2] - c[1][1] * c[0][2]);
    if (det < 0) {
        for (int r = 0; r < 3; ++r) c[0][r] = -c[0][r];
        o.s[0] = -o.s[0];
    }
    const float tr = c[0][0] + c[1][1] + c[2][2];
    float* q = o.r;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1) * 2;
        q[3] = 0.25f * s, q[0] = (c[1][2] - c[2][1]) / s, q[1] = (c[2][0] - c[0][2]) / s, q[2] = (c[0][1] - c[1][0]) / s;
    } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
        const float s = std::sqrt(1 + c[0][0] - c[1][1] - c[2][2]) * 2;
        q[3] = (c[1][2] - c[2][1]) / s, q[0] = 0.25f * s, q[1] = (c[1][0] + c[0][1]) / s, q[2] = (c[2][0] + c[0][2]) / s;
    } else if (c[1][1] > c[2][2]) {
        const float s = std::sqrt(1 + c[1][1] - c[0][0] - c[2][2]) * 2;
        q[3] = (c[2][0] - c[0][2]) / s, q[0] = (c[1][0] + c[0][1]) / s, q[1] = 0.25f * s, q[2] = (c[2][1] + c[1][2]) / s;
    } else {
        const float s = std::sqrt(1 + c[2][2] - c[0][0] - c[1][1]) * 2;
        q[3] = (c[0][1] - c[1][0]) / s, q[0] = (c[2][0] + c[0][2]) / s, q[1] = (c[2][1] + c[1][2]) / s, q[2] = 0.25f * s;
    }
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int k = 0; k < 4; ++k) q[k] = l > 0 ? q[k] / l : (k == 3 ? 1.0f : 0.0f);
    return o;
}

void normalise(float* v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-12f) v[0] /= l, v[1] /= l, v[2] /= l;
}

void cross(const float* a, const float* b, float* o) {
    o[0] = a[1] * b[2] - a[2] * b[1], o[1] = a[2] * b[0] - a[0] * b[2], o[2] = a[0] * b[1] - a[1] * b[0];
}

// ---- Writing ----

std::string num(float v) {
    if (!std::isfinite(v)) v = 0;
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", double(v));
    return b;
}
std::string nums(const float* v, int n) {
    std::string s = "[";
    for (int i = 0; i < n; ++i) s += (i ? "," : "") + num(v[i]);
    return s + "]";
}
std::string quoted(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
    return o + "\"";
}
std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
    return s;
}
// a name that stays one through editors (letters, digits, '_' and '-')
std::string tidy(const std::string& s) {
    std::string o;
    for (char c : s) o += std::isalnum(static_cast<unsigned char>(c)) || c == '-' ? c : '_';
    return o;
}

struct Writer {
    std::vector<uint8_t> bin;
    std::vector<std::string> views, accessors;
    int view(const void* data, size_t size, int target) {
        while (bin.size() % 4) bin.push_back(0);
        const size_t at = bin.size();
        bin.insert(bin.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        std::string v = "{\"buffer\":0,\"byteOffset\":" + std::to_string(at) + ",\"byteLength\":" + std::to_string(size);
        if (target) v += ",\"target\":" + std::to_string(target);
        views.push_back(v + "}");
        return int(views.size()) - 1;
    }
    int accessor(const void* data, size_t count, int comps, int componentType, const char* type, int target,
                 const float* mn = nullptr, const float* mx = nullptr) {
        const size_t size = componentType == 5126 || componentType == 5125 ? 4 : componentType == 5123 ? 2 : 1;
        const int v = view(data, count * size_t(comps) * size, target);
        std::string a = "{\"bufferView\":" + std::to_string(v) + ",\"componentType\":" + std::to_string(componentType) +
                        ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"";
        if (mn && mx) a += ",\"min\":" + nums(mn, comps) + ",\"max\":" + nums(mx, comps);
        accessors.push_back(a + "}");
        return int(accessors.size()) - 1;
    }
};

void pngOut(void* ctx, void* data, int size) {
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}

// the joints' parents (Skeleton::links' 0x7fff, -1 a root)
std::vector<int> parentsOf(const Skeleton& sk) {
    std::vector<int> p(sk.basePose.size(), -1);
    for (const auto& [joint, link] : sk.links) {
        const unsigned parent = link & 0x7fff;
        if (joint < p.size() && parent < 0x4000 && parent < p.size() && parent != joint) p[joint] = int(parent);
    }
    return p;
}

// the joints' locals in glTF's terms (parent's world undone: Skeleton::world's scale compensation folded in)
std::vector<Trs> localsOf(const std::vector<Mat34>& world, const std::vector<int>& parent) {
    std::vector<Trs> out(world.size());
    for (size_t j = 0; j < world.size(); ++j) {
        const M4 w = fromMat34(world[j]);
        out[j] = trsOf(parent[j] >= 0 ? fromMat34(world[size_t(parent[j])]).inverse() * w : w);
    }
    return out;
}

// A TRS as a matrix (glTF's: T * R * S)
M4 matrixOf(const Trs& t) {
    const float x = t.r[0], y = t.r[1], z = t.r[2], w = t.r[3];
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                           {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                           {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    M4 l;
    for (int c = 0; c < 3; ++c)
        for (int row = 0; row < 3; ++row) l.m[c * 4 + row] = r[row][c] * t.s[c];
    l.m[12] = t.t[0], l.m[13] = t.t[1], l.m[14] = t.t[2];
    return l;
}

// ---- Reading ----

int jointIndex(const cgltf_node* n, size_t joints) {
    int i = -1, used = 0;
    if (n && n->name && std::sscanf(n->name, "node_%d%n", &i, &used) == 1 && (n->name[used] == 0 || n->name[used] == '_') && i >= 0 &&
        size_t(i) < joints)
        return i;
    return -1;
}
int jointOf(const cgltf_node* n, size_t joints) {
    for (; n; n = n->parent)
        if (const int i = jointIndex(n, joints); i >= 0) return i;
    return -1;
}

int materialOf(const cgltf_material* m) {
    int i = -1;
    if (m && m->name && std::sscanf(m->name, "cr_mat_%d", &i) == 1) return i;
    return -1;
}

bool imageBytes(const cgltf_image* img, const std::string& dir, std::vector<uint8_t>& out) {
    if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
        const uint8_t* p = static_cast<const uint8_t*>(img->buffer_view->buffer->data) + img->buffer_view->offset;
        out.assign(p, p + img->buffer_view->size);
        return true;
    }
    if (!img->uri) return false;
    if (std::strncmp(img->uri, "data:", 5) == 0) {
        const char* comma = std::strchr(img->uri, ',');
        if (!comma) return false;
        size_t len = std::strlen(comma + 1), pad = 0;
        while (len > 0 && comma[len] == '=') --len, ++pad;
        const size_t size = (len + pad) / 4 * 3 - pad;
        void* data = nullptr;
        cgltf_options opt{};
        if (cgltf_load_buffer_base64(&opt, size, comma + 1, &data) != cgltf_result_success) return false;
        out.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        std::free(data);
        return true;
    }
    std::string uri = img->uri;
    cgltf_decode_uri(uri.data());
    uri.resize(std::strlen(uri.c_str()));
    std::FILE* f = std::fopen((dir + uri).c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return !out.empty();
}

} // namespace

bool exportFigureGlb(const FigureExport& in, const std::string& path, std::string* error, GltfStats* stats) {
    if (!in.geom || !in.skeleton) return fail(error, "no figure");
    const Geom& g = *in.geom;
    const Skeleton& sk = *in.skeleton;
    GltfStats st;
    Writer w;
    const size_t n = sk.basePose.size();
    const bool skinned = n > 0 && g.inverseBind.size() == n;
    const std::vector<int> parent = parentsOf(sk);
    const std::vector<Mat34> baseWorld = sk.world(sk.basePose);
    const std::vector<Trs> rest = skinned ? localsOf(baseWorld, parent) : std::vector<Trs>();
    std::vector<std::string> nodes, sceneNodes;
    for (size_t j = 0; j < n && skinned; ++j) {
        const Trs& t = rest[j];
        std::string name = "node_" + std::to_string(j);
        if (j < in.jointNames.size() && !in.jointNames[j].empty()) name += "_" + tidy(in.jointNames[j]);
        std::string s = "{\"name\":" + quoted(name) + ",\"translation\":" + nums(t.t, 3) + ",\"rotation\":" + nums(t.r, 4) + ",\"scale\":" + nums(t.s, 3);
        std::vector<std::string> c;
        for (size_t k = 0; k < n; ++k)
            if (parent[k] == int(j)) c.push_back(std::to_string(k));
        if (!c.empty()) s += ",\"children\":[" + joined(c) + "]";
        nodes.push_back(s + "}");
        if (parent[j] < 0) sceneNodes.push_back(std::to_string(j));
    }

    // the pictures the materials name
    std::vector<std::string> images, textures;
    std::map<std::string, int> textureOf;
    auto texture = [&](const std::string& name) {
        if (name.empty()) return -1;
        if (auto it = textureOf.find(name); it != textureOf.end()) return it->second;
        const Image* img = nullptr;
        for (const auto& [k, v] : in.images)
            if (k == name) img = &v;
        if (!img || img->width <= 0 || img->rgba.size() < size_t(img->width) * img->height * 4) return textureOf[name] = -1;
        std::vector<uint8_t> png;
        stbi_write_png_to_func(pngOut, &png, img->width, img->height, 4, img->rgba.data(), img->width * 4);
        if (png.empty()) return textureOf[name] = -1;
        const int v = w.view(png.data(), png.size(), 0);
        images.push_back("{\"name\":" + quoted("tex_" + name) + ",\"bufferView\":" + std::to_string(v) + ",\"mimeType\":\"image/png\"}");
        textures.push_back("{\"source\":" + std::to_string(images.size() - 1) + ",\"sampler\":0}");
        ++st.images;
        return textureOf[name] = int(textures.size()) - 1;
    };
    std::vector<std::string> materials;
    for (size_t i = 0; i < g.materials.size(); ++i) {
        const GeomMaterial& m = g.materials[i];
        std::string name = "cr_mat_" + std::to_string(i);
        if (i < in.materialNames.size() && !in.materialNames[i].empty()) name += "_" + tidy(in.materialNames[i]);
        float colour[4] = {1, 1, 1, 1};
        m.param(0x33, colour, 4); // (DiffuseColor)
        std::string s = "{\"name\":" + quoted(name) + ",\"pbrMetallicRoughness\":{\"baseColorFactor\":" + nums(colour, 4);
        if (const int t = texture(m.diffuse()); t >= 0) s += ",\"baseColorTexture\":{\"index\":" + std::to_string(t) + "}";
        s += ",\"metallicFactor\":0,\"roughnessFactor\":1}";
        if (const int t = texture(m.texture(0x35)); t >= 0) s += ",\"normalTexture\":{\"index\":" + std::to_string(t) + "}";
        uint32_t v = 0;
        if (m.state(0x7c, 0, v) && v) s += ",\"alphaMode\":\"BLEND\"";
        else if (m.state(0x79, 0, v) && v) {
            uint32_t ref = 0;
            m.state(0x78, 0, ref, 1);
            float f = 0;
            std::memcpy(&f, &ref, 4);
            s += ",\"alphaMode\":\"MASK\",\"alphaCutoff\":" + num(std::clamp(f, 0.0f, 1.0f));
        }
        if (m.state(0x7e, 1, v) && !v) s += ",\"doubleSided\":true";
        materials.push_back(s + "}");
    }

    std::vector<std::string> meshes;
    for (size_t mi = 0; mi < g.meshes.size(); ++mi) {
        const GeomMesh& gm = g.meshes[mi];
        const size_t nv = gm.vertexCount;
        const std::vector<uint32_t> idx = gm.triangles();
        if (!nv || idx.empty() || gm.position.size() < nv * 3) continue;
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t v = 0; v < nv; ++v)
            for (int c = 0; c < 3; ++c) mn[c] = std::min(mn[c], gm.position[v * 3 + size_t(c)]), mx[c] = std::max(mx[c], gm.position[v * 3 + size_t(c)]);
        std::string attrs = "\"POSITION\":" + std::to_string(w.accessor(gm.position.data(), nv, 3, 5126, "VEC3", 34962, mn, mx));
        if (gm.normal.size() >= nv * 3) {
            std::vector<float> nrm(gm.normal.begin(), gm.normal.begin() + long(nv * 3));
            for (size_t v = 0; v < nv; ++v) normalise(&nrm[v * 3]);
            attrs += ",\"NORMAL\":" + std::to_string(w.accessor(nrm.data(), nv, 3, 5126, "VEC3", 34962));
            if (gm.tangent.size() >= nv * 3) {
                std::vector<float> tan(nv * 4);
                for (size_t v = 0; v < nv; ++v) {
                    float t[3] = {gm.tangent[v * 3], gm.tangent[v * 3 + 1], gm.tangent[v * 3 + 2]}, c[3];
                    normalise(t);
                    cross(&nrm[v * 3], t, c);
                    const float* b = gm.binormal.size() >= nv * 3 ? &gm.binormal[v * 3] : nullptr;
                    tan[v * 4] = t[0], tan[v * 4 + 1] = t[1], tan[v * 4 + 2] = t[2];
                    tan[v * 4 + 3] = b && c[0] * b[0] + c[1] * b[1] + c[2] * b[2] < 0 ? -1.0f : 1.0f;
                }
                attrs += ",\"TANGENT\":" + std::to_string(w.accessor(tan.data(), nv, 4, 5126, "VEC4", 34962));
            }
        }
        if (gm.uv.size() >= nv * 2) attrs += ",\"TEXCOORD_0\":" + std::to_string(w.accessor(gm.uv.data(), nv, 2, 5126, "VEC2", 34962));
        if (gm.uv1.size() >= nv * 2) attrs += ",\"TEXCOORD_1\":" + std::to_string(w.accessor(gm.uv1.data(), nv, 2, 5126, "VEC2", 34962));
        if (gm.colour.size() >= nv * 4) attrs += ",\"COLOR_0\":" + std::to_string(w.accessor(gm.colour.data(), nv, 4, 5126, "VEC4", 34962));
        const bool onJoints = skinned && !gm.palette.empty();
        if (onJoints) {
            std::vector<uint16_t> joints(nv * 4, 0);
            std::vector<float> weights(nv * 4, 0);
            const uint32_t first = gm.palette[0] < n ? gm.palette[0] : 0;
            for (size_t v = 0; v < nv; ++v) {
                float sum = 0;
                for (int k = 0; k < 4 && !gm.joints.empty(); ++k) {
                    const float wt = gm.weights.empty() ? (k == 0 ? 1.0f : 0.0f) : gm.weights[v * 4 + size_t(k)];
                    const size_t slot = gm.joints[v * 4 + size_t(k)] / 3;
                    if (wt <= 0 || slot >= gm.palette.size() || gm.palette[slot] >= n) continue;
                    joints[v * 4 + size_t(k)] = uint16_t(gm.palette[slot]);
                    weights[v * 4 + size_t(k)] = wt;
                    sum += wt;
                }
                if (sum <= 1e-6f) {
                    for (int k = 0; k < 4; ++k) joints[v * 4 + size_t(k)] = 0, weights[v * 4 + size_t(k)] = 0;
                    joints[v * 4] = uint16_t(first), weights[v * 4] = 1;
                } else {
                    for (int k = 0; k < 4; ++k) weights[v * 4 + size_t(k)] /= sum;
                }
            }
            attrs += ",\"JOINTS_0\":" + std::to_string(w.accessor(joints.data(), nv, 4, 5123, "VEC4", 34962));
            attrs += ",\"WEIGHTS_0\":" + std::to_string(w.accessor(weights.data(), nv, 4, 5126, "VEC4", 34962));
        }
        const int ia = w.accessor(idx.data(), idx.size(), 1, 5125, "SCALAR", 34963);
        std::string prim = "{\"attributes\":{" + attrs + "},\"indices\":" + std::to_string(ia);
        if (gm.material < g.materials.size()) prim += ",\"material\":" + std::to_string(gm.material);
        meshes.push_back("{\"name\":\"mesh_" + std::to_string(mi) + "\",\"primitives\":[" + prim + "}]}");
        nodes.push_back("{\"name\":\"mesh_" + std::to_string(mi) + "\",\"mesh\":" + std::to_string(meshes.size() - 1) +
                        (onJoints ? ",\"skin\":0" : "") + "}");
        sceneNodes.push_back(std::to_string(nodes.size() - 1));
        ++st.meshes;
        st.vertices += int(nv);
        st.triangles += int(idx.size() / 3);
    }
    if (meshes.empty()) return fail(error, "no meshes");

    std::string skins;
    if (skinned) {
        std::vector<float> ibm(n * 16);
        for (size_t j = 0; j < n; ++j) std::memcpy(&ibm[j * 16], fromMat34(g.inverseBind[j]).m, 16 * sizeof(float));
        std::vector<std::string> jl;
        for (size_t j = 0; j < n; ++j) jl.push_back(std::to_string(j));
        skins = "[{\"joints\":[" + joined(jl) + "],\"inverseBindMatrices\":" + std::to_string(w.accessor(ibm.data(), n, 16, 5126, "MAT4", 0)) + "}]";
    }

    std::vector<std::string> animations;
    for (const auto& [aname, anim] : in.anims) {
        if (!skinned || !anim || anim->joints() != int(n) || anim->frames() < 1) continue;
        const int frames = std::max(2, anim->frames());
        const float rate = anim->sampleRate() > 0 ? anim->sampleRate() : 30.0f;
        std::vector<std::vector<Trs>> poses;
        for (int f = 0; f < frames; ++f) {
            const float fr = std::min(float(f), float(anim->frames() - 1));
            const std::vector<JointTransform> p = in.absolute ? anim->poseAbsolute(sk.basePose, fr) : anim->poseOver(sk.basePose, fr);
            poses.push_back(localsOf(sk.world(p), parent));
        }
        std::vector<float> times(static_cast<size_t>(frames));
        for (int f = 0; f < frames; ++f) times[size_t(f)] = float(f) / rate;
        int input = -1;
        std::vector<std::string> samplers, channels;
        for (size_t j = 0; j < n; ++j)
            for (int kind = 0; kind < 3; ++kind) {
                const int comps = kind == 1 ? 4 : 3;
                std::vector<float> out(size_t(frames) * size_t(comps));
                bool moves = false;
                for (int f = 0; f < frames; ++f) {
                    Trs& t = poses[size_t(f)][j];
                    if (kind == 1 && f > 0) {
                        const float* q0 = &out[size_t(f - 1) * 4];
                        if (q0[0] * t.r[0] + q0[1] * t.r[1] + q0[2] * t.r[2] + q0[3] * t.r[3] < 0)
                            for (float& c : t.r) c = -c;
                    }
                    const float* v = kind == 0 ? t.t : kind == 1 ? t.r : t.s;
                    const float* rv = kind == 0 ? rest[j].t : kind == 1 ? rest[j].r : rest[j].s;
                    for (int c = 0; c < comps; ++c) {
                        out[size_t(f) * size_t(comps) + size_t(c)] = v[c];
                        if (std::fabs(v[c] - rv[c]) > 1e-4f && !(kind == 1 && std::fabs(std::fabs(v[c]) - std::fabs(rv[c])) < 1e-4f)) moves = true;
                    }
                }
                if (!moves) continue;
                if (input < 0) {
                    const float mn = 0, mx = times.back();
                    input = w.accessor(times.data(), times.size(), 1, 5126, "SCALAR", 0, &mn, &mx);
                }
                const int output = w.accessor(out.data(), size_t(frames), comps, 5126, kind == 1 ? "VEC4" : "VEC3", 0);
                samplers.push_back("{\"input\":" + std::to_string(input) + ",\"output\":" + std::to_string(output) + ",\"interpolation\":\"LINEAR\"}");
                channels.push_back("{\"sampler\":" + std::to_string(samplers.size() - 1) + ",\"target\":{\"node\":" + std::to_string(j) +
                                   ",\"path\":\"" + (kind == 0 ? "translation" : kind == 1 ? "rotation" : "scale") + "\"}}");
            }
        if (channels.empty()) continue;
        st.channels += int(channels.size());
        ++st.animations;
        animations.push_back("{\"name\":" + quoted(aname) + ",\"samplers\":[" + joined(samplers) + "],\"channels\":[" + joined(channels) + "]}");
    }

    while (w.bin.size() % 4) w.bin.push_back(0);
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"CR2-U model bridge\"},\"scene\":0,\"scenes\":[{\"nodes\":[" +
                       joined(sceneNodes) + "]}]";
    json += ",\"nodes\":[" + joined(nodes) + "],\"meshes\":[" + joined(meshes) + "]";
    if (!materials.empty()) json += ",\"materials\":[" + joined(materials) + "]";
    if (!images.empty())
        json += ",\"images\":[" + joined(images) + "],\"textures\":[" + joined(textures) + "],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9987}]";
    if (!skins.empty()) json += ",\"skins\":" + skins;
    if (!animations.empty()) json += ",\"animations\":[" + joined(animations) + "]";
    json += ",\"accessors\":[" + joined(w.accessors) + "],\"bufferViews\":[" + joined(w.views) + "]";
    json += ",\"buffers\":[{\"byteLength\":" + std::to_string(w.bin.size()) + "}]}";
    while (json.size() % 4) json += ' ';
    std::vector<uint8_t> glb;
    auto u32 = [&](uint32_t v) { for (int k = 0; k < 4; ++k) glb.push_back(uint8_t(v >> (8 * k))); };
    u32(0x46546C67), u32(2), u32(uint32_t(12 + 8 + json.size() + 8 + w.bin.size()));
    u32(uint32_t(json.size())), u32(0x4E4F534A);
    glb.insert(glb.end(), json.begin(), json.end());
    u32(uint32_t(w.bin.size())), u32(0x004E4942);
    glb.insert(glb.end(), w.bin.begin(), w.bin.end());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    const bool written = f && std::fwrite(glb.data(), 1, glb.size(), f) == glb.size();
    if (f) std::fclose(f);
    if (!written) return fail(error, "can't write " + path);
    st.bytes = glb.size();
    if (stats) *stats = st;
    return true;
}

bool applyFigureReplacement(Geom& geom, const Skeleton& skeleton, const std::string& path, const std::string& imagePrefix,
                            std::vector<std::pair<std::string, Image>>& images, std::string* error, GltfStats* stats,
                            std::vector<std::vector<std::pair<int, uint32_t>>>* sources) {
    if (sources) sources->clear();
    int primitive = -1;
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) return fail(error, "not a glTF file");
    struct Free {
        cgltf_data* d;
        ~Free() { cgltf_free(d); }
    } freeIt{data};
    if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) return fail(error, "its buffers can't be read");
    const std::string dir = path.substr(0, path.find_last_of("/\\") + 1);

    GltfStats st;
    const size_t n = skeleton.basePose.size();
    const bool skinnedFigure = n > 0 && geom.inverseBind.size() == n;
    std::vector<M4> skin(n), skinInverse(n), exported(n);
    std::vector<bool> undo(n, false);
    if (skinnedFigure) {
        const std::vector<Mat34> world = skeleton.world(skeleton.basePose);
        // the export's own skinning (its joints as TRS: a sheared one - the scale compensation under a
        // scaled parent - is what the file holds, not quite the figure's)
        const std::vector<int> parent = parentsOf(skeleton);
        const std::vector<Trs> locals = localsOf(world, parent);
        std::vector<M4> rebuilt(n);
        std::vector<bool> done(n, false);
        for (size_t pass = 0; pass < n; ++pass) // (parents first, whatever the order)
            for (size_t j = 0; j < n; ++j)
                if (!done[j] && (parent[j] < 0 || done[size_t(parent[j])]))
                    rebuilt[j] = parent[j] >= 0 ? rebuilt[size_t(parent[j])] * matrixOf(locals[j]) : matrixOf(locals[j]), done[j] = true;
        for (size_t j = 0; j < n; ++j) {
            skin[j] = fromMat34(world[j] * geom.inverseBind[j]);
            exported[j] = rebuilt[j] * fromMat34(geom.inverseBind[j]);
            undo[j] = skin[j].invertible();
            skinInverse[j] = skin[j].inverse();
        }
    }
    // the figure's own picture names (an image "tex_<one of them>" is that picture)
    std::set<std::string> own;
    for (const GeomMaterial& m : geom.materials)
        for (const auto& [k, v] : m.textures) own.insert(v);
    std::map<const cgltf_image*, std::string> brought;
    auto pictureOf = [&](const cgltf_texture_view& view, std::string& name) {
        const cgltf_image* img = view.texture ? view.texture->image : nullptr;
        if (!img) return false;
        if (img->name && std::strncmp(img->name, "tex_", 4) == 0 && own.count(img->name + 4)) return false; // (as it was)
        if (auto it = brought.find(img); it != brought.end()) {
            name = it->second;
            return !name.empty();
        }
        std::vector<uint8_t> bytes;
        int wdt = 0, hgt = 0, ch = 0;
        uint8_t* px = imageBytes(img, dir, bytes) ? stbi_load_from_memory(bytes.data(), int(bytes.size()), &wdt, &hgt, &ch, 4) : nullptr;
        if (!px) return !(brought[img] = "").empty();
        Image im;
        im.width = wdt, im.height = hgt, im.mipmaps = true;
        im.rgba.assign(px, px + size_t(wdt) * size_t(hgt) * 4);
        stbi_image_free(px);
        name = imagePrefix + std::to_string(images.size());
        images.emplace_back(name, std::move(im));
        brought[img] = name;
        return true;
    };
    std::map<std::pair<int, std::string>, uint32_t> copies; // (material, its pictures) -> the copy's index
    std::vector<GeomMaterial> extra;

    std::vector<GeomMesh> meshes;
    for (size_t ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        float nw[16];
        cgltf_node_transform_world(node, nw);
        const M4 world = fromArray(nw);
        const int carrier = skinnedFigure ? jointOf(node, n) : -1;
        const cgltf_skin* sk = node->skin;
        std::vector<M4> jointMatrix;
        std::vector<int> jointNode;
        if (sk && skinnedFigure)
            for (size_t j = 0; j < sk->joints_count; ++j) {
                float jw[16], ibm[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                cgltf_node_transform_world(sk->joints[j], jw);
                if (sk->inverse_bind_matrices) cgltf_accessor_read_float(sk->inverse_bind_matrices, j, ibm, 16);
                int jn = jointOf(sk->joints[j], n);
                if (jn < 0) ++st.unmatchedJoints, jn = 0;
                jointNode.push_back(jn);
                // into the figure's bind space: its skinning undone - else (a joint at scale 0 at rest) the
                // vertices taken as they are, already in it
                const M4 m = fromArray(jw) * fromArray(ibm);
                M4 c = undo[size_t(jn)] ? skinInverse[size_t(jn)] * m : M4();
                // (the figure's own skinning, as its export wrote it: exactly none - a joint at a tiny
                // scale loses its precision undone)
                auto close = [&](const M4& a) {
                    for (int k = 0; k < 16; ++k)
                        if (std::fabs(m.m[k] - a.m[k]) > 1e-5f * (1 + std::fabs(a.m[k]))) return false;
                    return true;
                };
                if (close(skin[size_t(jn)]) || close(exported[size_t(jn)])) c = M4();
                jointMatrix.push_back(c);
            }
        for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            ++primitive;
            const cgltf_accessor *position = nullptr, *normal = nullptr, *tangent = nullptr, *uv0 = nullptr, *uv1 = nullptr,
                                 *colour = nullptr, *joints = nullptr, *weights = nullptr;
            for (size_t k = 0; k < prim.attributes_count; ++k) {
                const cgltf_attribute& at = prim.attributes[k];
                if (at.type == cgltf_attribute_type_position) position = at.data;
                else if (at.type == cgltf_attribute_type_normal) normal = at.data;
                else if (at.type == cgltf_attribute_type_tangent) tangent = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) uv0 = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 1) uv1 = at.data;
                else if (at.type == cgltf_attribute_type_color && at.index == 0) colour = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) joints = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) weights = at.data;
            }
            if (!position || !position->count) continue;
            const size_t nv = position->count;
            const bool useSkin = !jointMatrix.empty() && joints && weights && joints->count == nv && weights->count == nv;
            // the material: the one it names (or the figure's first), a copy for pictures of its own
            int mat = materialOf(prim.material);
            if (mat < 0 || size_t(mat) >= geom.materials.size()) mat = geom.meshes.empty() ? 0 : int(geom.meshes[0].material);
            uint32_t material = uint32_t(mat);
            if (prim.material && size_t(mat) < geom.materials.size()) {
                std::string colourName, normalName;
                const bool c = prim.material->has_pbr_metallic_roughness && pictureOf(prim.material->pbr_metallic_roughness.base_color_texture, colourName);
                const bool nm = pictureOf(prim.material->normal_texture, normalName);
                if (c || nm) {
                    const auto key = std::make_pair(mat, colourName + "|" + normalName);
                    if (auto it = copies.find(key); it != copies.end()) material = it->second;
                    else {
                        GeomMaterial copy = geom.materials[size_t(mat)];
                        if (c) copy.textures[0x32] = colourName;
                        if (nm) copy.textures[0x35] = normalName;
                        extra.push_back(copy);
                        material = copies[key] = uint32_t(geom.materials.size() + extra.size() - 1);
                    }
                }
            }
            const GeomMaterial& gm = material < geom.materials.size() ? geom.materials[material] : extra[material - geom.materials.size()];
            const bool needTangents = !gm.texture(0x35).empty();
            // the vertices in the figure's bind space, and each one's joints
            std::vector<float> pos(nv * 3), nrm(normal ? nv * 3 : 0), tan(tangent ? nv * 3 : 0), handed(nv, 1.0f), uv(nv * 2, 0.0f),
                uvb(uv1 ? nv * 2 : 0), col(colour ? nv * 4 : 0);
            std::vector<std::array<std::pair<int, float>, 4>> inf(nv);
            std::vector<int> infCount(nv, 0);
            for (size_t v = 0; v < nv; ++v) {
                float p[3] = {0, 0, 0}, nn[3] = {0, 0, 1}, tt[4] = {1, 0, 0, 1};
                cgltf_accessor_read_float(position, v, p, 3);
                if (normal) cgltf_accessor_read_float(normal, v, nn, 3);
                if (tangent) cgltf_accessor_read_float(tangent, v, tt, 4);
                float op[3] = {0, 0, 0}, on[3] = {0, 0, 0}, ot[3] = {0, 0, 0};
                int count = 0;
                if (useSkin) {
                    cgltf_uint jv[4] = {0, 0, 0, 0};
                    float wv[4] = {0, 0, 0, 0}, sum = 0;
                    cgltf_accessor_read_uint(joints, v, jv, 4);
                    cgltf_accessor_read_float(weights, v, wv, 4);
                    for (int k = 0; k < 4; ++k) {
                        if (wv[k] <= 0 || jv[k] >= jointMatrix.size()) continue;
                        float q[3];
                        jointMatrix[jv[k]].point(p, q);
                        for (int c = 0; c < 3; ++c) op[c] += wv[k] * q[c];
                        jointMatrix[jv[k]].vector(nn, q);
                        for (int c = 0; c < 3; ++c) on[c] += wv[k] * q[c];
                        jointMatrix[jv[k]].vector(tt, q);
                        for (int c = 0; c < 3; ++c) ot[c] += wv[k] * q[c];
                        sum += wv[k];
                        const int jn = jointNode[jv[k]];
                        bool merged = false;
                        for (int e = 0; e < count; ++e)
                            if (inf[v][size_t(e)].first == jn) inf[v][size_t(e)].second += wv[k], merged = true;
                        if (!merged) inf[v][size_t(count++)] = {jn, wv[k]};
                    }
                    if (sum > 1e-6f) {
                        for (float& c : op) c /= sum;
                        for (int e = 0; e < count; ++e) inf[v][size_t(e)].second /= sum;
                    } else {
                        count = 0;
                    }
                }
                if (count == 0) { // without a skin: on the node's joint (rigid), else where it stands
                    float q[3];
                    world.point(p, q);
                    world.vector(nn, on);
                    world.vector(tt, ot);
                    std::memcpy(op, q, sizeof q);
                    if (carrier >= 0) {
                        const M4& back = undo[size_t(carrier)] ? skinInverse[size_t(carrier)] : M4();
                        back.point(q, op);
                        float t1[3];
                        back.vector(on, t1), std::memcpy(on, t1, sizeof t1);
                        back.vector(ot, t1), std::memcpy(ot, t1, sizeof t1);
                        inf[v][0] = {carrier, 1.0f}, count = 1;
                    }
                }
                infCount[v] = count;
                std::memcpy(&pos[v * 3], op, sizeof op);
                if (normal) normalise(on), std::memcpy(&nrm[v * 3], on, sizeof on);
                if (tangent) normalise(ot), std::memcpy(&tan[v * 3], ot, sizeof ot), handed[v] = tt[3] < 0 ? -1.0f : 1.0f;
                if (uv0) cgltf_accessor_read_float(uv0, v, &uv[v * 2], 2);
                if (uv1) cgltf_accessor_read_float(uv1, v, &uvb[v * 2], 2);
                if (colour) {
                    float c[4] = {1, 1, 1, 1};
                    cgltf_accessor_read_float(colour, v, c, colour->type == cgltf_type_vec3 ? 3 : 4);
                    std::memcpy(&col[v * 4], c, sizeof c);
                }
            }
            std::vector<uint32_t> idx;
            if (prim.indices)
                for (size_t k = 0; k < prim.indices->count; ++k) idx.push_back(uint32_t(std::min<size_t>(cgltf_accessor_read_index(prim.indices, k), nv - 1)));
            else
                for (size_t k = 0; k < nv; ++k) idx.push_back(uint32_t(k));
            idx.resize(idx.size() / 3 * 3);
            if (idx.empty()) continue;
            // normals from the faces when it has none; tangents (for a normal map) from the UVs
            if (nrm.empty()) {
                nrm.assign(nv * 3, 0.0f);
                for (size_t t = 0; t < idx.size(); t += 3) {
                    const float *a = &pos[idx[t] * 3], *b = &pos[idx[t + 1] * 3], *c = &pos[idx[t + 2] * 3];
                    const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                    float f[3];
                    cross(e1, e2, f);
                    for (int k = 0; k < 3; ++k)
                        for (int c3 = 0; c3 < 3; ++c3) nrm[idx[t + size_t(k)] * 3 + size_t(c3)] += f[c3];
                }
                for (size_t v = 0; v < nv; ++v) normalise(&nrm[v * 3]);
            }
            if (tan.empty() && needTangents) {
                tan.assign(nv * 3, 0.0f);
                std::vector<float> bit(nv * 3, 0.0f);
                for (size_t t = 0; t < idx.size(); t += 3) {
                    const uint32_t i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
                    const float e1[3] = {pos[i1 * 3] - pos[i0 * 3], pos[i1 * 3 + 1] - pos[i0 * 3 + 1], pos[i1 * 3 + 2] - pos[i0 * 3 + 2]};
                    const float e2[3] = {pos[i2 * 3] - pos[i0 * 3], pos[i2 * 3 + 1] - pos[i0 * 3 + 1], pos[i2 * 3 + 2] - pos[i0 * 3 + 2]};
                    const float du1 = uv[i1 * 2] - uv[i0 * 2], dv1 = uv[i1 * 2 + 1] - uv[i0 * 2 + 1];
                    const float du2 = uv[i2 * 2] - uv[i0 * 2], dv2 = uv[i2 * 2 + 1] - uv[i0 * 2 + 1];
                    const float d = du1 * dv2 - du2 * dv1;
                    if (std::fabs(d) < 1e-12f) continue;
                    const float r = 1.0f / d;
                    for (int c = 0; c < 3; ++c) {
                        const float tc = (e1[c] * dv2 - e2[c] * dv1) * r, bc = (e2[c] * du1 - e1[c] * du2) * r;
                        for (uint32_t i : {i0, i1, i2}) tan[i * 3 + size_t(c)] += tc, bit[i * 3 + size_t(c)] += bc;
                    }
                }
                for (size_t v = 0; v < nv; ++v) { // (Gram-Schmidt against the normal, the handedness kept)
                    float* t = &tan[v * 3];
                    const float* nn = &nrm[v * 3];
                    const float dn = t[0] * nn[0] + t[1] * nn[1] + t[2] * nn[2];
                    for (int c = 0; c < 3; ++c) t[c] -= nn[c] * dn;
                    if (t[0] * t[0] + t[1] * t[1] + t[2] * t[2] < 1e-20f) t[0] = 1, t[1] = 0, t[2] = 0;
                    normalise(t);
                    float c3[3];
                    cross(nn, t, c3);
                    handed[v] = c3[0] * bit[v * 3] + c3[1] * bit[v * 3 + 1] + c3[2] * bit[v * 3 + 2] < 0 ? -1.0f : 1.0f;
                }
            }
            // split into meshes of at most 85 joints (the palette's rows: a byte, 3 a joint)
            size_t at = 0;
            while (at < idx.size()) {
                std::map<int, int> slot;
                std::vector<uint32_t> tris;
                for (; at < idx.size(); at += 3) {
                    std::set<int> need;
                    for (int k = 0; k < 3; ++k)
                        for (int e = 0; e < infCount[idx[at + size_t(k)]]; ++e)
                            if (!slot.count(inf[idx[at + size_t(k)]][size_t(e)].first)) need.insert(inf[idx[at + size_t(k)]][size_t(e)].first);
                    if (slot.size() + need.size() > 85 && !tris.empty()) break;
                    for (int j : need) {
                        const int s = int(slot.size());
                        slot[j] = s;
                    }
                    tris.insert(tris.end(), {idx[at], idx[at + 1], idx[at + 2]});
                }
                std::map<uint32_t, uint32_t> remap;
                std::vector<std::pair<int, uint32_t>> from;
                GeomMesh m;
                m.material = material;
                m.rows = true;
                m.palette.resize(slot.size());
                for (const auto& [j, s] : slot) m.palette[size_t(s)] = uint32_t(j);
                const bool onJoints = !slot.empty();
                for (uint32_t& i : tris) {
                    auto it = remap.find(i);
                    if (it == remap.end()) {
                        const uint32_t o = i;
                        it = remap.emplace(o, uint32_t(remap.size())).first;
                        from.emplace_back(primitive, o);
                        m.position.insert(m.position.end(), &pos[o * 3], &pos[o * 3] + 3);
                        m.normal.insert(m.normal.end(), &nrm[o * 3], &nrm[o * 3] + 3);
                        if (!tan.empty()) {
                            m.tangent.insert(m.tangent.end(), &tan[o * 3], &tan[o * 3] + 3);
                            float b[3];
                            cross(&nrm[o * 3], &tan[o * 3], b);
                            for (float& c : b) c *= handed[o];
                            m.binormal.insert(m.binormal.end(), b, b + 3);
                        }
                        m.uv.insert(m.uv.end(), &uv[o * 2], &uv[o * 2] + 2);
                        if (!uvb.empty()) m.uv1.insert(m.uv1.end(), &uvb[o * 2], &uvb[o * 2] + 2);
                        if (!col.empty()) m.colour.insert(m.colour.end(), &col[o * 4], &col[o * 4] + 4);
                        if (onJoints)
                            for (int k = 0; k < 4; ++k) {
                                const bool has = k < infCount[o];
                                m.joints.push_back(uint8_t(has ? slot[inf[o][size_t(k)].first] * 3 : 0));
                                m.weights.push_back(has ? inf[o][size_t(k)].second : 0.0f);
                            }
                    }
                    i = it->second;
                }
                m.vertexCount = uint32_t(remap.size());
                m.list = std::move(tris);
                st.vertices += int(m.vertexCount);
                st.triangles += int(m.list.size() / 3);
                ++st.meshes;
                meshes.push_back(std::move(m));
                if (sources) sources->push_back(std::move(from));
            }
        }
    }
    if (meshes.empty()) return fail(error, "no triangle meshes in it");
    geom.meshes = std::move(meshes);
    geom.materials.insert(geom.materials.end(), extra.begin(), extra.end());
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (const GeomMesh& m : geom.meshes)
        for (size_t v = 0; v + 2 < m.position.size(); v += 3)
            for (int c = 0; c < 3; ++c) mn[c] = std::min(mn[c], m.position[v + size_t(c)]), mx[c] = std::max(mx[c], m.position[v + size_t(c)]);
    std::memcpy(geom.boundsMin, mn, sizeof mn);
    std::memcpy(geom.boundsMax, mx, sizeof mx);
    if (stats) *stats = st;
    return true;
}

} // namespace cr2
