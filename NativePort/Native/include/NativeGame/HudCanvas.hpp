#pragma once

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include "SDL_render_debug_font.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace NativeGame {

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

    void point(float x, float y, HudColor color)
    {
        blendPixel(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)), color);
    }

    void line(float x0, float y0, float x1, float y1, HudColor color)
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

    void fillRect(float x, float y, float w, float h, HudColor color)
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

    void strokeRect(float x, float y, float w, float h, HudColor color)
    {
        const float x2 = x + w;
        const float y2 = y + h;
        line(x, y, x2, y, color);
        line(x2, y, x2, y2, color);
        line(x2, y2, x, y2, color);
        line(x, y2, x, y, color);
    }

    void text(float x, float y, std::string_view textValue, HudColor color)
    {
        int cursorX = static_cast<int>(std::lround(x));
        const int baselineY = static_cast<int>(std::lround(y));

        for (unsigned char ch : textValue) {
            if (ch == '\n') {
                cursorX = static_cast<int>(std::lround(x));
                continue;
            }

            drawGlyph(cursorX, baselineY, ch, color);
            cursorX += SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
        }
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

    void drawGlyph(int x, int y, unsigned char codepoint, HudColor color)
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
                blendPixel(x + col, y + row, color);
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
    std::vector<std::uint8_t> pixels_;
};

}  // namespace NativeGame
