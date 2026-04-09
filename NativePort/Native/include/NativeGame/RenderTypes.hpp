#pragma once

#include "NativeGame/Math.hpp"
#include "NativeGame/StlLoader.hpp"

namespace NativeGame {

struct Camera {
    Vec3 pos {};
    Quat rot = quatIdentity();
    float fovRadians = radians(80.0f);
    float nearClipMeters = 0.05f;
    float farClipMeters = 42000.0f;
    WorldShape worldShape = WorldShape::Plane;
    Vec3 planetCenter {};
    float planetRadiusMeters = 6371000.0f;
    float atmosphereTopMeters = 120000.0f;
};

struct RenderObject {
    const Model* model = nullptr;
    Vec3 pos {};
    Quat rot = quatIdentity();
    Vec3 scale { 1.0f, 1.0f, 1.0f };
    Vec3 color { 0.8f, 0.8f, 0.8f };
    float alpha = 1.0f;
    float fogNear = 450.0f;
    float fogFar = 2600.0f;
    bool cullBackfaces = true;
    bool gpuResident = false;
};

}  // namespace NativeGame
