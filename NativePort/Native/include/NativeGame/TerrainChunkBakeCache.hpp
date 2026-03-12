#pragma once

#include "NativeGame/World.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace NativeGame {

struct TerrainChunkKey {
    std::string worldId = "default";
    int seed = 1;
    int generatorVersion = 1;
    int band = 0;
    int detail = 0;
    int tileX = 0;
    int tileZ = 0;
    std::uint64_t paramsSignature = 0;
    std::uint64_t sourceSignature = 0;
};

struct TerrainChunkData {
    TerrainChunkKey key {};
    TerrainPatchBounds bounds {};
    float cellSize = 1.0f;
    int gridWidth = 0;
    int gridHeight = 0;
    std::vector<float> surfaceHeights;
    std::vector<float> snowWeights;
    std::vector<float> waterHeights;
    std::vector<float> waterWeights;
};

struct CompiledTerrainChunk {
    TerrainChunkKey key {};
    TerrainChunkData sourceData {};
    Model terrainModel {};
    Model waterModel {};
    Model propModel {};
    std::vector<TerrainPropCollider> propColliders;
};

class TerrainChunkBakeCache {
public:
    static constexpr std::uint32_t kFormatVersion = 2u;

    static std::optional<TerrainChunkBakeCache> open(const std::filesystem::path& rootPath, std::string* error = nullptr)
    {
        if (rootPath.empty()) {
            if (error != nullptr) {
                *error = "empty terrain chunk cache path";
            }
            return std::nullopt;
        }

        std::error_code ec;
        std::filesystem::create_directories(rootPath, ec);
        if (ec) {
            if (error != nullptr) {
                *error = "failed to create terrain chunk cache directory";
            }
            return std::nullopt;
        }

        TerrainChunkBakeCache cache;
        cache.rootPath_ = rootPath;
        return cache;
    }

    const std::filesystem::path& rootPath() const
    {
        return rootPath_;
    }

    std::filesystem::path chunkPath(const TerrainChunkKey& key) const
    {
        const std::string digest = keyDigest(key);
        const std::string prefix = digest.substr(0, std::min<std::size_t>(2u, digest.size()));
        return rootPath_ / sanitizePathToken(key.worldId.empty() ? std::string("default") : key.worldId) / prefix / (digest + ".bin");
    }

    bool load(const TerrainChunkKey& key, CompiledTerrainChunk& outChunk) const
    {
        const std::filesystem::path path = chunkPath(key);
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open()) {
            return false;
        }

        std::uint32_t magic = 0u;
        std::uint32_t version = 0u;
        if (!readValue(input, magic) || !readValue(input, version) || magic != 0x5443484Bu || version != kFormatVersion) {
            return false;
        }

        CompiledTerrainChunk chunk;
        if (!readTerrainChunkKey(input, chunk.key) ||
            !readTerrainChunkData(input, chunk.sourceData) ||
            !readModel(input, chunk.terrainModel) ||
            !readModel(input, chunk.waterModel) ||
            !readModel(input, chunk.propModel) ||
            !readTerrainPropColliders(input, chunk.propColliders)) {
            return false;
        }

        if (chunk.key.worldId != key.worldId ||
            chunk.key.seed != key.seed ||
            chunk.key.generatorVersion != key.generatorVersion ||
            chunk.key.band != key.band ||
            chunk.key.detail != key.detail ||
            chunk.key.tileX != key.tileX ||
            chunk.key.tileZ != key.tileZ ||
            chunk.key.paramsSignature != key.paramsSignature ||
            chunk.key.sourceSignature != key.sourceSignature) {
            return false;
        }

        outChunk = std::move(chunk);
        return true;
    }

    bool save(const CompiledTerrainChunk& chunk, std::string* error = nullptr) const
    {
        const std::filesystem::path path = chunkPath(chunk.key);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error != nullptr) {
                *error = "failed to create terrain chunk cache subdirectory";
            }
            return false;
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            if (error != nullptr) {
                *error = "failed to open terrain chunk cache file for writing";
            }
            return false;
        }

        writeValue(output, static_cast<std::uint32_t>(0x5443484Bu));
        writeValue(output, kFormatVersion);
        writeTerrainChunkKey(output, chunk.key);
        writeTerrainChunkData(output, chunk.sourceData);
        writeModel(output, chunk.terrainModel);
        writeModel(output, chunk.waterModel);
        writeModel(output, chunk.propModel);
        writeTerrainPropColliders(output, chunk.propColliders);
        if (!output.good()) {
            if (error != nullptr) {
                *error = "failed while writing terrain chunk cache file";
            }
            return false;
        }
        return true;
    }

