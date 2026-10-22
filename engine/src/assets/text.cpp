#include "assets/text.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace cr2 {

namespace {

uint32_t u32(const std::vector<uint8_t>& d, size_t at) {
    return at + 4 <= d.size() ? uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24 : 0;
}

// the speaker tags, in the game's order (EvtWindowMessage::GetNameTagNumber's table)
const char* const kTags[] = {"d001", "d002", "d003", "d004", "d005", "d006", "d007", "d101", "d102", "d103", "d301",
                             "d303", "d305", "d307", "d309", "d310", "d312", "d407", "d999", "d510", "d509"};

} // namespace

bool TextData::parse(const std::vector<uint8_t>& data) {
    *this = TextData();
    if (data.size() < 0x10) return false;
    records_ = u32(data, 0), strings_ = u32(data, 4), count_ = int(u32(data, 8));
    if (records_ > data.size() || strings_ > data.size() || size_t(count_) * 0x20 > data.size() - records_) return *this = TextData(), false;
    if (data.size() >= 8 && std::memcmp(&data[data.size() - 8], "voic", 4) == 0) {
        const uint32_t n = u32(data, data.size() - 4);
        if (size_t(n) * 4 + 8 <= data.size())
            for (uint32_t k = 0; k < n; ++k) voices_.push_back(int(u32(data, data.size() - 8 - 4 * n + 4 * k)));
    }
    data_ = data;
    return true;
}

std::string TextData::message(int n, int language) const {
    if (n < 1 || n > count_ || language < 0 || language > 7) return {};
    const size_t at = strings_ + u32(data_, records_ + 0x20 * size_t(n - 1) + 4 * size_t(language));
    size_t end = at;
    while (end < data_.size() && data_[end]) ++end;
    return at < data_.size() ? std::string(data_.begin() + long(at), data_.begin() + long(end)) : std::string();
}

int TextData::voice(int n) const { return n >= 1 && size_t(n) <= voices_.size() ? voices_[size_t(n - 1)] : -1; }

ParsedMessage parseMessage(const std::string& t) {
    ParsedMessage out;
    TextPage page;
    TextLine line;
    const auto typed = [&] { // (the page's code points so far)
        int n = 0;
        for (unsigned char c : line.text) n += (c & 0xc0) != 0x80;
        for (const TextLine& l : page.lines)
            for (unsigned char c : l.text) n += (c & 0xc0) != 0x80;
        return n;
    };
    const auto endPage = [&] {
        page.lines.push_back(line), line = TextLine();
        const int n = typed();
        while (!page.stops.empty() && page.stops.back().wait == 0 && page.stops.back().at >= n) page.stops.pop_back();
        out.pages.push_back(page), page = TextPage();
    };
    for (size_t i = 0; i < t.size();) {
        const char ch = t[i];
        if (ch == '\\' && i + 1 < t.size()) {
            const char c = t[i + 1];
            i += 2;
            if (c == 'n') {
                page.lines.push_back(line), line = TextLine();
            } else if (c == 'f') {
                endPage();
            } else if (c == 'c') {
                line.centred = true;
            } else if (c == '#' && i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
                line.colour = t[i++] - '0';
            } else if (c == 'd') {
                size_t k = i;
                while (k < t.size() && k < i + 3 && std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
                const std::string tag = "d" + t.substr(i, k - i);
                for (int s = 0; s < int(sizeof kTags / sizeof *kTags); ++s)
                    if (tag == kTags[s]) out.speaker = s;
                i = k;
            } else if (c == 'w') {
                size_t k = i;
                while (k < t.size() && std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
                if (const int n = k > i ? std::atoi(t.substr(i, k - i).c_str()) : 0; n > 0) page.stops.push_back({typed(), n});
                i = k;
            } else if (c == '@') {
                page.stops.push_back({typed(), 0});
            } else if (c == 'm') {
                out.noSkip = true;
            } else if (c == '*') {
                out.ends = true;
                break;
            } else if (c == '\\') {
                line.text += '\\';
            }
            continue;
        }
        if (ch != '\r' && ch != '\n') line.text += ch;
        ++i;
    }
    if (!line.text.empty() || !page.lines.empty()) endPage();
    return out;
}

void textColour(int colour, float rgb[3]) {
    static const float kColours[8][3] = {{1, 1, 1}, {1, 0, 0}, {0, 0.8f, 1}, {0, 1, 0}, {1, 0.8f, 0}, {1, 0, 1}, {1, 1, 1}, {0, 0, 0}};
    const float* c = kColours[colour >= 0 && colour < 8 ? colour : 0];
    rgb[0] = c[0], rgb[1] = c[1], rgb[2] = c[2];
}

} // namespace cr2
