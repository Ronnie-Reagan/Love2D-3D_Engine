#include "NativeGame/VulkanRenderer.hpp"

#include "imgui.h"
#include "imgui_impl_sdlgpu3.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace NativeGame {
namespace {

constexpr std::size_t kInitialSceneCapacityBytes = 2u * 1024u * 1024u;
constexpr float kDefaultNearClip = 0.05f;
constexpr float kDefaultFarClip = 42000.0f;

struct Mat4 {
    std::array<float, 16> m {};
};

struct alignas(16) CompositeUniforms {
    float cameraRight[4] {};
    float cameraUp[4] {};
    float cameraForward[4] {};
    float projectionParams[4] {};
    float cameraWorldPosition[4] {};
    float planetCenter[4] {};
    float planetParams[4] {};
    float lightDirection[4] {};
    float lightColor[4] {};
    float skyColor[4] {};
    float groundColor[4] {};
    float fogColor[4] {};
    float fogAndExposure[4] {};
};

bool fail(std::string* errorMessage, std::string_view context)
{
    if (errorMessage != nullptr) {
        *errorMessage = std::string(context) + ": " + SDL_GetError();
    }
    return false;
}

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

Mat4 multiply(const Mat4& lhs, const Mat4& rhs)
{
    Mat4 result;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) {
                value += lhs.m[(k * 4) + row] * rhs.m[(col * 4) + k];
            }
            result.m[(col * 4) + row] = value;
        }
    }
    return result;
}

Mat4 makePerspective(float fovRadians, float aspect, float zNear, float zFar)
{
    Mat4 projection;
    const float f = 1.0f / std::tan(fovRadians * 0.5f);
    projection.m[0] = f / std::max(0.0001f, aspect);
    projection.m[5] = f;
    projection.m[10] = (zNear + zFar) / (zNear - zFar);
    projection.m[11] = -1.0f;
    projection.m[14] = (2.0f * zNear * zFar) / (zNear - zFar);
    return projection;
}

Mat4 makeView(const Camera& camera)
{
    const Vec3 right = rightFromRotation(camera.rot);
    const Vec3 up = upFromRotation(camera.rot);
    const Vec3 forward = forwardFromRotation(camera.rot);

    Mat4 view;
    view.m[0] = right.x;
    view.m[4] = right.y;
    view.m[8] = right.z;
    view.m[12] = -dot(right, camera.pos);

    view.m[1] = up.x;
    view.m[5] = up.y;
    view.m[9] = up.z;
    view.m[13] = -dot(up, camera.pos);

    view.m[2] = -forward.x;
    view.m[6] = -forward.y;
    view.m[10] = -forward.z;
    view.m[14] = dot(forward, camera.pos);
    view.m[15] = 1.0f;
    return view;
}

Vec3 inverseScaleAdjustedNormal(const Vec3& normal, const Vec3& scale)
{
    const auto safeReciprocal = [](float value) {
        return 1.0f / std::max(std::fabs(value), 1.0e-4f);
    };
    return {
        normal.x * safeReciprocal(scale.x),
        normal.y * safeReciprocal(scale.y),
        normal.z * safeReciprocal(scale.z)
    };
}

Vec3 toneMapAces(const Vec3& color)
{
    const Vec3 clamped {
        std::max(0.0f, color.x),
        std::max(0.0f, color.y),
        std::max(0.0f, color.z)
    };
    return {
        clamp((clamped.x * (2.51f * clamped.x + 0.03f)) / (clamped.x * (2.43f * clamped.x + 0.59f) + 0.14f), 0.0f, 1.0f),
        clamp((clamped.y * (2.51f * clamped.y + 0.03f)) / (clamped.y * (2.43f * clamped.y + 0.59f) + 0.14f), 0.0f, 1.0f),
        clamp((clamped.z * (2.51f * clamped.z + 0.03f)) / (clamped.z * (2.43f * clamped.z + 0.59f) + 0.14f), 0.0f, 1.0f)
    };
}

Vec3 linearToSrgb(const Vec3& color)
{
    return {
        std::pow(std::max(0.0f, color.x), 1.0f / 2.2f),
        std::pow(std::max(0.0f, color.y), 1.0f / 2.2f),
        std::pow(std::max(0.0f, color.z), 1.0f / 2.2f)
    };
}

SDL_GPUTextureFormat chooseDepthTextureFormat(SDL_GPUDevice* device)
{
    constexpr SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    }
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}

Uint32 mipLevelCountForSize(const int width, const int height)
{
    Uint32 levels = 1;
    int maxDimension = std::max(width, height);
    while (maxDimension > 1) {
        maxDimension = std::max(1, maxDimension / 2);
        levels += 1;
    }
    return levels;
}

SDL_GPUShader* loadShader(
    SDL_GPUDevice* device,
    const std::filesystem::path& path,
    SDL_GPUShaderStage stage,
    Uint32 numSamplers,
    Uint32 numUniformBuffers,
    std::string* errorMessage)
{
    const std::vector<std::uint8_t> bytes = readBinaryFile(path);
    if (bytes.empty()) {
        fail(errorMessage, "failed to read shader: " + path.string());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo createInfo {};
    createInfo.code_size = bytes.size();
    createInfo.code = bytes.data();
    createInfo.entrypoint = "main";
    createInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
    createInfo.stage = stage;
    createInfo.num_samplers = numSamplers;
    createInfo.num_storage_textures = 0;
    createInfo.num_storage_buffers = 0;
    createInfo.num_uniform_buffers = numUniformBuffers;
    createInfo.props = 0;

    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &createInfo);
    if (shader == nullptr) {
        fail(errorMessage, "SDL_CreateGPUShader");
    }
    return shader;
}

void applyAlphaBlend(SDL_GPUColorTargetBlendState& blendState)
{
    blendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    blendState.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R |
        SDL_GPU_COLORCOMPONENT_G |
        SDL_GPU_COLORCOMPONENT_B |
        SDL_GPU_COLORCOMPONENT_A;
    blendState.enable_blend = true;
    blendState.enable_color_write_mask = true;
}

std::string buildResidentMeshKey(const RenderObject& object)
{
    std::string key = object.model != nullptr ? object.model->assetKey : std::string {};
    key += "|p=" + std::to_string(object.pos.x) + "," + std::to_string(object.pos.y) + "," + std::to_string(object.pos.z);
    key += "|r=" + std::to_string(object.rot.w) + "," + std::to_string(object.rot.x) + "," + std::to_string(object.rot.y) + "," + std::to_string(object.rot.z);
    key += "|s=" + std::to_string(object.scale.x) + "," + std::to_string(object.scale.y) + "," + std::to_string(object.scale.z);
    key += "|c=" + std::to_string(object.color.x) + "," + std::to_string(object.color.y) + "," + std::to_string(object.color.z);
    key += "|a=" + std::to_string(object.alpha);
    key += object.cullBackfaces ? "|cb=1" : "|cb=0";
    return key;
}

SDL_GPUPresentMode choosePresentMode(SDL_GPUDevice* device, SDL_Window* window, bool enableVsync)
{
    if (enableVsync || device == nullptr || window == nullptr) {
        return SDL_GPU_PRESENTMODE_VSYNC;
    }
    if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
        return SDL_GPU_PRESENTMODE_IMMEDIATE;
    }
    if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX)) {
        return SDL_GPU_PRESENTMODE_MAILBOX;
    }
    return SDL_GPU_PRESENTMODE_VSYNC;
}

std::size_t textureFormatBytesPerPixel(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
        return 2u;
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
    default:
        return 4u;
    }
}

std::size_t estimateTextureBytes(Uint32 width, Uint32 height, Uint32 levels, std::size_t bytesPerPixel)
{
    if (width == 0u || height == 0u || levels == 0u) {
        return 0u;
    }
    std::size_t totalBytes = 0;
    Uint32 levelWidth = std::max<Uint32>(1u, width);
    Uint32 levelHeight = std::max<Uint32>(1u, height);
    const Uint32 mipLevels = std::max<Uint32>(1u, levels);
    for (Uint32 level = 0; level < mipLevels; ++level) {
        totalBytes += static_cast<std::size_t>(levelWidth) * static_cast<std::size_t>(levelHeight) * bytesPerPixel;
        if (levelWidth == 1u && levelHeight == 1u) {
            break;
        }
        levelWidth = std::max<Uint32>(1u, levelWidth / 2u);
        levelHeight = std::max<Uint32>(1u, levelHeight / 2u);
    }
    return totalBytes;
}

}  // namespace

VulkanRenderer::~VulkanRenderer()
{
    shutdown();
}

bool VulkanRenderer::initialize(SDL_Window* window, const std::filesystem::path& shaderDirectory, std::string* errorMessage)
{
    shutdown();

    window_ = window;
    shaderDirectory_ = shaderDirectory;
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    if (device_ == nullptr) {
        window_ = nullptr;
        shaderDirectory_.clear();
        return fail(errorMessage, "SDL_CreateGPUDevice");
    }

    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        return fail(errorMessage, "SDL_ClaimWindowForGPUDevice");
    }

    SDL_SetGPUAllowedFramesInFlight(device_, 2);
    depthTextureFormat_ = chooseDepthTextureFormat(device_);
    presentMode_ = SDL_GPU_PRESENTMODE_VSYNC;
    swapchainFormat_ = SDL_GetGPUSwapchainTextureFormat(device_, window_);

    SDL_GPUSamplerCreateInfo samplerInfo {};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.mip_lod_bias = 0.0f;
    samplerInfo.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    samplerInfo.enable_anisotropy = false;
    samplerInfo.max_anisotropy = 1.0f;
    samplerInfo.enable_compare = false;
    samplerInfo.min_lod = 0.0f;
    samplerInfo.max_lod = 0.0f;
    sceneSampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
    if (sceneSampler_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUSampler");
    }

    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.mip_lod_bias = -0.85f;
    samplerInfo.enable_anisotropy = true;
    samplerInfo.max_anisotropy = 8.0f;
    samplerInfo.min_lod = -0.75f;
    samplerInfo.max_lod = 16.0f;
    sceneMipmapSampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
    if (sceneMipmapSampler_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUSampler");
    }

    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.mip_lod_bias = 0.0f;
    samplerInfo.enable_anisotropy = false;
    samplerInfo.max_anisotropy = 1.0f;
    samplerInfo.max_lod = 0.0f;
    overlaySampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
    if (overlaySampler_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUSampler");
    }

    if (!createPipelines(shaderDirectory, errorMessage)) {
        return false;
    }
    if (!createOverlayGeometry(errorMessage)) {
        return false;
    }
    if (!ensureSceneCapacity(kInitialSceneCapacityBytes, errorMessage)) {
        return false;
    }

    refreshMemoryStats();
    return true;
}

