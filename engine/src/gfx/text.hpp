#pragma once
// The game's text as images (MVGL::Draw::TextRenderer; Android's Text2Bitmap): a line drawn white
// and antialiased in the release's font at a pixel size, into a bitmap the advance's width (plus
// twice the stroke) by the font's top-to-bottom extent, the baseline (size - 1) from its top. The
// fonts are the user's: Android's TypeBank Gothic Pro (assets/00000 Bold for Japanese, 00001
// Regular for English - Text2Bitmap's typefaces 0 and 1 - and the system's for the rest), the
// Vita's CR2/resources/font; the iPad's release uses iOS's own, so a system font stands in.
// FreeType (the system's) draws them; without it there is no text.

#include <memory>
#include <string>
#include <vector>

#include "assets/image.hpp"

namespace cr2 {

class TextRasterizer {
public:
    TextRasterizer();
    ~TextRasterizer();
    TextRasterizer(const TextRasterizer&) = delete;
    TextRasterizer& operator=(const TextRasterizer&) = delete;

    // a font file (OpenType / TrueType, face 0); false: none could be read
    bool load(const std::string& path, std::string* error = nullptr);
    bool loaded() const;
    const std::string& path() const { return path_; }

    // one line of UTF-8 at `sizePx` (rows top first): white, the coverage in alpha. Empty when the
    // text draws nothing. `width` / `height` the bitmap's (Text2Bitmap's measureText / font metrics)
    // `penX` (optional): each code point's left edge in the bitmap, and the end (size + 1)
    Image render(const std::string& utf8, int sizePx, int& width, int& height, std::vector<float>* penX = nullptr);

    // the first of the candidates that loads (empty ones skipped)
    static std::string firstFont(const std::vector<std::string>& candidates);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string path_;
};

} // namespace cr2
