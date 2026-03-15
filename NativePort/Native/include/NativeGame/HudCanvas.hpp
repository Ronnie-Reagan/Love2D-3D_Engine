#pragma once

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include "SDL_render_debug_font.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NativeGame {

#ifdef _WIN32
namespace detail {

struct HudGlyphBitmap {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> alpha {};
};

struct HudWindowsFontCache {
    HDC deviceContext = nullptr;
    HFONT font = nullptr;
    int glyphAdvance = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    int lineHeight = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    bool available = false;
    std::unordered_map<unsigned int, HudGlyphBitmap> glyphs {};

    HudWindowsFontCache()
    {
        initialize();
    }

    ~HudWindowsFontCache()
    {
        if (font != nullptr) {
            DeleteObject(font);
        }
        if (deviceContext != nullptr) {
            DeleteDC(deviceContext);
        }
    }

    void initialize()
    {
        deviceContext = CreateCompatibleDC(nullptr);
        if (deviceContext == nullptr) {
            return;
        }

        font = CreateFontW(
            -12,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_TT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,
            FIXED_PITCH | FF_MODERN,
            L"Consolas");
        if (font == nullptr) {
            return;
        }

        (void)SelectObject(deviceContext, font);
        SetBkMode(deviceContext, TRANSPARENT);
        SetTextColor(deviceContext, RGB(255, 255, 255));
        SetTextAlign(deviceContext, TA_LEFT | TA_TOP | TA_NOUPDATECP);

        wchar_t faceName[LF_FACESIZE] {};
        if (GetTextFaceW(deviceContext, LF_FACESIZE, faceName) <= 0 || lstrcmpiW(faceName, L"Consolas") != 0) {
            return;
        }

        TEXTMETRICW metrics {};
        SIZE sampleSize {};
        const wchar_t sampleGlyph = L'0';
        if (!GetTextMetricsW(deviceContext, &metrics) ||
            !GetTextExtentPoint32W(deviceContext, &sampleGlyph, 1, &sampleSize)) {
            return;
        }

        glyphAdvance = std::max(1, static_cast<int>(sampleSize.cx));
        lineHeight = std::max(1, static_cast<int>(metrics.tmHeight));
        available = true;
    }

    const HudGlyphBitmap& glyph(unsigned int codepoint)
    {
        const auto existing = glyphs.find(codepoint);
        if (existing != glyphs.end()) {
            return existing->second;
        }

        HudGlyphBitmap bitmap {};
        if (available) {
            const int width = std::max(1, glyphAdvance);
            const int height = std::max(1, lineHeight);

            BITMAPINFO bitmapInfo {};
            bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth = width;
            bitmapInfo.bmiHeader.biHeight = -height;
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;

            void* bits = nullptr;
            HBITMAP dib = CreateDIBSection(deviceContext, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (dib != nullptr && bits != nullptr) {
                HGDIOBJ previousBitmap = SelectObject(deviceContext, dib);
                std::fill_n(static_cast<std::uint32_t*>(bits), static_cast<std::size_t>(width * height), 0u);

                const wchar_t glyphChar = static_cast<wchar_t>(codepoint);
                (void)TextOutW(deviceContext, 0, 0, &glyphChar, 1);

                bitmap.width = width;
                bitmap.height = height;
                bitmap.alpha.resize(static_cast<std::size_t>(width * height), 0);
                const auto* pixelBytes = static_cast<const std::uint8_t*>(bits);
                for (int index = 0; index < width * height; ++index) {
                    const std::size_t byteOffset = static_cast<std::size_t>(index) * 4u;
                    bitmap.alpha[static_cast<std::size_t>(index)] = std::max({
                        pixelBytes[byteOffset + 0],
                        pixelBytes[byteOffset + 1],
                        pixelBytes[byteOffset + 2]
                    });
                }

                (void)SelectObject(deviceContext, previousBitmap);
                DeleteObject(dib);
            }
        }

        const auto [inserted, _] = glyphs.emplace(codepoint, std::move(bitmap));
        return inserted->second;
    }
};

inline HudWindowsFontCache& hudWindowsFontCache()
{
    static HudWindowsFontCache cache;
    return cache;
}

} // namespace detail
#endif

struct HudColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

class HudCanvas {
public:
    HudCanvas() = default;

    HudCanvas(int width, int height)
    {
        resize(width, height);
    }

    void resize(int width, int height)
    {
        width_ = std::max(1, width);
        height_ = std::max(1, height);
        pixels_.assign(static_cast<std::size_t>(width_ * height_ * 4), 0);
    }

    void clear(HudColor color = {})
    {
        for (std::size_t i = 0; i < pixels_.size(); i += 4) {
            pixels_[i + 0] = color.r;
            pixels_[i + 1] = color.g;
            pixels_[i + 2] = color.b;
            pixels_[i + 3] = color.a;
        }
    }

