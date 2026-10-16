// cr2tool: CR2-U's command-line checks on your own copy of the game's data.
//
//   cr2tool check <archive.mvgl | .obb>              every file decodes to its size
//   cr2tool scripts <archive> [name]                 every script (script\*.nut) loads in the
//                                                    Squirrel 2.2.4 VM and its main runs; the
//                                                    native stubs it calls
//   cr2tool run <archive> <script> [function]        loads one script, runs main, then its
//                                                    function (default: the script's name) with
//                                                    every native a stub; the stubs it reached
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "assets/mvgl.hpp"
#include "script/vm.hpp"

namespace {

int usage() {
    std::fprintf(stderr,
                 "cr2tool check <archive>\n"
                 "cr2tool scripts <archive> [name]\n"
                 "cr2tool run <archive> <script> [function]\n");
    return 2;
}

bool openArchive(cr2::MvglArchive& a, const char* path) {
    std::string err;
    if (!a.open(path, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return false;
    }
    return true;
}

int check(const char* path) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    size_t ok = 0, bad = 0;
    std::vector<uint8_t> data;
    for (const auto& e : a.entries()) {
        std::string err;
        if (a.read(e, data, &err) && data.size() == e.size) ++ok;
        else ++bad, std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
    }
    std::printf("%s: %zu files decode, %zu bad\n", path, ok, bad);
    return bad ? 1 : 0;
}

int scripts(const char* path, const char* only) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    size_t loaded = 0, ran = 0, total = 0;
    std::map<std::string, int> stubs;
    std::vector<uint8_t> data;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 4 || e.name.compare(e.name.size() - 4, 4, ".nut") != 0) continue;
        if (only && e.name.find(only) == std::string::npos) continue;
        ++total;
        if (!a.read(e, data)) {
            std::printf("%s: unreadable\n", e.name.c_str());
            continue;
        }
        cr2::ScriptVM vm; // (one VM per script: each defines the same helper library)
        if (!vm.load(data, e.name)) {
            std::printf("%s: %s\n", e.name.c_str(), vm.error().c_str());
            continue;
        }
        ++loaded;
        if (!vm.runMain()) {
            std::printf("%s: main failed: %s\n", e.name.c_str(), vm.error().c_str());
            continue;
        }
        ++ran;
        for (const auto& [k, n] : vm.stubCalls()) stubs[k] += n;
    }
    std::printf("%zu scripts: %zu load, %zu run main\n", total, loaded, ran);
    for (const auto& [k, n] : stubs) std::printf("  stub called in main: %s x%d\n", k.c_str(), n);
    return loaded == total && ran == total ? 0 : 1;
}

int run(const char* path, const char* script, const char* function) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::string name = script;
    if (name.find('.') == std::string::npos) name += ".nut";
    if (name.find('\\') == std::string::npos) name = "script\\" + name;
    std::vector<uint8_t> data;
    std::string err;
    if (!a.read(name, data, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr2::ScriptVM vm;
    if (!vm.load(data, name) || !vm.runMain()) return std::fprintf(stderr, "%s\n", vm.error().c_str()), 1;
    std::string fn = function ? function : name.substr(name.rfind('\\') + 1, name.rfind('.') - name.rfind('\\') - 1);
    if (const std::string f = vm.findFunction(fn); !f.empty()) fn = f;
    const bool ok = vm.call(fn);
    std::printf("%s.%s: %s\n", name.c_str(), fn.c_str(), ok ? "returned" : ("failed: " + vm.error()).c_str());
    for (const auto& [k, n] : vm.stubCalls()) std::printf("  %s x%d\n", k.c_str(), n);
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    if (!std::strcmp(argv[1], "check")) return check(argv[2]);
    if (!std::strcmp(argv[1], "scripts")) return scripts(argv[2], argc > 3 ? argv[3] : nullptr);
    if (!std::strcmp(argv[1], "run") && argc > 3) return run(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    return usage();
}