bool VulkanRenderer::prepareForDisplayChange(std::string* errorMessage)
{
    if (device_ == nullptr) {
        return true;
    }
    if (!SDL_WaitForGPUIdle(device_)) {
        return fail(errorMessage, "SDL_WaitForGPUIdle");
    }
    releaseFramebufferResources();
    return true;
}

bool VulkanRenderer::syncDisplayState(bool enableVsync, std::string* errorMessage)
{
    if (device_ == nullptr || window_ == nullptr) {
        return fail(errorMessage, "renderer not initialized");
    }
    if (!SDL_WaitForGPUIdle(device_)) {
        return fail(errorMessage, "SDL_WaitForGPUIdle");
    }

    presentMode_ = choosePresentMode(device_, window_, enableVsync);
    if (!SDL_SetGPUSwapchainParameters(
            device_,
            window_,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
            presentMode_)) {
        return fail(errorMessage, "SDL_SetGPUSwapchainParameters");
    }

    const SDL_GPUTextureFormat newSwapchainFormat = SDL_GetGPUSwapchainTextureFormat(device_, window_);
    if (newSwapchainFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
        return fail(errorMessage, "SDL_GetGPUSwapchainTextureFormat");
    }

    const bool pipelinesDirty =
        newSwapchainFormat != swapchainFormat_ ||
        opaquePipeline_ == nullptr ||
        opaqueCullPipeline_ == nullptr ||
        translucentPipeline_ == nullptr ||
        translucentCullPipeline_ == nullptr ||
        overlayPipeline_ == nullptr;
    swapchainFormat_ = newSwapchainFormat;
    if (pipelinesDirty) {
        releasePipelines();
        if (!createPipelines(shaderDirectory_, errorMessage)) {
            return false;
        }
    }

    releaseFramebufferResources();
    refreshMemoryStats();
    return true;
}

void VulkanRenderer::setMemoryBudgets(std::size_t residentMeshBudgetBytes, std::size_t sceneTextureBudgetBytes)
{
    residentMeshBudgetBytes_ = residentMeshBudgetBytes;
    sceneTextureBudgetBytes_ = sceneTextureBudgetBytes;
    refreshMemoryStats();
}

const RendererMemoryStats& VulkanRenderer::memoryStats() const
{
    return memoryStats_;
}

void VulkanRenderer::shutdown()
{
    if (device_ != nullptr) {
        releaseSceneTextures();
        releaseResidentMeshes();
        releaseFramebufferResources();
        SDL_ReleaseGPUBuffer(device_, overlayVertexBuffer_);
        SDL_ReleaseGPUTransferBuffer(device_, sceneTransferBuffer_);
        SDL_ReleaseGPUBuffer(device_, sceneVertexBuffer_);
        SDL_ReleaseGPUSampler(device_, sceneSampler_);
        SDL_ReleaseGPUSampler(device_, sceneMipmapSampler_);
        SDL_ReleaseGPUSampler(device_, overlaySampler_);
        releasePipelines();
        if (window_ != nullptr) {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
        }
        SDL_DestroyGPUDevice(device_);
    }

    window_ = nullptr;
    device_ = nullptr;
    opaquePipeline_ = nullptr;
    opaqueCullPipeline_ = nullptr;
    translucentPipeline_ = nullptr;
    translucentCullPipeline_ = nullptr;
    overlayPipeline_ = nullptr;
    sceneSampler_ = nullptr;
    overlaySampler_ = nullptr;
    sceneVertexBuffer_ = nullptr;
    sceneTransferBuffer_ = nullptr;
    overlayVertexBuffer_ = nullptr;
    sceneMipmapSampler_ = nullptr;
    swapchainFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    shaderDirectory_.clear();
    residentMeshIndex_.clear();
    frameCounter_ = 0;
    depthTextureFormat_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    residentMeshBudgetBytes_ = 0;
    sceneTextureBudgetBytes_ = 0;
    maxStreamingUploadBytes_ = std::numeric_limits<std::size_t>::max();
    streamingUploadBytesThisFrame_ = 0;
    drawableWidth_ = 0;
    drawableHeight_ = 0;
    sceneRenderWidth_ = 0;
    sceneRenderHeight_ = 0;
    sceneCapacityBytes_ = 0;
    hudTransferCapacityBytes_ = 0;
    memoryStats_ = {};
}

void VulkanRenderer::releaseSceneTextures()
{
    if (device_ == nullptr) {
        sceneTextures_.clear();
        return;
    }

    for (CachedSceneTexture& cached : sceneTextures_) {
        releaseSceneTextureEntry(cached);
    }
    sceneTextures_.clear();
    refreshMemoryStats();
}

void VulkanRenderer::releaseSceneTextureEntry(CachedSceneTexture& cached)
{
    if (device_ != nullptr && cached.texture != nullptr) {
        SDL_ReleaseGPUTexture(device_, cached.texture);
    }
    cached.texture = nullptr;
    cached.image = nullptr;
    cached.version = 0;
    cached.textureBytes = 0;
    cached.lastFrameUsed = 0;
}

void VulkanRenderer::releaseResidentMeshes()
{
    if (device_ == nullptr) {
        residentMeshes_.clear();
        residentMeshIndex_.clear();
        return;
    }

    for (CachedResidentMesh& mesh : residentMeshes_) {
        releaseResidentMeshEntry(mesh);
    }
    residentMeshes_.clear();
    residentMeshIndex_.clear();
    refreshMemoryStats();
}

void VulkanRenderer::releaseResidentMeshEntry(CachedResidentMesh& mesh)
{
    if (device_ != nullptr && mesh.vertexBuffer != nullptr) {
        SDL_ReleaseGPUBuffer(device_, mesh.vertexBuffer);
    }
    mesh.key.clear();
    mesh.sourceVertexData = nullptr;
    mesh.sourceVertexCount = 0;
    mesh.sourceFaceCount = 0;
    mesh.sourceMaterialCount = 0;
    mesh.sourceModelRevision = 0;
    mesh.vertexBuffer = nullptr;
    mesh.vertexBufferBytes = 0;
    mesh.opaqueCommands.clear();
    mesh.translucentCommands.clear();
    mesh.lastFrameUsed = 0;
}

void VulkanRenderer::releaseFramebufferResources()
{
    if (device_ != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, hudTransferBuffer_);
        SDL_ReleaseGPUTexture(device_, hudTexture_);
        SDL_ReleaseGPUTexture(device_, depthTexture_);
        SDL_ReleaseGPUTexture(device_, sceneColorTexture_);
    }
    hudTransferBuffer_ = nullptr;
    hudTexture_ = nullptr;
    depthTexture_ = nullptr;
    sceneColorTexture_ = nullptr;
    drawableWidth_ = 0;
    drawableHeight_ = 0;
    sceneRenderWidth_ = 0;
    sceneRenderHeight_ = 0;
    hudTransferCapacityBytes_ = 0;
}

void VulkanRenderer::releasePipelines()
{
    if (device_ != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device_, overlayPipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, translucentPipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, translucentCullPipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, opaquePipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, opaqueCullPipeline_);
    }
    overlayPipeline_ = nullptr;
    translucentPipeline_ = nullptr;
    translucentCullPipeline_ = nullptr;
    opaquePipeline_ = nullptr;
    opaqueCullPipeline_ = nullptr;
}

void VulkanRenderer::rebuildResidentMeshIndex()
{
    residentMeshIndex_.clear();
    residentMeshIndex_.reserve(residentMeshes_.size());
    for (std::size_t index = 0; index < residentMeshes_.size(); ++index) {
        if (!residentMeshes_[index].key.empty()) {
            residentMeshIndex_[residentMeshes_[index].key] = index;
        }
    }
}

VulkanRenderer::CachedResidentMesh* VulkanRenderer::findResidentMesh(const std::string& key)
{
    const auto it = residentMeshIndex_.find(key);
    if (it == residentMeshIndex_.end() || it->second >= residentMeshes_.size()) {
        return nullptr;
    }
    CachedResidentMesh& mesh = residentMeshes_[it->second];
    return mesh.key == key ? &mesh : nullptr;
}

bool VulkanRenderer::isResidentMeshCurrent(const CachedResidentMesh& mesh, const RenderObject& object) const
{
    if (object.model == nullptr) {
        return false;
    }

    return mesh.sourceModelRevision == object.model->cacheRevision &&
        mesh.sourceVertexData == object.model->vertices.data() &&
        mesh.sourceVertexCount == object.model->vertices.size() &&
        mesh.sourceFaceCount == object.model->faces.size() &&
        mesh.sourceMaterialCount == object.model->materials.size();
}

VulkanRenderer::CachedResidentMesh* VulkanRenderer::ensureResidentMesh(
    SDL_GPUCopyPass* copyPass,
    const RenderObject& object,
    std::vector<SDL_GPUTransferBuffer*>& uploadBuffers,
    std::string* errorMessage)
{
    (void)errorMessage;
    if (object.model == nullptr || object.model->assetKey.empty()) {
        return nullptr;
    }

    const std::string key = buildResidentMeshKey(object);
    CachedResidentMesh* mesh = findResidentMesh(key);
    if (mesh != nullptr && !isResidentMeshCurrent(*mesh, object)) {
        const std::string preservedKey = mesh->key;
        releaseResidentMeshEntry(*mesh);
        mesh->key = preservedKey;
    }

    if (mesh == nullptr) {
        residentMeshes_.push_back({});
        mesh = &residentMeshes_.back();
        mesh->key = key;
        residentMeshIndex_[key] = residentMeshes_.size() - 1u;
    }
    mesh->lastFrameUsed = frameCounter_;

    if (mesh->vertexBuffer != nullptr || (mesh->opaqueCommands.empty() && mesh->translucentCommands.empty() && object.model->faces.empty())) {
        return mesh;
    }
    if (copyPass == nullptr) {
        return nullptr;
    }

    std::vector<SceneVertex> residentVertices;
    appendObjectVertices(residentVertices, mesh->opaqueCommands, mesh->translucentCommands, object);

    mesh->sourceVertexData = object.model->vertices.data();
    mesh->sourceVertexCount = object.model->vertices.size();
    mesh->sourceFaceCount = object.model->faces.size();
    mesh->sourceMaterialCount = object.model->materials.size();
    mesh->sourceModelRevision = object.model->cacheRevision;

    if (residentVertices.empty()) {
        return mesh;
    }

    const std::size_t bufferBytes = residentVertices.size() * sizeof(SceneVertex);
    refreshMemoryStats();
    if (residentMeshBudgetBytes_ > 0u && (memoryStats_.residentMeshBytes + bufferBytes) > residentMeshBudgetBytes_) {
        pruneResidentMeshes(frameCounter_ > 2 ? frameCounter_ - 2 : 0);
        refreshMemoryStats();
        if ((memoryStats_.residentMeshBytes + bufferBytes) > residentMeshBudgetBytes_) {
            return nullptr;
        }
    }
    if (!reserveStreamingUploadBytes(bufferBytes)) {
        return nullptr;
    }

    SDL_GPUBufferCreateInfo bufferInfo {};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufferInfo.size = static_cast<Uint32>(bufferBytes);
    mesh->vertexBuffer = SDL_CreateGPUBuffer(device_, &bufferInfo);
    if (mesh->vertexBuffer == nullptr) {
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo transferInfo {};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(bufferBytes);
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (transferBuffer == nullptr) {
        SDL_ReleaseGPUBuffer(device_, mesh->vertexBuffer);
        mesh->vertexBuffer = nullptr;
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, true);
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
        SDL_ReleaseGPUBuffer(device_, mesh->vertexBuffer);
        mesh->vertexBuffer = nullptr;
        return nullptr;
    }

    std::memcpy(mapped, residentVertices.data(), bufferBytes);
    SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

    SDL_GPUTransferBufferLocation source {};
    source.transfer_buffer = transferBuffer;
    source.offset = 0;

    SDL_GPUBufferRegion destination {};
    destination.buffer = mesh->vertexBuffer;
    destination.offset = 0;
    destination.size = static_cast<Uint32>(bufferBytes);
    SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
    uploadBuffers.push_back(transferBuffer);

    mesh->vertexBufferBytes = bufferBytes;
    memoryStats_.uploadBytesThisFrame += bufferBytes;
    refreshMemoryStats();
    return mesh;
}

