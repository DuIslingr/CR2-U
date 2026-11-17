// A fan translation as a language of its own (#11): metastigma's Russian - the game's message\*.dat (and
// data\*.dat, and the art it redrew, images\*.img) with the translation in English's slot, loose in
// translation/ru/. Read over the archive's (MvglArchive::setOverlay) while the Options' Language is it.
#include "viewer/viewer.hpp"

#include <system_error>

namespace {

std::filesystem::path u8(const std::string& s) { return std::filesystem::u8path(s); }
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string utf8(const std::filesystem::path& p) {
    const auto s = p.u8string(); // (std::string, or C++20's std::u8string)
    return std::string(s.begin(), s.end());
}
bool readAll(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(utf8(p).c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
// the game's files in a folder, at most six down: (path, file name in lower case), .dat and .img
template <typename F> void eachFile(const std::string& folder, F&& f) {
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(u8(folder), std::filesystem::directory_options::skip_permission_denied, ec), end;
    for (; it != end && !ec; it.increment(ec)) {
        if (it.depth() >= 6) it.disable_recursion_pending();
        const std::string name = lower(utf8(it->path().filename()));
        if (name.empty() || name[0] == '.') {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec)) continue;
        const size_t dot = name.rfind('.');
        const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
        if (ext == ".dat" || ext == ".img") f(it->path(), name);
    }
}
// a message file's English (its second slot): the strings it points at, at most `limit` bytes
std::string englishOf(const std::vector<uint8_t>& d, size_t limit = 4096) {
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
    const uint32_t records = u32(0), strings = u32(4), n = u32(8);
    if (records != 0x10 || strings <= records || strings > d.size() || n == 0 || (strings - records) % n) return {};
    const size_t stride = (strings - records) / n;
    if (stride < 8) return {};
    std::string out;
    for (uint32_t k = 0; k < n && out.size() < limit; ++k)
        for (size_t c = size_t(strings) + u32(records + k * stride + 4); c < d.size() && d[c]; ++c) out += char(d[c]);
    return out;
}
bool codeLike(const std::string& n) { // ("ru", "pt-BR", "zh-Hans")
    if (n.size() < 2 || n.size() > 7) return false;
    for (size_t i = 0; i < n.size(); ++i)
        if (!(std::isalpha(static_cast<unsigned char>(n[i])) || (n[i] == '-' && i >= 2 && i + 1 < n.size()))) return false;
    return std::islower(static_cast<unsigned char>(n[0])) && std::islower(static_cast<unsigned char>(n[1]));
}

} // namespace

namespace cr2_viewer {

bool Viewer::fanFilesIn(const std::string& folder) {
    bool any = false;
    eachFile(folder, [&](const std::filesystem::path& p, const std::string& name) {
        if (any) return;
        std::vector<uint8_t> d;
        any = name.size() > 4 && name.compare(name.size() - 4, 4, ".img") == 0 ? true : readAll(p, d) && !englishOf(d, 1).empty();
    });
    return any;
}

std::string Viewer::fanLanguageCode(const std::string& folder) {
    size_t cyrillic = 0, letters = 0;
    eachFile(folder, [&](const std::filesystem::path& p, const std::string& name) {
        std::vector<uint8_t> d;
        if (letters >= 2000 || name.compare(name.size() - 4, 4, ".dat") != 0 || !readAll(p, d)) return;
        const std::string e = englishOf(d);
        for (size_t i = 0; i < e.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(e[i]);
            if (c < 0x80) letters += std::isalpha(c) != 0;
            else if ((c & 0xe0) == 0xc0 && i + 1 < e.size()) { // (U+0400..U+04FF: D0 / D1 / D2 / D3 xx)
                ++letters;
                cyrillic += c >= 0xd0 && c <= 0xd3;
            }
        }
    });
    if (letters >= 8 && cyrillic * 2 >= letters) return "ru";
    // else its folder's name, if one is a language's code (translation/pt-BR/...)
    for (std::filesystem::path p = u8(folder); !p.empty() && p.has_filename(); p = p.parent_path())
        if (const std::string n = utf8(p.filename()); codeLike(n)) return n;
    if (std::string f = folder; !f.empty() && (f.back() == '/' || f.back() == '\\')) {
        f.pop_back();
        if (const std::string n = utf8(u8(f).filename()); codeLike(n)) return n;
    }
    return {};
}

bool Viewer::openFanTranslation(const std::string& given, const std::vector<std::string>& places) {
    fan = FanSource();
    auto open = [&](const std::string& folder) {
        std::unordered_map<std::string, std::string> files;
        size_t extra = 0;
        eachFile(folder, [&](const std::filesystem::path& p, const std::string& name) {
            // the archive's name: its folders as they are, else the archive's message\, data\, images\ by its name
            std::string rel = lower(utf8(p.lexically_relative(u8(folder))));
            for (char& c : rel) c = c == '/' ? '\\' : c;
            for (const std::string& n : {rel, "message\\" + name, "data\\" + name, "images\\" + name, name})
                if (archive.contains(n)) return void(files[cr2::MvglArchive::key(n)] = utf8(p));
            ++extra;
        });
        if (files.empty()) return false;
        fan.files = std::move(files), fan.where = folder;
        fan.code = fanLanguageCode(folder);
        fan.name = fan.code.empty() ? std::string() : uitext::languageName(fan.code);
        if (fan.name.empty() || fan.name == fan.code) fan.name = fan.code == "ru" ? "Русский" : tr("Fan translation");
        size_t messages = 0, images = 0;
        for (const auto& [k, f] : fan.files) messages += k.rfind("message\\", 0) == 0, images += k.rfind("images\\", 0) == 0;
        SDL_Log("fan translation: %s (%s) in %s - %zu files (%zu messages, %zu images)%s", fan.name.c_str(), fan.code.empty() ? "?" : fan.code.c_str(),
                folder.c_str(), fan.files.size(), messages, images, extra ? (", " + std::to_string(extra) + " not the game's: left out").c_str() : "");
        return true;
    };
    if (!given.empty()) {
        if (open(given)) return true;
        SDL_Log("fan translation: none in %s", given.c_str());
        return false;
    }
    for (const std::string& place : places) { // (translation/<code>/: the first, by name, with the game's files)
        std::error_code ec;
        std::vector<std::string> dirs;
        for (const auto& e : std::filesystem::directory_iterator(u8(place + "translation"), ec))
            if (const std::string n = utf8(e.path().filename()); !n.empty() && n[0] != '.' && e.is_directory(ec)) dirs.push_back(utf8(e.path()) + "/");
        std::sort(dirs.begin(), dirs.end());
        for (const std::string& d : dirs)
            if (open(d)) return true;
    }
    return false;
}

void Viewer::applyFanText() {
    fanApplied = fanActive();
    archive.setOverlay(fanApplied ? fan.files : std::unordered_map<std::string, std::string>());
    messageFiles.clear(); // (the names, the announcements: read again)
    int redrawn = 0;
    for (auto& [name, t] : textures) // (the art it redrew: the translation's, or the game's again)
        if (t.id && fan.files.count(cr2::MvglArchive::key("images\\" + name + ".img"))) reloadTexture(name, t), ++redrawn;
    SDL_Log("fan translation %s: %d textures replaced", fanApplied ? "on" : "off", redrawn);
}

} // namespace cr2_viewer
