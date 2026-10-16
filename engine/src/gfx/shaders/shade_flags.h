// The features of MVGL's generated surface shaders (shaders/<key>_fp.shad and _vp.shad in the
// game's data) that shading.glsl reproduces, as bits. gfx/material.cpp decodes a material's shader
// key into them. Shared by C++ (render_device.hpp) and GLSL (shading.glsl): #defines only.
#ifndef CR2_SHADE_FLAGS_H
#define CR2_SHADE_FLAGS_H

#define SHADE_ON 0x1                // the game's shading (else the device's fixed path)
#define SHADE_TEXTURE 0x2           // ColorSampler (else the colour is DiffuseColor alone)
#define SHADE_NORMAL_MAP 0x4        // NormalSampler bumps the normal (Bumpiness)
#define SHADE_LIT 0x8               // colour *= light + ambient
#define SHADE_DIR_LIGHT 0x10        // DirLamp01
#define SHADE_HEMISPHERE 0x20       // ambient between AmbientColor (sky) and GroundColor, else flat
#define SHADE_CLUT 0x40             // toon: CLUTSampler at (N.L / 2 + 1/2, N.H) is the light
#define SHADE_SPECULAR 0x80         // SpecularStrength x Schlick's N.H power x the light
#define SHADE_SPEC_VERTEX_ALPHA 0x100 // the vertex colour's alpha is the specular strength
#define SHADE_OBSCURE 0x200         // the vertex colour's red shades the light (not the ambient)
#define SHADE_OBSCURE_SPEC 0x400    // ... and the specular
#define SHADE_TINT 0x800            // the vertex colour multiplies the colour (rgb)
#define SHADE_TINT_ALPHA 0x1000     // ... and alpha
#define SHADE_VELVET 0x2000         // SurfaceColor, SubColor, RollOff, VelvetStrength
#define SHADE_FUZZY 0x4000          // velvet's FuzzySpecColor rim
#define SHADE_ALPHA_TEST 0x8000     // the texture's alpha < AlphaRef discarded
#define SHADE_FOG 0x10000           // FogColor by view depth (FogParams)
#define SHADE_LIGHT_MAP 0x20000     // LightSampler's alpha is the colour's
#define SHADE_SCROLL 0x40000        // uv += fract(ScrollSpeedSet1 x Time)
#define SHADE_OFFSET 0x80000        // uv += OffsetSet1
#define SHADE_SPEC_MAP_ALPHA 0x100000 // the normal map's alpha is the specular strength (the iPad's)

#endif