void VulkanRenderer::pruneResidentMeshes(std::uint64_t minFrameToKeep)
{
    if (device_ == nullptr) {
        residentMeshes_.clear();
        residentMeshIndex_.clear();
        return;
    }

    std::size_t residentBytes = 0;
    for (const CachedResidentMesh& mesh : residentMeshes_) {
        residentBytes += mesh.vertexBufferBytes;
    }

    residentMeshes_.erase(
        std::remove_if(
            residentMeshes_.begin(),
            residentMeshes_.end(),
            [&](CachedResidentMesh& mesh) {
                const bool stale = mesh.lastFrameUsed < minFrameToKeep;
                const bool overBudget =
                    residentMeshBudgetBytes_ > 0u &&
                    residentBytes > residentMeshBudgetBytes_ &&
                    mesh.lastFrameUsed < frameCounter_;
                if (!stale && !overBudget) {
                    return false;
                }
                residentBytes = residentBytes > mesh.vertexBufferBytes
                                    ? residentBytes - mesh.vertexBufferBytes
                                    : 0u;
                releaseResidentMeshEntry(mesh);
                return true;
            }),
        residentMeshes_.end());
    rebuildResidentMeshIndex();
    refreshMemoryStats();
}

void VulkanRenderer::pruneSceneTextures(std::uint64_t minFrameToKeep)
{
    if (device_ == nullptr) {
        sceneTextures_.clear();
        return;
    }

    std::size_t sceneTextureBytes = 0;
    for (const CachedSceneTexture& cached : sceneTextures_) {
        sceneTextureBytes += cached.textureBytes;
    }

    sceneTextures_.erase(
        std::remove_if(
            sceneTextures_.begin(),
            sceneTextures_.end(),
            [&](CachedSceneTexture& cached) {
                const bool stale = cached.lastFrameUsed < minFrameToKeep;
                const bool overBudget =
                    sceneTextureBudgetBytes_ > 0u &&
                    sceneTextureBytes > sceneTextureBudgetBytes_ &&
                    cached.lastFrameUsed < frameCounter_;
                if (!stale && !overBudget) {
                    return false;
                }
                sceneTextureBytes = sceneTextureBytes > cached.textureBytes
                                        ? sceneTextureBytes - cached.textureBytes
                                        : 0u;
                releaseSceneTextureEntry(cached);
                return true;
            }),
        sceneTextures_.end());
    refreshMemoryStats();
}

bool VulkanRenderer::createPipelines(const std::filesystem::path& shaderDirectory, std::string* errorMessage)
{
    const SDL_GPUTextureFormat swapchainFormat =
        swapchainFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID
            ? swapchainFormat_
            : SDL_GetGPUSwapchainTextureFormat(device_, window_);

    SDL_GPUShader* sceneVertexShader = loadShader(
        device_,
        shaderDirectory / "scene.vert.spv",
        SDL_GPU_SHADERSTAGE_VERTEX,
        0,
        1,
        errorMessage);
    if (sceneVertexShader == nullptr) {
        return false;
    }

    SDL_GPUShader* sceneFragmentShader = loadShader(
        device_,
        shaderDirectory / "scene.frag.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        1,
        2,
        errorMessage);
    if (sceneFragmentShader == nullptr) {
        SDL_ReleaseGPUShader(device_, sceneVertexShader);
        return false;
    }

    SDL_GPUShader* overlayVertexShader = loadShader(
        device_,
        shaderDirectory / "hud.vert.spv",
        SDL_GPU_SHADERSTAGE_VERTEX,
        0,
        0,
        errorMessage);
    if (overlayVertexShader == nullptr) {
        SDL_ReleaseGPUShader(device_, sceneVertexShader);
        SDL_ReleaseGPUShader(device_, sceneFragmentShader);
        return false;
    }

    SDL_GPUShader* overlayFragmentShader = loadShader(
        device_,
        shaderDirectory / "hud.frag.spv",
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        1,
        1,
        errorMessage);
    if (overlayFragmentShader == nullptr) {
        SDL_ReleaseGPUShader(device_, sceneVertexShader);
        SDL_ReleaseGPUShader(device_, sceneFragmentShader);
        SDL_ReleaseGPUShader(device_, overlayVertexShader);
        return false;
    }

    SDL_GPUVertexBufferDescription sceneVertexBufferDesc {};
    sceneVertexBufferDesc.slot = 0;
    sceneVertexBufferDesc.pitch = sizeof(SceneVertex);
    sceneVertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    std::array<SDL_GPUVertexAttribute, 6> sceneAttributes {};
    sceneAttributes[0].location = 0;
    sceneAttributes[0].buffer_slot = 0;
    sceneAttributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    sceneAttributes[0].offset = 0;

    sceneAttributes[1].location = 1;
    sceneAttributes[1].buffer_slot = 0;
    sceneAttributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    sceneAttributes[1].offset = sizeof(float) * 3;

    sceneAttributes[2].location = 2;
    sceneAttributes[2].buffer_slot = 0;
    sceneAttributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    sceneAttributes[2].offset = sizeof(float) * 6;

    sceneAttributes[3].location = 3;
    sceneAttributes[3].buffer_slot = 0;
    sceneAttributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    sceneAttributes[3].offset = sizeof(float) * 10;

    sceneAttributes[4].location = 4;
    sceneAttributes[4].buffer_slot = 0;
    sceneAttributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    sceneAttributes[4].offset = sizeof(float) * 12;

    sceneAttributes[5].location = 5;
    sceneAttributes[5].buffer_slot = 0;
    sceneAttributes[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    sceneAttributes[5].offset = sizeof(float) * 14;

    SDL_GPUColorTargetDescription sceneColorTarget {};
    sceneColorTarget.format = swapchainFormat;

    SDL_GPUGraphicsPipelineCreateInfo scenePipelineInfo {};
    scenePipelineInfo.vertex_shader = sceneVertexShader;
    scenePipelineInfo.fragment_shader = sceneFragmentShader;
    scenePipelineInfo.vertex_input_state.vertex_buffer_descriptions = &sceneVertexBufferDesc;
    scenePipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    scenePipelineInfo.vertex_input_state.vertex_attributes = sceneAttributes.data();
    scenePipelineInfo.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(sceneAttributes.size());
    scenePipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    scenePipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    scenePipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    scenePipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    scenePipelineInfo.rasterizer_state.enable_depth_clip = true;
    scenePipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    scenePipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    scenePipelineInfo.depth_stencil_state.enable_depth_test = true;
    scenePipelineInfo.depth_stencil_state.enable_depth_write = true;
    scenePipelineInfo.target_info.color_target_descriptions = &sceneColorTarget;
    scenePipelineInfo.target_info.num_color_targets = 1;
    scenePipelineInfo.target_info.depth_stencil_format = depthTextureFormat_;
    scenePipelineInfo.target_info.has_depth_stencil_target = true;

    opaquePipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &scenePipelineInfo);
    if (opaquePipeline_ == nullptr) {
        SDL_ReleaseGPUShader(device_, sceneVertexShader);
        SDL_ReleaseGPUShader(device_, sceneFragmentShader);
        SDL_ReleaseGPUShader(device_, overlayVertexShader);
        SDL_ReleaseGPUShader(device_, overlayFragmentShader);
        return fail(errorMessage, "SDL_CreateGPUGraphicsPipeline");
    }

    scenePipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    opaqueCullPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &scenePipelineInfo);
    if (opaqueCullPipeline_ == nullptr) {
        SDL_ReleaseGPUShader(device_, sceneVertexShader);
        SDL_ReleaseGPUShader(device_, sceneFragmentShader);
        SDL_ReleaseGPUShader(device_, overlayVertexShader);
        SDL_ReleaseGPUShader(device_, overlayFragmentShader);
        return fail(errorMessage, "SDL_CreateGPUGraphicsPipeline");
    }

    applyAlphaBlend(sceneColorTarget.blend_state);
    scenePipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    scenePipelineInfo.depth_stencil_state.enable_depth_write = false;
    translucentPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &scenePipelineInfo);
    if (translucentPipeline_ == nullptr) {
        SDL_ReleaseGPUShader(device_, sceneVertexShader);
        SDL_ReleaseGPUShader(device_, sceneFragmentShader);
        SDL_ReleaseGPUShader(device_, overlayVertexShader);
        SDL_ReleaseGPUShader(device_, overlayFragmentShader);
        return fail(errorMessage, "SDL_CreateGPUGraphicsPipeline");
    }

    scenePipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    translucentCullPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &scenePipelineInfo);
    if (translucentCullPipeline_ == nullptr) {
        SDL_ReleaseGPUShader(device_, sceneVertexShader);
        SDL_ReleaseGPUShader(device_, sceneFragmentShader);
        SDL_ReleaseGPUShader(device_, overlayVertexShader);
        SDL_ReleaseGPUShader(device_, overlayFragmentShader);
        return fail(errorMessage, "SDL_CreateGPUGraphicsPipeline");
    }

    SDL_GPUVertexBufferDescription overlayVertexBufferDesc {};
    overlayVertexBufferDesc.slot = 0;
    overlayVertexBufferDesc.pitch = sizeof(OverlayVertex);
    overlayVertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    std::array<SDL_GPUVertexAttribute, 2> overlayAttributes {};
    overlayAttributes[0].location = 0;
    overlayAttributes[0].buffer_slot = 0;
    overlayAttributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    overlayAttributes[0].offset = 0;

    overlayAttributes[1].location = 1;
    overlayAttributes[1].buffer_slot = 0;
    overlayAttributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    overlayAttributes[1].offset = sizeof(float) * 2;

    SDL_GPUColorTargetDescription overlayColorTarget {};
    overlayColorTarget.format = swapchainFormat;
    applyAlphaBlend(overlayColorTarget.blend_state);

    SDL_GPUGraphicsPipelineCreateInfo overlayPipelineInfo {};
    overlayPipelineInfo.vertex_shader = overlayVertexShader;
    overlayPipelineInfo.fragment_shader = overlayFragmentShader;
    overlayPipelineInfo.vertex_input_state.vertex_buffer_descriptions = &overlayVertexBufferDesc;
    overlayPipelineInfo.vertex_input_state.num_vertex_buffers = 1;
    overlayPipelineInfo.vertex_input_state.vertex_attributes = overlayAttributes.data();
    overlayPipelineInfo.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(overlayAttributes.size());
    overlayPipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    overlayPipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    overlayPipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    overlayPipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    overlayPipelineInfo.rasterizer_state.enable_depth_clip = true;
    overlayPipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    overlayPipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    overlayPipelineInfo.depth_stencil_state.enable_depth_test = false;
    overlayPipelineInfo.depth_stencil_state.enable_depth_write = false;
    overlayPipelineInfo.target_info.color_target_descriptions = &overlayColorTarget;
    overlayPipelineInfo.target_info.num_color_targets = 1;
    overlayPipelineInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    overlayPipelineInfo.target_info.has_depth_stencil_target = false;

    overlayPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &overlayPipelineInfo);
    if (overlayPipeline_ == nullptr) {
        SDL_ReleaseGPUShader(device_, sceneVertexShader);
        SDL_ReleaseGPUShader(device_, sceneFragmentShader);
        SDL_ReleaseGPUShader(device_, overlayVertexShader);
        SDL_ReleaseGPUShader(device_, overlayFragmentShader);
        return fail(errorMessage, "SDL_CreateGPUGraphicsPipeline");
    }

    SDL_ReleaseGPUShader(device_, sceneVertexShader);
    SDL_ReleaseGPUShader(device_, sceneFragmentShader);
    SDL_ReleaseGPUShader(device_, overlayVertexShader);
    SDL_ReleaseGPUShader(device_, overlayFragmentShader);
    return true;
}

