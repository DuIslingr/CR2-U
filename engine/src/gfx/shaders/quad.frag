#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set = 0, binding = 0) uniform sampler2D uTex;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 fragColor;
#ifdef SCENE
layout(location = 2) in vec4 vCur;
layout(location = 3) in vec4 vPrev;
layout(location = 1) out vec2 fragMotion; // UV offset to this point last frame
layout(location = 2) out float fragReactive;
#endif
void main() {
    fragColor = texture(uTex, vUV) * pc.color;
#ifdef SCENE
    fragMotion = (vPrev.xy / vPrev.w - vCur.xy / vCur.w) * pc.motion.zw;
    fragReactive = 0.0;
#endif
}
