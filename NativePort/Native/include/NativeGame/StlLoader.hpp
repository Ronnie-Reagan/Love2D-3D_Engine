#pragma once

#include "NativeGame/ImageCodec.hpp"
#include "NativeGame/Math.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace NativeGame {

struct Face {
    std::vector<int> indices;
    int materialIndex = 0;
};

enum class AlphaMode {
    Opaque = 0,
    Mask = 1,
    Blend = 2
};

struct TextureRef {
    int imageIndex = -1;
    int texCoord = 0;
    float scale = 1.0f;
    float strength = 1.0f;
    int textureIndex = -1;

    [[nodiscard]] bool valid() const
    {
        return imageIndex >= 0;
    }
};

struct Material {
    std::string name = "default";
    Vec4 baseColorFactor { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    TextureRef baseColorTexture {};
    TextureRef metallicRoughnessTexture {};
    TextureRef normalTexture {};
    TextureRef occlusionTexture {};
    TextureRef emissiveTexture {};
    Vec3 emissiveFactor { 0.0f, 0.0f, 0.0f };
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct Model {
    std::vector<Vec3> vertices;
    std::vector<Face> faces;
    std::vector<Vec3> faceColors;
    std::vector<Vec3> vertexNormals;
    std::vector<Vec2> texCoords;
    std::vector<Material> materials;
    std::vector<RgbaImage> images;
    bool hasTexCoords = false;
    bool hasTextureImages = false;
    bool hasPaintableMaterial = false;
    std::string assetKey;
    std::uint64_t cacheRevision = 1;
};

inline std::uint32_t byteSwap32(std::uint32_t value)
{
    return ((value & 0x000000FFu) << 24) |
        ((value & 0x0000FF00u) << 8) |
        ((value & 0x00FF0000u) >> 8) |
        ((value & 0xFF000000u) >> 24);
}

inline bool shouldFlipFaceByFacetNormal(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& facetNormal)
{
    if (lengthSquared(facetNormal) <= 1.0e-12f) {
        return false;
    }

    const Vec3 computed = cross(b - a, c - a);
    if (lengthSquared(computed) <= 1.0e-12f) {
        return false;
    }

    return dot(computed, facetNormal) < 0.0f;
}

inline std::optional<Model> parseAsciiStl(const std::string& data, std::string* error)
{
    std::istringstream input(data);
    std::string line;
    std::vector<Vec3> vertices;
    std::vector<Face> faces;
    Vec3 facetNormal {};

    while (std::getline(input, line)) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (line.find("facet normal") != std::string::npos) {
            if (std::sscanf(line.c_str(), " facet normal %f %f %f", &x, &y, &z) == 3 ||
                std::sscanf(line.c_str(), "\tfacet normal %f %f %f", &x, &y, &z) == 3) {
                facetNormal = { x, y, z };
            }
            continue;
        }

        if (line.find("vertex") != std::string::npos) {
            if (std::sscanf(line.c_str(), " vertex %f %f %f", &x, &y, &z) == 3 ||
                std::sscanf(line.c_str(), "\t\tvertex %f %f %f", &x, &y, &z) == 3 ||
                std::sscanf(line.c_str(), "%*s vertex %f %f %f", &x, &y, &z) == 3) {
                vertices.push_back({ x, y, z });
            }
            continue;
        }

        if (line.find("endfacet") != std::string::npos) {
            if (vertices.size() >= 3) {
                const int i1 = static_cast<int>(vertices.size()) - 3;
                int i2 = static_cast<int>(vertices.size()) - 2;
                int i3 = static_cast<int>(vertices.size()) - 1;
                if (shouldFlipFaceByFacetNormal(vertices[i1], vertices[i2], vertices[i3], facetNormal)) {
                    std::swap(i2, i3);
                }
                faces.push_back({ { i1, i2, i3 } });
            }
            facetNormal = {};
        }
    }

    if (faces.empty()) {
        if (error != nullptr) {
            *error = "no faces found in ASCII STL";
        }
        return std::nullopt;
    }

    return Model { std::move(vertices), std::move(faces), {} };
}

inline std::optional<Model> parseBinaryStl(const std::vector<std::uint8_t>& bytes, std::string* error)
{
    if (bytes.size() < 84) {
        if (error != nullptr) {
            *error = "binary STL header too short";
        }
        return std::nullopt;
    }

    const auto readU32 = [&](std::size_t offset) -> std::uint32_t {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        if constexpr (std::endian::native == std::endian::big) {
            value = byteSwap32(value);
        }
        return value;
    };

    const auto readF32 = [&](std::size_t offset) -> float {
        const std::uint32_t raw = readU32(offset);
        return std::bit_cast<float>(raw);
    };

    const std::uint32_t triangleCount = readU32(80);
    const std::size_t expectedBytes = 84u + (static_cast<std::size_t>(triangleCount) * 50u);
    if (bytes.size() < expectedBytes) {
        if (error != nullptr) {
            *error = "binary STL truncated";
        }
        return std::nullopt;
    }

    std::vector<Vec3> vertices;
    std::vector<Face> faces;
    vertices.reserve(static_cast<std::size_t>(triangleCount) * 3u);
    faces.reserve(triangleCount);

    std::size_t cursor = 84;
    for (std::uint32_t triangle = 0; triangle < triangleCount; ++triangle) {
        const Vec3 normal {
            readF32(cursor + 0),
            readF32(cursor + 4),
            readF32(cursor + 8)
        };
        const Vec3 a {
            readF32(cursor + 12),
            readF32(cursor + 16),
            readF32(cursor + 20)
        };
        const Vec3 b {
            readF32(cursor + 24),
            readF32(cursor + 28),
            readF32(cursor + 32)
        };
        const Vec3 c {
            readF32(cursor + 36),
            readF32(cursor + 40),
            readF32(cursor + 44)
        };

        const int base = static_cast<int>(vertices.size());
        vertices.push_back(a);
        vertices.push_back(b);
        vertices.push_back(c);

        int i1 = base;
        int i2 = base + 1;
        int i3 = base + 2;
        if (shouldFlipFaceByFacetNormal(vertices[i1], vertices[i2], vertices[i3], normal)) {
            std::swap(i2, i3);
        }
        faces.push_back({ { i1, i2, i3 } });
        cursor += 50;
    }

    return Model { std::move(vertices), std::move(faces), {} };
}

inline std::optional<Model> loadStl(const std::filesystem::path& path, std::string* error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error != nullptr) {
            *error = "failed to open STL";
        }
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        if (error != nullptr) {
            *error = "empty STL";
        }
        return std::nullopt;
    }

    if (bytes.size() >= 84) {
        std::uint32_t triangleCount = 0;
        std::memcpy(&triangleCount, bytes.data() + 80, sizeof(triangleCount));
        if constexpr (std::endian::native == std::endian::big) {
            triangleCount = byteSwap32(triangleCount);
        }
        const std::size_t expectedBytes = 84u + (static_cast<std::size_t>(triangleCount) * 50u);
        if (expectedBytes == bytes.size()) {
            if (auto binary = parseBinaryStl(bytes, error)) {
                return binary;
            }
        }
    }

    const std::string ascii(bytes.begin(), bytes.end());
    if (auto text = parseAsciiStl(ascii, error)) {
        return text;
    }

    return parseBinaryStl(bytes, error);
}

