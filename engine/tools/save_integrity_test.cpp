// A save is taken whole or not at all: GameState::load on a save cut short at any byte before its
// last required line refuses it and leaves the game as it was - never a mix of the two.
#include "script/gamestate.hpp"
#include <cstdio>
#include <map>
#include <string>
namespace game = TEST_NAMESPACE;

int main() {
    int problems = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) { ++problems; std::printf("save integrity FAILED: %s\n", what.c_str()); }
    };
    game::GameState g;
    g.oz = 4321;
    g.setFlag(7, true);
    g.setFlag(1500, true);
    g.itemCount[5] = 3;
    const std::map<std::string, std::string> extra = {{"area", "t0108"}, {"map", "d0101"}, {"place", "1 2 3 4"}, {"playtime", "200"}, {"saved", "1"}};
    const std::string text = g.save(extra);

    game::GameState whole;
    std::map<std::string, std::string> found;
    std::string error;
    check(whole.load(text, &found, &error), "a whole save loads: " + error);
    check(whole.oz == 4321 && whole.flag(1500) && whole.itemCount[5] == 3 && found == extra, "a whole save's values");
    check(whole.save(extra) == text, "a whole save round-trips");

    const size_t lastLine = text.find(std::string("\n") + SAVE_LAST_REQUIRED + "\n") != std::string::npos
                                ? text.find(std::string("\n") + SAVE_LAST_REQUIRED + "\n") + 1
                                : text.find(std::string("\n") + SAVE_LAST_REQUIRED + " ") + 1;
    check(lastLine > 1, "the last required line is in the save");
    const size_t required = text.find('\n', lastLine) + 1;
    auto untouched = [](const game::GameState& s) { return s.oz == 777 && s.flag(3) && !s.flag(7) && s.itemCount[5] == 0; };
    int refused = 0;
    for (size_t cut = 0; cut < required; ++cut) {
        game::GameState t;
        t.oz = 777;
        t.setFlag(3, true);
        std::map<std::string, std::string> partial;
        const bool loaded = t.load(text.substr(0, cut), &partial);
        check(!loaded, "cut at byte " + std::to_string(cut) + " of " + std::to_string(text.size()) + " was accepted");
        check(untouched(t), "cut at byte " + std::to_string(cut) + " changed the game");
        refused += !loaded;
        if (problems > 8) break;
    }
    // (a last line that lost its newline is cut too, whatever comes after it)
    game::GameState t;
    check(!t.load(text.substr(0, text.size() - 1)), "a save without its last newline was accepted");
    check(!t.load(""), "an empty save was accepted");
    { // a whole save's damaged numbers: a flag doesn't grow the table to fit, nor a level overflow what uses it
        std::string damaged = text;
        const size_t flags = damaged.find("flags ") + 6;
        damaged.insert(flags, "2147483647 ");
        const size_t level = damaged.find("difficulty ") + 11;
        damaged.replace(level, damaged.find('\n', level) - level, "2147483647");
        game::GameState d;
        check(d.load(damaged), "a damaged number doesn't refuse the save");
        check(d.flags.size() <= 0x10000 && d.flag(1500), "a damaged flag number is left out");
        check(d.difficulty <= 999, "a damaged difficulty is held in range");
        const auto charaLine = [&](const std::string& numbers) { // (character 2's own line, the rest as they are)
            const size_t at = text.find("\nchara 2 ") + 1;
            return text.substr(0, at) + "chara 2 " + numbers + text.substr(text.find('\n', at));
        };
        game::GameState c;
        check(c.load(charaLine("2147483647 2147483647 2147483647")) && c.level[2] <= 999 && c.exp[2] <= 99999999 && c.mp[2] <= 9999999,
              "a character's damaged numbers are held in range");
        game::GameState top;
        check(top.load(charaLine("200 39703470 9999")) && top.level[2] == 200 && top.exp[2] == 39703470 && top.mp[2] == 9999,
              "a level 200 character keeps its numbers");
    }
    std::printf("save integrity: %zu bytes, %d cuts refused; %d problems\n", text.size(), refused, problems);
    return problems ? 1 : 0;
}