    void setTransform(float scaleX, float scaleY, float offsetX = 0.0f, float offsetY = 0.0f)
    {
        scaleX_ = std::max(0.01f, scaleX);
        scaleY_ = std::max(0.01f, scaleY);
        offsetX_ = offsetX;
        offsetY_ = offsetY;
    }

    void resetTransform()
    {
        scaleX_ = 1.0f;
        scaleY_ = 1.0f;
        offsetX_ = 0.0f;
        offsetY_ = 0.0f;
    }

    void point(float x, float y, HudColor color)
    {
        blendPixel(
            static_cast<int>(std::lround(transformX(x))),
            static_cast<int>(std::lround(transformY(y))),
            color);
    }

    void line(float x0, float y0, float x1, float y1, HudColor color)
    {
        lineRaw(
            transformX(x0),
            transformY(y0),
            transformX(x1),
            transformY(y1),
            color);
    }

    void fillRect(float x, float y, float w, float h, HudColor color)
    {
        fillRectRaw(
            transformX(x),
            transformY(y),
            w * scaleX_,
            h * scaleY_,
            color);
    }

    void strokeRect(float x, float y, float w, float h, HudColor color)
    {
        const float x0 = transformX(x);
        const float y0 = transformY(y);
        const float x1 = transformX(x + w);
        const float y1 = transformY(y + h);
        lineRaw(x0, y0, x1, y0, color);
        lineRaw(x1, y0, x1, y1, color);
        lineRaw(x1, y1, x0, y1, color);
        lineRaw(x0, y1, x0, y0, color);
    }

    void text(float x, float y, std::string_view textValue, HudColor color)
    {
        float cursorX = transformX(x);
        float cursorY = transformY(y);
        const float lineStartX = cursorX;
        const float glyphScaleX = scaleX_;
        const float glyphScaleY = scaleY_;
        const float glyphAdvance = static_cast<float>(baseGlyphAdvance()) * glyphScaleX;
        const float lineAdvance = static_cast<float>(baseLineHeight()) * glyphScaleY;

        for (unsigned char ch : textValue) {
            if (ch == '\n') {
                cursorX = lineStartX;
                cursorY += lineAdvance;
                continue;
            }

            if (!drawSystemGlyph(cursorX, cursorY, ch, color, glyphScaleX, glyphScaleY)) {
                drawDebugGlyph(cursorX, cursorY, ch, color, glyphScaleX, glyphScaleY);
            }
            cursorX += glyphAdvance;
        }
    }

    [[nodiscard]] float textWidth(std::string_view textValue) const
    {
        float lineWidth = 0.0f;
        float maxWidth = 0.0f;
        const float glyphAdvance = static_cast<float>(baseGlyphAdvance());
        for (unsigned char ch : textValue) {
            if (ch == '\n') {
                maxWidth = std::max(maxWidth, lineWidth);
                lineWidth = 0.0f;
                continue;
            }
            lineWidth += glyphAdvance;
        }
        return std::max(maxWidth, lineWidth);
    }

    [[nodiscard]] float textLineHeight() const
    {
        return static_cast<float>(baseLineHeight());
    }

    [[nodiscard]] int width() const
    {
        return width_;
    }