bool VulkanRenderer::createOverlayGeometry(std::string* errorMessage)
{
    constexpr std::array<OverlayVertex, 6> overlayVertices {
        OverlayVertex { -1.0f, -1.0f, 0.0f, 1.0f },
        OverlayVertex { 1.0f, -1.0f, 1.0f, 1.0f },
        OverlayVertex { 1.0f, 1.0f, 1.0f, 0.0f },
        OverlayVertex { -1.0f, -1.0f, 0.0f, 1.0f },
        OverlayVertex { 1.0f, 1.0f, 1.0f, 0.0f },
        OverlayVertex { -1.0f, 1.0f, 0.0f, 0.0f }
    };

    SDL_GPUBufferCreateInfo bufferInfo {};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufferInfo.size = static_cast<Uint32>(sizeof(overlayVertices));
    overlayVertexBuffer_ = SDL_CreateGPUBuffer(device_, &bufferInfo);
    if (overlayVertexBuffer_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUBuffer");
    }

    SDL_GPUTransferBufferCreateInfo transferInfo {};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(sizeof(overlayVertices));
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (transferBuffer == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUTransferBuffer");
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, false);
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
        return fail(errorMessage, "SDL_MapGPUTransferBuffer");
    }

    std::memcpy(mapped, overlayVertices.data(), sizeof(overlayVertices));
    SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
        return fail(errorMessage, "SDL_AcquireGPUCommandBuffer");
    }

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    SDL_GPUTransferBufferLocation source {};
    source.transfer_buffer = transferBuffer;
    source.offset = 0;

    SDL_GPUBufferRegion destination {};
    destination.buffer = overlayVertexBuffer_;
    destination.offset = 0;
    destination.size = static_cast<Uint32>(sizeof(overlayVertices));
    SDL_UploadToGPUBuffer(copyPass, &source, &destination, false);
    SDL_EndGPUCopyPass(copyPass);

    const bool submitted = SDL_SubmitGPUCommandBuffer(commandBuffer);
    SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    if (!submitted) {
        return fail(errorMessage, "SDL_SubmitGPUCommandBuffer");
    }
    return true;
}

SDL_GPUTexture* VulkanRenderer::ensureSceneTexture(
    SDL_GPUCopyPass* copyPass,
    const RgbaImage* image,
    std::vector<SDL_GPUTransferBuffer*>& uploadBuffers,
    std::vector<SDL_GPUTexture*>* mipmapTextures,
    std::string* errorMessage)
{
    const RgbaImage* sourceImage = image != nullptr ? image : &fallbackWhiteImage_;
    if (sourceImage->width <= 0 ||
        sourceImage->height <= 0 ||
        sourceImage->pixels.size() != static_cast<std::size_t>(sourceImage->width) * static_cast<std::size_t>(sourceImage->height) * 4u) {
        return nullptr;
    }

    for (CachedSceneTexture& cached : sceneTextures_) {
        if (cached.image == sourceImage) {
            if (cached.version == sourceImage->version && cached.texture != nullptr) {
                cached.lastFrameUsed = frameCounter_;
                return cached.texture;
            }
            if (copyPass == nullptr) {
                break;
            }
            releaseSceneTextureEntry(cached);
            cached.image = sourceImage;
            break;
        }
    }

    if (copyPass == nullptr) {
        for (CachedSceneTexture& cached : sceneTextures_) {
            if (cached.image == &fallbackWhiteImage_ && cached.texture != nullptr) {
                cached.lastFrameUsed = frameCounter_;
                return cached.texture;
            }
        }
        fail(errorMessage, "scene texture was not prepared");
        return nullptr;
    }

    SDL_GPUTextureCreateInfo textureInfo {};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    textureInfo.width = static_cast<Uint32>(sourceImage->width);
    textureInfo.height = static_cast<Uint32>(sourceImage->height);
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = mipLevelCountForSize(sourceImage->width, sourceImage->height);
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    const std::size_t textureBytes = estimateTextureBytes(
        textureInfo.width,
        textureInfo.height,
        textureInfo.num_levels,
        textureFormatBytesPerPixel(textureInfo.format));
    refreshMemoryStats();
    if (sceneTextureBudgetBytes_ > 0u &&
        sourceImage != &fallbackWhiteImage_ &&
        (memoryStats_.sceneTextureBytes + textureBytes) > sceneTextureBudgetBytes_) {
        pruneSceneTextures(frameCounter_ > 2 ? frameCounter_ - 2 : 0);
        refreshMemoryStats();
        if ((memoryStats_.sceneTextureBytes + textureBytes) > sceneTextureBudgetBytes_) {
            return ensureSceneTexture(copyPass, nullptr, uploadBuffers, mipmapTextures, errorMessage);
        }
    }
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &textureInfo);
    if (texture == nullptr) {
        fail(errorMessage, "SDL_CreateGPUTexture");
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo transferInfo {};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(sourceImage->pixels.size());
    if (sourceImage != &fallbackWhiteImage_ && !reserveStreamingUploadBytes(sourceImage->pixels.size())) {
        SDL_ReleaseGPUTexture(device_, texture);
        return ensureSceneTexture(copyPass, nullptr, uploadBuffers, mipmapTextures, errorMessage);
    }
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (transferBuffer == nullptr) {
        SDL_ReleaseGPUTexture(device_, texture);
        fail(errorMessage, "SDL_CreateGPUTransferBuffer");
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, true);
    if (mapped == nullptr) {
        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
        SDL_ReleaseGPUTexture(device_, texture);
        fail(errorMessage, "SDL_MapGPUTransferBuffer");
        return nullptr;
    }
    std::memcpy(mapped, sourceImage->pixels.data(), sourceImage->pixels.size());
    SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

    SDL_GPUTextureTransferInfo source {};
    source.transfer_buffer = transferBuffer;
    source.offset = 0;
    source.pixels_per_row = static_cast<Uint32>(sourceImage->width);
    source.rows_per_layer = static_cast<Uint32>(sourceImage->height);

    SDL_GPUTextureRegion destination {};
    destination.texture = texture;
    destination.mip_level = 0;
    destination.layer = 0;
    destination.x = 0;
    destination.y = 0;
    destination.z = 0;
    destination.w = static_cast<Uint32>(sourceImage->width);
    destination.h = static_cast<Uint32>(sourceImage->height);
    destination.d = 1;
    SDL_UploadToGPUTexture(copyPass, &source, &destination, true);
    uploadBuffers.push_back(transferBuffer);
    memoryStats_.uploadBytesThisFrame += sourceImage->pixels.size();

    for (CachedSceneTexture& cached : sceneTextures_) {
        if (cached.image == sourceImage) {
            cached.texture = texture;
            cached.version = sourceImage->version;
            cached.textureBytes = textureBytes;
            cached.lastFrameUsed = frameCounter_;
            if (mipmapTextures != nullptr && textureInfo.num_levels > 1) {
                mipmapTextures->push_back(texture);
            }
            refreshMemoryStats();
            return texture;
        }
    }

    if (mipmapTextures != nullptr && textureInfo.num_levels > 1) {
        mipmapTextures->push_back(texture);
    }
    sceneTextures_.push_back({ sourceImage, sourceImage->version, texture, textureBytes, frameCounter_ });
    refreshMemoryStats();
    return texture;
}

