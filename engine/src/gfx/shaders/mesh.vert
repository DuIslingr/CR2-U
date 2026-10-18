#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "shade_flags.h"
#include "shading.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 4) in vec3 aNormal;
layout(location = 5) in vec3 aTangent;
layout(location = 6) in vec3 aBinormal;
layout(location = 7) in vec2 aUV1;
layout(location = 0) out vec2 vUV;
layout(location = 9) out vec2 vUV1;
layout(location = 1) out vec4 vColor;
layout(location = 4) out vec3 vNormal;
layout(location = 5) out vec3 vTangent;
layout(location = 6) out vec3 vBinormal;
layout(location = 7) out vec3 vView;
layout(location = 8) out float vFog;
#ifdef SCENE
layout(location = 3) in vec3 aPrevPos; // last frame's vertices (re-skinned meshes)
layout(location = 2) out vec4 vCur;
layout(location = 3) out vec4 vPrev;
#endif
void main() {
    vUV = aUV;
    vUV1 = aUV1;
    vColor = aColor;
    vNormal = aNormal;
    gl_Position = pc.matrix * vec4(aPos, 1.0);
    if ((mat.flags.x & SHADE_ON) != 0) {
        vUV = shadeUV(aUV);
        vUV1 = shadeUV1(aUV1);
        vTangent = aTangent;
        vBinormal = aBinormal;
        vView = shadeView(aPos);
        vFog = shadeFog(gl_Position.w);
    } else {
        vTangent = vBinormal = vView = vec3(0.0);
        vFog = 0.0;
    }
#ifdef SCENE
    vCur = gl_Position;
    vPrev = pc.prevMatrix * vec4(aPrevPos, 1.0);
    gl_Position.xy += pc.motion.xy * gl_Position.w;
#endif
}
