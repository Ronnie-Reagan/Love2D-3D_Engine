#pragma once

#include "NativeGame/World.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

namespace NativeGame {

struct TerrainChunkKey {
    std::string worldId = "default";
    int seed = 1;
    int generatorVersion = 1;
    WorldShape worldShape = WorldShape::Plane;
    PlanetConfig planet {};
    PlanetTileId planetTile {};
    int band = 0;
    int detail = 0;
    int tileScale = 1;
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
    std::vector<float> wetnessWeights;
    std::vector<float> snowWeights;
    std::vector<float> rockWeights;
    std::vector<float> biomeWeights;
    std::vector<float> waterHeights;
    std::vector<float> waterWeights;
    std::vector<float> hardnessWeights;
    std::vector<float> resourceWeights;
    std::vector<float> erosionWeights;
    std::vector<float> flowWeights;
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
    static constexpr std::uint32_t kFormatVersion = 7u;
    static constexpr std::uint32_t kMaxStringLength = 256u;
    static constexpr std::uint32_t kMaxPathTokenLength = 128u;
    static constexpr std::uint32_t kMaxTerrainGridAxis = 4096u;
    static constexpr std::uint32_t kMaxTerrainGridSamples = 4096u * 4096u;
    static constexpr std::uint32_t kMaxTerrainPropColliders = 65536u;
    static constexpr std::uint32_t kMaxModelVertices = 2097152u;
    static constexpr std::uint32_t kMaxModelFaces = 4194304u;
    static constexpr std::uint32_t kMaxFaceIndices = 16u;
    static constexpr std::uint32_t kMaxMaterials = 4096u;
    static constexpr std::uint32_t kMaxFloatVectorCount = kMaxTerrainGridSamples;
    static constexpr float kMaxTerrainCellSize = 4096.0f;

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

        if (!validateLoadedChunk(key, chunk)) {
            return false;
        }

        outChunk = std::move(chunk);
        return true;
    }

    bool save(const CompiledTerrainChunk& chunk, std::string* error = nullptr) const
    {
        const std::filesystem::path path = chunkPath(chunk.key);
        const std::filesystem::path tempPath = makeTempChunkPath(path);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error != nullptr) {
                *error = "failed to create terrain chunk cache subdirectory";
            }
            return false;
        }

        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
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
        output.flush();
        if (!output.good()) {
            if (error != nullptr) {
                *error = "failed while writing terrain chunk cache file";
            }
            std::error_code cleanupEc;
            std::filesystem::remove(tempPath, cleanupEc);
            return false;
        }

        output.close();
        if (!output.good()) {
            if (error != nullptr) {
                *error = "failed while closing terrain chunk cache file";
            }
            std::error_code cleanupEc;
            std::filesystem::remove(tempPath, cleanupEc);
            return false;
        }
        if (!commitTempFile(tempPath, path)) {
            if (error != nullptr) {
                *error = "failed to commit terrain chunk cache file";
            }
            std::error_code cleanupEc;
            std::filesystem::remove(tempPath, cleanupEc);
            return false;
        }
        return true;
    }