bool VulkanRenderer::ensureSceneCapacity(std::size_t requiredBytes, std::string* errorMessage)
{
    if (requiredBytes <= sceneCapacityBytes_ && sceneVertexBuffer_ != nullptr && sceneTransferBuffer_ != nullptr) {
        return true;
    }

    std::size_t newCapacity = std::max(requiredBytes, kInitialSceneCapacityBytes);
    if (sceneCapacityBytes_ > 0) {
        newCapacity = std::max(newCapacity, sceneCapacityBytes_ * 2);
    }

    SDL_ReleaseGPUTransferBuffer(device_, sceneTransferBuffer_);
    SDL_ReleaseGPUBuffer(device_, sceneVertexBuffer_);
    sceneTransferBuffer_ = nullptr;
    sceneVertexBuffer_ = nullptr;

    SDL_GPUBufferCreateInfo bufferInfo {};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bufferInfo.size = static_cast<Uint32>(newCapacity);
    sceneVertexBuffer_ = SDL_CreateGPUBuffer(device_, &bufferInfo);
    if (sceneVertexBuffer_ == nullptr) {
        sceneCapacityBytes_ = 0;
        return fail(errorMessage, "SDL_CreateGPUBuffer");
    }

    SDL_GPUTransferBufferCreateInfo transferInfo {};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(newCapacity);
    sceneTransferBuffer_ = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (sceneTransferBuffer_ == nullptr) {
        SDL_ReleaseGPUBuffer(device_, sceneVertexBuffer_);
        sceneVertexBuffer_ = nullptr;
        sceneCapacityBytes_ = 0;
        return fail(errorMessage, "SDL_CreateGPUTransferBuffer");
    }

    sceneCapacityBytes_ = newCapacity;
    refreshMemoryStats();
    return true;
}

bool VulkanRenderer::ensureFramebufferResources(
    Uint32 drawableWidth,
    Uint32 drawableHeight,
    Uint32 sceneRenderWidth,
    Uint32 sceneRenderHeight,
    std::string* errorMessage)
{
    if (drawableWidth == drawableWidth_ &&
        drawableHeight == drawableHeight_ &&
        sceneRenderWidth == sceneRenderWidth_ &&
        sceneRenderHeight == sceneRenderHeight_ &&
        sceneColorTexture_ != nullptr &&
        depthTexture_ != nullptr &&
        hudTexture_ != nullptr &&
        hudTransferBuffer_ != nullptr) {
        return true;
    }

    if ((sceneColorTexture_ != nullptr || depthTexture_ != nullptr || hudTexture_ != nullptr || hudTransferBuffer_ != nullptr) &&
        !SDL_WaitForGPUIdle(device_)) {
        return fail(errorMessage, "SDL_WaitForGPUIdle");
    }
    releaseFramebufferResources();

    SDL_GPUTextureCreateInfo sceneColorInfo {};
    sceneColorInfo.type = SDL_GPU_TEXTURETYPE_2D;
    sceneColorInfo.format =
        swapchainFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID
            ? swapchainFormat_
            : SDL_GetGPUSwapchainTextureFormat(device_, window_);
    sceneColorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    sceneColorInfo.width = sceneRenderWidth;
    sceneColorInfo.height = sceneRenderHeight;
    sceneColorInfo.layer_count_or_depth = 1;
    sceneColorInfo.num_levels = 1;
    sceneColorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    sceneColorTexture_ = SDL_CreateGPUTexture(device_, &sceneColorInfo);
    if (sceneColorTexture_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUTexture");
    }

    SDL_GPUTextureCreateInfo depthInfo {};
    depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
    depthInfo.format = depthTextureFormat_;
    depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depthInfo.width = sceneRenderWidth;
    depthInfo.height = sceneRenderHeight;
    depthInfo.layer_count_or_depth = 1;
    depthInfo.num_levels = 1;
    depthInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    depthTexture_ = SDL_CreateGPUTexture(device_, &depthInfo);
    if (depthTexture_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUTexture");
    }

    SDL_GPUTextureCreateInfo hudInfo {};
    hudInfo.type = SDL_GPU_TEXTURETYPE_2D;
    hudInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    hudInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    hudInfo.width = drawableWidth;
    hudInfo.height = drawableHeight;
    hudInfo.layer_count_or_depth = 1;
    hudInfo.num_levels = 1;
    hudInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    hudTexture_ = SDL_CreateGPUTexture(device_, &hudInfo);
    if (hudTexture_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUTexture");
    }

    SDL_GPUTransferBufferCreateInfo transferInfo {};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = drawableWidth * drawableHeight * 4u;
    hudTransferBuffer_ = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (hudTransferBuffer_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUTransferBuffer");
    }

    drawableWidth_ = drawableWidth;
    drawableHeight_ = drawableHeight;
    sceneRenderWidth_ = sceneRenderWidth;
    sceneRenderHeight_ = sceneRenderHeight;
    hudTransferCapacityBytes_ = static_cast<std::size_t>(drawableWidth) * static_cast<std::size_t>(drawableHeight) * 4u;
    refreshMemoryStats();
    return true;
}

bool VulkanRenderer::uploadHudTexture(SDL_GPUCopyPass* copyPass, const HudCanvas& hudCanvas, std::string* errorMessage)
{
    if (hudTexture_ == nullptr || hudTransferBuffer_ == nullptr) {
        return fail(errorMessage, "HUD resources not initialized");
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, hudTransferBuffer_, true);
    if (mapped == nullptr) {
        return fail(errorMessage, "SDL_MapGPUTransferBuffer");
    }

    std::memcpy(mapped, hudCanvas.pixels().data(), hudCanvas.pixels().size());
    SDL_UnmapGPUTransferBuffer(device_, hudTransferBuffer_);

    SDL_GPUTextureTransferInfo source {};
    source.transfer_buffer = hudTransferBuffer_;
    source.offset = 0;
    source.pixels_per_row = static_cast<Uint32>(hudCanvas.width());
    source.rows_per_layer = static_cast<Uint32>(hudCanvas.height());

    SDL_GPUTextureRegion destination {};
    destination.texture = hudTexture_;
    destination.mip_level = 0;
    destination.layer = 0;
    destination.x = 0;
    destination.y = 0;
    destination.z = 0;
    destination.w = static_cast<Uint32>(hudCanvas.width());
    destination.h = static_cast<Uint32>(hudCanvas.height());
    destination.d = 1;
    SDL_UploadToGPUTexture(copyPass, &source, &destination, true);
    memoryStats_.uploadBytesThisFrame += hudCanvas.pixels().size();
    return true;
}

bool VulkanRenderer::reserveStreamingUploadBytes(std::size_t bytes)
{
    if (maxStreamingUploadBytes_ != std::numeric_limits<std::size_t>::max() &&
        (streamingUploadBytesThisFrame_ + bytes) > maxStreamingUploadBytes_) {
        return false;
    }
    streamingUploadBytesThisFrame_ += bytes;
    return true;
}

void VulkanRenderer::resetFrameUploadStats(const RendererFrameSettings& frameSettings)
{
    streamingUploadBytesThisFrame_ = 0;
    maxStreamingUploadBytes_ = frameSettings.maxUploadBytes;
    memoryStats_.uploadBytesThisFrame = 0;
    memoryStats_.maxUploadBytesThisFrame = frameSettings.maxUploadBytes;
}

void VulkanRenderer::refreshMemoryStats()
{
    std::size_t residentMeshBytes = 0;
    for (const CachedResidentMesh& mesh : residentMeshes_) {
        residentMeshBytes += mesh.vertexBufferBytes;
    }

    std::size_t sceneTextureBytes = 0;
    for (const CachedSceneTexture& cached : sceneTextures_) {
        sceneTextureBytes += cached.textureBytes;
    }

    std::size_t framebufferBytes = 0;
    framebufferBytes += estimateTextureBytes(
        sceneRenderWidth_,
        sceneRenderHeight_,
        sceneColorTexture_ != nullptr ? 1u : 0u,
        textureFormatBytesPerPixel(device_ != nullptr && window_ != nullptr ? SDL_GetGPUSwapchainTextureFormat(device_, window_) : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM));
    framebufferBytes += estimateTextureBytes(
        sceneRenderWidth_,
        sceneRenderHeight_,
        depthTexture_ != nullptr ? 1u : 0u,
        textureFormatBytesPerPixel(depthTextureFormat_));
    framebufferBytes += estimateTextureBytes(
        drawableWidth_,
        drawableHeight_,
        hudTexture_ != nullptr ? 1u : 0u,
        textureFormatBytesPerPixel(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM));

    memoryStats_.residentMeshBytes = residentMeshBytes;
    memoryStats_.residentMeshBudgetBytes = residentMeshBudgetBytes_;
    memoryStats_.sceneTextureBytes = sceneTextureBytes;
    memoryStats_.sceneTextureBudgetBytes = sceneTextureBudgetBytes_;
    memoryStats_.framebufferBytes = framebufferBytes;
    memoryStats_.transientBufferBytes = sceneCapacityBytes_ + hudTransferCapacityBytes_;
}