inline Model normalizeModel(const Model& model, float targetExtent)
{
    if (model.vertices.empty()) {
        return model;
    }

    Vec3 minBounds = model.vertices.front();
    Vec3 maxBounds = model.vertices.front();
    for (const Vec3& vertex : model.vertices) {
        minBounds.x = std::min(minBounds.x, vertex.x);
        minBounds.y = std::min(minBounds.y, vertex.y);
        minBounds.z = std::min(minBounds.z, vertex.z);
        maxBounds.x = std::max(maxBounds.x, vertex.x);
        maxBounds.y = std::max(maxBounds.y, vertex.y);
        maxBounds.z = std::max(maxBounds.z, vertex.z);
    }

    const float largestSpan = std::max({
        maxBounds.x - minBounds.x,
        maxBounds.y - minBounds.y,
        maxBounds.z - minBounds.z,
        1.0e-6f
    });
    const float scale = targetExtent / largestSpan;
    const Vec3 center {
        (minBounds.x + maxBounds.x) * 0.5f,
        (minBounds.y + maxBounds.y) * 0.5f,
        (minBounds.z + maxBounds.z) * 0.5f
    };

    Model out = model;
    out.vertices.clear();
    out.vertices.reserve(model.vertices.size());
    for (const Vec3& vertex : model.vertices) {
        out.vertices.push_back((vertex - center) * scale);
    }
    return out;
}

inline Model makeCubeModel()
{
    return {
        {
            { -1.0f, -1.0f, -1.0f },
            { 1.0f, -1.0f, -1.0f },
            { 1.0f, 1.0f, -1.0f },
            { -1.0f, 1.0f, -1.0f },
            { -1.0f, -1.0f, 1.0f },
            { 1.0f, -1.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f },
            { -1.0f, 1.0f, 1.0f }
        },
        {
            { { 3, 2, 1, 0 } },
            { { 4, 5, 6, 7 } },
            { { 0, 1, 5, 4 } },
            { { 1, 2, 6, 5 } },
            { { 2, 3, 7, 6 } },
            { { 3, 0, 4, 7 } }
        },
        {}
    };
}

inline Model makeOctahedronModel()
{
    return {
        {
            { 0.0f, 1.0f, 0.0f },
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f },
            { -1.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, -1.0f },
            { 0.0f, -1.0f, 0.0f }
        },
        {
            { { 0, 1, 2 } },
            { { 0, 2, 3 } },
            { { 0, 3, 4 } },
            { { 0, 4, 1 } },
            { { 5, 2, 1 } },
            { { 5, 3, 2 } },
            { { 5, 4, 3 } },
            { { 5, 1, 4 } }
        },
        {}
    };
}

}  // namespace NativeGame
