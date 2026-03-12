#include "NativeGame/VulkanRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>

namespace NativeGame {
namespace {

constexpr SDL_GPUTextureFormat kDepthTextureFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
constexpr std::size_t kInitialSceneCapacityBytes = 2u * 1024u * 1024u;
constexpr float kNearClip = 0.05f;
constexpr float kFarClip = 4200.0f;

struct Mat4 {
    std::array<float, 16> m {};
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
    key += "|f=" + std::to_string(object.fogNear) + "," + std::to_string(object.fogFar);
    key += object.cullBackfaces ? "|cb=1" : "|cb=0";
    return key;
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
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, "vulkan");
    if (device_ == nullptr) {
        window_ = nullptr;
        return fail(errorMessage, "SDL_CreateGPUDevice");
    }

    if (!SDL_ClaimWindowForGPUDevice(device_, window_)) {
        return fail(errorMessage, "SDL_ClaimWindowForGPUDevice");
    }

    SDL_SetGPUAllowedFramesInFlight(device_, 2);

    SDL_GPUSamplerCreateInfo samplerInfo {};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    samplerInfo.min_lod = 0.0f;
    samplerInfo.max_lod = 0.0f;
    sceneSampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
    if (sceneSampler_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUSampler");
    }

    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
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

    return true;
}

void VulkanRenderer::shutdown()
{
    drawableWidth_ = 0;
    drawableHeight_ = 0;
    sceneCapacityBytes_ = 0;
    hudTransferCapacityBytes_ = 0;

    if (device_ != nullptr) {
        releaseSceneTextures();
        releaseResidentMeshes();
        SDL_ReleaseGPUTransferBuffer(device_, hudTransferBuffer_);
        SDL_ReleaseGPUTexture(device_, hudTexture_);
        SDL_ReleaseGPUTexture(device_, depthTexture_);
        SDL_ReleaseGPUBuffer(device_, overlayVertexBuffer_);
        SDL_ReleaseGPUTransferBuffer(device_, sceneTransferBuffer_);
        SDL_ReleaseGPUBuffer(device_, sceneVertexBuffer_);
        SDL_ReleaseGPUSampler(device_, sceneSampler_);
        SDL_ReleaseGPUSampler(device_, overlaySampler_);
        SDL_ReleaseGPUGraphicsPipeline(device_, overlayPipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, translucentPipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, translucentCullPipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, opaquePipeline_);
        SDL_ReleaseGPUGraphicsPipeline(device_, opaqueCullPipeline_);
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
    depthTexture_ = nullptr;
    hudTexture_ = nullptr;
    hudTransferBuffer_ = nullptr;
    frameCounter_ = 0;
}

void VulkanRenderer::releaseSceneTextures()
{
    if (device_ == nullptr) {
        sceneTextures_.clear();
        return;
    }

    for (CachedSceneTexture& cached : sceneTextures_) {
        SDL_ReleaseGPUTexture(device_, cached.texture);
        cached.texture = nullptr;
        cached.image = nullptr;
        cached.version = 0;
    }
    sceneTextures_.clear();
}

void VulkanRenderer::releaseResidentMeshes()
{
    if (device_ == nullptr) {
        residentMeshes_.clear();
        return;
    }

    for (CachedResidentMesh& mesh : residentMeshes_) {
        SDL_ReleaseGPUBuffer(device_, mesh.vertexBuffer);
        mesh.vertexBuffer = nullptr;
        mesh.vertexBufferBytes = 0;
        mesh.opaqueCommands.clear();
        mesh.translucentCommands.clear();
    }
    residentMeshes_.clear();
}

VulkanRenderer::CachedResidentMesh* VulkanRenderer::findResidentMesh(const std::string& key)
{
    for (CachedResidentMesh& mesh : residentMeshes_) {
        if (mesh.key == key) {
            return &mesh;
        }
    }
    return nullptr;
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
        SDL_ReleaseGPUBuffer(device_, mesh->vertexBuffer);
        mesh->vertexBuffer = nullptr;
        mesh->vertexBufferBytes = 0;
        mesh->opaqueCommands.clear();
        mesh->translucentCommands.clear();
        mesh->sourceVertexData = nullptr;
        mesh->sourceVertexCount = 0;
        mesh->sourceFaceCount = 0;
        mesh->sourceMaterialCount = 0;
        mesh->sourceModelRevision = 0;
    }

    if (mesh == nullptr) {
        residentMeshes_.push_back({});
        mesh = &residentMeshes_.back();
        mesh->key = key;
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
    return mesh;
}

void VulkanRenderer::pruneResidentMeshes(std::uint64_t minFrameToKeep)
{
    if (device_ == nullptr) {
        residentMeshes_.clear();
        return;
    }

    residentMeshes_.erase(
        std::remove_if(
            residentMeshes_.begin(),
            residentMeshes_.end(),
            [&](CachedResidentMesh& mesh) {
                if (mesh.lastFrameUsed >= minFrameToKeep) {
                    return false;
                }
                SDL_ReleaseGPUBuffer(device_, mesh.vertexBuffer);
                mesh.vertexBuffer = nullptr;
                return true;
            }),
        residentMeshes_.end());
}

bool VulkanRenderer::createPipelines(const std::filesystem::path& shaderDirectory, std::string* errorMessage)
{
    const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(device_, window_);

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
        1,
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
        0,
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
    scenePipelineInfo.target_info.depth_stencil_format = kDepthTextureFormat;
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
    overlayPipelineInfo.target_info.depth_stencil_format = kDepthTextureFormat;
    overlayPipelineInfo.target_info.has_depth_stencil_target = true;

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
                return cached.texture;
            }
            if (copyPass == nullptr) {
                fail(errorMessage, "scene texture cache needs upload");
                return nullptr;
            }
            SDL_ReleaseGPUTexture(device_, cached.texture);
            cached.texture = nullptr;
            cached.version = 0;
            break;
        }
    }

    if (copyPass == nullptr) {
        fail(errorMessage, "scene texture was not prepared");
        return nullptr;
    }

    SDL_GPUTextureCreateInfo textureInfo {};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = static_cast<Uint32>(sourceImage->width);
    textureInfo.height = static_cast<Uint32>(sourceImage->height);
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    textureInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &textureInfo);
    if (texture == nullptr) {
        fail(errorMessage, "SDL_CreateGPUTexture");
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo transferInfo {};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = static_cast<Uint32>(sourceImage->pixels.size());
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

    for (CachedSceneTexture& cached : sceneTextures_) {
        if (cached.image == sourceImage) {
            cached.texture = texture;
            cached.version = sourceImage->version;
            return texture;
        }
    }

    sceneTextures_.push_back({ sourceImage, sourceImage->version, texture });
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
    return true;
}

