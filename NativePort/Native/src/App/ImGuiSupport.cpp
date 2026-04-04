#include "App/ImGuiSupport.hpp"

#include <algorithm>
#include <cmath>

namespace TrueFlightApp {

namespace {

ImU32 imguiPackedColor(const NativeGame::HudColor& color)
{
    return IM_COL32(color.r, color.g, color.b, color.a);
}

}  // namespace

void applyTrueFlightImGuiStyle(float contentScale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
    style.WindowRounding = 8.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.ScaleAllSizes(std::max(0.75f, contentScale));

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = imguiColor(0.92f, 0.95f, 0.98f);
    colors[ImGuiCol_TextDisabled] = imguiColor(0.56f, 0.62f, 0.70f);
    colors[ImGuiCol_WindowBg] = imguiColor(0.05f, 0.08f, 0.12f, 0.94f);
    colors[ImGuiCol_ChildBg] = imguiColor(0.07f, 0.10f, 0.14f, 0.92f);
    colors[ImGuiCol_PopupBg] = imguiColor(0.06f, 0.09f, 0.13f, 0.98f);
    colors[ImGuiCol_Border] = imguiColor(0.31f, 0.42f, 0.56f, 0.85f);
    colors[ImGuiCol_FrameBg] = imguiColor(0.11f, 0.16f, 0.22f, 0.92f);
    colors[ImGuiCol_FrameBgHovered] = imguiColor(0.16f, 0.23f, 0.31f, 0.95f);
    colors[ImGuiCol_FrameBgActive] = imguiColor(0.20f, 0.29f, 0.38f, 1.00f);
    colors[ImGuiCol_TitleBg] = imguiColor(0.05f, 0.08f, 0.12f, 0.98f);
    colors[ImGuiCol_TitleBgActive] = imguiColor(0.09f, 0.14f, 0.20f, 0.98f);
    colors[ImGuiCol_MenuBarBg] = imguiColor(0.07f, 0.11f, 0.16f, 0.96f);
    colors[ImGuiCol_ScrollbarBg] = imguiColor(0.06f, 0.08f, 0.11f, 0.60f);
    colors[ImGuiCol_ScrollbarGrab] = imguiColor(0.21f, 0.30f, 0.40f, 0.95f);
    colors[ImGuiCol_ScrollbarGrabHovered] = imguiColor(0.28f, 0.39f, 0.51f, 0.95f);
    colors[ImGuiCol_ScrollbarGrabActive] = imguiColor(0.36f, 0.50f, 0.64f, 1.00f);
    colors[ImGuiCol_CheckMark] = imguiColor(0.60f, 0.90f, 0.76f, 1.00f);
    colors[ImGuiCol_SliderGrab] = imguiColor(0.42f, 0.70f, 0.96f, 0.95f);
    colors[ImGuiCol_SliderGrabActive] = imguiColor(0.63f, 0.86f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = imguiColor(0.16f, 0.24f, 0.33f, 0.96f);
    colors[ImGuiCol_ButtonHovered] = imguiColor(0.24f, 0.35f, 0.47f, 1.00f);
    colors[ImGuiCol_ButtonActive] = imguiColor(0.30f, 0.44f, 0.58f, 1.00f);
    colors[ImGuiCol_Header] = imguiColor(0.13f, 0.20f, 0.28f, 0.94f);
    colors[ImGuiCol_HeaderHovered] = imguiColor(0.20f, 0.31f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive] = imguiColor(0.26f, 0.39f, 0.51f, 1.00f);
    colors[ImGuiCol_Separator] = imguiColor(0.31f, 0.42f, 0.56f, 0.80f);
    colors[ImGuiCol_ResizeGrip] = imguiColor(0.38f, 0.56f, 0.73f, 0.45f);
    colors[ImGuiCol_ResizeGripHovered] = imguiColor(0.47f, 0.69f, 0.88f, 0.78f);
    colors[ImGuiCol_ResizeGripActive] = imguiColor(0.58f, 0.82f, 1.00f, 0.95f);
    colors[ImGuiCol_Tab] = imguiColor(0.12f, 0.18f, 0.25f, 0.96f);
    colors[ImGuiCol_TabHovered] = imguiColor(0.23f, 0.34f, 0.46f, 1.00f);
    colors[ImGuiCol_TabActive] = imguiColor(0.18f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_TabDimmed] = imguiColor(0.10f, 0.14f, 0.20f, 0.92f);
    colors[ImGuiCol_TabDimmedSelected] = imguiColor(0.16f, 0.24f, 0.33f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = imguiColor(0.11f, 0.16f, 0.23f, 0.98f);
    colors[ImGuiCol_TableBorderStrong] = imguiColor(0.24f, 0.34f, 0.46f, 0.95f);
    colors[ImGuiCol_TableBorderLight] = imguiColor(0.15f, 0.21f, 0.29f, 0.75f);
    colors[ImGuiCol_TableRowBg] = imguiColor(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = imguiColor(0.09f, 0.13f, 0.19f, 0.30f);
    colors[ImGuiCol_TextSelectedBg] = imguiColor(0.24f, 0.44f, 0.66f, 0.35f);
    colors[ImGuiCol_DockingPreview] = imguiColor(0.42f, 0.70f, 0.96f, 0.42f);
    colors[ImGuiCol_DockingEmptyBg] = imguiColor(0.05f, 0.08f, 0.12f, 1.00f);
    colors[ImGuiCol_NavHighlight] = imguiColor(0.63f, 0.86f, 1.00f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg] = imguiColor(0.01f, 0.02f, 0.03f, 0.55f);
}

ImGuiHudCanvas::ImGuiHudCanvas(ImDrawList* drawList)
    : drawList_(drawList != nullptr ? drawList : ImGui::GetBackgroundDrawList())
{
    ImGuiIO& io = ImGui::GetIO();
    font_ = io.FontDefault;
    if (font_ == nullptr && io.Fonts != nullptr && !io.Fonts->Fonts.empty())
    {
        font_ = io.Fonts->Fonts.front();
    }
    baseFontSize_ = 13.0f;
    if (font_ != nullptr && font_->LegacySize > 1.0f)
    {
        baseFontSize_ = std::max(11.0f, font_->LegacySize * 0.72f);
    }
}

void ImGuiHudCanvas::setTransform(float scaleX, float scaleY, float offsetX, float offsetY)
{
    scaleX_ = scaleX;
    scaleY_ = scaleY;
    offsetX_ = offsetX;
    offsetY_ = offsetY;
}

void ImGuiHudCanvas::resetTransform()
{
    scaleX_ = 1.0f;
    scaleY_ = 1.0f;
    offsetX_ = 0.0f;
    offsetY_ = 0.0f;
}

float ImGuiHudCanvas::textWidth(std::string_view text) const
{
    if (font_ == nullptr || text.empty())
    {
        return 0.0f;
    }
    return font_->CalcTextSizeA(baseFontSize_, 1000000.0f, 0.0f, text.data(), text.data() + text.size()).x;
}

void ImGuiHudCanvas::fillRect(float x, float y, float w, float h, NativeGame::HudColor color)
{
    const ImVec2 a = transformPoint(x, y);
    const ImVec2 b = transformPoint(x + w, y + h);
    drawList_->AddRectFilled(rectMin(a, b), rectMax(a, b), imguiPackedColor(color));
}

void ImGuiHudCanvas::strokeRect(float x, float y, float w, float h, NativeGame::HudColor color)
{
    const ImVec2 a = transformPoint(x, y);
    const ImVec2 b = transformPoint(x + w, y + h);
    drawList_->AddRect(rectMin(a, b), rectMax(a, b), imguiPackedColor(color), 0.0f, 0, lineThickness());
}

void ImGuiHudCanvas::line(float x0, float y0, float x1, float y1, NativeGame::HudColor color)
{
    drawList_->AddLine(transformPoint(x0, y0), transformPoint(x1, y1), imguiPackedColor(color), lineThickness());
}

void ImGuiHudCanvas::point(float x, float y, NativeGame::HudColor color)
{
    drawList_->AddCircleFilled(transformPoint(x, y), std::max(1.0f, lineThickness() * 0.8f), imguiPackedColor(color), 12);
}

void ImGuiHudCanvas::text(float x, float y, std::string_view label, NativeGame::HudColor color)
{
    if (font_ == nullptr || label.empty())
    {
        return;
    }

    drawList_->AddText(
        font_,
        fontSize(),
        transformPoint(x, y),
        imguiPackedColor(color),
        label.data(),
        label.data() + label.size());
}

ImVec2 ImGuiHudCanvas::rectMin(const ImVec2& a, const ImVec2& b)
{
    return {std::min(a.x, b.x), std::min(a.y, b.y)};
}

ImVec2 ImGuiHudCanvas::rectMax(const ImVec2& a, const ImVec2& b)
{
    return {std::max(a.x, b.x), std::max(a.y, b.y)};
}

ImVec2 ImGuiHudCanvas::transformPoint(float x, float y) const
{
    return {
        offsetX_ + (x * scaleX_),
        offsetY_ + (y * scaleY_)};
}

float ImGuiHudCanvas::uniformScale() const
{
    return std::max(0.01f, (std::fabs(scaleX_) + std::fabs(scaleY_)) * 0.5f);
}

float ImGuiHudCanvas::lineThickness() const
{
    return std::max(1.0f, uniformScale());
}

float ImGuiHudCanvas::fontSize() const
{
    return std::max(8.0f, baseFontSize_ * uniformScale());
}

}  // namespace TrueFlightApp