void VulkanRenderer::appendObjectVertices(
    std::vector<SceneVertex>& vertices,
    std::vector<SceneDrawCommand>& opaqueCommands,
    std::vector<SceneDrawCommand>& translucentCommands,
    const RenderObject& object) const
{
    if (object.model == nullptr || object.model->vertices.empty() || object.model->faces.empty() || object.alpha <= 0.0f) {
        return;
    }

    struct LocalBatch {
        const RgbaImage* image = nullptr;
        bool translucent = false;
        bool cullBackfaces = false;
        std::vector<SceneVertex> vertices;
        Vec3 sortCenterAccumulator {};
        std::size_t sortVertexCount = 0;
    };

    auto findOrCreateBatch = [](std::vector<LocalBatch>& batches, const RgbaImage* image, const bool translucent, const bool cullBackfaces) -> LocalBatch& {
        for (LocalBatch& batch : batches) {
            if (batch.image == image && batch.translucent == translucent && batch.cullBackfaces == cullBackfaces) {
                return batch;
            }
        }
        batches.push_back({ image, translucent, cullBackfaces, {} });
        return batches.back();
    };

    std::vector<Vec3> worldVertices;
    worldVertices.reserve(object.model->vertices.size());
    for (const Vec3& vertex : object.model->vertices) {
        const Vec3 scaled = hadamard(vertex, object.scale);
        worldVertices.push_back(rotateVector(object.rot, scaled) + object.pos);
    }

    const float fogNear = object.fogNear;
    const float fogFar = std::max(object.fogNear + 1.0f, object.fogFar);
    std::vector<LocalBatch> batches;

    for (std::size_t faceIndex = 0; faceIndex < object.model->faces.size(); ++faceIndex) {
        const Face& face = object.model->faces[faceIndex];
        if (face.indices.size() < 3) {
            continue;
        }

        std::vector<int> indices;
        indices.reserve(face.indices.size());
        bool validFace = true;
        for (int index : face.indices) {
            if (index < 0 || index >= static_cast<int>(worldVertices.size())) {
                validFace = false;
                break;
            }
            indices.push_back(index);
        }
        if (!validFace || indices.size() < 3) {
            continue;
        }

        const Vec3& a = worldVertices[indices[0]];
        const Vec3& b = worldVertices[indices[1]];
        const Vec3& c = worldVertices[indices[2]];
        const Vec3 normal = normalize(cross(b - a, c - a), { 0.0f, 1.0f, 0.0f });
        if (lengthSquared(normal) <= 1.0e-8f) {
            continue;
        }

        const int requestedMaterialIndex = face.materialIndex;
        const Material* material =
            requestedMaterialIndex >= 0 &&
            static_cast<std::size_t>(requestedMaterialIndex) < object.model->materials.size()
                ? &object.model->materials[static_cast<std::size_t>(requestedMaterialIndex)]
                : nullptr;
        const bool cullBackfaces = object.cullBackfaces && !(material != nullptr && material->doubleSided);

        const Vec4 factor =
            material != nullptr
                ? material->baseColorFactor
                : Vec4 { object.color.x, object.color.y, object.color.z, 1.0f };
        const Vec3 baseColor =
            faceIndex < object.model->faceColors.size()
                ? object.model->faceColors[faceIndex]
                : Vec3 { factor.x, factor.y, factor.z };
        const float alpha = object.alpha * factor.w;
        const float alphaCutoff =
            material != nullptr && material->alphaMode == AlphaMode::Mask
                ? material->alphaCutoff
                : -1.0f;
        const bool translucent =
            alpha < 0.999f || (material != nullptr && material->alphaMode == AlphaMode::Blend);

        const RgbaImage* image = nullptr;
        if (material != nullptr &&
            material->baseColorTexture.valid() &&
            static_cast<std::size_t>(material->baseColorTexture.imageIndex) < object.model->images.size()) {
            image = &object.model->images[static_cast<std::size_t>(material->baseColorTexture.imageIndex)];
        }

        LocalBatch& batch = findOrCreateBatch(batches, image, translucent, cullBackfaces);

        const auto pushVertex = [&](const int sourceIndex) {
            const Vec3& position = worldVertices[static_cast<std::size_t>(sourceIndex)];
            Vec3 sourceNormal = normal;
            if (static_cast<std::size_t>(sourceIndex) < object.model->vertexNormals.size() &&
                lengthSquared(object.model->vertexNormals[static_cast<std::size_t>(sourceIndex)]) > 1.0e-8f) {
                sourceNormal = object.model->vertexNormals[static_cast<std::size_t>(sourceIndex)];
            }
            const Vec3 worldNormal = normalize(
                rotateVector(object.rot, inverseScaleAdjustedNormal(sourceNormal, object.scale)),
                normal);
            const int texCoordSet = material != nullptr ? std::max(0, material->baseColorTexture.texCoord) : 0;
            const Vec2 uv =
                texCoordSet == 1 && static_cast<std::size_t>(sourceIndex) < object.model->texCoords1.size()
                    ? object.model->texCoords1[static_cast<std::size_t>(sourceIndex)]
                    : (static_cast<std::size_t>(sourceIndex) < object.model->texCoords.size()
                        ? object.model->texCoords[static_cast<std::size_t>(sourceIndex)]
                        : Vec2 { 0.0f, 0.0f });
            batch.vertices.push_back({
                position.x,
                position.y,
                position.z,
                worldNormal.x,
                worldNormal.y,
                worldNormal.z,
                baseColor.x,
                baseColor.y,
                baseColor.z,
                alpha,
                uv.x,
                uv.y,
                fogNear,
                fogFar,
                alphaCutoff
            });
            batch.sortCenterAccumulator += position;
            batch.sortVertexCount += 1;
        };

        for (std::size_t i = 1; (i + 1) < indices.size(); ++i) {
            pushVertex(indices[0]);
            pushVertex(indices[i]);
            pushVertex(indices[i + 1]);
        }
    }

    for (LocalBatch& batch : batches) {
        if (batch.vertices.empty()) {
            continue;
        }
        SceneDrawCommand command {};
        command.firstVertex = static_cast<Uint32>(vertices.size());
        command.vertexCount = static_cast<Uint32>(batch.vertices.size());
        command.image = batch.image;
        command.cullBackfaces = batch.cullBackfaces;
        command.fogNear = fogNear;
        command.fogFar = fogFar;
        command.sortCenter =
            batch.sortVertexCount > 0
                ? batch.sortCenterAccumulator / static_cast<float>(batch.sortVertexCount)
                : object.pos;
        vertices.insert(vertices.end(), batch.vertices.begin(), batch.vertices.end());
        if (batch.translucent) {
            translucentCommands.push_back(command);
        } else {
            opaqueCommands.push_back(command);
        }
    }
}

