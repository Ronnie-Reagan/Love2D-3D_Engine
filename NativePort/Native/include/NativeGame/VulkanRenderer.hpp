#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include "NativeGame/HudCanvas.hpp"
#include "NativeGame/RenderTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace NativeGame {

struct RendererLightingState {
    Vec3 sunDirection { 0.0f, 1.0f, 0.0f };
    Vec3 lightColor { 1.3f, 1.24f, 1.12f };
    Vec3 skyColor { 0.34f, 0.46f, 0.68f };
    Vec3 groundColor { 0.14f, 0.12f, 0.09f };
    Vec3 fogColor { 0.64f, 0.73f, 0.84f };
    Vec3 backgroundColor { 0.64f, 0.73f, 0.84f };
    float ambientStrength = 0.16f;
    float specularAmbientStrength = 0.18f;
    float bounceStrength = 0.10f;
    float fogDensity = 0.00018f;
    float fogHeightFalloff = 0.0017f;
    float exposureEv = 0.0f;
    float turbidity = 2.4f;
    bool shadowEnabled = true;
    float shadowSoftness = 1.6f;
    float shadowDistance = 1800.0f;
};

enum class RendererPressureTier {
    Normal = 0,
    Pressure = 1,
    Critical = 2
};

struct RendererFrameSettings {
    float renderScale = 1.0f;
    float dynamicRenderScale = 1.0f;
    bool textureMipmaps = true;
    std::size_t maxUploadBytes = std::numeric_limits<std::size_t>::max();
    RendererPressureTier pressureTier = RendererPressureTier::Normal;
};

struct RendererMemoryStats {
    std::size_t residentMeshBytes = 0;
    std::size_t residentMeshBudgetBytes = 0;
    std::size_t sceneTextureBytes = 0;
    std::size_t sceneTextureBudgetBytes = 0;
    std::size_t framebufferBytes = 0;
    std::size_t transientBufferBytes = 0;
    std::size_t uploadBytesThisFrame = 0;
    std::size_t maxUploadBytesThisFrame = 0;
};

class VulkanRenderer {
public:
    VulkanRenderer() = default;
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    ~VulkanRenderer();

    bool initialize(SDL_Window* window, const std::filesystem::path& shaderDirectory, std::string* errorMessage);
    void shutdown();

    bool render(
        const Camera& camera,
        const RendererFrameSettings& frameSettings,
        const RendererLightingState& lightingState,
        const std::vector<RenderObject>& opaqueObjects,
        const std::vector<RenderObject>& translucentObjects,
        const HudCanvas& hudCanvas,
        std::string* errorMessage);

    void setMemoryBudgets(std::size_t residentMeshBudgetBytes, std::size_t sceneTextureBudgetBytes);
    [[nodiscard]] const RendererMemoryStats& memoryStats() const;
    [[nodiscard]] const char* backendName() const;

private:
    struct SceneVertex {
        float px = 0.0f;
        float py = 0.0f;
        float pz = 0.0f;
        float nx = 0.0f;
        float ny = 1.0f;
        float nz = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
        float u = 0.0f;
        float v = 0.0f;
        float fogNear = 450.0f;
        float fogFar = 2600.0f;
        float alphaCutoff = -1.0f;
    };

    struct SceneDrawCommand {
        Uint32 firstVertex = 0;
        Uint32 vertexCount = 0;
        const RgbaImage* image = nullptr;
        bool cullBackfaces = false;
        Vec3 sortCenter {};
    };

    struct CachedSceneTexture {
        const RgbaImage* image = nullptr;
        std::uint64_t version = 0;
        SDL_GPUTexture* texture = nullptr;
        std::size_t textureBytes = 0;
        std::uint64_t lastFrameUsed = 0;
    };

    struct CachedResidentMesh {
        std::string key;
        const void* sourceVertexData = nullptr;
        std::size_t sourceVertexCount = 0;
        std::size_t sourceFaceCount = 0;
        std::size_t sourceMaterialCount = 0;
        std::uint64_t sourceModelRevision = 0;
        SDL_GPUBuffer* vertexBuffer = nullptr;
        std::size_t vertexBufferBytes = 0;
        std::vector<SceneDrawCommand> opaqueCommands;
        std::vector<SceneDrawCommand> translucentCommands;
        std::uint64_t lastFrameUsed = 0;
    };

    struct OverlayVertex {
        float x = 0.0f;
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
    };

    struct alignas(16) SceneUniforms {
        float viewProjection[16] {};
        float worldOrigin[4] {};
    };