    [[nodiscard]] int height() const
    {
        return height_;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& pixels() const
    {
        return pixels_;
    }

private:
    [[nodiscard]] int baseGlyphAdvance() const
    {
#ifdef _WIN32
        const detail::HudWindowsFontCache& fontCache = detail::hudWindowsFontCache();
        if (fontCache.available) {
            return fontCache.glyphAdvance;
        }
#endif
        return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    }

    [[nodiscard]] int baseLineHeight() const
    {
#ifdef _WIN32
        const detail::HudWindowsFontCache& fontCache = detail::hudWindowsFontCache();
        if (fontCache.available) {
            return fontCache.lineHeight;
        }
#endif
        return SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    }

    [[nodiscard]] float transformX(float x) const
    {
        return offsetX_ + (x * scaleX_);
    }

    [[nodiscard]] float transformY(float y) const
    {
        return offsetY_ + (y * scaleY_);
    }

    void lineRaw(float x0, float y0, float x1, float y1, HudColor color)
    {
        int xStart = static_cast<int>(std::lround(x0));
        int yStart = static_cast<int>(std::lround(y0));
        const int xEnd = static_cast<int>(std::lround(x1));
        const int yEnd = static_cast<int>(std::lround(y1));

        const int dx = std::abs(xEnd - xStart);
        const int sx = xStart < xEnd ? 1 : -1;
        const int dy = -std::abs(yEnd - yStart);
        const int sy = yStart < yEnd ? 1 : -1;
        int error = dx + dy;

        while (true) {
            blendPixel(xStart, yStart, color);
            if (xStart == xEnd && yStart == yEnd) {
                break;
            }
            const int twiceError = error * 2;
            if (twiceError >= dy) {
                error += dy;
                xStart += sx;
            }
            if (twiceError <= dx) {
                error += dx;
                yStart += sy;
            }
        }
    }

    void fillRectRaw(float x, float y, float w, float h, HudColor color)
    {
        const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, width_);
        const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, height_);
        const int x1 = std::clamp(static_cast<int>(std::ceil(x + w)), 0, width_);
        const int y1 = std::clamp(static_cast<int>(std::ceil(y + h)), 0, height_);

        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                blendPixel(px, py, color);
            }
        }
    }
    static int glyphIndex(unsigned int codepoint)
    {
        if (codepoint <= 32u || (codepoint >= 127u && codepoint <= 160u)) {
            return -1;
        }
        if (codepoint >= SDL_DEBUG_FONT_NUM_GLYPHS) {
            return SDL_DEBUG_FONT_NUM_GLYPHS - 1;
        }
        if (codepoint < 127u) {
            return static_cast<int>(codepoint) - 33;
        }
        return static_cast<int>(codepoint) - 67;
    }

    bool drawSystemGlyph(float x, float y, unsigned char codepoint, HudColor color, float scaleX, float scaleY)
    {
#ifdef _WIN32
        const detail::HudWindowsFontCache& fontCache = detail::hudWindowsFontCache();
        if (!fontCache.available) {
            return false;
        }

        const detail::HudGlyphBitmap& glyph = detail::hudWindowsFontCache().glyph(codepoint);
        if (glyph.width <= 0 || glyph.height <= 0 || glyph.alpha.empty()) {
            return true;
        }

        for (int row = 0; row < glyph.height; ++row) {
            for (int col = 0; col < glyph.width; ++col) {
                const std::uint8_t glyphAlpha = glyph.alpha[static_cast<std::size_t>((row * glyph.width) + col)];
                if (glyphAlpha == 0) {
                    continue;
                }
                HudColor blended = color;
                blended.a = static_cast<std::uint8_t>((static_cast<unsigned int>(color.a) * glyphAlpha + 127u) / 255u);
                fillRectRaw(
                    x + (static_cast<float>(col) * scaleX),
                    y + (static_cast<float>(row) * scaleY),
                    scaleX,
                    scaleY,
                    blended);
            }
        }
        return true;
#else
        (void)x;
        (void)y;
        (void)codepoint;
        (void)color;
        (void)scaleX;
        (void)scaleY;
        return false;
#endif
    }

    void drawDebugGlyph(float x, float y, unsigned char codepoint, HudColor color, float scaleX, float scaleY)
    {
        const int glyph = glyphIndex(codepoint);
        if (glyph < 0) {
            return;
        }

        const Uint8* glyphRows = SDL_RenderDebugTextFontData + (glyph * 8);
        for (int row = 0; row < 8; ++row) {
            const Uint8 bits = glyphRows[row];
            for (int col = 0; col < 8; ++col) {
                if ((bits & (1u << col)) == 0) {
                    continue;
                }
                fillRectRaw(
                    x + (static_cast<float>(col) * scaleX),
                    y + (static_cast<float>(row) * scaleY),
                    scaleX,
                    scaleY,
                    color);
            }
        }
    }

    void blendPixel(int x, int y, HudColor color)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_ || color.a == 0) {
            return;
        }

        const std::size_t index = static_cast<std::size_t>(((y * width_) + x) * 4);
        if (color.a == 255) {
            pixels_[index + 0] = color.r;
            pixels_[index + 1] = color.g;
            pixels_[index + 2] = color.b;
            pixels_[index + 3] = 255;
            return;
        }

        const std::uint32_t srcA = color.a;
        const std::uint32_t dstA = pixels_[index + 3];
        const std::uint32_t invSrcA = 255u - srcA;
        const std::uint32_t outA = srcA + ((dstA * invSrcA) / 255u);
        if (outA == 0u) {
            pixels_[index + 0] = 0;
            pixels_[index + 1] = 0;
            pixels_[index + 2] = 0;
            pixels_[index + 3] = 0;
            return;
        }

        const auto blendChannel = [&](std::uint8_t src, std::uint8_t dst) -> std::uint8_t {
            const std::uint32_t srcPremul = static_cast<std::uint32_t>(src) * srcA;
            const std::uint32_t dstPremul = static_cast<std::uint32_t>(dst) * dstA;
            const std::uint32_t outPremul = srcPremul + ((dstPremul * invSrcA) / 255u);
            return static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, (outPremul + (outA / 2u)) / outA));
        };

        pixels_[index + 0] = blendChannel(color.r, pixels_[index + 0]);
        pixels_[index + 1] = blendChannel(color.g, pixels_[index + 1]);
        pixels_[index + 2] = blendChannel(color.b, pixels_[index + 2]);
        pixels_[index + 3] = static_cast<std::uint8_t>(outA);
    }

    int width_ = 1;
    int height_ = 1;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;
    std::vector<std::uint8_t> pixels_;
};

}  // namespace NativeGame
