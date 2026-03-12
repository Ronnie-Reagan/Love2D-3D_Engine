#pragma once

#include "NativeGame/Math.hpp"
#include "NativeGame/RenderTypes.hpp"
#include "NativeGame/StlLoader.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace NativeGame {

class SoftwareRenderer {
public:
    SoftwareRenderer(int width, int height)
    {
        resize(width, height);
    }

    void resize(int width, int height)
    {
        width_ = std::max(1, width);
        height_ = std::max(1, height);
        pixels_.assign(static_cast<std::size_t>(width_ * height_), 0);
        depthBuffer_.assign(static_cast<std::size_t>(width_ * height_), std::numeric_limits<float>::infinity());
    }

    void clear(const Vec3& clearColor)
    {
        const std::uint32_t packed = packColor(clearColor);
        std::fill(pixels_.begin(), pixels_.end(), packed);
        std::fill(depthBuffer_.begin(), depthBuffer_.end(), std::numeric_limits<float>::infinity());
    }

    void drawObject(
        const RenderObject& object,
        const Camera& camera,
        const Vec3& sunDirection,
        const Vec3& skyColor,
        float fogNear = 450.0f,
        float fogFar = 2600.0f)
    {
        if (object.model == nullptr || object.model->vertices.empty() || object.model->faces.empty()) {
            return;
        }

        std::vector<Vec3> worldVertices;
        std::vector<Vec3> cameraVertices;
        worldVertices.reserve(object.model->vertices.size());
        cameraVertices.reserve(object.model->vertices.size());

        for (const Vec3& vertex : object.model->vertices) {
            const Vec3 scaled = hadamard(vertex, object.scale);
            const Vec3 world = rotateVector(object.rot, scaled) + object.pos;
            const Vec3 relative = world - camera.pos;
            const Vec3 cameraSpace = rotateVector(quatConjugate(camera.rot), relative);
            worldVertices.push_back(world);
            cameraVertices.push_back(cameraSpace);
        }

        const Vec3 normalizedSun = normalize(sunDirection, { 0.0f, 1.0f, 0.0f });

        for (std::size_t faceIndex = 0; faceIndex < object.model->faces.size(); ++faceIndex) {
            const Face& face = object.model->faces[faceIndex];
            if (face.indices.size() < 3) {
                continue;
            }

            const int i0 = face.indices[0];
            const int i1 = face.indices[1];
            const int i2 = face.indices[2];
            if (i0 < 0 || i1 < 0 || i2 < 0 ||
                i0 >= static_cast<int>(cameraVertices.size()) ||
                i1 >= static_cast<int>(cameraVertices.size()) ||
                i2 >= static_cast<int>(cameraVertices.size())) {
                continue;
            }

            if (object.cullBackfaces) {
                const Vec3 edge1 = cameraVertices[i1] - cameraVertices[i0];
                const Vec3 edge2 = cameraVertices[i2] - cameraVertices[i0];
                const Vec3 faceNormal = normalize(cross(edge1, edge2));
                const Vec3 toCamera = normalize(-cameraVertices[i0]);
                if (dot(faceNormal, toCamera) <= 0.0f) {
                    continue;
                }
            }

            const Vec3 worldNormal = normalize(
                cross(worldVertices[i1] - worldVertices[i0], worldVertices[i2] - worldVertices[i0]),
                { 0.0f, 1.0f, 0.0f });
            const float lighting = clamp(0.26f + (std::max(0.0f, dot(worldNormal, normalizedSun)) * 0.74f), 0.0f, 1.0f);
            const Vec3 baseColor =
                (faceIndex < object.model->faceColors.size()) ? object.model->faceColors[faceIndex] : object.color;
            const Vec3 litColor = baseColor * lighting;

            std::vector<Vec3> polygon;
            polygon.reserve(face.indices.size());
            for (int vertexIndex : face.indices) {
                if (vertexIndex >= 0 && vertexIndex < static_cast<int>(cameraVertices.size())) {
                    polygon.push_back(cameraVertices[vertexIndex]);
                }
            }

            polygon = clipPolygonToNearPlane(polygon, 0.05f);
            if (polygon.size() < 3) {
                continue;
            }

            std::vector<ProjectedVertex> projected;
            projected.reserve(polygon.size());
            for (const Vec3& vertex : polygon) {
                projected.push_back(project(vertex, camera));
            }

            for (std::size_t i = 1; (i + 1) < projected.size(); ++i) {
                drawTriangle(projected[0], projected[i], projected[i + 1], litColor, skyColor, fogNear, fogFar);
            }
        }
    }