    struct alignas(16) SceneLightingUniforms {
        float lightDirection[4] {};
        float lightColor[4] {};
        float skyColor[4] {};
        float groundColor[4] {};
        float fogColor[4] {};
        float cameraPosition[4] {};
        float ambientAndGi[4] {};
        float fogAndExposure[4] {};
        float shadowParams[4] {};
    };

    bool createPipelines(const std::filesystem::path& shaderDirectory, std::string* errorMessage);
    bool createOverlayGeometry(std::string* errorMessage);
    bool ensureSceneCapacity(std::size_t requiredBytes, std::string* errorMessage);
    bool ensureFramebufferResources(
        Uint32 drawableWidth,
        Uint32 drawableHeight,
        Uint32 sceneRenderWidth,
        Uint32 sceneRenderHeight,
        std::string* errorMessage);
    bool uploadHudTexture(SDL_GPUCopyPass* copyPass, const HudCanvas& hudCanvas, std::string* errorMessage);
    SDL_GPUTexture* ensureSceneTexture(
        SDL_GPUCopyPass* copyPass,
        const RgbaImage* image,
        std::vector<SDL_GPUTransferBuffer*>& uploadBuffers,
        std::vector<SDL_GPUTexture*>* mipmapTextures,
        std::string* errorMessage);
    void releaseSceneTextures();
    void releaseSceneTextureEntry(CachedSceneTexture& cached);
    void releaseResidentMeshes();
    void releaseResidentMeshEntry(CachedResidentMesh& mesh);
    void pruneSceneTextures(std::uint64_t minFrameToKeep);
    CachedResidentMesh* findResidentMesh(const std::string& key);
    bool isResidentMeshCurrent(const CachedResidentMesh& mesh, const RenderObject& object) const;
    CachedResidentMesh* ensureResidentMesh(
        SDL_GPUCopyPass* copyPass,
        const RenderObject& object,
        std::vector<SDL_GPUTransferBuffer*>& uploadBuffers,
        std::string* errorMessage);
    void pruneResidentMeshes(std::uint64_t minFrameToKeep);
    void appendObjectVertices(
        std::vector<SceneVertex>& vertices,
        std::vector<SceneDrawCommand>& opaqueCommands,
        std::vector<SceneDrawCommand>& translucentCommands,
        const RenderObject& object) const;
    bool reserveStreamingUploadBytes(std::size_t bytes);
    void resetFrameUploadStats(const RendererFrameSettings& frameSettings);
    void refreshMemoryStats();

    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* opaquePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* opaqueCullPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* translucentPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* translucentCullPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* overlayPipeline_ = nullptr;
    SDL_GPUSampler* sceneSampler_ = nullptr;
    SDL_GPUSampler* sceneMipmapSampler_ = nullptr;
    SDL_GPUSampler* overlaySampler_ = nullptr;
    SDL_GPUBuffer* sceneVertexBuffer_ = nullptr;
    SDL_GPUTransferBuffer* sceneTransferBuffer_ = nullptr;
    SDL_GPUBuffer* overlayVertexBuffer_ = nullptr;
    SDL_GPUTexture* sceneColorTexture_ = nullptr;
    SDL_GPUTexture* depthTexture_ = nullptr;
    SDL_GPUTexture* hudTexture_ = nullptr;
    SDL_GPUTransferBuffer* hudTransferBuffer_ = nullptr;
    Uint32 drawableWidth_ = 0;
    Uint32 drawableHeight_ = 0;
    Uint32 sceneRenderWidth_ = 0;
    Uint32 sceneRenderHeight_ = 0;
    std::size_t sceneCapacityBytes_ = 0;
    std::size_t hudTransferCapacityBytes_ = 0;
    SDL_GPUTextureFormat depthTextureFormat_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    std::vector<CachedSceneTexture> sceneTextures_;
    std::vector<CachedResidentMesh> residentMeshes_;
    std::uint64_t frameCounter_ = 0;
    std::size_t residentMeshBudgetBytes_ = 0;
    std::size_t sceneTextureBudgetBytes_ = 0;
    std::size_t maxStreamingUploadBytes_ = std::numeric_limits<std::size_t>::max();
    std::size_t streamingUploadBytesThisFrame_ = 0;
    RendererMemoryStats memoryStats_ {};
    RgbaImage fallbackWhiteImage_ { 1, 1, { 255, 255, 255, 255 }, 1 };
};

}  // namespace NativeGame