private:
    std::filesystem::path rootPath_ {};

    template <typename T>
    static void writeValue(std::ostream& output, const T& value)
    {
        output.write(reinterpret_cast<const char*>(&value), static_cast<std::streamsize>(sizeof(T)));
    }

    template <typename T>
    static bool readValue(std::istream& input, T& value)
    {
        return static_cast<bool>(input.read(reinterpret_cast<char*>(&value), static_cast<std::streamsize>(sizeof(T))));
    }

    static void writeString(std::ostream& output, const std::string& value)
    {
        const std::uint32_t length = static_cast<std::uint32_t>(value.size());
        writeValue(output, length);
        if (length > 0u) {
            output.write(value.data(), static_cast<std::streamsize>(length));
        }
    }

    static bool readString(std::istream& input, std::string& value)
    {
        std::uint32_t length = 0u;
        if (!readValue(input, length)) {
            return false;
        }
        value.resize(length);
        return length == 0u || static_cast<bool>(input.read(value.data(), static_cast<std::streamsize>(length)));
    }

    static void writeVec3(std::ostream& output, const Vec3& value)
    {
        writeValue(output, value.x);
        writeValue(output, value.y);
        writeValue(output, value.z);
    }

    static bool readVec3(std::istream& input, Vec3& value)
    {
        return readValue(input, value.x) && readValue(input, value.y) && readValue(input, value.z);
    }

    static void writeVec2(std::ostream& output, const Vec2& value)
    {
        writeValue(output, value.x);
        writeValue(output, value.y);
    }

    static bool readVec2(std::istream& input, Vec2& value)
    {
        return readValue(input, value.x) && readValue(input, value.y);
    }

    static void writeTerrainPropColliders(std::ostream& output, const std::vector<TerrainPropCollider>& colliders)
    {
        const std::uint32_t count = static_cast<std::uint32_t>(colliders.size());
        writeValue(output, count);
        for (const TerrainPropCollider& collider : colliders) {
            const std::uint8_t propClass = static_cast<std::uint8_t>(collider.propClass);
            writeValue(output, propClass);
            writeVec3(output, collider.center);
            writeValue(output, collider.radius);
            writeValue(output, collider.halfHeight);
            writeValue(output, collider.softness);
        }
    }

    static bool readTerrainPropColliders(std::istream& input, std::vector<TerrainPropCollider>& colliders)
    {
        std::uint32_t count = 0u;
        if (!readValue(input, count)) {
            return false;
        }

        colliders.clear();
        colliders.reserve(count);
        for (std::uint32_t i = 0u; i < count; ++i) {
            std::uint8_t propClassValue = 0u;
            TerrainPropCollider collider;
            if (!readValue(input, propClassValue) ||
                !readVec3(input, collider.center) ||
                !readValue(input, collider.radius) ||
                !readValue(input, collider.halfHeight) ||
                !readValue(input, collider.softness)) {
                return false;
            }
            collider.propClass = propClassValue == static_cast<std::uint8_t>(TerrainPropClass::Blocker)
                ? TerrainPropClass::Blocker
                : TerrainPropClass::Brush;
            colliders.push_back(collider);
        }
        return true;
    }

    static void writeFloatVector(std::ostream& output, const std::vector<float>& values)
    {
        const std::uint32_t count = static_cast<std::uint32_t>(values.size());
        writeValue(output, count);
        if (count > 0u) {
            output.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(count * sizeof(float)));
        }
    }

    static bool readFloatVector(std::istream& input, std::vector<float>& values)
    {
        std::uint32_t count = 0u;
        if (!readValue(input, count)) {
            return false;
        }
        values.resize(count);
        return count == 0u || static_cast<bool>(input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(float))));
    }

    static void writeTerrainChunkKey(std::ostream& output, const TerrainChunkKey& key)
    {
        writeString(output, key.worldId);
        writeValue(output, key.seed);
        writeValue(output, key.generatorVersion);
        writeValue(output, key.band);
        writeValue(output, key.detail);
        writeValue(output, key.tileX);
        writeValue(output, key.tileZ);
        writeValue(output, key.paramsSignature);
        writeValue(output, key.sourceSignature);
    }

    static bool readTerrainChunkKey(std::istream& input, TerrainChunkKey& key)
    {
        return readString(input, key.worldId) &&
            readValue(input, key.seed) &&
            readValue(input, key.generatorVersion) &&
            readValue(input, key.band) &&
            readValue(input, key.detail) &&
            readValue(input, key.tileX) &&
            readValue(input, key.tileZ) &&
            readValue(input, key.paramsSignature) &&
            readValue(input, key.sourceSignature);
    }

    static void writeTerrainChunkData(std::ostream& output, const TerrainChunkData& data)
    {
        writeTerrainChunkKey(output, data.key);
        writeValue(output, data.bounds.x0);
        writeValue(output, data.bounds.x1);
        writeValue(output, data.bounds.z0);
        writeValue(output, data.bounds.z1);
        writeValue(output, data.bounds.hasHole);
        writeValue(output, data.bounds.holeX0);
        writeValue(output, data.bounds.holeX1);
        writeValue(output, data.bounds.holeZ0);
        writeValue(output, data.bounds.holeZ1);
        writeValue(output, data.cellSize);
        writeValue(output, data.gridWidth);
        writeValue(output, data.gridHeight);
        writeFloatVector(output, data.surfaceHeights);
        writeFloatVector(output, data.snowWeights);
        writeFloatVector(output, data.waterHeights);
        writeFloatVector(output, data.waterWeights);
    }

    static bool readTerrainChunkData(std::istream& input, TerrainChunkData& data)
    {
        return readTerrainChunkKey(input, data.key) &&
            readValue(input, data.bounds.x0) &&
            readValue(input, data.bounds.x1) &&
            readValue(input, data.bounds.z0) &&
            readValue(input, data.bounds.z1) &&
            readValue(input, data.bounds.hasHole) &&
            readValue(input, data.bounds.holeX0) &&
            readValue(input, data.bounds.holeX1) &&
            readValue(input, data.bounds.holeZ0) &&
            readValue(input, data.bounds.holeZ1) &&
            readValue(input, data.cellSize) &&
            readValue(input, data.gridWidth) &&
            readValue(input, data.gridHeight) &&
            readFloatVector(input, data.surfaceHeights) &&
            readFloatVector(input, data.snowWeights) &&
            readFloatVector(input, data.waterHeights) &&
            readFloatVector(input, data.waterWeights);
    }

    static void writeMaterial(std::ostream& output, const Material& material)
    {
        writeString(output, material.name);
        writeValue(output, material.baseColorFactor.x);
        writeValue(output, material.baseColorFactor.y);
        writeValue(output, material.baseColorFactor.z);
        writeValue(output, material.baseColorFactor.w);
        const std::int32_t alphaMode = static_cast<std::int32_t>(material.alphaMode);
        writeValue(output, alphaMode);
        writeValue(output, material.alphaCutoff);
        writeValue(output, material.doubleSided);
    }

    static bool readMaterial(std::istream& input, Material& material)
    {
        std::int32_t alphaMode = 0;
        return readString(input, material.name) &&
            readValue(input, material.baseColorFactor.x) &&
            readValue(input, material.baseColorFactor.y) &&
            readValue(input, material.baseColorFactor.z) &&
            readValue(input, material.baseColorFactor.w) &&
            readValue(input, alphaMode) &&
            readValue(input, material.alphaCutoff) &&
            readValue(input, material.doubleSided) &&
            ((material.alphaMode = static_cast<AlphaMode>(alphaMode)), true);
    }

    static void writeModel(std::ostream& output, const Model& model)
    {
        const std::uint32_t vertexCount = static_cast<std::uint32_t>(model.vertices.size());
        const std::uint32_t faceCount = static_cast<std::uint32_t>(model.faces.size());
        const std::uint32_t normalCount = static_cast<std::uint32_t>(model.vertexNormals.size());
        const std::uint32_t faceColorCount = static_cast<std::uint32_t>(model.faceColors.size());
        const std::uint32_t texCoordCount = static_cast<std::uint32_t>(model.texCoords.size());
        const std::uint32_t materialCount = static_cast<std::uint32_t>(model.materials.size());

        writeString(output, model.assetKey);
        writeValue(output, vertexCount);
        for (const Vec3& vertex : model.vertices) {
            writeVec3(output, vertex);
        }

        writeValue(output, faceCount);
        for (const Face& face : model.faces) {
            const std::uint32_t indexCount = static_cast<std::uint32_t>(face.indices.size());
            writeValue(output, indexCount);
            writeValue(output, face.materialIndex);
            for (int index : face.indices) {
                writeValue(output, index);
            }
        }

        writeValue(output, faceColorCount);
        for (const Vec3& color : model.faceColors) {
            writeVec3(output, color);
        }

        writeValue(output, normalCount);
        for (const Vec3& normal : model.vertexNormals) {
            writeVec3(output, normal);
        }

        writeValue(output, texCoordCount);
        for (const Vec2& texCoord : model.texCoords) {
            writeVec2(output, texCoord);
        }

        writeValue(output, materialCount);
        for (const Material& material : model.materials) {
            writeMaterial(output, material);
        }

        writeValue(output, model.hasTexCoords);
        writeValue(output, model.hasTextureImages);
        writeValue(output, model.hasPaintableMaterial);
    }

    static bool readModel(std::istream& input, Model& model)
    {
        std::uint32_t vertexCount = 0u;
        std::uint32_t faceCount = 0u;
        std::uint32_t faceColorCount = 0u;
        std::uint32_t normalCount = 0u;
        std::uint32_t texCoordCount = 0u;
        std::uint32_t materialCount = 0u;
        if (!readString(input, model.assetKey) ||
            !readValue(input, vertexCount)) {
            return false;
        }
        model.vertices.resize(vertexCount);
        for (Vec3& vertex : model.vertices) {
            if (!readVec3(input, vertex)) {
                return false;
            }
        }

        if (!readValue(input, faceCount)) {
            return false;
        }
        model.faces.resize(faceCount);
        for (Face& face : model.faces) {
            std::uint32_t indexCount = 0u;
            if (!readValue(input, indexCount) || !readValue(input, face.materialIndex)) {
                return false;
            }
            face.indices.resize(indexCount);
            for (int& index : face.indices) {
                if (!readValue(input, index)) {
                    return false;
                }
            }
        }

        if (!readValue(input, faceColorCount)) {
            return false;
        }
        model.faceColors.resize(faceColorCount);
        for (Vec3& color : model.faceColors) {
            if (!readVec3(input, color)) {
                return false;
            }
        }

        if (!readValue(input, normalCount)) {
            return false;
        }
        model.vertexNormals.resize(normalCount);
        for (Vec3& normal : model.vertexNormals) {
            if (!readVec3(input, normal)) {
                return false;
            }
        }

        if (!readValue(input, texCoordCount)) {
            return false;
        }
        model.texCoords.resize(texCoordCount);
        for (Vec2& texCoord : model.texCoords) {
            if (!readVec2(input, texCoord)) {
                return false;
            }
        }

        if (!readValue(input, materialCount)) {
            return false;
        }
        model.materials.resize(materialCount);
        for (Material& material : model.materials) {
            if (!readMaterial(input, material)) {
                return false;
            }
        }

        return readValue(input, model.hasTexCoords) &&
            readValue(input, model.hasTextureImages) &&
            readValue(input, model.hasPaintableMaterial);
    }

    static std::string sanitizePathToken(const std::string& value)
    {
        std::string out;
        out.reserve(value.size());
        for (char ch : value.empty() ? std::string("default") : value) {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (std::isalnum(uch) || ch == '_' || ch == '-') {
                out.push_back(static_cast<char>(std::tolower(uch)));
            } else {
                out.push_back('_');
            }
        }
        return out.empty() ? std::string("default") : out;
    }

    static std::uint64_t fnv1a64(const std::string& value)
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (unsigned char ch : value) {
            hash ^= static_cast<std::uint64_t>(ch);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    static std::string keyDigest(const TerrainChunkKey& key)
    {
        std::ostringstream stream;
        stream << key.worldId << '|'
               << key.seed << '|'
               << key.generatorVersion << '|'
               << key.band << '|'
               << key.detail << '|'
               << key.tileX << '|'
               << key.tileZ << '|'
               << key.paramsSignature << '|'
               << key.sourceSignature;
        const std::uint64_t hash = fnv1a64(stream.str());
        std::ostringstream hex;
        hex.setf(std::ios::hex, std::ios::basefield);
        hex.fill('0');
        hex.width(16);
        hex << hash;
        return hex.str();
    }
};

}  // namespace NativeGame
