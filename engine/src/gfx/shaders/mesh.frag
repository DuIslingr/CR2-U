#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "shade_flags.h"
#define SHADING_FRAGMENT
#include "shading.glsl"
// A surface: the game's shading (shading.glsl) for materials with a shader key, else GL ES
// 1.1-style texture modes (as gl_device.cpp's), an alpha test and a directional light
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 4) in vec3 vNormal;
layout(location = 5) in vec3 vTangent;
layout(location = 6) in vec3 vBinormal;
layout(location = 7) in vec3 vView;
layout(location = 8) in float vFog;
layout(location = 0) out vec4 fragColor;
#ifdef SCENE
layout(location = 2) in vec4 vCur;
layout(location = 3) in vec4 vPrev;
layout(location = 1) out vec2 fragMotion;  // UV offset to this point last frame
layout(location = 2) out float fragReactive;
#endif
void main() {
    vec4 c;
    if ((mat.flags.x & SHADE_ON) != 0) {
        c = shade(vUV, vColor, vNormal, vTangent, vBinormal, vView, vFog);
    } else {
        vec4 base = pc.color * vColor;
        c = base;
        if (pc.texMode >= 0) {
            vec4 t = texture(uTex, vUV);
            if (pc.alphaTex != 0) c = vec4(base.rgb, pc.texMode == 0 ? t.a : base.a * t.a);
            else if (pc.texMode == 0) c = t;
            else if (pc.texMode == 1) c = vec4(base.rgb + t.rgb, base.a * t.a);
            else if (pc.texMode == 2) c = vec4(base.rgb * (vec3(1.0) - t.rgb), base.a * t.a);
            else if (pc.texMode == 3) c = vec4(mix(base.rgb, t.rgb, t.a), base.a);
            else c = base * t;
        }
        if (pc.alphaRef >= 0.0 && c.a <= pc.alphaRef) discard;
        if (pc.light.w > 0.0) {
            vec3 n = normalize(vNormal);
            c.rgb *= 0.45 + 0.65 * max(dot(n, normalize(pc.light.xyz)), 0.0);
        }
    }
    fragColor = c;
#ifdef SCENE
    fragMotion = (vPrev.xy / vPrev.w - vCur.xy / vCur.w) * pc.motion.zw;
    fragReactive = pc.reactive > 0.0 ? pc.reactive * clamp(c.a, 0.0, 1.0)
                                     : -pc.reactive * clamp(max(c.r, max(c.g, c.b)), 0.0, 1.0);
#endif
}
