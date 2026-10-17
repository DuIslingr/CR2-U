#include "gfx/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef CR2_HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace cr2 {

namespace {

// UTF-8 to code points (malformed bytes skipped)
std::vector<uint32_t> codePoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + size_t(n) > s.size()) { ++i; continue; }
        uint32_t cp = n == 1 ? c : n == 2 ? c & 0x1f : n == 3 ? c & 0x0f : c & 0x07;
        for (int k = 1; k < n; ++k) cp = cp << 6 | (static_cast<unsigned char>(s[i + size_t(k)]) & 0x3f);
        out.push_back(cp);
        i += size_t(n);
    }
    return out;
}

} // namespace

#ifdef CR2_HAVE_FREETYPE

struct TextRasterizer::Impl {
    FT_Library lib = nullptr;
    FT_Face face = nullptr;
    std::vector<uint8_t> bytes; // (a face from memory reads them while it lives)
    ~Impl() {
        if (face) FT_Done_Face(face);
        if (lib) FT_Done_FreeType(lib);
    }
};

TextRasterizer::TextRasterizer() : impl_(std::make_unique<Impl>()) {
    if (FT_Init_FreeType(&impl_->lib)) impl_->lib = nullptr;
}

TextRasterizer::~TextRasterizer() = default;

bool TextRasterizer::load(const std::string& path, std::string* error) {
    if (!impl_->lib) {
        if (error) *error = "FreeType could not start";
        return false;
    }
    FT_Face f = nullptr;
    if (FT_New_Face(impl_->lib, path.c_str(), 0, &f)) {
        if (error) *error = path + ": not a font FreeType reads";
        return false;
    }
    if (impl_->face) FT_Done_Face(impl_->face);
    impl_->face = f;
    impl_->bytes.clear();
    path_ = path;
    return true;
}

bool TextRasterizer::loadMemory(std::vector<uint8_t> bytes, const std::string& name, std::string* error) {
    if (!impl_->lib) {
        if (error) *error = "FreeType could not start";
        return false;
    }
    FT_Face f = nullptr;
    if (bytes.empty() || FT_New_Memory_Face(impl_->lib, bytes.data(), FT_Long(bytes.size()), 0, &f)) {
        if (error) *error = name + ": not a font FreeType reads";
        return false;
    }
    if (impl_->face) FT_Done_Face(impl_->face);
    impl_->face = f;
    impl_->bytes = std::move(bytes);
    path_ = name;
    return true;
}

bool TextRasterizer::loaded() const { return impl_->face != nullptr; }

Image TextRasterizer::render(const std::string& utf8, int sizePx, int& width, int& height, std::vector<float>* penX) {
    Image img;
    width = height = 0;
    if (penX) penX->clear();
    FT_Face f = impl_->face;
    if (!f || sizePx <= 0) return img;
    FT_Set_Pixel_Sizes(f, 0, FT_UInt(sizePx));
    // Paint's FontMetrics top / bottom: the font's extremes at this size
    const float scale = float(f->size->metrics.y_ppem) / float(f->units_per_EM ? f->units_per_EM : 1000);
    const float top = std::fabs(float(f->bbox.yMax) * scale), bottom = std::fabs(float(f->bbox.yMin) * scale);
    // measureText: the advances (kerning as the font has it)
    const std::vector<uint32_t> cps = codePoints(utf8);
    float advance = 0;
    FT_UInt prev = 0;
    std::vector<std::pair<FT_UInt, float>> glyphs; // glyph, pen x
    for (uint32_t cp : cps) {
        const FT_UInt g = FT_Get_Char_Index(f, cp);
        if (prev && g && FT_HAS_KERNING(f)) {
            FT_Vector k;
            if (!FT_Get_Kerning(f, prev, g, FT_KERNING_DEFAULT, &k)) advance += float(k.x) / 64.0f;
        }
        glyphs.push_back({g, advance});
        if (penX) penX->push_back(advance);
        if (!FT_Load_Glyph(f, g, FT_LOAD_DEFAULT)) advance += float(f->glyph->advance.x) / 64.0f;
        prev = g;
    }
    if (penX) penX->push_back(advance);
    width = int(advance + 0.5f);
    height = int(top + bottom + 0.5f);
    if (width <= 0 || height <= 0) return img;
    img.width = width;
    img.height = height;
    img.rgba.assign(size_t(width) * size_t(height) * 4, 0);
    for (size_t p = 0; p < img.rgba.size(); p += 4) img.rgba[p] = img.rgba[p + 1] = img.rgba[p + 2] = 255;
    const int baseline = sizePx - 1; // (Text2Bitmap: drawText at y = size - 1)
    for (const auto& [g, penX] : glyphs) {
        if (FT_Load_Glyph(f, g, FT_LOAD_DEFAULT) || FT_Render_Glyph(f->glyph, FT_RENDER_MODE_NORMAL)) continue;
        const FT_Bitmap& b = f->glyph->bitmap;
        const int ox = int(std::floor(penX)) + f->glyph->bitmap_left, oy = baseline - f->glyph->bitmap_top;
        for (unsigned y = 0; y < b.rows; ++y)
            for (unsigned x = 0; x < b.width; ++x) {
                const int px = ox + int(x), py = oy + int(y);
                if (px < 0 || py < 0 || px >= width || py >= height) continue;
                uint8_t& a = img.rgba[(size_t(py) * size_t(width) + size_t(px)) * 4 + 3];
                a = std::max<uint8_t>(a, b.buffer[size_t(y) * size_t(std::abs(b.pitch)) + x]);
            }
    }
    return img;
}

#else

struct TextRasterizer::Impl {};
TextRasterizer::TextRasterizer() : impl_(std::make_unique<Impl>()) {}
TextRasterizer::~TextRasterizer() = default;
bool TextRasterizer::load(const std::string& path, std::string* error) {
    if (error) *error = path + ": built without FreeType";
    return false;
}
bool TextRasterizer::loadMemory(std::vector<uint8_t>, const std::string& name, std::string* error) {
    if (error) *error = name + ": built without FreeType";
    return false;
}
bool TextRasterizer::loaded() const { return false; }
Image TextRasterizer::render(const std::string&, int, int& width, int& height, std::vector<float>*) {
    width = height = 0;
    return Image();
}

#endif

std::string TextRasterizer::firstFont(const std::vector<std::string>& candidates) {
    for (const std::string& c : candidates) {
        if (c.empty()) continue;
        if (std::FILE* f = std::fopen(c.c_str(), "rb")) {
            std::fclose(f);
            return c;
        }
    }
    return {};
}

} // namespace cr2