bool VulkanRenderer::ensureFramebufferResources(Uint32 width, Uint32 height, std::string* errorMessage)
{
    if (width == drawableWidth_ &&
        height == drawableHeight_ &&
        depthTexture_ != nullptr &&
        hudTexture_ != nullptr &&
        hudTransferBuffer_ != nullptr) {
        return true;
    }

    SDL_ReleaseGPUTransferBuffer(device_, hudTransferBuffer_);
    SDL_ReleaseGPUTexture(device_, hudTexture_);
    SDL_ReleaseGPUTexture(device_, depthTexture_);
    hudTransferBuffer_ = nullptr;
    hudTexture_ = nullptr;
    depthTexture_ = nullptr;

    SDL_GPUTextureCreateInfo depthInfo {};
    depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
    depthInfo.format = kDepthTextureFormat;
    depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depthInfo.width = width;
    depthInfo.height = height;
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
    hudInfo.width = width;
    hudInfo.height = height;
    hudInfo.layer_count_or_depth = 1;
    hudInfo.num_levels = 1;
    hudInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
    hudTexture_ = SDL_CreateGPUTexture(device_, &hudInfo);
    if (hudTexture_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUTexture");
    }

    SDL_GPUTransferBufferCreateInfo transferInfo {};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = width * height * 4u;
    hudTransferBuffer_ = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (hudTransferBuffer_ == nullptr) {
        return fail(errorMessage, "SDL_CreateGPUTransferBuffer");
    }

    drawableWidth_ = width;
    drawableHeight_ = height;
    hudTransferCapacityBytes_ = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
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
    return true;
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
            const Vec2 uv =
                static_cast<std::size_t>(sourceIndex) < object.model->texCoords.size()
                    ? object.model->texCoords[static_cast<std::size_t>(sourceIndex)]
                    : Vec2 { 0.0f, 0.0f };
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
    const RendererLightingState& lightingState,
    const std::vector<RenderObject>& opaqueObjects,
    const std::vector<RenderObject>& translucentObjects,
    const HudCanvas& hudCanvas,
    std::string* errorMessage)
{
    if (device_ == nullptr || window_ == nullptr) {
        return fail(errorMessage, "renderer not initialized");
    }

    ++frameCounter_;
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

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr) {
        return fail(errorMessage, "SDL_AcquireGPUCommandBuffer");
    }

    SDL_GPUTexture* swapchainTexture = nullptr;
    Uint32 drawableWidth = 0;
    Uint32 drawableHeight = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commandBuffer, window_, &swapchainTexture, &drawableWidth, &drawableHeight)) {
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return fail(errorMessage, "SDL_WaitAndAcquireGPUSwapchainTexture");
    }
    if (swapchainTexture == nullptr) {
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return true;
    }

    if (drawableWidth != static_cast<Uint32>(hudCanvas.width()) || drawableHeight != static_cast<Uint32>(hudCanvas.height())) {
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return fail(errorMessage, "HUD canvas size does not match drawable size");
    }

    if (!ensureFramebufferResources(drawableWidth, drawableHeight, errorMessage)) {
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return false;
    }

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    std::vector<SDL_GPUTransferBuffer*> uploadBuffers;
    if (!sceneVertices.empty()) {
        void* mapped = SDL_MapGPUTransferBuffer(device_, sceneTransferBuffer_, true);
        if (mapped == nullptr) {
            SDL_EndGPUCopyPass(copyPass);
            SDL_CancelGPUCommandBuffer(commandBuffer);
            return fail(errorMessage, "SDL_MapGPUTransferBuffer");
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
            for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
                SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
            }
            SDL_EndGPUCopyPass(copyPass);
            SDL_CancelGPUCommandBuffer(commandBuffer);
            return fail(errorMessage, "terrain resident mesh upload");
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
            for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
                SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
            }
            SDL_EndGPUCopyPass(copyPass);
            SDL_CancelGPUCommandBuffer(commandBuffer);
            return fail(errorMessage, "terrain resident mesh upload");
        }
        if (!mesh->opaqueCommands.empty()) {
            residentOpaqueMeshes.push_back(mesh);
        }
        if (!mesh->translucentCommands.empty()) {
            residentTranslucentMeshes.push_back(mesh);
        }
    }

    for (const SceneDrawCommand& command : opaqueCommands) {
        if (ensureSceneTexture(copyPass, command.image, uploadBuffers, errorMessage) == nullptr) {
            for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
                SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
            }
            SDL_EndGPUCopyPass(copyPass);
            SDL_CancelGPUCommandBuffer(commandBuffer);
            return false;
        }
    }
    for (const CachedResidentMesh* mesh : residentOpaqueMeshes) {
        for (const SceneDrawCommand& command : mesh->opaqueCommands) {
            if (ensureSceneTexture(copyPass, command.image, uploadBuffers, errorMessage) == nullptr) {
                for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
                    SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
                }
                SDL_EndGPUCopyPass(copyPass);
                SDL_CancelGPUCommandBuffer(commandBuffer);
                return false;
            }
        }
    }
    for (const SceneDrawCommand& command : translucentCommands) {
        if (ensureSceneTexture(copyPass, command.image, uploadBuffers, errorMessage) == nullptr) {
            for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
                SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
            }
            SDL_EndGPUCopyPass(copyPass);
            SDL_CancelGPUCommandBuffer(commandBuffer);
            return false;
        }
    }
    for (const CachedResidentMesh* mesh : residentTranslucentMeshes) {
        for (const SceneDrawCommand& command : mesh->translucentCommands) {
            if (ensureSceneTexture(copyPass, command.image, uploadBuffers, errorMessage) == nullptr) {
                for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
                    SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
                }
                SDL_EndGPUCopyPass(copyPass);
                SDL_CancelGPUCommandBuffer(commandBuffer);
                return false;
            }
        }
    }

    if (!uploadHudTexture(copyPass, hudCanvas, errorMessage)) {
        for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
            SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
        }
        SDL_EndGPUCopyPass(copyPass);
        SDL_CancelGPUCommandBuffer(commandBuffer);
        return false;
    }
    SDL_EndGPUCopyPass(copyPass);

    const float aspect = static_cast<float>(drawableWidth) / std::max(1.0f, static_cast<float>(drawableHeight));
    Camera relativeCamera = camera;
    relativeCamera.pos = {};
    const Mat4 view = makeView(relativeCamera);
    const Mat4 projection = makePerspective(camera.fovRadians, aspect, kNearClip, kFarClip);
    const Mat4 viewProjection = multiply(projection, view);

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

    SDL_GPUColorTargetInfo colorTarget {};
    colorTarget.texture = swapchainTexture;
    colorTarget.clear_color.r = lightingState.backgroundColor.x;
    colorTarget.clear_color.g = lightingState.backgroundColor.y;
    colorTarget.clear_color.b = lightingState.backgroundColor.z;
    colorTarget.clear_color.a = 1.0f;
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depthTarget {};
    depthTarget.texture = depthTexture_;
    depthTarget.clear_depth = 1.0f;
    depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthTarget.cycle = true;

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commandBuffer, &colorTarget, 1, &depthTarget);
    SDL_GPUBufferBinding sceneBinding {};
    sceneBinding.buffer = sceneVertexBuffer_;
    sceneBinding.offset = 0;

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

            SDL_GPUTexture* sceneTexture = ensureSceneTexture(nullptr, command.image, uploadBuffers, errorMessage);
            if (sceneTexture == nullptr) {
                return false;
            }
            SDL_GPUTextureSamplerBinding sceneSamplerBinding {};
            sceneSamplerBinding.texture = sceneTexture;
            sceneSamplerBinding.sampler = sceneSampler_;
            SDL_BindGPUFragmentSamplers(renderPass, 0, &sceneSamplerBinding, 1);
            SDL_DrawGPUPrimitives(renderPass, command.vertexCount, 1, command.firstVertex, 0);
        }

        return true;
    };

    if (!opaqueCommands.empty() && !drawCommands(sceneBinding.buffer, opaqueCommands, false)) {
        SDL_EndGPURenderPass(renderPass);
        SDL_CancelGPUCommandBuffer(commandBuffer);
        for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
            SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
        }
        return false;
    }

    for (const CachedResidentMesh* mesh : residentOpaqueMeshes) {
        if (!drawCommands(mesh->vertexBuffer, mesh->opaqueCommands, false)) {
            SDL_EndGPURenderPass(renderPass);
            SDL_CancelGPUCommandBuffer(commandBuffer);
            for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
                SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
            }
            return false;
        }
    }

    if (!translucentCommands.empty() && !drawCommands(sceneBinding.buffer, translucentCommands, true)) {
        SDL_EndGPURenderPass(renderPass);
        SDL_CancelGPUCommandBuffer(commandBuffer);
        for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
            SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
        }
        return false;
    }

    for (const CachedResidentMesh* mesh : residentTranslucentMeshes) {
        if (!drawCommands(mesh->vertexBuffer, mesh->translucentCommands, true)) {
            SDL_EndGPURenderPass(renderPass);
            SDL_CancelGPUCommandBuffer(commandBuffer);
            for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
                SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
            }
            return false;
        }
    }

    SDL_GPUBufferBinding overlayBinding {};
    overlayBinding.buffer = overlayVertexBuffer_;
    overlayBinding.offset = 0;

    SDL_GPUTextureSamplerBinding overlaySamplerBinding {};
    overlaySamplerBinding.texture = hudTexture_;
    overlaySamplerBinding.sampler = overlaySampler_;

    SDL_BindGPUGraphicsPipeline(renderPass, overlayPipeline_);
    SDL_BindGPUVertexBuffers(renderPass, 0, &overlayBinding, 1);
    SDL_BindGPUFragmentSamplers(renderPass, 0, &overlaySamplerBinding, 1);
    SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);
    SDL_EndGPURenderPass(renderPass);

    if (!SDL_SubmitGPUCommandBuffer(commandBuffer)) {
        for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
            SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
        }
        return fail(errorMessage, "SDL_SubmitGPUCommandBuffer");
    }

    for (SDL_GPUTransferBuffer* uploadBuffer : uploadBuffers) {
        SDL_ReleaseGPUTransferBuffer(device_, uploadBuffer);
    }

    pruneResidentMeshes(frameCounter_ > 8 ? frameCounter_ - 8 : 0);

    return true;
}

const char* VulkanRenderer::backendName() const
{
    return device_ != nullptr ? SDL_GetGPUDeviceDriver(device_) : nullptr;
}

}  // namespace NativeGame