    [[nodiscard]] const std::vector<std::uint32_t>& pixels() const
    {
        return pixels_;
    }

    [[nodiscard]] int width() const
    {
        return width_;
    }

    [[nodiscard]] int height() const
    {
        return height_;
    }

private:
    struct ProjectedVertex {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    static std::uint32_t packColor(const Vec3& color)
    {
        const std::uint8_t r = static_cast<std::uint8_t>(clamp(color.x, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t g = static_cast<std::uint8_t>(clamp(color.y, 0.0f, 1.0f) * 255.0f);
        const std::uint8_t b = static_cast<std::uint8_t>(clamp(color.z, 0.0f, 1.0f) * 255.0f);
        return (0xFFu << 24) | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
    }

    [[nodiscard]] ProjectedVertex project(const Vec3& vertex, const Camera& camera) const
    {
        const float z = std::max(0.01f, vertex.z);
        const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
        const float f = 1.0f / std::tan(camera.fovRadians * 0.5f);
        const float px = vertex.x * (f / aspect) / z;
        const float py = vertex.y * f / z;
        return {
            (static_cast<float>(width_) * 0.5f) + (px * static_cast<float>(width_) * 0.5f),
            (static_cast<float>(height_) * 0.5f) - (py * static_cast<float>(height_) * 0.5f),
            z
        };
    }

    static std::vector<Vec3> clipPolygonToNearPlane(const std::vector<Vec3>& vertices, float nearZ)
    {
        std::vector<Vec3> clipped;
        if (vertices.empty()) {
            return clipped;
        }

        const auto intersect = [nearZ](const Vec3& a, const Vec3& b) {
            const float t = (nearZ - a.z) / (b.z - a.z);
            return Vec3 {
                a.x + (t * (b.x - a.x)),
                a.y + (t * (b.y - a.y)),
                nearZ
            };
        };

        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3 current = vertices[i];
            const Vec3 next = vertices[(i + 1) % vertices.size()];
            const bool currentInside = current.z > nearZ;
            const bool nextInside = next.z > nearZ;

            if (currentInside) {
                clipped.push_back(current);
            }
            if (currentInside != nextInside) {
                clipped.push_back(intersect(current, next));
            }
        }

        return clipped;
    }

    void drawTriangle(
        const ProjectedVertex& a,
        const ProjectedVertex& b,
        const ProjectedVertex& c,
        const Vec3& color,
        const Vec3& skyColor,
        float fogNear,
        float fogFar)
    {
        const float minX = std::floor(std::min({ a.x, b.x, c.x }));
        const float maxX = std::ceil(std::max({ a.x, b.x, c.x }));
        const float minY = std::floor(std::min({ a.y, b.y, c.y }));
        const float maxY = std::ceil(std::max({ a.y, b.y, c.y }));

        const int x0 = std::max(0, static_cast<int>(minX));
        const int x1 = std::min(width_ - 1, static_cast<int>(maxX));
        const int y0 = std::max(0, static_cast<int>(minY));
        const int y1 = std::min(height_ - 1, static_cast<int>(maxY));
        if (x0 > x1 || y0 > y1) {
            return;
        }

        const auto edge = [](float ax, float ay, float bx, float by, float px, float py) {
            return ((px - ax) * (by - ay)) - ((py - ay) * (bx - ax));
        };

        const float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
        if (std::fabs(area) <= 1.0e-8f) {
            return;
        }

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;

                float w0 = edge(b.x, b.y, c.x, c.y, px, py);
                float w1 = edge(c.x, c.y, a.x, a.y, px, py);
                float w2 = edge(a.x, a.y, b.x, b.y, px, py);

                const bool inside =
                    (area > 0.0f && w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) ||
                    (area < 0.0f && w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
                if (!inside) {
                    continue;
                }

                w0 /= area;
                w1 /= area;
                w2 /= area;
                const float depth = (a.z * w0) + (b.z * w1) + (c.z * w2);
                const std::size_t index = static_cast<std::size_t>((y * width_) + x);
                if (depth >= depthBuffer_[index]) {
                    continue;
                }
                depthBuffer_[index] = depth;

                const float fogT = clamp((depth - fogNear) / std::max(1.0f, fogFar - fogNear), 0.0f, 1.0f);
                pixels_[index] = packColor(lerp(color, skyColor, fogT));
            }
        }
    }

    int width_ = 1;
    int height_ = 1;
    std::vector<std::uint32_t> pixels_;
    std::vector<float> depthBuffer_;
};

}  // namespace NativeGame
