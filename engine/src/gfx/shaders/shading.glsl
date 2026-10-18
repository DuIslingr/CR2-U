// MVGL's generated surface shaders as one (the game's shaders/<key>_fp.shad and _vp.shad, GLSL ES
// 1.00 written per shader key): the same formulas, in the same order, chosen by the SHADE_* bits
// (shade_flags.h, which comes first). Shared by the Vulkan shaders (#include) and the GL device
// (the text, after its version line); SHADING_FRAGMENT adds the samplers and shade().
//
// The block is ShadingBlock in render_device.hpp (std140, 288 bytes). Lights, the camera and
// positions are in one space: the mesh's vertices as drawn.
#ifdef GL_ES
#define SHADE_HIGHP highp // (the flags need more than mediump's 16 bits)
#else
#define SHADE_HIGHP
#endif
#ifdef VULKAN
layout(std140, set = 1, binding = 0) uniform Material {
#else
layout(std140) uniform Material {
#endif
    vec4 diffuse;          // DiffuseColor
    vec4 params;           // Bumpiness, SpecularStrength, SpecularPower, AlphaRef
    vec4 velvet;           // RollOff, VelvetStrength, Time, -
    SHADE_HIGHP ivec4 flags; // SHADE_* bits, -, -, -
    vec4 surface;          // SurfaceColor
    vec4 fuzzy;            // FuzzySpecColor
    vec4 sub;              // SubColor
    vec4 uvAnim;           // ScrollSpeedSet1, OffsetSet1
    vec4 lightDir;         // DirLamp01Dir (towards the light)
    vec4 lightColor;       // DirLamp01Color
    vec4 ambient;          // AmbientColor
    vec4 ground;           // GroundColor
    vec4 skyDir;           // SkyDir
    vec4 fogParams;        // FogParams: far / (far - near), -1 / (far - near), alpha
    vec4 fogColor;         // FogColor
    vec4 camera;           // CameraPosition
    vec4 uvAnim2;          // ScrollSpeedSet2, OffsetSet2
    vec4 strengths;        // OverlayStrength, DistortionStrength, -, -
} mat;

// ---- the vertex programs' parts ----

vec2 shadeUV(vec2 uv) {
    int f = mat.flags.x;
    if ((f & SHADE_OFFSET) != 0) uv = uv + mat.uvAnim.zw;
    if ((f & SHADE_SCROLL) != 0) uv += fract(mat.uvAnim.xy * mat.velvet.z);
    return uv;
}

// (uv1: as the game's vertex programs, OffsetSet2 or the scroll - no key has both)
vec2 shadeUV1(vec2 uv) {
    int f = mat.flags.x;
    if ((f & SHADE_OFFSET2) != 0) uv = uv + mat.uvAnim2.zw;
    if ((f & SHADE_SCROLL2) != 0) uv += fract(mat.uvAnim2.xy * mat.velvet.z);
    return uv;
}

// from gl_Position.w (the view depth)
float shadeFog(float w) {
    if ((mat.flags.x & SHADE_FOG) == 0) return 0.0;
    return clamp(1.0 - (mat.fogParams.x + w * mat.fogParams.y), 0.0, 1.0) * mat.fogParams.z;
}

vec3 shadeView(vec3 pos) { return normalize(mat.camera.xyz - pos); }

#ifdef SHADING_FRAGMENT
#ifdef VULKAN
layout(set = 0, binding = 0) uniform sampler2D uTex;    // ColorSampler
layout(set = 2, binding = 0) uniform sampler2D uNormal; // NormalSampler
layout(set = 3, binding = 0) uniform sampler2D uExtra;  // CLUTSampler, LightSampler or OverlayColorSampler
#else
uniform sampler2D uTex;
uniform sampler2D uNormal;
uniform sampler2D uExtra;
#endif

vec4 shade(vec2 uv, vec2 uv1, vec4 vColor, vec3 vNormal, vec3 vTangent, vec3 vBinormal, vec3 vView, float ff) {
    int f = mat.flags.x;
    vec3 Nn = vNormal;
    vec4 normalRGBA = vec4(0.5, 0.5, 0.5, 1.0);
    vec2 colorUV = uv;
    if ((f & SHADE_NORMAL_MAP) != 0) {
        normalRGBA = texture(uNormal, (f & SHADE_DISTORTION) != 0 ? uv1 : uv); // (the distortion's at uv1)
        if ((f & SHADE_DISTORTION) != 0) colorUV = uv + (normalRGBA.rgb - vec3(0.5, 0.5, 0.5)).xy * mat.strengths.y;
        vec3 bump = (normalRGBA.rgb - vec3(0.5, 0.5, 0.5)) * mat.params.x;
        Nn = Nn + bump.x * vTangent + bump.y * vBinormal;
    }
    Nn = normalize(Nn);
    vec4 colorRGBA = vec4(1.0);
    vec4 diffuseColor = mat.diffuse;
    if ((f & SHADE_TEXTURE) != 0) {
        colorRGBA = texture(uTex, colorUV);
        diffuseColor = colorRGBA;
        if ((f & SHADE_LIGHT_MAP) != 0) diffuseColor.a = texture(uExtra, uv).a;
        if ((f & SHADE_OVERLAY) != 0) {
            vec4 overlayColorRGBA = texture(uExtra, (f & SHADE_UV1) != 0 ? uv1 : uv);
            float overlayStlength = mat.strengths.x * overlayColorRGBA.a;
            diffuseColor.rgb = diffuseColor.rgb * (1.0 - overlayStlength) + overlayColorRGBA.rgb * overlayStlength;
        }
        diffuseColor *= mat.diffuse;
    } else if ((f & SHADE_LIGHT_MAP) != 0) {
        diffuseColor.a = texture(uExtra, uv).a;
    }
    if ((f & SHADE_TINT_ALPHA) != 0) diffuseColor *= vColor;
    else if ((f & SHADE_TINT) != 0) diffuseColor.rgb *= vColor.rgb;
    if ((f & SHADE_LIT) != 0) {
        vec3 diffTmp = vec3(0.0);
        vec3 specTmp = vec3(0.0);
        float spStr = (f & SHADE_SPEC_VERTEX_ALPHA) != 0 ? vColor.a : (f & SHADE_SPEC_MAP_ALPHA) != 0 ? normalRGBA.a : mat.params.y;
        if ((f & SHADE_DIR_LIGHT) != 0) {
            vec3 Ln = mat.lightDir.xyz;
            vec3 Lc = mat.lightColor.rgb;
            float ldn = dot(Ln, Nn);
            if ((f & SHADE_CLUT) != 0) {
                vec3 Hn = normalize(vView + Ln);
                vec4 lightPow = texture(uExtra, vec2(ldn * 0.5 + 0.5, dot(Hn, Nn)));
                diffTmp = lightPow.rgb * Lc;
                if ((f & SHADE_SPECULAR) != 0) specTmp = lightPow.a * spStr * diffTmp;
            } else {
                ldn = max(0.0, ldn);
                diffTmp = ldn * Lc;
                if ((f & SHADE_SPECULAR) != 0) {
                    vec3 Hn = normalize(vView + Ln);
                    float d = dot(Hn, Nn);
                    float pw = max(d / (mat.params.z * (1.0 - d) + d), 0.0);
                    specTmp = step(0.0, ldn) * pw * spStr * diffTmp;
                }
            }
            if ((f & SHADE_VELVET) != 0) {
                diffTmp *= mat.surface.rgb;
                float subLamb = max(0.0, smoothstep(-mat.velvet.x, 1.0, ldn) - smoothstep(0.0, 1.0, ldn));
                diffTmp += subLamb * mat.sub.rgb * mat.velvet.y;
            }
            if ((f & SHADE_FUZZY) != 0) {
                float vdn = 1.0 - dot(vView, Nn);
                specTmp += vec3(vdn, vdn, vdn) * mat.fuzzy.rgb * mat.velvet.y;
            }
        }
        vec3 ambient = mat.ambient.rgb;
        if ((f & SHADE_HEMISPHERE) != 0) {
            float hst = (dot(Nn, mat.skyDir.xyz) + 1.0) * 0.5;
            ambient = mat.ambient.rgb * hst + mat.ground.rgb * (1.0 - hst);
        }
        float obscure = (f & SHADE_OBSCURE) != 0 ? vColor.r : 1.0;
        diffuseColor.rgb *= (diffTmp * obscure + ambient);
        diffuseColor.rgb += specTmp * ((f & SHADE_OBSCURE_SPEC) != 0 ? obscure : 1.0);
    }
    if ((f & SHADE_FOG) != 0) diffuseColor.rgb = mat.fogColor.rgb * ff + diffuseColor.rgb * (1.0 - ff);
    if ((f & SHADE_ALPHA_TEST) != 0 && colorRGBA.w < mat.params.w) discard;
    return diffuseColor;
}
#endif
