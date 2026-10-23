#include "gfx/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifdef CR2_HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
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
    std::vector<FT_Face> fallbacks;
    std::vector<uint8_t> bytes; // (a face from memory reads them while it lives)
    ~Impl() {
        if (face) FT_Done_Face(face);
        for (FT_Face f : fallbacks) FT_Done_Face(f);
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

bool TextRasterizer::addFallback(const std::string& path, std::string* error) {
    FT_Face f = nullptr;
    if (!impl_->lib || FT_New_Face(impl_->lib, path.c_str(), 0, &f)) {
        if (error) *error = path + ": not a font FreeType reads";
        return false;
    }
    impl_->fallbacks.push_back(f);
    return true;
}

Image TextRasterizer::render(const std::string& utf8, int sizePx, int& width, int& height, std::vector<float>* penX, int border) {
    Image img;
    width = height = 0;
    if (penX) penX->clear();
    FT_Face f = impl_->face;
    if (!f || sizePx <= 0) return img;
    FT_Set_Pixel_Sizes(f, 0, FT_UInt(sizePx));
    for (FT_Face b : impl_->fallbacks) FT_Set_Pixel_Sizes(b, 0, FT_UInt(sizePx));
    // Paint's FontMetrics top / bottom: the font's extremes at this size
    const float scale = float(f->size->metrics.y_ppem) / float(f->units_per_EM ? f->units_per_EM : 1000);
    const float top = std::fabs(float(f->bbox.yMax) * scale), bottom = std::fabs(float(f->bbox.yMin) * scale);
    // measureText: the advances (kerning as the font has it)
    const std::vector<uint32_t> cps = codePoints(utf8);
    float advance = 0;
    FT_UInt prev = 0;
    FT_Face prevFace = nullptr;
    struct Glyph {
        FT_Face face;
        FT_UInt index;
        float pen;
    };
    std::vector<Glyph> glyphs;
    for (uint32_t cp : cps) {
        if (cp < 0x20) { // (a control character - the real line break after a message's "\n" -: nothing drawn)
            if (penX) penX->push_back(advance);
            continue;
        }
        FT_Face gf = f;
        FT_UInt g = FT_Get_Char_Index(f, cp);
        for (size_t k = 0; !g && k < impl_->fallbacks.size(); ++k) // (the first font without it: a fallback's)
            if (FT_UInt h = FT_Get_Char_Index(impl_->fallbacks[k], cp)) gf = impl_->fallbacks[k], g = h;
        if (!g) gf = f; // (none has it: the first's missing glyph)
        if (prev && g && gf == prevFace && FT_HAS_KERNING(gf)) {
            FT_Vector k;
            if (!FT_Get_Kerning(gf, prev, g, FT_KERNING_DEFAULT, &k)) advance += float(k.x) / 64.0f;
        }
        glyphs.push_back({gf, g, advance});
        if (penX) penX->push_back(advance);
        if (!FT_Load_Glyph(gf, g, FT_LOAD_DEFAULT)) advance += float(gf->glyph->advance.x) / 64.0f;
        prev = g, prevFace = gf;
    }
    if (penX) penX->push_back(advance);
    border = std::max(border, 0);
    width = int(advance + 0.5f);
    height = int(top + bottom + 0.5f);
    if (width <= 0 || height <= 0) return img;
    width += 2 * border;
    img.width = width;
    img.height = height;
    img.rgba.assign(size_t(width) * size_t(height) * 4, 0);
    for (size_t p = 0; p < img.rgba.size(); p += 4) img.rgba[p] = img.rgba[p + 1] = img.rgba[p + 2] = 255;
    const int baseline = sizePx - 1; // (Text2Bitmap: drawText at y = size - 1)
    if (border > 0) { // (the shadow: Paint's stroke `border` wide - half each side - over the fill, at x = border)
        std::vector<uint8_t> shadow(size_t(width) * size_t(height), 0);
        FT_Stroker stroker = nullptr;
        if (!FT_Stroker_New(impl_->lib, &stroker)) {
            FT_Stroker_Set(stroker, FT_Fixed(border * 32), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
            for (const Glyph& gl : glyphs) {
                FT_Face gf = gl.face;
                FT_Glyph g = nullptr;
                if (FT_Load_Glyph(gf, gl.index, FT_LOAD_DEFAULT) || FT_Get_Glyph(gf->glyph, &g)) continue;
                if (g->format == FT_GLYPH_FORMAT_OUTLINE && !FT_Glyph_StrokeBorder(&g, stroker, 0, 1) &&
                    !FT_Glyph_To_Bitmap(&g, FT_RENDER_MODE_NORMAL, nullptr, 1)) {
                    const FT_BitmapGlyph bg = reinterpret_cast<FT_BitmapGlyph>(g);
                    const FT_Bitmap& b = bg->bitmap;
                    const int ox = border + int(std::floor(gl.pen)) + bg->left, oy = baseline - bg->top;
                    for (unsigned y = 0; y < b.rows; ++y)
                        for (unsigned x = 0; x < b.width; ++x) {
                            const int px = ox + int(x), py = oy + int(y);
                            if (px < 0 || py < 0 || px >= width || py >= height) continue;
                            uint8_t& a = shadow[size_t(py) * size_t(width) + size_t(px)];
                            a = std::max<uint8_t>(a, b.buffer[size_t(y) * size_t(std::abs(b.pitch)) + x]);
                        }
                }
                FT_Done_Glyph(g);
            }
            FT_Stroker_Done(stroker);
        }
        for (size_t p = 0; p < shadow.size(); ++p) img.rgba[p * 4] = img.rgba[p * 4 + 1] = img.rgba[p * 4 + 2] = 0, img.rgba[p * 4 + 3] = shadow[p];
    }
    for (const Glyph& gl : glyphs) {
        FT_Face gf = gl.face;
        if (FT_Load_Glyph(gf, gl.index, FT_LOAD_DEFAULT) || FT_Render_Glyph(gf->glyph, FT_RENDER_MODE_NORMAL)) continue;
        const FT_Bitmap& b = gf->glyph->bitmap;
        const int ox = int(std::floor(gl.pen)) + gf->glyph->bitmap_left, oy = baseline - gf->glyph->bitmap_top;
        for (unsigned y = 0; y < b.rows; ++y)
            for (unsigned x = 0; x < b.width; ++x) {
                const int px = ox + int(x), py = oy + int(y);
                if (px < 0 || py < 0 || px >= width || py >= height) continue;
                uint8_t* q = &img.rgba[(size_t(py) * size_t(width) + size_t(px)) * 4];
                const uint8_t c = b.buffer[size_t(y) * size_t(std::abs(b.pitch)) + x];
                if (border > 0 && c) q[0] = q[1] = q[2] = std::max(q[0], c); // (over the shadow: grey by its coverage)
                q[3] = std::max(q[3], c);
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
bool TextRasterizer::addFallback(const std::string& path, std::string* error) {
    if (error) *error = path + ": built without FreeType";
    return false;
}
Image TextRasterizer::render(const std::string&, int, int& width, int& height, std::vector<float>*, int) {
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
