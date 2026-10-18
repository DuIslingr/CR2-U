#pragma once
// Where the leader can walk on a field, for the explorer (`cr2view --explore`) and `cr2tool nav`:
// a grid over the map's floors, flood-filled from where the leader stands - a cell walkable where a
// floor lies under it (a sweep straight down, a slope the character controller stands on), its
// neighbours joined where the leader's sphere passes between them without meeting a wall and the
// floor steps no more than the controller climbs - and paths over it (A*). The game itself has no
// such thing: the player steers; this only stands in for one.

#include <cstdint>
#include <functional>
#include <vector>

#include "field/collision.hpp"

namespace cr2 {

class NavGrid {
public:
    // The cells the leader reaches from `start` (on its floor) with a sphere of `radius`
    void build(const CollisionWorld& world, const Vec3& start, float radius, float step, float cell = 0.02f);
    // More of it: what the leader reaches from `start` too, where that's off what's reached so far (a
    // sprint's or a jump's landing). False: `start` was on it already, or nowhere to stand
    bool grow(const CollisionWorld& world, const Vec3& start, float radius, float step);
    bool reaches(const Vec3& p) const; // (`p`'s cell reached)
    bool empty() const { return reached_ == 0; }
    size_t reached() const { return reached_; }
    // Zones over the cells - each cell's from `zoneOf` at its floor (-1 none): the exits, which a walk
    // goes into only if they're where it is going
    void markZones(const std::function<int(const Vec3&)>& zoneOf);
    // A walk from `from` to the reached cell nearest `to` (its floor's height ignored): the cells'
    // middles, the last that nearest one - through no zone but `allowZone`. False: `from` is off the
    // grid, or no such walk
    bool path(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, int allowZone = -1) const;
    // The reached cell nearest `to` across the ground, and how far it lies from it
    bool nearest(const Vec3& to, Vec3& at, float& distance) const;

private:
    float x0_ = 0, z0_ = 0, cell_ = 0.02f;
    int w_ = 0, h_ = 0;
    size_t reached_ = 0;
    std::vector<float> floor_;  // per cell: its floor's height (NaN: not reached)
    std::vector<uint8_t> links_; // per cell: bit d its neighbour d joined (d: 0..7)
    std::vector<int16_t> zone_;  // per cell: its zone (-1 none)
    int index(int x, int z) const { return z * w_ + x; }
    bool cellOf(const Vec3& p, int& x, int& z) const;
};

} // namespace cr2
