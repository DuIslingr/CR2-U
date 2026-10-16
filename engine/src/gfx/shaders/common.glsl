// Shared by every Vulkan shader: one push-constant block per draw (112 bytes; 192 for the scene
// variants, built with SCENE: frames a temporal upscaler resolves)
layout(push_constant) uniform Draw {
    mat4 matrix;     // clip space (already Vulkan's: depth 0..1)
    vec4 color;
    int texMode;     // meshes: 0 replace, 1 add, 2 blend, 3 decal, 4 modulate, -1 untextured
    int alphaTex;    // GL_ALPHA texture: colour always comes from the fragment
    float alphaRef;  // alpha test: alpha <= this discarded (< 0 = off)
    float reactive;  // scene: > 0 alpha-blended (reactive = this x alpha), < 0 additive (x brightness)
    vec4 light;      // xyz: the light's direction (towards it); w > 0: the mesh is lit
#ifdef SCENE
    mat4 prevMatrix; // this draw's matrix last frame (unjittered, as `matrix`)
    vec4 motion;     // xy: the frame's jitter in clip units; zw: NDC -> render-target UV
#endif
} pc;