private:
    std::filesystem::path rootPath_ {};

    static bool isFiniteNumber(float value)
    {
        return std::isfinite(value);
    }

    static bool isFiniteNumber(double value)
    {
        return std::isfinite(value);
    }

    template <typename T>
    static bool isFiniteNumber(const T&)
    {
        return true;
    }

    template <typename T>
    static bool readFiniteValue(std::istream& input, T& value)
    {
        return readValue(input, value) && isFiniteNumber(value);
    }

    static bool readCount(std::istream& input, std::uint32_t& value, std::uint32_t maxValue)
    {
        return readValue(input, value) && value <= maxValue;
    }

    static bool readStrictBool(std::istream& input, bool& value)
    {
        std::uint8_t raw = 0u;
        if (!readValue(input, raw) || raw > 1u) {
            return false;
        }
        value = raw != 0u;
        return true;
    }

    static bool isValidTerrainShape(std::uint8_t value)
    {
        return value == static_cast<std::uint8_t>(WorldShape::Plane) || value == static_cast<std::uint8_t>(WorldShape::Planet);
    }

    static bool isValidPropClass(std::uint8_t value)
    {
        return value == static_cast<std::uint8_t>(TerrainPropClass::Brush) ||
            value == static_cast<std::uint8_t>(TerrainPropClass::Blocker);
    }

    static bool isValidAlphaMode(std::int32_t value)
    {
        return value >= static_cast<std::int32_t>(AlphaMode::Opaque) &&
            value <= static_cast<std::int32_t>(AlphaMode::Blend);
    }

    static bool isValidGridDimensions(int width, int height)
    {
        return width > 0 && height > 0 &&
            static_cast<std::uint32_t>(width) <= kMaxTerrainGridAxis &&
            static_cast<std::uint32_t>(height) <= kMaxTerrainGridAxis;
    }

    static bool isReasonableFaceIndexCount(std::uint32_t value)
    {
        return value >= 3u && value <= kMaxFaceIndices;
    }

    static bool validateFloatVectorSize(const std::vector<float>& values, std::size_t expectedSize)
    {
        return values.size() == expectedSize &&
            values.size() <= kMaxFloatVectorCount &&
            std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    }

    static bool validateTerrainPropColliders(const std::vector<TerrainPropCollider>& colliders)
    {
        if (colliders.size() > kMaxTerrainPropColliders) {
            return false;
        }

        for (const TerrainPropCollider& collider : colliders) {
            if (!isValidPropClass(static_cast<std::uint8_t>(collider.propClass)) ||
                !std::isfinite(collider.center.x) ||
                !std::isfinite(collider.center.y) ||
                !std::isfinite(collider.center.z) ||
                !std::isfinite(collider.radius) ||
                !std::isfinite(collider.halfHeight) ||
                !std::isfinite(collider.softness) ||
                collider.radius < 0.0f ||
                collider.halfHeight < 0.0f ||
                collider.softness < 0.0f) {
                return false;
            }
        }

        return true;
    }

    static bool validateTerrainChunkKey(const TerrainChunkKey& expectedKey, const TerrainChunkKey& loadedKey)
    {
        return loadedKey.worldId == expectedKey.worldId &&
            loadedKey.seed == expectedKey.seed &&
            loadedKey.generatorVersion == expectedKey.generatorVersion &&
            loadedKey.worldShape == expectedKey.worldShape &&
            loadedKey.planet.radiusMeters == expectedKey.planet.radiusMeters &&
            loadedKey.planet.gravitationalParameter == expectedKey.planet.gravitationalParameter &&
            loadedKey.planet.rotationRateRadPerSec == expectedKey.planet.rotationRateRadPerSec &&
            loadedKey.planet.atmosphereHeightMeters == expectedKey.planet.atmosphereHeightMeters &&
            loadedKey.planet.localOrigin.latitudeDeg == expectedKey.planet.localOrigin.latitudeDeg &&
            loadedKey.planet.localOrigin.longitudeDeg == expectedKey.planet.localOrigin.longitudeDeg &&
            loadedKey.planet.localOrigin.altitudeMeters == expectedKey.planet.localOrigin.altitudeMeters &&
            loadedKey.planetTile.face == expectedKey.planetTile.face &&
            loadedKey.planetTile.lod == expectedKey.planetTile.lod &&
            loadedKey.planetTile.tx == expectedKey.planetTile.tx &&
            loadedKey.planetTile.ty == expectedKey.planetTile.ty &&
            loadedKey.band == expectedKey.band &&
            loadedKey.detail == expectedKey.detail &&
            loadedKey.tileScale == expectedKey.tileScale &&
            loadedKey.tileX == expectedKey.tileX &&
            loadedKey.tileZ == expectedKey.tileZ &&
            loadedKey.paramsSignature == expectedKey.paramsSignature &&
            loadedKey.sourceSignature == expectedKey.sourceSignature;
    }

    static bool validateLoadedChunk(const TerrainChunkKey& expectedKey, const CompiledTerrainChunk& chunk)
    {
        return validateTerrainChunkKey(expectedKey, chunk.key) &&
            validateTerrainChunkKey(chunk.key, chunk.sourceData.key) &&
            validateTerrainChunkData(chunk.sourceData) &&
            validateModel(chunk.terrainModel) &&
            validateModel(chunk.waterModel) &&
            validateModel(chunk.propModel) &&
            validateTerrainPropColliders(chunk.propColliders);
    }

    static bool validateModel(const Model& model)
    {
        if (model.vertices.size() > kMaxModelVertices ||
            model.faces.size() > kMaxModelFaces ||
            model.faceColors.size() > kMaxModelFaces ||
            model.vertexNormals.size() > kMaxModelVertices ||
            model.texCoords.size() > kMaxModelVertices ||
            model.texCoords1.size() > kMaxModelVertices ||
            model.materials.size() > kMaxMaterials) {
            return false;
        }

        if (!std::all_of(model.vertices.begin(), model.vertices.end(), [](const Vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }) ||
            !std::all_of(model.faceColors.begin(), model.faceColors.end(), [](const Vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }) ||
            !std::all_of(model.vertexNormals.begin(), model.vertexNormals.end(), [](const Vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }) ||
            !std::all_of(model.texCoords.begin(), model.texCoords.end(), [](const Vec2& value) { return std::isfinite(value.x) && std::isfinite(value.y); }) ||
            !std::all_of(model.texCoords1.begin(), model.texCoords1.end(), [](const Vec2& value) { return std::isfinite(value.x) && std::isfinite(value.y); }) ||
            !std::all_of(model.materials.begin(), model.materials.end(), [](const Material& value) {
                return value.name.size() <= kMaxStringLength &&
                    std::isfinite(value.baseColorFactor.x) &&
                    std::isfinite(value.baseColorFactor.y) &&
                    std::isfinite(value.baseColorFactor.z) &&
                    std::isfinite(value.baseColorFactor.w) &&
                    isValidAlphaMode(static_cast<std::int32_t>(value.alphaMode)) &&
                    std::isfinite(value.alphaCutoff);
            })) {
            return false;
        }

        for (const Face& face : model.faces) {
            if (!isReasonableFaceIndexCount(static_cast<std::uint32_t>(face.indices.size())) ||
                face.materialIndex < 0 ||
                static_cast<std::size_t>(face.materialIndex) >= model.materials.size()) {
                return false;
            }
            for (int index : face.indices) {
                if (index < 0 || static_cast<std::size_t>(index) >= model.vertices.size()) {
                    return false;
                }
            }
        }

        return true;
    }

    static bool validateTerrainChunkData(const TerrainChunkData& data)
    {
        if (!std::isfinite(data.bounds.x0) ||
            !std::isfinite(data.bounds.x1) ||
            !std::isfinite(data.bounds.z0) ||
            !std::isfinite(data.bounds.z1) ||
            !std::isfinite(data.bounds.holeX0) ||
            !std::isfinite(data.bounds.holeX1) ||
            !std::isfinite(data.bounds.holeZ0) ||
            !std::isfinite(data.bounds.holeZ1) ||
            !std::isfinite(data.cellSize) ||
            data.cellSize <= 0.0f ||
            data.cellSize > kMaxTerrainCellSize ||
            !isValidGridDimensions(data.gridWidth, data.gridHeight) ||
            data.bounds.x1 <= data.bounds.x0 ||
            data.bounds.z1 <= data.bounds.z0) {
            return false;
        }

        if (data.bounds.hasHole && !(data.bounds.holeX1 > data.bounds.holeX0 && data.bounds.holeZ1 > data.bounds.holeZ0)) {
            return false;
        }
        if (data.bounds.hasHole && !(data.bounds.holeX0 >= data.bounds.x0 && data.bounds.holeX1 <= data.bounds.x1 && data.bounds.holeZ0 >= data.bounds.z0 && data.bounds.holeZ1 <= data.bounds.z1)) {
            return false;
        }

        const std::size_t expectedSamples = static_cast<std::size_t>(data.gridWidth) * static_cast<std::size_t>(data.gridHeight);
        if (expectedSamples == 0u || expectedSamples > kMaxTerrainGridSamples) {
            return false;
        }

        return validateFloatVectorSize(data.surfaceHeights, expectedSamples) &&
            validateFloatVectorSize(data.wetnessWeights, expectedSamples) &&
            validateFloatVectorSize(data.snowWeights, expectedSamples) &&
            validateFloatVectorSize(data.rockWeights, expectedSamples) &&
            validateFloatVectorSize(data.biomeWeights, expectedSamples) &&
            validateFloatVectorSize(data.waterHeights, expectedSamples) &&
            validateFloatVectorSize(data.waterWeights, expectedSamples) &&
            validateFloatVectorSize(data.hardnessWeights, expectedSamples) &&
            validateFloatVectorSize(data.resourceWeights, expectedSamples) &&
            validateFloatVectorSize(data.erosionWeights, expectedSamples) &&
            validateFloatVectorSize(data.flowWeights, expectedSamples);
    }

    static std::filesystem::path makeTempChunkPath(const std::filesystem::path& path)
    {
        static std::atomic<std::uint64_t> nextTempId { 1u };
        std::filesystem::path tempPath = path;
        tempPath += ".tmp.";
        tempPath += std::to_string(nextTempId.fetch_add(1u, std::memory_order_relaxed));
        return tempPath;
    }

    static bool commitTempFile(const std::filesystem::path& tempPath, const std::filesystem::path& finalPath)
    {
#ifdef _WIN32
        const std::wstring tempNative = tempPath.native();
        const std::wstring finalNative = finalPath.native();
        return MoveFileExW(tempNative.c_str(), finalNative.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code ec;
        std::filesystem::rename(tempPath, finalPath, ec);
        return !ec;
#endif
    }

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

    static bool readString(std::istream& input, std::string& value, std::uint32_t maxLength = kMaxStringLength)
    {
        std::uint32_t length = 0u;
        if (!readValue(input, length) || length > maxLength) {
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
        return readFiniteValue(input, value.x) && readFiniteValue(input, value.y) && readFiniteValue(input, value.z);
    }

    static void writeVec2(std::ostream& output, const Vec2& value)
    {
        writeValue(output, value.x);
        writeValue(output, value.y);
    }

    static bool readVec2(std::istream& input, Vec2& value)
    {
        return readFiniteValue(input, value.x) && readFiniteValue(input, value.y);
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
        if (!readCount(input, count, kMaxTerrainPropColliders)) {
            return false;
        }

        colliders.clear();
        colliders.reserve(count);
        for (std::uint32_t i = 0u; i < count; ++i) {
            std::uint8_t propClassValue = 0u;
            TerrainPropCollider collider;
            if (!readValue(input, propClassValue) ||
                !readVec3(input, collider.center) ||
                !readFiniteValue(input, collider.radius) ||
                !readFiniteValue(input, collider.halfHeight) ||
                !readFiniteValue(input, collider.softness) ||
                !isValidPropClass(propClassValue)) {
                return false;
            }
            collider.propClass = static_cast<TerrainPropClass>(propClassValue);
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

    static bool readFloatVectorExact(std::istream& input, std::vector<float>& values, std::uint32_t expectedCount)
    {
        std::uint32_t count = 0u;
        if (!readValue(input, count) || count != expectedCount || count > kMaxFloatVectorCount) {
            return false;
        }
        values.resize(count);
        const bool readOk = count == 0u || static_cast<bool>(input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(float))));
        return readOk && std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    }

    static void writeTerrainChunkKey(std::ostream& output, const TerrainChunkKey& key)
    {
        writeString(output, key.worldId);
        writeValue(output, key.seed);
        writeValue(output, key.generatorVersion);
        const std::uint8_t worldShape = static_cast<std::uint8_t>(key.worldShape);
        writeValue(output, worldShape);
        writeValue(output, key.planet.radiusMeters);
        writeValue(output, key.planet.gravitationalParameter);
        writeValue(output, key.planet.rotationRateRadPerSec);
        writeValue(output, key.planet.atmosphereHeightMeters);
        writeValue(output, key.planet.localOrigin.latitudeDeg);
        writeValue(output, key.planet.localOrigin.longitudeDeg);
        writeValue(output, key.planet.localOrigin.altitudeMeters);
        writeValue(output, key.planetTile.face);
        writeValue(output, key.planetTile.lod);
        writeValue(output, key.planetTile.tx);
        writeValue(output, key.planetTile.ty);
        writeValue(output, key.band);
        writeValue(output, key.detail);
        writeValue(output, key.tileScale);
        writeValue(output, key.tileX);
        writeValue(output, key.tileZ);
        writeValue(output, key.paramsSignature);
        writeValue(output, key.sourceSignature);
    }

    static bool readTerrainChunkKey(std::istream& input, TerrainChunkKey& key)
    {
        std::uint8_t worldShape = 0u;
        return readString(input, key.worldId, kMaxStringLength) &&
            readValue(input, key.seed) &&
            readValue(input, key.generatorVersion) &&
            readValue(input, worldShape) &&
            isValidTerrainShape(worldShape) &&
            readFiniteValue(input, key.planet.radiusMeters) &&
            readFiniteValue(input, key.planet.gravitationalParameter) &&
            readFiniteValue(input, key.planet.rotationRateRadPerSec) &&
            readFiniteValue(input, key.planet.atmosphereHeightMeters) &&
            readFiniteValue(input, key.planet.localOrigin.latitudeDeg) &&
            readFiniteValue(input, key.planet.localOrigin.longitudeDeg) &&
            readFiniteValue(input, key.planet.localOrigin.altitudeMeters) &&
            readValue(input, key.planetTile.face) &&
            readValue(input, key.planetTile.lod) &&
            readValue(input, key.planetTile.tx) &&
            readValue(input, key.planetTile.ty) &&
            readValue(input, key.band) &&
            readValue(input, key.detail) &&
            readValue(input, key.tileScale) &&
            readValue(input, key.tileX) &&
            readValue(input, key.tileZ) &&
            readValue(input, key.paramsSignature) &&
            readValue(input, key.sourceSignature) &&
            ((key.worldShape = static_cast<WorldShape>(worldShape)), true);
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
        writeFloatVector(output, data.wetnessWeights);
        writeFloatVector(output, data.snowWeights);
        writeFloatVector(output, data.rockWeights);
        writeFloatVector(output, data.biomeWeights);
        writeFloatVector(output, data.waterHeights);
        writeFloatVector(output, data.waterWeights);
        writeFloatVector(output, data.hardnessWeights);
        writeFloatVector(output, data.resourceWeights);
        writeFloatVector(output, data.erosionWeights);
        writeFloatVector(output, data.flowWeights);
    }

    static bool readTerrainChunkData(std::istream& input, TerrainChunkData& data)
    {
        if (!readTerrainChunkKey(input, data.key) ||
            !readFiniteValue(input, data.bounds.x0) ||
            !readFiniteValue(input, data.bounds.x1) ||
            !readFiniteValue(input, data.bounds.z0) ||
            !readFiniteValue(input, data.bounds.z1) ||
            !readStrictBool(input, data.bounds.hasHole) ||
            !readFiniteValue(input, data.bounds.holeX0) ||
            !readFiniteValue(input, data.bounds.holeX1) ||
            !readFiniteValue(input, data.bounds.holeZ0) ||
            !readFiniteValue(input, data.bounds.holeZ1) ||
            !readFiniteValue(input, data.cellSize) ||
            !readValue(input, data.gridWidth) ||
            !readValue(input, data.gridHeight) ||
            !isValidGridDimensions(data.gridWidth, data.gridHeight)) {
            return false;
        }

        if (!(data.bounds.x1 > data.bounds.x0 && data.bounds.z1 > data.bounds.z0)) {
            return false;
        }
        if (data.bounds.hasHole && !(data.bounds.holeX1 > data.bounds.holeX0 && data.bounds.holeZ1 > data.bounds.holeZ0)) {
            return false;
        }

        const std::uint32_t expectedSamples = static_cast<std::uint32_t>(data.gridWidth) * static_cast<std::uint32_t>(data.gridHeight);
        if (expectedSamples == 0u || expectedSamples > kMaxTerrainGridSamples) {
            return false;
        }

        return readFloatVectorExact(input, data.surfaceHeights, expectedSamples) &&
            readFloatVectorExact(input, data.wetnessWeights, expectedSamples) &&
            readFloatVectorExact(input, data.snowWeights, expectedSamples) &&
            readFloatVectorExact(input, data.rockWeights, expectedSamples) &&
            readFloatVectorExact(input, data.biomeWeights, expectedSamples) &&
            readFloatVectorExact(input, data.waterHeights, expectedSamples) &&
            readFloatVectorExact(input, data.waterWeights, expectedSamples) &&
            readFloatVectorExact(input, data.hardnessWeights, expectedSamples) &&
            readFloatVectorExact(input, data.resourceWeights, expectedSamples) &&
            readFloatVectorExact(input, data.erosionWeights, expectedSamples) &&
            readFloatVectorExact(input, data.flowWeights, expectedSamples);
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
        return readString(input, material.name, kMaxStringLength) &&
            readFiniteValue(input, material.baseColorFactor.x) &&
            readFiniteValue(input, material.baseColorFactor.y) &&
            readFiniteValue(input, material.baseColorFactor.z) &&
            readFiniteValue(input, material.baseColorFactor.w) &&
            readValue(input, alphaMode) &&
            isValidAlphaMode(alphaMode) &&
            readFiniteValue(input, material.alphaCutoff) &&
            readStrictBool(input, material.doubleSided) &&
            ((material.alphaMode = static_cast<AlphaMode>(alphaMode)), true);
    }

    static void writeModel(std::ostream& output, const Model& model)
    {
        const std::uint32_t vertexCount = static_cast<std::uint32_t>(model.vertices.size());
        const std::uint32_t faceCount = static_cast<std::uint32_t>(model.faces.size());
        const std::uint32_t normalCount = static_cast<std::uint32_t>(model.vertexNormals.size());
        const std::uint32_t faceColorCount = static_cast<std::uint32_t>(model.faceColors.size());
        const std::uint32_t texCoordCount = static_cast<std::uint32_t>(model.texCoords.size());
        const std::uint32_t texCoord1Count = static_cast<std::uint32_t>(model.texCoords1.size());
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

        writeValue(output, texCoord1Count);
        for (const Vec2& texCoord : model.texCoords1) {
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
        std::uint32_t texCoord1Count = 0u;
        std::uint32_t materialCount = 0u;
        if (!readString(input, model.assetKey, kMaxStringLength) ||
            !readCount(input, vertexCount, kMaxModelVertices)) {
            return false;
        }
        model.vertices.resize(vertexCount);
        for (Vec3& vertex : model.vertices) {
            if (!readVec3(input, vertex)) {
                return false;
            }
        }

        if (!readCount(input, faceCount, kMaxModelFaces)) {
            return false;
        }
        model.faces.resize(faceCount);
        for (Face& face : model.faces) {
            std::uint32_t indexCount = 0u;
            if (!readCount(input, indexCount, kMaxFaceIndices) || !readValue(input, face.materialIndex) || !isReasonableFaceIndexCount(indexCount)) {
                return false;
            }
            face.indices.resize(indexCount);
            for (int& index : face.indices) {
                if (!readValue(input, index) || index < 0 || static_cast<std::size_t>(index) >= model.vertices.size()) {
                    return false;
                }
            }
        }

        if (!readCount(input, faceColorCount, kMaxModelFaces)) {
            return false;
        }
        model.faceColors.resize(faceColorCount);
        for (Vec3& color : model.faceColors) {
            if (!readVec3(input, color)) {
                return false;
            }
        }

        if (!readCount(input, normalCount, kMaxModelVertices)) {
            return false;
        }
        model.vertexNormals.resize(normalCount);
        for (Vec3& normal : model.vertexNormals) {
            if (!readVec3(input, normal)) {
                return false;
            }
        }

        if (!readCount(input, texCoordCount, kMaxModelVertices)) {
            return false;
        }
        model.texCoords.resize(texCoordCount);
        for (Vec2& texCoord : model.texCoords) {
            if (!readVec2(input, texCoord)) {
                return false;
            }
        }

        if (!readCount(input, texCoord1Count, kMaxModelVertices)) {
            return false;
        }
        model.texCoords1.resize(texCoord1Count);
        for (Vec2& texCoord : model.texCoords1) {
            if (!readVec2(input, texCoord)) {
                return false;
            }
        }

        if (!readCount(input, materialCount, kMaxMaterials)) {
            return false;
        }
        model.materials.resize(materialCount);
        for (Material& material : model.materials) {
            if (!readMaterial(input, material)) {
                return false;
            }
        }

        if (!readStrictBool(input, model.hasTexCoords) ||
            !readStrictBool(input, model.hasTextureImages) ||
            !readStrictBool(input, model.hasPaintableMaterial)) {
            return false;
        }

        for (const Face& face : model.faces) {
            if (face.materialIndex < 0 || static_cast<std::size_t>(face.materialIndex) >= model.materials.size()) {
                return false;
            }
        }

        return true;
    }

    static std::string sanitizePathToken(const std::string& value)
    {
        std::string out;
        out.reserve(std::min<std::size_t>(value.size(), kMaxPathTokenLength));
        for (char ch : value.empty() ? std::string("default") : value) {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (std::isalnum(uch) || ch == '_' || ch == '-') {
                out.push_back(static_cast<char>(std::tolower(uch)));
            } else {
                out.push_back('_');
            }
            if (out.size() >= kMaxPathTokenLength) {
                break;
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
               << static_cast<int>(key.worldShape) << '|'
               << key.planet.radiusMeters << '|'
               << key.planet.gravitationalParameter << '|'
               << key.planet.rotationRateRadPerSec << '|'
               << key.planet.atmosphereHeightMeters << '|'
               << key.planet.localOrigin.latitudeDeg << '|'
               << key.planet.localOrigin.longitudeDeg << '|'
               << key.planet.localOrigin.altitudeMeters << '|'
               << key.planetTile.face << '|'
               << key.planetTile.lod << '|'
               << key.planetTile.tx << '|'
               << key.planetTile.ty << '|'
               << key.band << '|'
               << key.detail << '|'
               << key.tileScale << '|'
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
