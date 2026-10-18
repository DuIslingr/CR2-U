#pragma once
// The screen breaking away (BtlPostEffectGlassFragment, GlassFragmentEffect; docs/BATTLE.md,
// "The screen breaking away"): Darwin's third awakening's scene op 33. The map alone drawn into a
// texture, cut into triangles about random points (a Delaunay triangulation), each flying out
// from the screen's middle - turning, growing - as the whole fades over 1.5 s.
//
// The shards' places are the screen's, centred, in pixels: x across [-w, w], y up [-h, h] (w, h
// half the screen's size).

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cr2 {

// lrand48: the C library's 48-bit generator (the game seeds it with the clock: srand48)
struct Rand48 {
    uint64_t x = 0x1234abcd330eULL; // (the C library's seed before any srand48)
    void seed(uint32_t s) { x = (uint64_t(s) << 16) | 0x330e; }
    uint32_t next() {
        x = (0x5deece66dULL * x + 0xb) & ((uint64_t(1) << 48) - 1);
        return uint32_t(x >> 17);
    }
    float unit() { return float(next()) * 4.656613e-10f; } // (the game's: [0, 1))
};

struct GlassShard {
    float x[3], y[3], z[3]; // its corners (the screen's place)
    float u[3], v[3];       // the texture's (v from the bottom: GL's render target)
    float move[3], moveGain[3];   // +0x40 a frame's shift, +0x50 what that gains each frame
    float turn[3], turnGain[3];   // +0x60 a frame's turn (radians about x, y, z), +0x70 (0)
    float scale[3], scaleGain[3]; // +0x80 a frame's growth, +0x90
};

class GlassFragments {
public:
    static constexpr int kPoints = 230;       // (Setup: GlassFragmentEffect(w, h, 230, ...))
    static constexpr float kLength = 1.5f;    // its seconds (Update: on while its clock is <= 1.5)
    static constexpr float kFrame = 1.0f / 60.0f;

    // w, h: half the screen's size
    void build(int w, int h, Rand48& rng);
    // One of the game's frames (Update: each shard turned and grown about its middle, moved on)
    void step();
    // Its alpha by its clock (+0x11c: 1 - t / 1.5)
    float alpha() const { return 1.0f - std::max(clock - kFrame, 0.0f) / kLength; } // (set after the frame, by its clock before)
    bool running() const { return built && clock <= kLength; }
    // Its frames to `t` seconds (one each 1/60 s)
    void advanceTo(float t);

    std::vector<GlassShard> shards;
    float clock = 0; // (+0x28)
    int w = 0, h = 0;
    bool built = false;
};

// Delaunay triangles of points (Bowyer-Watson, the points' hull covered): each three indices
std::vector<int> delaunay(const std::vector<float>& xs, const std::vector<float>& ys);

} // namespace cr2
