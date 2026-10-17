#include "gfx/material.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace cr2 {

ShaderFeatures decodeShaderKey(const std::array<uint32_t, 4>& key) {
    ShaderFeatures out;
    const uint32_t w0 = key[0], w1 = key[1], w2 = key[2];
    if (!w0 && !w1 && !w2 && !key[3]) return out;
    uint32_t s = SHADE_ON;
    if (w0 & 0x10000) s |= SHADE_TEXTURE;
    if (w0 & 0x20000) s |= SHADE_NORMAL_MAP;
    if (w0 & 0x60) s |= SHADE_LIT; // (exactly the keys with a light: 0x1 or 0x4)
    if (w0 & 0x1) s |= SHADE_DIR_LIGHT;
    if (w0 & 0x20) s |= SHADE_HEMISPHERE;
    if (w0 & 0x80) s |= SHADE_CLUT;
    if (w0 & 0x100) {
        s |= SHADE_SPECULAR;
        if (w0 & 0xe00) s |= SHADE_SPEC_VERTEX_ALPHA;
        if (w0 & 0x4000) s |= SHADE_OBSCURE_SPEC;
        if (w1 & 0x4000) s |= SHADE_FUZZY;
    }
    if (w0 & 0x4000) s |= SHADE_OBSCURE;
    if (w0 & 0x8000) s |= (w1 & 0x10000) ? SHADE_TINT | SHADE_TINT_ALPHA : SHADE_TINT;
    if (w1 & 0x4000) s |= SHADE_VELVET;
    if (w1 & 0x01000000) s |= SHADE_ALPHA_TEST;
    if (w1 & 0x02000000) s |= SHADE_SCROLL;
    if (w1 & 0x10000000) s |= SHADE_OFFSET;
    if (w2 & 0x10) s |= SHADE_FOG;
    if (w2 & 0x700) s |= SHADE_LIGHT_MAP;
    out.shade = s;
    if (w0 & 0x4) out.unsupported |= kUnsupportedPointLight;
    if (w0 & 0x80000) out.unsupported |= kUnsupportedOverlay;
    if (w1 & 0x20000000) out.unsupported |= kUnsupportedDistortion;
    if ((w1 & 0x04000000) || (w2 & 0x1)) out.unsupported |= kUnsupportedUv1;
    if (w2 & 0x4) out.unsupported |= kUnsupportedBillboard;
    return out;
}

ShaderFeatures featuresFromGlsl(const std::string& vp, const std::string& fp) {
    auto has = [](const std::string& src, const char* line) { return src.find(line) != std::string::npos; };
    ShaderFeatures out;
    struct Line {
        uint32_t shade, unsupported;
        const char* fp; // the fragment program has this
        const char* vp; // or the vertex program this
    };
    static const Line kLines[] = {
        {SHADE_TEXTURE, 0, "texture2D(ColorSampler", nullptr},
        {SHADE_NORMAL_MAP, 0, "Nn = Nn + bump.x", nullptr},
        {SHADE_LIT, 0, "diffContrib=diffTmp", nullptr},
        {SHADE_DIR_LIGHT, 0, "Ln = DirLamp01Dir;", nullptr},
        {SHADE_HEMISPHERE, 0, "SkyDir", nullptr},
        {SHADE_CLUT, 0, "CLUTSampler", nullptr},
        {SHADE_SPECULAR, 0, "specContrib=specTmp", nullptr},
        {SHADE_SPEC_VERTEX_ALPHA, 0, "spStr = vColor.a", nullptr},
        {SHADE_SPEC_MAP_ALPHA, 0, "spStr = normalRGBA.a", nullptr},
        {SHADE_OBSCURE, 0, "obscure *= vColor.r", nullptr},
        {SHADE_OBSCURE_SPEC, 0, "specContrib*obscure", nullptr},
        {SHADE_TINT, 0, "diffuseColor.rgb *= vColor.rgb", nullptr},
        {SHADE_TINT | SHADE_TINT_ALPHA, 0, "diffuseColor *= vColor;", nullptr},
        {SHADE_VELVET, 0, "subContrib*VelvetStrength", nullptr},
        {SHADE_FUZZY, 0, "vdn = ", nullptr},
        {SHADE_ALPHA_TEST, 0, "discard", nullptr},
        {SHADE_FOG, 0, "FogColor", nullptr},
        {SHADE_LIGHT_MAP, 0, "LightSampler", nullptr},
        {SHADE_SCROLL, 0, nullptr, "ScrollSpeedSet1"},
        {SHADE_OFFSET, 0, nullptr, "OffsetSet1"},
        {0, kUnsupportedPointLight, "PointLamp01", nullptr},
        {0, kUnsupportedOverlay, "OverlayColorSampler", nullptr},
        {0, kUnsupportedDistortion, "DistortionStrength", nullptr},
        {0, kUnsupportedUv1, nullptr, "vTexcoord1"},
        {0, kUnsupportedBillboard, nullptr, "ViewInverse"},
    };
    out.shade = SHADE_ON;
    for (const Line& l : kLines)
        if ((l.fp && has(fp, l.fp)) || (l.vp && has(vp, l.vp))) {
            out.shade |= l.shade;
            out.unsupported |= l.unsupported;
        }
    return out;
}

