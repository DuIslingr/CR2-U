#include "battle/special.hpp"

#include <cstring>
#include <iterator>

namespace cr2 {

namespace {

#include "battle/specialtable.inc"
#include "battle/effecttable.inc"

} // namespace

const SpecialScene* specialScene(int command, bool own, bool* advent) {
    for (const auto& e : kSpecialOf) {
        if (e.command != command || e.own != own) continue;
        const SpecialScene* s = nullptr;
        if (e.advent && e.scene >= 0 && e.scene < int(std::size(kBuildAdventScenes))) s = &kBuildAdventScenes[e.scene];
        else if (!e.advent && e.scene >= 0 && e.scene < int(std::size(kBuildAwakeScenes))) s = &kBuildAwakeScenes[e.scene];
        if (!s || !s->steps) return nullptr;
        if (advent) *advent = e.advent;
        return s;
    }
    return nullptr;
}

const EffectSetting& effectSetting(const std::string& name) {
    for (const EffectSetting& e : kEffectSettings)
        if (e.name && name == e.name) return e;
    return kEffectSettings[0]; // ("common")
}

const char* tposJoint(int tpos) {
    static const char* const kJoints[9] = {"RootLocator", "DummyEP01", "DummyEP02", "DummyEP03", "DummyEP04",
                                           "DummyDP01", "DummyDP02", "DummyCPS", "DummyCP"};
    return tpos >= 0 && tpos < 9 ? kJoints[tpos] : nullptr;
}

} // namespace cr2
