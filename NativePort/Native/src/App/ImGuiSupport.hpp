#pragma once

#include "NativeGame/HudCanvas.hpp"
#include "imgui.h"

#include <string_view>

namespace TrueFlightApp {

inline ImVec4 imguiColor(float r, float g, float b, float a = 1.0f)
{
    return ImVec4(r, g, b, a);
}

void applyTrueFlightImGuiStyle(float contentScale);

class ImGuiHudCanvas {
public:
    explicit ImGuiHudCanvas(ImDrawList* drawList = nullptr);

    void setTransform(float scaleX, float scaleY, float offsetX, float offsetY);
    void resetTransform();
    float textWidth(std::string_view text) const;
    void fillRect(float x, float y, float w, float h, NativeGame::HudColor color);
    void strokeRect(float x, float y, float w, float h, NativeGame::HudColor color);
    void line(float x0, float y0, float x1, float y1, NativeGame::HudColor color);
    void point(float x, float y, NativeGame::HudColor color);
    void text(float x, float y, std::string_view label, NativeGame::HudColor color);

private:
    static ImVec2 rectMin(const ImVec2& a, const ImVec2& b);
    static ImVec2 rectMax(const ImVec2& a, const ImVec2& b);
    ImVec2 transformPoint(float x, float y) const;
    float uniformScale() const;
    float lineThickness() const;
    float fontSize() const;

    ImDrawList* drawList_ = nullptr;
    ImFont* font_ = nullptr;
    float baseFontSize_ = 13.0f;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    float offsetX_ = 0.0f;
    float offsetY_ = 0.0f;
};

}  // namespace TrueFlightApp