bool VulkanRenderer::render(
    const Camera& camera,
    const RendererFrameSettings& frameSettings,
    const RendererLightingState& lightingState,
    const std::vector<RenderObject>& opaqueObjects,
    const std::vector<RenderObject>& translucentObjects,
    const HudCanvas& hudCanvas,
    ImDrawData* imguiDrawData,
    std::string* errorMessage)
{
    if (device_ == nullptr || window_ == nullptr) {
        return fail(errorMessage, "renderer not initialized");
    }

    ++frameCounter_;
    resetFrameUploadStats(frameSettings);
    refreshMemoryStats();
    std::vector<SceneVertex> sceneVertices;
    sceneVertices.reserve((opaqueObjects.size() + translucentObjects.size()) * 512u);
    std::vector<SceneDrawCommand> opaqueCommands;
    std::vector<SceneDrawCommand> translucentCommands;
    std::vector<const RenderObject*> residentOpaqueObjects;
    std::vector<const RenderObject*> residentTranslucentObjects;
    std::vector<const CachedResidentMesh*> residentOpaqueMeshes;
    std::vector<const CachedResidentMesh*> residentTranslucentMeshes;

    for (const RenderObject& object : opaqueObjects) {
        if (object.gpuResident && object.model != nullptr && !object.model->assetKey.empty()) {
            residentOpaqueObjects.push_back(&object);
            continue;
        }
        appendObjectVertices(sceneVertices, opaqueCommands, translucentCommands, object);
    }

    for (const RenderObject& object : translucentObjects) {
        if (object.gpuResident && object.model != nullptr && !object.model->assetKey.empty()) {
            residentTranslucentObjects.push_back(&object);
            continue;
        }
        appendObjectVertices(sceneVertices, opaqueCommands, translucentCommands, object);
    }

    const std::size_t requiredBytes = std::max<std::size_t>(sizeof(SceneVertex), sceneVertices.size() * sizeof(SceneVertex));
    if (!ensureSceneCapacity(requiredBytes, errorMessage)) {
        return false;
    }
    if ((swapchainFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID || opaquePipeline_ == nullptr) &&
        !syncDisplayState(presentMode_ == SDL_GPU_PRESENTMODE_VSYNC, errorMessage)) {
        return false;
    }

    std::vector<SDL_GPUTransferBuffer*> uploadBuffers;
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr) {
        return fail(errorMessage, "SDL_AcquireGPUCommandBuffer");
    }
    bool swapchainAcquired = false;
    const auto releaseUploadBuffers = [&]() {
        for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
            SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
        }
        uploadBuffers.clear();
    };
    const auto abandonFrame = [&]() -> bool {
        releaseUploadBuffers();
        if (!swapchainAcquired) {
            SDL_CancelGPUCommandBuffer(commandBuffer);
            return false;
        }
        if (!SDL_SubmitGPUCommandBuffer(commandBuffer) && errorMessage != nullptr && errorMessage->empty()) {
            fail(errorMessage, "SDL_SubmitGPUCommandBuffer");
        }
        return false;
    };
    const auto completeDroppedFrame = [&]() -> bool {
        releaseUploadBuffers();
        if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
            return fail(errorMessage, "SDL_SubmitGPUCommandBuffer");
        }
        return true;
    };

    SDL_GPUTexture* swapchainTexture = nullptr;
    Uint32 drawableWidth = 0;
    Uint32 drawableHeight = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window_, &swapchainTexture, &drawableWidth, &drawableHeight)) {
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return fail(errorMessage, "SDL_WaitAndAcquireGPUSwapchainTexture");
    }
    swapchainAcquired = true;
    if (swapchainTexture == nullptr) {
        return completeDroppedFrame();
    }

    if (drawableWidth != static_cast<Uint32>(hudCanvas.width()) || drawableHeight != static_cast<Uint32>(hudCanvas.height())) {
        return completeDroppedFrame();
    }

    const float dynamicRenderScale = clamp(
        std::isfinite(frameSettings.dynamicRenderScale) ? frameSettings.dynamicRenderScale : 1.0f,
        0.5f,
        1.5f);
    const float renderScale = clamp(
        (std::isfinite(frameSettings.renderScale) ? frameSettings.renderScale : 1.0f) * dynamicRenderScale,
        0.25f,
        2.0f);
    const Uint32 sceneRenderWidth = std::max<Uint32>(
        1u,
        static_cast<Uint32>(std::lround(static_cast<double>(drawableWidth) * renderScale)));
    const Uint32 sceneRenderHeight = std::max<Uint32>(
        1u,
        static_cast<Uint32>(std::lround(static_cast<double>(drawableHeight) * renderScale)));

    if (!ensureFramebufferResources(drawableWidth, drawableHeight, sceneRenderWidth, sceneRenderHeight, errorMessage)) {
        return abandonFrame();
    }

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    std::vector<SDL_GPUTexture*> mipmapTextures;
    if (ensureSceneTexture(copyPass, nullptr, uploadBuffers, &mipmapTextures, errorMessage) == nullptr) {
        SDL_EndGPUCopyPass(copyPass);
        return abandonFrame();
    }
    if (!sceneVertices.empty()) {
        void* mapped = SDL_MapGPUTransferBuffer(device_, sceneTransferBuffer_, true);
        if (mapped == nullptr) {
            SDL_EndGPUCopyPass(copyPass);
            fail(errorMessage, "SDL_MapGPUTransferBuffer");
            return abandonFrame();
        }

        std::memcpy(mapped, sceneVertices.data(), sceneVertices.size() * sizeof(SceneVertex));
        SDL_UnmapGPUTransferBuffer(device_, sceneTransferBuffer_);

        SDL_GPUTransferBufferLocation source {};
        source.transfer_buffer = sceneTransferBuffer_;
        source.offset = 0;

        SDL_GPUBufferRegion destination {};
        destination.buffer = sceneVertexBuffer_;
        destination.offset = 0;
        destination.size = static_cast<Uint32>(sceneVertices.size() * sizeof(SceneVertex));
        SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
        memoryStats_.uploadBytesThisFrame += sceneVertices.size() * sizeof(SceneVertex);
    }

    // Resident mesh pointers are cached for later draw submission. Reserve the
    // backing store up front so `ensureResidentMesh()` cannot invalidate them
    // by growing `residentMeshes_` mid-frame.
    residentMeshes_.reserve(
        residentMeshes_.size() +
        residentOpaqueObjects.size() +
        residentTranslucentObjects.size());

    residentOpaqueMeshes.reserve(residentOpaqueObjects.size());
    for (const RenderObject* object : residentOpaqueObjects) {
        CachedResidentMesh* mesh = ensureResidentMesh(copyPass, *object, uploadBuffers, errorMessage);
        if (mesh == nullptr) {
            continue;
        }
        if (!mesh->opaqueCommands.empty()) {
            residentOpaqueMeshes.push_back(mesh);
        }
        if (!mesh->translucentCommands.empty()) {
            residentTranslucentMeshes.push_back(mesh);
        }
    }

    residentTranslucentMeshes.reserve(residentTranslucentObjects.size());
    for (const RenderObject* object : residentTranslucentObjects) {
        CachedResidentMesh* mesh = ensureResidentMesh(copyPass, *object, uploadBuffers, errorMessage);
        if (mesh == nullptr) {
            continue;
        }
        if (!mesh->opaqueCommands.empty()) {
            residentOpaqueMeshes.push_back(mesh);
        }
        if (!mesh->translucentCommands.empty()) {
            residentTranslucentMeshes.push_back(mesh);
        }
    }

    for (const SceneDrawCommand& command : opaqueCommands) {
        if (ensureSceneTexture(copyPass, command.image, uploadBuffers, &mipmapTextures, errorMessage) == nullptr) {
            SDL_EndGPUCopyPass(copyPass);
            return abandonFrame();
        }
    }
    for (const CachedResidentMesh* mesh : residentOpaqueMeshes) {
        for (const SceneDrawCommand& command : mesh->opaqueCommands) {
            if (ensureSceneTexture(copyPass, command.image, uploadBuffers, &mipmapTextures, errorMessage) == nullptr) {
                SDL_EndGPUCopyPass(copyPass);
                return abandonFrame();
            }
        }
    }
    for (const SceneDrawCommand& command : translucentCommands) {
        if (ensureSceneTexture(copyPass, command.image, uploadBuffers, &mipmapTextures, errorMessage) == nullptr) {
            SDL_EndGPUCopyPass(copyPass);
            return abandonFrame();
        }
    }
    for (const CachedResidentMesh* mesh : residentTranslucentMeshes) {
        for (const SceneDrawCommand& command : mesh->translucentCommands) {
            if (ensureSceneTexture(copyPass, command.image, uploadBuffers, &mipmapTextures, errorMessage) == nullptr) {
                SDL_EndGPUCopyPass(copyPass);
                return abandonFrame();
            }
        }
    }

    if (!uploadHudTexture(copyPass, hudCanvas, errorMessage)) {
        SDL_EndGPUCopyPass(copyPass);
        return abandonFrame();
    }
    SDL_EndGPUCopyPass(copyPass);

    for (SDL_GPUTexture* mipTexture : mipmapTextures) {
        SDL_GenerateMipmapsForGPUTexture(commandBuffer, mipTexture);
    }

    const float aspect = static_cast<float>(sceneRenderWidth) / std::max(1.0f, static_cast<float>(sceneRenderHeight));
    const float nearClip = std::max(
        0.001f,
        std::isfinite(camera.nearClipMeters) ? camera.nearClipMeters : kDefaultNearClip);
    const float farClip = std::max(
        nearClip + 1.0f,
        std::isfinite(camera.farClipMeters) ? camera.farClipMeters : kDefaultFarClip);
    Camera relativeCamera = camera;
    relativeCamera.pos = {};
    const Mat4 view = makeView(relativeCamera);
    const Mat4 projection = makePerspective(camera.fovRadians, aspect, nearClip, farClip);
    const Mat4 viewProjection = multiply(projection, view);
    const Vec3 cameraRight = rightFromRotation(camera.rot);
    const Vec3 cameraUp = upFromRotation(camera.rot);
    const Vec3 cameraForward = forwardFromRotation(camera.rot);

    SceneUniforms uniforms {};
    std::copy(viewProjection.m.begin(), viewProjection.m.end(), std::begin(uniforms.viewProjection));
    uniforms.worldOrigin[0] = camera.pos.x;
    uniforms.worldOrigin[1] = camera.pos.y;
    uniforms.worldOrigin[2] = camera.pos.z;
    uniforms.worldOrigin[3] = 1.0f;
    SDL_PushGPUVertexUniformData(commandBuffer, 0, &uniforms, sizeof(uniforms));

    const Vec3 normalizedSun = normalize(lightingState.sunDirection, { 0.0f, 1.0f, 0.0f });
    SceneLightingUniforms lightingUniforms {};
    lightingUniforms.lightDirection[0] = normalizedSun.x;
    lightingUniforms.lightDirection[1] = normalizedSun.y;
    lightingUniforms.lightDirection[2] = normalizedSun.z;
    lightingUniforms.lightDirection[3] = 1.0f;
    lightingUniforms.lightColor[0] = lightingState.lightColor.x;
    lightingUniforms.lightColor[1] = lightingState.lightColor.y;
    lightingUniforms.lightColor[2] = lightingState.lightColor.z;
    lightingUniforms.lightColor[3] = 1.0f;
    lightingUniforms.skyColor[0] = lightingState.skyColor.x;
    lightingUniforms.skyColor[1] = lightingState.skyColor.y;
    lightingUniforms.skyColor[2] = lightingState.skyColor.z;
    lightingUniforms.skyColor[3] = 1.0f;
    lightingUniforms.groundColor[0] = lightingState.groundColor.x;
    lightingUniforms.groundColor[1] = lightingState.groundColor.y;
    lightingUniforms.groundColor[2] = lightingState.groundColor.z;
    lightingUniforms.groundColor[3] = 1.0f;
    lightingUniforms.fogColor[0] = lightingState.fogColor.x;
    lightingUniforms.fogColor[1] = lightingState.fogColor.y;
    lightingUniforms.fogColor[2] = lightingState.fogColor.z;
    lightingUniforms.fogColor[3] = 1.0f;
    lightingUniforms.cameraPosition[0] = camera.pos.x;
    lightingUniforms.cameraPosition[1] = camera.pos.y;
    lightingUniforms.cameraPosition[2] = camera.pos.z;
    lightingUniforms.cameraPosition[3] = 1.0f;
    lightingUniforms.ambientAndGi[0] = lightingState.ambientStrength;
    lightingUniforms.ambientAndGi[1] = lightingState.specularAmbientStrength;
    lightingUniforms.ambientAndGi[2] = lightingState.bounceStrength;
    lightingUniforms.ambientAndGi[3] = lightingState.turbidity;
    lightingUniforms.fogAndExposure[0] = lightingState.fogDensity;
    lightingUniforms.fogAndExposure[1] = lightingState.fogHeightFalloff;
    lightingUniforms.fogAndExposure[2] = lightingState.exposureEv;
    lightingUniforms.fogAndExposure[3] = lightingState.turbidity;
    lightingUniforms.shadowParams[0] = lightingState.shadowEnabled ? 1.0f : 0.0f;
    lightingUniforms.shadowParams[1] = lightingState.shadowSoftness;
    lightingUniforms.shadowParams[2] = lightingState.shadowDistance;
    lightingUniforms.shadowParams[3] = 0.0f;
    SDL_PushGPUFragmentUniformData(commandBuffer, 0, &lightingUniforms, sizeof(lightingUniforms));
    const auto pushObjectUniforms = [&](const SceneDrawCommand& command) {
        SceneObjectUniforms objectUniforms {};
        objectUniforms.fogRange[0] = command.fogNear;
        objectUniforms.fogRange[1] = std::max(command.fogNear + 1.0f, command.fogFar);
        SDL_PushGPUFragmentUniformData(commandBuffer, 1, &objectUniforms, sizeof(objectUniforms));
    };

    const float tanHalfFovY = std::tan(camera.fovRadians * 0.5f);
    CompositeUniforms compositeUniforms {};
    compositeUniforms.cameraRight[0] = cameraRight.x;
    compositeUniforms.cameraRight[1] = cameraRight.y;
    compositeUniforms.cameraRight[2] = cameraRight.z;
    compositeUniforms.cameraRight[3] = 0.0f;
    compositeUniforms.cameraUp[0] = cameraUp.x;
    compositeUniforms.cameraUp[1] = cameraUp.y;
    compositeUniforms.cameraUp[2] = cameraUp.z;
    compositeUniforms.cameraUp[3] = 0.0f;
    compositeUniforms.cameraForward[0] = cameraForward.x;
    compositeUniforms.cameraForward[1] = cameraForward.y;
    compositeUniforms.cameraForward[2] = cameraForward.z;
    compositeUniforms.cameraForward[3] = 0.0f;
    compositeUniforms.projectionParams[0] = tanHalfFovY * aspect;
    compositeUniforms.projectionParams[1] = tanHalfFovY;
    compositeUniforms.projectionParams[2] = 0.0f;
    compositeUniforms.projectionParams[3] = 0.0f;
    compositeUniforms.cameraWorldPosition[0] = camera.pos.x;
    compositeUniforms.cameraWorldPosition[1] = camera.pos.y;
    compositeUniforms.cameraWorldPosition[2] = camera.pos.z;
    compositeUniforms.cameraWorldPosition[3] = 1.0f;
    compositeUniforms.planetCenter[0] = camera.planetCenter.x;
    compositeUniforms.planetCenter[1] = camera.planetCenter.y;
    compositeUniforms.planetCenter[2] = camera.planetCenter.z;
    compositeUniforms.planetCenter[3] = 1.0f;
    compositeUniforms.planetParams[0] = camera.planetRadiusMeters;
    compositeUniforms.planetParams[1] = camera.atmosphereTopMeters;
    compositeUniforms.planetParams[2] = camera.worldShape == WorldShape::Planet ? 1.0f : 0.0f;
    compositeUniforms.planetParams[3] = 0.0f;
    compositeUniforms.lightDirection[0] = lightingUniforms.lightDirection[0];
    compositeUniforms.lightDirection[1] = lightingUniforms.lightDirection[1];
    compositeUniforms.lightDirection[2] = lightingUniforms.lightDirection[2];
    compositeUniforms.lightDirection[3] = 1.0f;
    compositeUniforms.lightColor[0] = lightingUniforms.lightColor[0];
    compositeUniforms.lightColor[1] = lightingUniforms.lightColor[1];
    compositeUniforms.lightColor[2] = lightingUniforms.lightColor[2];
    compositeUniforms.lightColor[3] = 1.0f;
    compositeUniforms.skyColor[0] = lightingUniforms.skyColor[0];
    compositeUniforms.skyColor[1] = lightingUniforms.skyColor[1];
    compositeUniforms.skyColor[2] = lightingUniforms.skyColor[2];
    compositeUniforms.skyColor[3] = 1.0f;
    compositeUniforms.groundColor[0] = lightingUniforms.groundColor[0];
    compositeUniforms.groundColor[1] = lightingUniforms.groundColor[1];
    compositeUniforms.groundColor[2] = lightingUniforms.groundColor[2];
    compositeUniforms.groundColor[3] = 1.0f;
    compositeUniforms.fogColor[0] = lightingUniforms.fogColor[0];
    compositeUniforms.fogColor[1] = lightingUniforms.fogColor[1];
    compositeUniforms.fogColor[2] = lightingUniforms.fogColor[2];
    compositeUniforms.fogColor[3] = 1.0f;
    compositeUniforms.fogAndExposure[0] = lightingUniforms.fogAndExposure[0];
    compositeUniforms.fogAndExposure[1] = lightingUniforms.fogAndExposure[1];
    compositeUniforms.fogAndExposure[2] = lightingUniforms.fogAndExposure[2];
    compositeUniforms.fogAndExposure[3] = lightingUniforms.fogAndExposure[3];

    struct TranslucentSubmission {
        SDL_GPUBuffer* vertexBuffer = nullptr;
        const SceneDrawCommand* command = nullptr;
        float distanceSquared = 0.0f;
    };
    std::vector<TranslucentSubmission> translucentSubmissions;
    translucentSubmissions.reserve(translucentCommands.size() + residentTranslucentMeshes.size() * 4u);
    for (const SceneDrawCommand& command : translucentCommands) {
        translucentSubmissions.push_back({
            sceneVertexBuffer_,
            &command,
            lengthSquared(command.sortCenter - camera.pos)
        });
    }
    for (const CachedResidentMesh* mesh : residentTranslucentMeshes) {
        for (const SceneDrawCommand& command : mesh->translucentCommands) {
            translucentSubmissions.push_back({
                mesh->vertexBuffer,
                &command,
                lengthSquared(command.sortCenter - camera.pos)
            });
        }
    }
    std::stable_sort(
        translucentSubmissions.begin(),
        translucentSubmissions.end(),
        [](const TranslucentSubmission& lhs, const TranslucentSubmission& rhs) {
            return lhs.distanceSquared > rhs.distanceSquared;
        });

    SDL_GPUColorTargetInfo sceneColorTarget {};
    sceneColorTarget.texture = sceneColorTexture_;
    sceneColorTarget.clear_color.r = 0.0f;
    sceneColorTarget.clear_color.g = 0.0f;
    sceneColorTarget.clear_color.b = 0.0f;
    sceneColorTarget.clear_color.a = 0.0f;
    sceneColorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    sceneColorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depthTarget {};
    depthTarget.texture = depthTexture_;
    depthTarget.clear_depth = 1.0f;
    depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.cycle = true;

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &sceneColorTarget, 1, &depthTarget);
    SDL_GPUBufferBinding sceneBinding {};
    sceneBinding.buffer = sceneVertexBuffer_;
    sceneBinding.offset = 0;
    SDL_GPUSampler* materialSampler = frameSettings.textureMipmaps ? sceneMipmapSampler_ : sceneSampler_;

    const auto drawCommands = [&](SDL_GPUBuffer* vertexBuffer, const std::vector<SceneDrawCommand>& commands, bool translucent) {
        if (commands.empty() || vertexBuffer == nullptr) {
            return true;
        }

        SDL_GPUBufferBinding binding {};
        binding.buffer = vertexBuffer;
        binding.offset = 0;
        SDL_BindGPUVertexBuffers(renderPass, 0, &binding, 1);

        bool currentCullState = false;
        bool pipelineBound = false;
        for (const SceneDrawCommand& command : commands) {
            if (!pipelineBound || currentCullState != command.cullBackfaces) {
                SDL_BindGPUGraphicsPipeline(
                    renderPass,
                    translucent
                        ? (command.cullBackfaces ? translucentCullPipeline_ : translucentPipeline_)
                        : (command.cullBackfaces ? opaqueCullPipeline_ : opaquePipeline_));
                currentCullState = command.cullBackfaces;
                pipelineBound = true;
            }

            SDL_GPUTexture* sceneTexture = ensureSceneTexture(nullptr, command.image, uploadBuffers, nullptr, errorMessage);
            if (sceneTexture == nullptr) {
                return false;
            }
            pushObjectUniforms(command);
            SDL_GPUTextureSamplerBinding sceneSamplerBinding {};
            sceneSamplerBinding.texture = sceneTexture;
            sceneSamplerBinding.sampler = materialSampler;
            SDL_BindGPUFragmentSamplers(renderPass, 0, &sceneSamplerBinding, 1);
            SDL_DrawGPUPrimitives(renderPass, command.vertexCount, 1, command.firstVertex, 0);
        }

        return true;
    };

    const auto drawTranslucentSubmissions = [&](const std::vector<TranslucentSubmission>& submissions) {
        if (submissions.empty()) {
            return true;
        }

        SDL_GPUBuffer* currentBuffer = nullptr;
        bool currentCullState = false;
        bool pipelineBound = false;
        for (const TranslucentSubmission& submission : submissions) {
            if (submission.vertexBuffer == nullptr || submission.command == nullptr) {
                continue;
            }

            if (currentBuffer != submission.vertexBuffer) {
                SDL_GPUBufferBinding binding {};
                binding.buffer = submission.vertexBuffer;
                binding.offset = 0;
                SDL_BindGPUVertexBuffers(renderPass, 0, &binding, 1);
                currentBuffer = submission.vertexBuffer;
            }

            if (!pipelineBound || currentCullState != submission.command->cullBackfaces) {
                SDL_BindGPUGraphicsPipeline(
                    renderPass,
                    submission.command->cullBackfaces ? translucentCullPipeline_ : translucentPipeline_);
                currentCullState = submission.command->cullBackfaces;
                pipelineBound = true;
            }

            SDL_GPUTexture* sceneTexture = ensureSceneTexture(nullptr, submission.command->image, uploadBuffers, nullptr, errorMessage);
            if (sceneTexture == nullptr) {
                return false;
            }
            pushObjectUniforms(*submission.command);
            SDL_GPUTextureSamplerBinding sceneSamplerBinding {};
            sceneSamplerBinding.texture = sceneTexture;
            sceneSamplerBinding.sampler = materialSampler;
            SDL_BindGPUFragmentSamplers(renderPass, 0, &sceneSamplerBinding, 1);
            SDL_DrawGPUPrimitives(renderPass, submission.command->vertexCount, 1, submission.command->firstVertex, 0);
        }

        return true;
    };

    if (!opaqueCommands.empty() && !drawCommands(sceneBinding.buffer, opaqueCommands, false)) {
        SDL_EndGPURenderPass(renderPass);
        return abandonFrame();
    }

    for (const CachedResidentMesh* mesh : residentOpaqueMeshes) {
        if (!drawCommands(mesh->vertexBuffer, mesh->opaqueCommands, false)) {
            SDL_EndGPURenderPass(renderPass);
            return abandonFrame();
        }
    }

    if (!drawTranslucentSubmissions(translucentSubmissions)) {
        SDL_EndGPURenderPass(renderPass);
        return abandonFrame();
    }

    SDL_EndGPURenderPass(renderPass);

    SDL_GPUColorTargetInfo compositeTarget {};
    compositeTarget.texture = swapchainTexture;
    compositeTarget.clear_color.r = lightingState.backgroundColor.x;
    compositeTarget.clear_color.g = lightingState.backgroundColor.y;
    compositeTarget.clear_color.b = lightingState.backgroundColor.z;
    compositeTarget.clear_color.a = 1.0f;
    compositeTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    compositeTarget.store_op = SDL_GPU_STOREOP_STORE;

    if (imguiDrawData != nullptr) {
        ImGui_ImplSDLGPU3_PrepareDrawData(imguiDrawData, commandBuffer);
    }

    SDL_GPURenderPass* compositePass = SDL_BeginGPURenderPass(commandBuffer, &compositeTarget, 1, nullptr);

    SDL_GPUBufferBinding overlayBinding {};
    overlayBinding.buffer = overlayVertexBuffer_;
    overlayBinding.offset = 0;

    SDL_BindGPUGraphicsPipeline(compositePass, overlayPipeline_);
    SDL_BindGPUVertexBuffers(compositePass, 0, &overlayBinding, 1);

    SDL_GPUTextureSamplerBinding sceneCompositeBinding {};
    sceneCompositeBinding.texture = sceneColorTexture_;
    sceneCompositeBinding.sampler = sceneSampler_;
    compositeUniforms.projectionParams[3] = 0.0f;
    SDL_PushGPUFragmentUniformData(commandBuffer, 0, &compositeUniforms, sizeof(compositeUniforms));
    SDL_BindGPUFragmentSamplers(compositePass, 0, &sceneCompositeBinding, 1);
    SDL_DrawGPUPrimitives(compositePass, 6, 1, 0, 0);

    SDL_GPUTextureSamplerBinding overlaySamplerBinding {};
    overlaySamplerBinding.texture = hudTexture_;
    overlaySamplerBinding.sampler = overlaySampler_;
    compositeUniforms.projectionParams[3] = 1.0f;
    SDL_PushGPUFragmentUniformData(commandBuffer, 0, &compositeUniforms, sizeof(compositeUniforms));
    SDL_BindGPUFragmentSamplers(compositePass, 0, &overlaySamplerBinding, 1);
    SDL_DrawGPUPrimitives(compositePass, 6, 1, 0, 0);
    if (imguiDrawData != nullptr) {
        ImGui_ImplSDLGPU3_RenderDrawData(imguiDrawData, commandBuffer, compositePass);
    }
    SDL_EndGPURenderPass(compositePass);

    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        releaseUploadBuffers();
        return fail(errorMessage, "SDL_SubmitGPUCommandBuffer");
    }

    releaseUploadBuffers();

    const std::uint64_t keepWindow =
        frameSettings.pressureTier == RendererPressureTier::Critical
            ? 1u
            : (frameSettings.pressureTier == RendererPressureTier::Pressure ? 4u : 8u);
    pruneResidentMeshes(frameCounter_ > keepWindow ? frameCounter_ - keepWindow : 0u);
    pruneSceneTextures(frameCounter_ > keepWindow ? frameCounter_ - keepWindow : 0u);
    refreshMemoryStats();

    return true;
}

const char* VulkanRenderer::backendName() const
{
    return device_ != nullptr ? SDL_GetGPUDeviceDriver(device_) : nullptr;
}

SDL_GPUDevice* VulkanRenderer::gpuDevice() const
{
    return device_;
}

SDL_GPUPresentMode VulkanRenderer::presentMode() const
{
    return presentMode_;
}

SDL_GPUTextureFormat VulkanRenderer::swapchainTextureFormat() const
{
    return swapchainFormat_;
}

}  // namespace NativeGame