ShaderFeatures ShaderLibrary::features(const std::array<uint32_t, 4>& key) {
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    ShaderFeatures f = decodeShaderKey(key);
    if (f.shade) {
        char name[64];
        std::snprintf(name, sizeof name, "shaders\\%08x_%08x_%08x_%08x", key[0], key[1], key[2], key[3]);
        std::vector<uint8_t> vp, fp;
        if (archive_.read(std::string(name) + "_vp.shad", vp) && archive_.read(std::string(name) + "_fp.shad", fp)) {
            const std::string vs(vp.begin(), vp.end()), fs(fp.begin(), fp.end());
            if (fs.find("gl_FragColor") != std::string::npos) f = featuresFromGlsl(vs, fs);
        }
    }
    cache_[key] = f;
    return f;
}

float ShaderLibrary::jointScale(const std::array<uint32_t, 4>& key) {
    if (auto it = jointScales_.find(key); it != jointScales_.end()) return it->second;
    char name[64];
    std::snprintf(name, sizeof name, "shaders\\%08x_%08x_%08x_%08x_vp.shad", key[0], key[1], key[2], key[3]);
    std::vector<uint8_t> vp;
    float scale = 1; // (no GLSL - the Vita's: already the row)
    if (archive_.read(name, vp)) {
        const std::string s(vp.begin(), vp.end());
        if (s.find("Indices * 3.0") != std::string::npos || s.find("Position.w * 3.0") != std::string::npos) scale = 3;
    }
    return jointScales_[key] = scale;
}

Sampling sampling(uint32_t flags) {
    Sampling s;
    s.clampS = wrapS(flags) == Wrap::Clamp;
    s.clampT = wrapT(flags) == Wrap::Clamp;
    s.mirrorS = wrapS(flags) == Wrap::Mirror;
    s.mirrorT = wrapT(flags) == Wrap::Mirror;
    s.minLinear = minLinear(flags);
    s.magLinear = magLinear(flags);
    return s;
}

void applyMaterial(const GeomMaterial& m, const ShaderFeatures& f, MeshShading& sh) {
    sh.shade = f.shade;
    m.param(0x33, sh.color, 4);
    m.param(0x36, &sh.bumpiness, 1);
    m.param(0x38, &sh.specularStrength, 1);
    m.param(0x39, &sh.specularPower, 1);
    m.param(0x3e, sh.surfaceColor, 3);
    m.param(0x3f, sh.fuzzySpecColor, 3);
    m.param(0x40, sh.subColor, 3);
    m.param(0x41, &sh.rollOff, 1);
    m.param(0x42, &sh.velvetStrength, 1);
    m.param(0x55, sh.scrollSpeed, 2);
    m.param(0x5e, sh.offset, 2);
    if (sh.shade & SHADE_ALPHA_TEST) {
        sh.alphaRef = 0.5f;
        uint32_t bits = 0;
        if (m.state(0x78, 0, bits, 1)) std::memcpy(&sh.alphaRef, &bits, 4);
    }
}

namespace {

void normalise(const float* v, float* out) {
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int k = 0; k < 3; ++k) out[k] = n > 0 ? v[k] / n : 0.0f;
}

} // namespace

bool lightsFromFigure(const Geom& g, const Skeleton& sk, SceneLighting& out) {
    const std::vector<Mat34> world = sk.world(sk.basePose);
    bool any = false;
    for (const GeomLight& l : g.lights) {
        const int j = sk.joint(l.hash);
        const Mat34* m = j >= 0 && size_t(j) < world.size() ? &world[size_t(j)] : nullptr;
        if (l.type == GeomLight::Directional && l.index == 0 && m) { // DirLamp01
            const float axis[3] = {m->m[2], m->m[6], m->m[10]};
            normalise(axis, out.lightDir);
            for (int k = 0; k < 3; ++k) out.lightColor[k] = l.intensity() * l.colour()[k];
            any = true;
        } else if (l.type == GeomLight::Ambient) {
            for (int k = 0; k < 3; ++k) {
                out.ambient[k] = l.intensity() * l.colour()[k];
                out.ground[k] = l.intensity() * l.ground()[k];
            }
            const float pos[3] = {m ? m->m[3] : 0.0f, m ? m->m[7] : 0.0f, m ? m->m[11] : 0.0f};
            if (std::sqrt(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]) < 1.1920929e-07f) {
                out.skyDir[0] = out.skyDir[2] = 0;
                out.skyDir[1] = 1;
            } else {
                normalise(pos, out.skyDir);
            }
            any = true;
        } else if (l.type == GeomLight::Fog) {
            const float far = l.v[0], near = l.v[1];
            if (far != near) {
                out.fogParams[0] = far / (far - near);
                out.fogParams[1] = -1.0f / (far - near);
            }
            out.fogParams[2] = l.v[5];
            for (int k = 0; k < 3; ++k) out.fogColor[k] = l.v[2 + k];
            any = true;
        }
    }
    return any;
}

} // namespace cr2
