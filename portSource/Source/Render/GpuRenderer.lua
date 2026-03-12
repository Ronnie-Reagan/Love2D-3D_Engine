local renderer = {}
local love = require "love"
local q = require "Source.Math.Quat"
local skyModel = require "Source.Render.SkyModel"
local shadowPass = require "Source.Render.ShadowPass"

local ATTR_POSITION = 0
local ATTR_COLOR = 2
local ATTR_TEXCOORD0 = 6
local ATTR_TEXCOORD1 = 7
local ATTR_NORMAL = 4
local ATTR_TANGENT = 5

-- LOVE 12 custom vertex attributes are bound by location index, not attribute name.
local vertexFormat = {
    { format = "floatvec3", location = ATTR_POSITION },
    { format = "floatvec2", location = ATTR_TEXCOORD0 },
    { format = "floatvec2", location = ATTR_TEXCOORD1 },
    { format = "floatvec4", location = ATTR_COLOR },
    { format = "floatvec3", location = ATTR_NORMAL },
    { format = "floatvec4", location = ATTR_TANGENT }
}

local shaderSource = [[
uniform vec3 uObjPos;
uniform vec3 uObjRight;
uniform vec3 uObjUp;
uniform vec3 uObjForward;
uniform vec3 uObjScale;
uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;
uniform float uPortalEnabled;
uniform vec3 uPortalOrigin;
uniform vec3 uPortalDir;
uniform float uPortalStart;
uniform float uPortalEnd;
uniform float uPortalRadiusNear;
uniform float uPortalRadiusFar;
uniform vec3 uColor;
uniform float uAlpha;
uniform float uFov;
uniform float uAspect;
uniform float uNear;
uniform float uFar;

uniform vec4 uBaseColorFactor;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;
uniform vec3 uEmissiveFactor;
uniform float uAlphaCutoff;
uniform float uAlphaMode; // 0=OPAQUE, 1=MASK, 2=BLEND
uniform float uDoubleSided;

uniform float uUseBaseColorTex;
uniform float uUseMetalRoughTex;
uniform float uUseNormalTex;
uniform float uUseOcclusionTex;
uniform float uUseEmissiveTex;
uniform float uUsePaintTex;
uniform float uUseSpecGlossWorkflow;
uniform float uUseDiffuseTex;
uniform float uUseSpecGlossTex;
uniform float uNormalScale;
uniform float uOcclusionStrength;
uniform float uFlipV;
uniform float uBaseColorTexCoord;
uniform float uMetalRoughTexCoord;
uniform float uNormalTexCoord;
uniform float uOcclusionTexCoord;
uniform float uEmissiveTexCoord;
uniform float uPaintTexCoord;
uniform float uDiffuseTexCoord;
uniform float uSpecGlossTexCoord;

uniform Image uBaseColorTex;
uniform Image uMetalRoughTex;
uniform Image uNormalTex;
uniform Image uOcclusionTex;
uniform Image uEmissiveTex;
uniform Image uPaintTex;
uniform Image uDiffuseTex;
uniform Image uSpecGlossTex;
uniform vec4 uDiffuseFactor;
uniform vec3 uSpecularFactor;
uniform float uGlossinessFactor;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform float uAmbientStrength;
uniform vec3 uSkyColor;
uniform vec3 uGroundColor;
uniform float uSpecularAmbientStrength;
uniform float uBounceStrength;
uniform float uFogDensity;
uniform float uFogHeightFalloff;
uniform vec3 uFogColor;
uniform float uExposureEV;
uniform float uShadowEnabled;
uniform float uShadowSoftness;

varying vec3 vWorldPos;
varying vec3 vWorldNormal;
varying vec3 vWorldTangent;
varying vec3 vWorldBitangent;
varying vec2 vUv0;
varying vec2 vUv1;

const float PI = 3.14159265359;

vec3 safeNormalize(vec3 v)
{
    float len2 = dot(v, v);
    if (len2 <= 1e-10) {
        return vec3(0.0, 0.0, 1.0);
    }
    return normalize(v);
}

#ifdef VERTEX
layout (location = 4) in vec3 aNormal;
layout (location = 5) in vec4 aTangent;
layout (location = 6) in vec2 aTexCoord0;
layout (location = 7) in vec2 aTexCoord1;


vec4 position(mat4 transform_projection, vec4 vertex_position)
{
    vec3 worldPos = uObjPos
        + (vertex_position.x * uObjScale.x) * uObjRight
        + (vertex_position.y * uObjScale.y) * uObjUp
        + (vertex_position.z * uObjScale.z) * uObjForward;

    vec3 worldNormal = safeNormalize(
        aNormal.x * uObjRight +
        aNormal.y * uObjUp +
        aNormal.z * uObjForward
    );

    vec3 worldTangent = safeNormalize(
        aTangent.x * uObjRight +
        aTangent.y * uObjUp +
        aTangent.z * uObjForward
    );

    vec3 worldBitangent = safeNormalize(cross(worldNormal, worldTangent) * aTangent.w);

    vWorldPos = worldPos;
    vWorldNormal = worldNormal;
    vWorldTangent = worldTangent;
    vWorldBitangent = worldBitangent;
    vUv0 = aTexCoord0;
    vUv1 = aTexCoord1;

    vec3 rel = worldPos - uCamPos;
    vec3 cam = vec3(
        dot(rel, uCamRight),
        dot(rel, uCamUp),
        dot(rel, uCamForward)
    );

    float f = 1.0 / tan(uFov * 0.5);
    float a = (uFar + uNear) / (uFar - uNear);
    float b = (-2.0 * uFar * uNear) / (uFar - uNear);

    return vec4(
        cam.x * f / uAspect,
        cam.y * f,
        a * cam.z + b,
        cam.z
    );
}
#endif

#ifdef PIXEL
float distributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 1e-5);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 1e-5);
}

float geometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx1 = geometrySchlickGGX(NdotV, roughness);
    float ggx2 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 srgbToLinear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

vec3 linearToSrgb(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 toneMapAces(vec3 c)
{
    const float a = 2.51;
    const float b = 0.03;
    const float d = 0.59;
    const float e = 0.14;
    c = max(c, vec3(0.0));
    return clamp((c * (a * c + b)) / (c * (2.43 * c + d) + e), 0.0, 1.0);
}

vec2 pickUv(float texCoordSet)
{
    vec2 uv = (texCoordSet > 0.5) ? vUv1 : vUv0;
    if (uFlipV > 0.5) {
        uv.y = 1.0 - uv.y;
    }
    return uv;
}

vec4 effect(vec4 color, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    if (uPortalEnabled > 0.5) {
        vec3 portalDir = safeNormalize(uPortalDir);
        vec3 toPoint = vWorldPos - uPortalOrigin;
        float tPortal = dot(toPoint, portalDir);
        if (tPortal >= uPortalStart && tPortal <= uPortalEnd) {
            vec3 closest = uPortalOrigin + portalDir * tPortal;
            float radial = length(vWorldPos - closest);
            float span = max(1e-4, uPortalEnd - uPortalStart);
            float k = clamp((tPortal - uPortalStart) / span, 0.0, 1.0);
            float radius = mix(uPortalRadiusNear, uPortalRadiusFar, k);
            if (radial <= radius) {
                discard;
            }
        }
    }

    vec2 uvBase = pickUv(uBaseColorTexCoord);
    vec4 baseColor = vec4(1.0);

    if (uUseSpecGlossWorkflow > 0.5) {
        vec2 uvDiffuse = pickUv(uDiffuseTexCoord);
        vec4 diffuseTex = Texel(uDiffuseTex, uvDiffuse);
        if (uUseDiffuseTex < 0.5) {
            diffuseTex = vec4(1.0);
        }
        baseColor.rgb = srgbToLinear(diffuseTex.rgb) * uDiffuseFactor.rgb * uColor * color.rgb;
        baseColor.a = diffuseTex.a * uDiffuseFactor.a * uAlpha * color.a;
    } else {
        vec4 baseTex = Texel(uBaseColorTex, uvBase);
        if (uUseBaseColorTex < 0.5) {
            baseTex = vec4(1.0);
        }
        baseColor.rgb = srgbToLinear(baseTex.rgb) * uBaseColorFactor.rgb * uColor * color.rgb;
        baseColor.a = baseTex.a * uBaseColorFactor.a * uAlpha * color.a;
    }

    float alpha = clamp(baseColor.a, 0.0, 1.0);

    if (uUsePaintTex > 0.5) {
        vec2 uvPaint = pickUv(uPaintTexCoord);
        vec4 paint = Texel(uPaintTex, uvPaint);
        baseColor.rgb = mix(baseColor.rgb, srgbToLinear(paint.rgb), clamp(paint.a, 0.0, 1.0));
    }

    float metallic = clamp(uMetallicFactor, 0.0, 1.0);
    float roughness = clamp(uRoughnessFactor, 0.04, 1.0);
    vec3 specularColor = vec3(0.04);
    if (uUseSpecGlossWorkflow > 0.5) {
        vec2 uvSG = pickUv(uSpecGlossTexCoord);
        vec4 sg = Texel(uSpecGlossTex, uvSG);
        if (uUseSpecGlossTex < 0.5) {
            sg = vec4(1.0);
        }
        specularColor = clamp(uSpecularFactor * srgbToLinear(sg.rgb), 0.0, 1.0);
        float glossiness = clamp(uGlossinessFactor * sg.a, 0.0, 1.0);
        roughness = clamp(1.0 - glossiness, 0.04, 1.0);
        metallic = 0.0;
    } else if (uUseMetalRoughTex > 0.5) {
        vec2 uvMR = pickUv(uMetalRoughTexCoord);
        vec4 mr = Texel(uMetalRoughTex, uvMR);
        metallic = clamp(metallic * mr.b, 0.0, 1.0);
        roughness = clamp(roughness * mr.g, 0.04, 1.0);
    }

    float ao = 1.0;
    if (uUseOcclusionTex > 0.5) {
        vec2 uvOcc = pickUv(uOcclusionTexCoord);
        float occ = Texel(uOcclusionTex, uvOcc).r;
        ao = mix(1.0, occ, clamp(uOcclusionStrength, 0.0, 1.0));
    }

    vec3 emissive = uEmissiveFactor;
    if (uUseEmissiveTex > 0.5) {
        vec2 uvEmissive = pickUv(uEmissiveTexCoord);
        emissive *= srgbToLinear(Texel(uEmissiveTex, uvEmissive).rgb);
    }

    vec3 N = safeNormalize(vWorldNormal);
    if (uUseNormalTex > 0.5) {
        vec2 uvNormal = pickUv(uNormalTexCoord);
        vec3 tangentNormal = Texel(uNormalTex, uvNormal).xyz * 2.0 - 1.0;
        tangentNormal.xy *= uNormalScale;
        mat3 tbn = mat3(
            safeNormalize(vWorldTangent),
            safeNormalize(vWorldBitangent),
            safeNormalize(vWorldNormal)
        );
        N = safeNormalize(tbn * tangentNormal);
    }

    vec3 V = safeNormalize(uCamPos - vWorldPos);
    if (uDoubleSided > 0.5 && dot(N, V) < 0.0) {
        N = -N;
    }
    vec3 L = safeNormalize(uLightDir);
    vec3 H = safeNormalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = (uUseSpecGlossWorkflow > 0.5) ? specularColor : mix(vec3(0.04), baseColor.rgb, metallic);
    vec3 F = fresnelSchlick(VdotH, F0);
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);

    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-5);
    vec3 diffuse = vec3(0.0);
    if (uUseSpecGlossWorkflow > 0.5) {
        float maxSpec = clamp(max(max(F0.r, F0.g), F0.b), 0.0, 1.0);
        diffuse = (baseColor.rgb * (1.0 - maxSpec)) / PI;
    } else {
        vec3 kD = (1.0 - F) * (1.0 - metallic);
        diffuse = (kD * baseColor.rgb) / PI;
    }

    vec3 direct = (diffuse + specular) * uLightColor * NdotL;
    if (uShadowEnabled > 0.5) {
        float heightMask = clamp((vWorldPos.y + 18.0) / max(10.0, 70.0 * uShadowSoftness), 0.0, 1.0);
        float pseudoShadow = mix(0.62, 1.0, heightMask);
        direct *= pseudoShadow;
    }

    float diffuseGiWeight = 1.0;
    if (uUseSpecGlossWorkflow > 0.5) {
        diffuseGiWeight = 1.0 - clamp(max(max(F0.r, F0.g), F0.b), 0.0, 1.0);
    } else {
        diffuseGiWeight = 1.0 - metallic;
    }

    float skyMix = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemiColor = mix(uGroundColor, uSkyColor, skyMix);
    vec3 diffuseGi = baseColor.rgb * diffuseGiWeight * hemiColor * (uAmbientStrength * ao);

    float sunAbove = clamp(dot(L, vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
    float upFacing = clamp(dot(N, vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
    vec3 bounceGi = baseColor.rgb * uGroundColor * (uBounceStrength * sunAbove * upFacing * ao);

    vec3 ambientSpec = F0 * uSkyColor * (uSpecularAmbientStrength * ao) * (1.0 - roughness * 0.65);
    vec3 ambient = diffuseGi + bounceGi + ambientSpec;

    vec3 finalColor = max(ambient + direct + emissive, vec3(0.0));
    finalColor *= exp2(uExposureEV);
    if (uFogDensity > 1e-6) {
        float viewDist = length(uCamPos - vWorldPos);
        float heightTerm = exp(-max(vWorldPos.y, 0.0) * max(0.0, uFogHeightFalloff));
        float fogFactor = 1.0 - exp(-viewDist * uFogDensity * max(0.05, heightTerm));
        finalColor = mix(finalColor, uFogColor, clamp(fogFactor, 0.0, 1.0));
    }
    finalColor = toneMapAces(finalColor);
    finalColor = linearToSrgb(finalColor);

    if (uAlphaMode > 0.5 && uAlphaMode < 1.5) {
        alpha = (alpha >= uAlphaCutoff) ? 1.0 : 0.0;
    } else if (uAlphaMode < 0.5) {
        alpha = 1.0;
    }

    return vec4(finalColor, alpha);
}
#endif
]]

local meshCache = setmetatable({}, { __mode = "k" })

local state = {
    ready = false,
    depthSupported = false,
    shader = nil,
    aspect = 1,
    nearPlane = 0.1,
    farPlane = 5000.0,
    log = function(_) end,
    loggedFirstFrame = false,
    loggedMaterialDebug = false,
    whiteTexture = nil,
    blackTexture = nil,
    normalTexture = nil,
    lightDir = { 0.35, 0.5, 0.25 },
    lightColor = { 1.3, 1.24, 1.12 },
    ambientStrength = 0.16,
    skyColor = { 0.34, 0.46, 0.68 },
    groundColor = { 0.14, 0.12, 0.09 },
    specularAmbientStrength = 0.18,
    bounceStrength = 0.10,
    fogDensity = 0.00055,
    fogHeightFalloff = 0.0017,
    fogColor = { 0.64, 0.73, 0.84 },
    exposureEV = 0.0,
    turbidity = 2.4,
    shadowEnabled = false,
    shadowSoftness = 1.6,
    meshBuildBudget = {
        maxPerFrame = 2,
        maxMs = 4.0
    },
    meshBuildQueue = {},
    meshBuildQueued = setmetatable({}, { __mode = "k" })
}

local defaultMaterial = {
    baseColorFactor = { 1, 1, 1, 1 },
    metallicFactor = 0,
    roughnessFactor = 1,
    workflow = "metalRough",
    diffuseFactor = { 1, 1, 1, 1 },
    specularFactor = { 1, 1, 1 },
    glossinessFactor = 1,
    emissiveFactor = { 0, 0, 0 },
    alphaMode = "OPAQUE",
    alphaCutoff = 0.5,
    doubleSided = false
}

local function log(message)
    state.log("[gpu] " .. tostring(message))
end

local function normalize3(v)
    local x = (v and v[1]) or 0
    local y = (v and v[2]) or 0
    local z = (v and v[3]) or 1
    local len = math.sqrt(x * x + y * y + z * z)
    if len <= 1e-8 then
        return { 0, 0, 1 }
    end
    return { x / len, y / len, z / len }
end

local function cross3(a, b)
    return {
        a[2] * b[3] - a[3] * b[2],
        a[3] * b[1] - a[1] * b[3],
        a[1] * b[2] - a[2] * b[1]
    }
end

local function sub3(a, b)
    return {
        (a[1] or 0) - (b[1] or 0),
        (a[2] or 0) - (b[2] or 0),
        (a[3] or 0) - (b[3] or 0)
    }
end

local function cameraSpaceDepth(worldPos, camera)
    if not worldPos or not camera or not camera.pos or not camera.rot then
        return -math.huge
    end
    local rel = {
        worldPos[1] - camera.pos[1],
        worldPos[2] - camera.pos[2],
        worldPos[3] - camera.pos[3]
    }
    local camConj = q.conjugate(camera.rot)
    local cam = q.rotateVector(camConj, rel)
    return cam[3]
end

local function buildSolidTexture(r, g, b, a)
    local imageData = love.image.newImageData(1, 1)
    imageData:setPixel(0, 0, r, g, b, a or 1)
    local okImage, image = pcall(function()
        return love.graphics.newImage(imageData, { mipmaps = true, linear = true })
    end)
    if not okImage or not image then
        image = love.graphics.newImage(imageData)
    end
    image:setFilter("linear", "linear", 4)
    pcall(function()
        image:setMipmapFilter("linear", 0.25)
    end)
    return image
end

local function ensureFallbackTextures()
    if not state.whiteTexture then
        state.whiteTexture = buildSolidTexture(1, 1, 1, 1)
    end
    if not state.blackTexture then
        state.blackTexture = buildSolidTexture(0, 0, 0, 1)
    end
    if not state.normalTexture then
        state.normalTexture = buildSolidTexture(0.5, 0.5, 1.0, 1)
    end
end

local function getMaterial(materials, index)
    if type(materials) == "table" then
        local m = materials[index]
        if type(m) == "table" then
            return m
        end
    end
    return defaultMaterial
end

local function materialIsTransparent(material, objectAlpha)
    if (objectAlpha or 1) < 0.999 then
        return true
    end
    if type(material) ~= "table" then
        return false
    end
    return material.alphaMode == "BLEND"
end

local function getTextureImage(images, textureRef, fallback)
    if type(textureRef) == "table" then
        local imageIndex = tonumber(textureRef.imageIndex)
        if imageIndex and type(images) == "table" then
            local imageRecord = images[math.floor(imageIndex)]
            if imageRecord and imageRecord.image then
                return imageRecord.image, true
            end
        end
    end
    return fallback, false
end

local function textureCoordIndex(textureRef)
    if type(textureRef) == "table" then
        return math.max(0, math.floor(tonumber(textureRef.texCoord) or 0))
    end
    return 0
end

local function computeFaceNormal(a, b, c)
    local e1 = sub3(b, a)
    local e2 = sub3(c, a)
    return normalize3(cross3(e1, e2))
end

local function fallbackTangentFromNormal(normal)
    local n = normalize3(normal)
    local ref = math.abs(n[2]) < 0.99 and { 0, 1, 0 } or { 1, 0, 0 }
    local tangent = cross3(ref, n)
    tangent = normalize3(tangent)
    return { tangent[1], tangent[2], tangent[3], 1 }
end

local function computeFaceTangent(a, b, c, uvA, uvB, uvC, normal)
    if not (uvA and uvB and uvC) then
        return fallbackTangentFromNormal(normal)
    end

    local edge1 = sub3(b, a)
    local edge2 = sub3(c, a)
    local du1 = (uvB[1] or 0) - (uvA[1] or 0)
    local dv1 = (uvB[2] or 0) - (uvA[2] or 0)
    local du2 = (uvC[1] or 0) - (uvA[1] or 0)
    local dv2 = (uvC[2] or 0) - (uvA[2] or 0)
    local denom = du1 * dv2 - du2 * dv1

    if math.abs(denom) <= 1e-8 then
        return fallbackTangentFromNormal(normal)
    end

    local r = 1 / denom
    local tangent = {
        (edge1[1] * dv2 - edge2[1] * dv1) * r,
        (edge1[2] * dv2 - edge2[2] * dv1) * r,
        (edge1[3] * dv2 - edge2[3] * dv1) * r
    }
    tangent = normalize3(tangent)
    return { tangent[1], tangent[2], tangent[3], 1 }
end

local function appendVertex(target, pos, uv0, uv1, color, normal, tangent)
    target[#target + 1] = {
        pos[1], pos[2], pos[3],
        uv0[1], uv0[2],
        uv1[1], uv1[2],
        color[1], color[2], color[3], color[4],
        normal[1], normal[2], normal[3],
        tangent[1], tangent[2], tangent[3], tangent[4]
    }
end

local function queueModelForMeshBuild(model)
    if not model or meshCache[model] ~= nil or state.meshBuildQueued[model] then
        return false
    end
    if not model.vertices or not model.faces then
        meshCache[model] = false
        return false
    end
    state.meshBuildQueued[model] = true
    state.meshBuildQueue[#state.meshBuildQueue + 1] = model
    return true
end

local function buildMeshSetForModelImmediate(model)
    if meshCache[model] ~= nil then
        return meshCache[model] or nil
    end
    if not model or not model.vertices or not model.faces then
        meshCache[model] = false
        return nil
    end

    local byMaterialVertices = {}
    local faceMaterials = model.faceMaterials
    local normals = model.vertexNormals
    local uvs = model.vertexUVs
    local uvs1 = model.vertexUVs1
    local tangents = model.vertexTangents
    local colors = model.vertexColors

    for faceIndex, face in ipairs(model.faces) do
        if type(face) == "table" and #face >= 3 then
            local materialIndex = tonumber(faceMaterials and faceMaterials[faceIndex]) or 1
            materialIndex = math.max(1, math.floor(materialIndex))
            local bucket = byMaterialVertices[materialIndex]
            if not bucket then
                bucket = {}
                byMaterialVertices[materialIndex] = bucket
            end

            for tri = 2, #face - 1 do
                local ia, ib, ic = face[1], face[tri], face[tri + 1]
                local a = model.vertices[ia]
                local b = model.vertices[ib]
                local c = model.vertices[ic]
                if a and b and c then
                    local uvA = uvs and uvs[ia] or { 0, 0 }
                    local uvB = uvs and uvs[ib] or { 0, 0 }
                    local uvC = uvs and uvs[ic] or { 0, 0 }
                    local uv1A = uvs1 and uvs1[ia] or uvA
                    local uv1B = uvs1 and uvs1[ib] or uvB
                    local uv1C = uvs1 and uvs1[ic] or uvC
                    local faceNormal = computeFaceNormal(a, b, c)

                    local nA = normalize3(normals and normals[ia] or faceNormal)
                    local nB = normalize3(normals and normals[ib] or faceNormal)
                    local nC = normalize3(normals and normals[ic] or faceNormal)

                    local faceTangent = computeFaceTangent(a, b, c, uvA, uvB, uvC, faceNormal)
                    local tA = tangents and tangents[ia] or faceTangent
                    local tB = tangents and tangents[ib] or faceTangent
                    local tC = tangents and tangents[ic] or faceTangent

                    local cA = colors and colors[ia] or { 1, 1, 1, 1 }
                    local cB = colors and colors[ib] or { 1, 1, 1, 1 }
                    local cC = colors and colors[ic] or { 1, 1, 1, 1 }

                    appendVertex(bucket, a, uvA, uv1A, cA, nA, tA)
                    appendVertex(bucket, b, uvB, uv1B, cB, nB, tB)
                    appendVertex(bucket, c, uvC, uv1C, cC, nC, tC)
                end
            end
        end
    end

    local meshSet = { byMaterial = {} }
    local hasMesh = false
    for materialIndex, vertices in pairs(byMaterialVertices) do
        if #vertices > 0 then
            local okMesh, meshOrErr = pcall(love.graphics.newMesh, vertexFormat, vertices, "triangles", "static")
            if okMesh and meshOrErr then
                meshSet.byMaterial[materialIndex] = {
                    mesh = meshOrErr,
                    triangleCount = #vertices / 3
                }
                hasMesh = true
            else
                log("mesh creation failed (material " .. tostring(materialIndex) .. "): " .. tostring(meshOrErr))
            end
        end
    end

    if not hasMesh then
        meshCache[model] = false
        return nil
    end

    meshCache[model] = meshSet
    return meshSet
end

local function buildMeshSetForModel(model)
    if meshCache[model] ~= nil then
        return meshCache[model] or nil
    end
    queueModelForMeshBuild(model)
    return nil
end

local function alphaModeCode(mode)
    if mode == "MASK" then
        return 1
    end
    if mode == "BLEND" then
        return 2
    end
    return 0
end

local function sortCallsBackToFront(a, b)
    return a.depth > b.depth
end

function renderer.isReady()
    return state.ready
end

function renderer.setMeshBuildBudget(opts)
    opts = opts or {}
    state.meshBuildBudget.maxPerFrame = math.max(
        1,
        math.floor(tonumber(opts.maxPerFrame) or state.meshBuildBudget.maxPerFrame or 2)
    )
    state.meshBuildBudget.maxMs = math.max(0.25, tonumber(opts.maxMs) or state.meshBuildBudget.maxMs or 4.0)
end

function renderer.pumpMeshBuildQueue(objects)
    if type(objects) == "table" then
        for i = 1, #objects do
            local obj = objects[i]
            if type(obj) == "table" and type(obj.model) == "table" then
                queueModelForMeshBuild(obj.model)
            end
        end
    end

    local budgetCount = math.max(1, math.floor(tonumber(state.meshBuildBudget.maxPerFrame) or 2))
    local budgetMs = math.max(0.25, tonumber(state.meshBuildBudget.maxMs) or 4.0)
    local startedAt = (love.timer and love.timer.getTime and love.timer.getTime()) or os.clock()
    local built = 0

    while built < budgetCount and #state.meshBuildQueue > 0 do
        local elapsedMs = (((love.timer and love.timer.getTime and love.timer.getTime()) or os.clock()) - startedAt) * 1000.0
        if elapsedMs >= budgetMs then
            break
        end
        local model = table.remove(state.meshBuildQueue, 1)
        state.meshBuildQueued[model] = nil
        if model and meshCache[model] == nil then
            buildMeshSetForModelImmediate(model)
            built = built + 1
        end
    end

    return built
end

function renderer.init(screen, camera, logFn, statusFn)
    state.log = logFn or state.log
    state.aspect = (screen and screen.h and screen.h ~= 0) and (screen.w / screen.h) or 1
    if camera and camera.fov then
        state.fov = camera.fov
    end

    local status = (type(statusFn) == "function") and statusFn or nil
    if status then
        status("Preparing fallback textures", 0.15)
    end
    ensureFallbackTextures()

    if status then
        status("Compiling world shader", 0.7)
    end
    local ok, shaderOrErr = pcall(love.graphics.newShader, shaderSource)
    if not ok then
        state.ready = false
        log("shader compile failed: " .. tostring(shaderOrErr))
        return false, shaderOrErr
    end

    state.shader = shaderOrErr

    if status then
        status("Configuring depth state", 0.9)
    end
    local depthOk = pcall(function()
        love.graphics.setDepthMode("lequal", true)
        love.graphics.setDepthMode("always", false)
    end)
    state.depthSupported = depthOk
    state.loggedFirstFrame = false

    state.ready = true
    if status then
        status("Renderer shader pipeline ready", 1.0)
    end
    log("initialized; depth_supported=" .. tostring(state.depthSupported))
    return true
end

function renderer.resize(screen)
    if not screen or not screen.w or not screen.h or screen.h == 0 then
        return
    end
    state.aspect = screen.w / screen.h
end

function renderer.setClipPlanes(nearPlane, farPlane)
    local nearValue = tonumber(nearPlane) or state.nearPlane or 0.1
    local farValue = tonumber(farPlane) or state.farPlane or 5000.0
    nearValue = math.max(0.001, nearValue)
    farValue = math.max(nearValue + 0.1, farValue)
    state.nearPlane = nearValue
    state.farPlane = farValue
end

function renderer.setLighting(config)
    if type(config) ~= "table" then
        return
    end

    if config.useAnalyticSky then
        local evaluated = skyModel.evaluate(config.direction or state.lightDir, {
            turbidity = config.turbidity or state.turbidity,
            sunIntensity = config.sunIntensity or config.intensity or 1.2,
            ambient = config.ambient or state.ambientStrength,
            exposureEV = config.exposureEV or state.exposureEV
        })
        config.direction = evaluated.lightDir
        config.color = evaluated.lightColor
        config.skyColor = evaluated.skyColor
        config.groundColor = evaluated.groundColor
        config.ambient = evaluated.ambient
        config.exposureEV = evaluated.exposureEV
    end

    if type(config.direction) == "table" then
        state.lightDir = normalize3({
            tonumber(config.direction[1]) or state.lightDir[1],
            tonumber(config.direction[2]) or state.lightDir[2],
            tonumber(config.direction[3]) or state.lightDir[3]
        })
    end

    if type(config.color) == "table" then
        state.lightColor = {
            math.max(0, tonumber(config.color[1]) or state.lightColor[1]),
            math.max(0, tonumber(config.color[2]) or state.lightColor[2]),
            math.max(0, tonumber(config.color[3]) or state.lightColor[3])
        }
    end

    if config.ambient ~= nil then
        state.ambientStrength = math.max(0, tonumber(config.ambient) or state.ambientStrength)
    end

    if type(config.skyColor) == "table" then
        state.skyColor = {
            math.max(0, tonumber(config.skyColor[1]) or state.skyColor[1]),
            math.max(0, tonumber(config.skyColor[2]) or state.skyColor[2]),
            math.max(0, tonumber(config.skyColor[3]) or state.skyColor[3])
        }
    end

    if type(config.groundColor) == "table" then
        state.groundColor = {
            math.max(0, tonumber(config.groundColor[1]) or state.groundColor[1]),
            math.max(0, tonumber(config.groundColor[2]) or state.groundColor[2]),
            math.max(0, tonumber(config.groundColor[3]) or state.groundColor[3])
        }
    end

    if config.specularAmbient ~= nil then
        state.specularAmbientStrength = math.max(0, tonumber(config.specularAmbient) or state.specularAmbientStrength)
    end

    if config.bounce ~= nil then
        state.bounceStrength = math.max(0, tonumber(config.bounce) or state.bounceStrength)
    end

    if config.fogDensity ~= nil then
        state.fogDensity = math.max(0, tonumber(config.fogDensity) or state.fogDensity)
    end
    if config.fogHeightFalloff ~= nil then
        state.fogHeightFalloff = math.max(0, tonumber(config.fogHeightFalloff) or state.fogHeightFalloff)
    end
    if type(config.fogColor) == "table" then
        state.fogColor = {
            math.max(0, tonumber(config.fogColor[1]) or state.fogColor[1]),
            math.max(0, tonumber(config.fogColor[2]) or state.fogColor[2]),
            math.max(0, tonumber(config.fogColor[3]) or state.fogColor[3])
        }
    end
    if config.exposureEV ~= nil then
        state.exposureEV = tonumber(config.exposureEV) or state.exposureEV
    end
    if config.turbidity ~= nil then
        state.turbidity = math.max(1.0, tonumber(config.turbidity) or state.turbidity)
    end
    if config.shadowEnabled ~= nil then
        state.shadowEnabled = config.shadowEnabled and true or false
    end
    if config.shadowSoftness ~= nil then
        state.shadowSoftness = math.max(0.4, tonumber(config.shadowSoftness) or state.shadowSoftness)
    end
    shadowPass.configure({
        enabled = state.shadowEnabled,
        maxDistance = config.shadowDistance or state.farPlane,
        softness = state.shadowSoftness
    })
end

function renderer.setShadowConfig(config)
    config = config or {}
    if config.enabled ~= nil then
        state.shadowEnabled = config.enabled and true or false
    end
    if config.softness ~= nil then
        state.shadowSoftness = math.max(0.4, tonumber(config.softness) or state.shadowSoftness)
    end
    shadowPass.configure({
        enabled = state.shadowEnabled,
        softness = state.shadowSoftness,
        maxDistance = config.distance or state.farPlane
    })
end

function renderer.drawWorld(objects, camera, backgroundColor)
    if not state.ready or not state.shader then
        return false, "renderer not initialized"
    end

    local bg = backgroundColor or { 0.2, 0.2, 0.75, 1.0 }
    love.graphics.clear(bg[1], bg[2], bg[3], bg[4] or 1.0)
    shadowPass.begin()

    if state.depthSupported then
        love.graphics.setDepthMode("lequal", true)
    end

    love.graphics.setShader(state.shader)
    -- Engine camera space is +Z forward with a custom projection, which flips front-face winding
    -- relative to LOVE's default expectation. Cull "front" so visible faces match CPU raster path.
    pcall(love.graphics.setMeshCullMode, "front")

    local camPos = camera.pos or { 0, 0, 0 }
    local camRot = camera.rot or { w = 1, x = 0, y = 0, z = 0 }
    local camRight = q.rotateVector(camRot, { 1, 0, 0 })
    local camUp = q.rotateVector(camRot, { 0, 1, 0 })
    local camForward = q.rotateVector(camRot, { 0, 0, 1 })

    state.shader:send("uCamPos", camPos)
    state.shader:send("uCamRight", camRight)
    state.shader:send("uCamUp", camUp)
    state.shader:send("uCamForward", camForward)
    state.shader:send("uFov", camera.fov or state.fov or math.rad(90))
    state.shader:send("uAspect", state.aspect)
    state.shader:send("uNear", state.nearPlane)
    state.shader:send("uFar", state.farPlane)

    state.shader:send("uLightDir", state.lightDir)
    state.shader:send("uLightColor", state.lightColor)
    state.shader:send("uAmbientStrength", state.ambientStrength)
    state.shader:send("uSkyColor", state.skyColor)
    state.shader:send("uGroundColor", state.groundColor)
    state.shader:send("uSpecularAmbientStrength", state.specularAmbientStrength)
    state.shader:send("uBounceStrength", state.bounceStrength)
    state.shader:send("uFogDensity", state.fogDensity)
    state.shader:send("uFogHeightFalloff", state.fogHeightFalloff)
    state.shader:send("uFogColor", state.fogColor)
    state.shader:send("uExposureEV", state.exposureEV)
    state.shader:send("uShadowEnabled", state.shadowEnabled and 1 or 0)
    state.shader:send("uShadowSoftness", state.shadowSoftness)

    local opaqueCalls = {}
    local transparentCalls = {}

    for _, obj in ipairs(objects) do
        local meshSet = buildMeshSetForModel(obj and obj.model)
        if meshSet then
            local objectAlpha = ((obj.color and obj.color[4]) or 1)
            for materialIndex, bundle in pairs(meshSet.byMaterial) do
                local material = getMaterial(obj.materials, materialIndex)
                local call = {
                    obj = obj,
                    mesh = bundle.mesh,
                    material = material,
                    triangleCount = bundle.triangleCount,
                    depth = cameraSpaceDepth(obj.pos, camera)
                }
                if materialIsTransparent(material, objectAlpha) then
                    transparentCalls[#transparentCalls + 1] = call
                else
                    opaqueCalls[#opaqueCalls + 1] = call
                end
            end
        end
    end

    local function drawCall(call)
        local obj = call.obj
        local material = call.material or defaultMaterial
        local mesh = call.mesh
        if not obj or not material or not mesh then
            return 0
        end

        local rot = obj.rot or { w = 1, x = 0, y = 0, z = 0 }
        local scale = obj.scale or { 1, 1, 1 }
        local objRight = q.rotateVector(rot, { 1, 0, 0 })
        local objUp = q.rotateVector(rot, { 0, 1, 0 })
        local objForward = q.rotateVector(rot, { 0, 0, 1 })

        local objectColor = obj.color or { 1, 1, 1, 1 }
        local hasMaterials = type(obj.materials) == "table" and #obj.materials > 0
        local tint = hasMaterials and { 1, 1, 1 } or { objectColor[1] or 1, objectColor[2] or 1, objectColor[3] or 1 }
        local alpha = objectColor[4] or 1

        state.shader:send("uObjPos", obj.pos or { 0, 0, 0 })
        state.shader:send("uObjRight", objRight)
        state.shader:send("uObjUp", objUp)
        state.shader:send("uObjForward", objForward)
        state.shader:send("uObjScale", { scale[1] or 1, scale[2] or 1, scale[3] or 1 })
        state.shader:send("uColor", tint)
        state.shader:send("uAlpha", alpha)
        local portal = obj.firstPersonPortal
        local portalEnabled = type(portal) == "table" and portal.enabled and type(portal.origin) == "table" and
            type(portal.dir) == "table"
        if portalEnabled then
            state.shader:send("uPortalEnabled", 1)
            state.shader:send("uPortalOrigin", {
                tonumber(portal.origin[1]) or 0,
                tonumber(portal.origin[2]) or 0,
                tonumber(portal.origin[3]) or 0
            })
            state.shader:send("uPortalDir", normalize3({
                tonumber(portal.dir[1]) or 0,
                tonumber(portal.dir[2]) or 0,
                tonumber(portal.dir[3]) or 1
            }))
            state.shader:send("uPortalStart", math.max(0, tonumber(portal.startDist) or 0))
            state.shader:send("uPortalEnd", math.max(0.01, tonumber(portal.endDist) or 0.01))
            state.shader:send("uPortalRadiusNear", math.max(0, tonumber(portal.radiusNear) or 0))
            state.shader:send("uPortalRadiusFar", math.max(0, tonumber(portal.radiusFar) or 0))
        else
            state.shader:send("uPortalEnabled", 0)
            state.shader:send("uPortalOrigin", { 0, 0, 0 })
            state.shader:send("uPortalDir", { 0, 0, 1 })
            state.shader:send("uPortalStart", 0)
            state.shader:send("uPortalEnd", 0.01)
            state.shader:send("uPortalRadiusNear", 0)
            state.shader:send("uPortalRadiusFar", 0)
        end

        local baseColorFactor = material.baseColorFactor or defaultMaterial.baseColorFactor
        state.shader:send("uBaseColorFactor", {
            baseColorFactor[1] or 1,
            baseColorFactor[2] or 1,
            baseColorFactor[3] or 1,
            baseColorFactor[4] or 1
        })
        state.shader:send("uMetallicFactor", tonumber(material.metallicFactor) or 0)
        state.shader:send("uRoughnessFactor", tonumber(material.roughnessFactor) or 1)
        state.shader:send("uEmissiveFactor", {
            math.max(0, (material.emissiveFactor and material.emissiveFactor[1]) or 0),
            math.max(0, (material.emissiveFactor and material.emissiveFactor[2]) or 0),
            math.max(0, (material.emissiveFactor and material.emissiveFactor[3]) or 0)
        })
        state.shader:send("uAlphaCutoff", tonumber(material.alphaCutoff) or 0.5)
        state.shader:send("uAlphaMode", alphaModeCode(material.alphaMode))
        state.shader:send("uDoubleSided", (material.doubleSided and 1) or 0)

        local useSpecGloss = (material.workflow == "specGloss") and true or false
        local baseTex, useBase = getTextureImage(obj.images, material.baseColorTexture, state.whiteTexture)
        local metalTex, useMetal = getTextureImage(obj.images, material.metallicRoughnessTexture, state.whiteTexture)
        local normalTex, useNormal = getTextureImage(obj.images, material.normalTexture, state.normalTexture)
        local occlusionTex, useOcc = getTextureImage(obj.images, material.occlusionTexture, state.whiteTexture)
        local emissiveTex, useEmissive = getTextureImage(obj.images, material.emissiveTexture, state.blackTexture)
        local diffuseTex, useDiffuse = getTextureImage(obj.images, material.diffuseTexture, state.whiteTexture)
        local specGlossTex, useSpecGlossTex = getTextureImage(obj.images, material.specularGlossinessTexture,
            state.whiteTexture)
        local paintImage = obj.paintOverlay and obj.paintOverlay.image

        state.shader:send("uBaseColorTex", baseTex)
        state.shader:send("uMetalRoughTex", metalTex)
        state.shader:send("uNormalTex", normalTex)
        state.shader:send("uOcclusionTex", occlusionTex)
        state.shader:send("uEmissiveTex", emissiveTex)
        state.shader:send("uPaintTex", paintImage or state.whiteTexture)
        state.shader:send("uDiffuseTex", diffuseTex)
        state.shader:send("uSpecGlossTex", specGlossTex)

        state.shader:send("uUseBaseColorTex", useBase and 1 or 0)
        state.shader:send("uUseMetalRoughTex", useMetal and 1 or 0)
        state.shader:send("uUseNormalTex", useNormal and 1 or 0)
        state.shader:send("uUseOcclusionTex", useOcc and 1 or 0)
        state.shader:send("uUseEmissiveTex", useEmissive and 1 or 0)
        state.shader:send("uUsePaintTex", paintImage and 1 or 0)
        state.shader:send("uUseSpecGlossWorkflow", useSpecGloss and 1 or 0)
        state.shader:send("uUseDiffuseTex", useDiffuse and 1 or 0)
        state.shader:send("uUseSpecGlossTex", useSpecGlossTex and 1 or 0)
        state.shader:send("uFlipV", obj.uvFlipV and 1 or 0)

        state.shader:send("uBaseColorTexCoord", textureCoordIndex(material.baseColorTexture))
        state.shader:send("uMetalRoughTexCoord", textureCoordIndex(material.metallicRoughnessTexture))
        state.shader:send("uNormalTexCoord", textureCoordIndex(material.normalTexture))
        state.shader:send("uOcclusionTexCoord", textureCoordIndex(material.occlusionTexture))
        state.shader:send("uEmissiveTexCoord", textureCoordIndex(material.emissiveTexture))
        state.shader:send("uPaintTexCoord", 0)
        state.shader:send("uDiffuseTexCoord", textureCoordIndex(material.diffuseTexture))
        state.shader:send("uSpecGlossTexCoord", textureCoordIndex(material.specularGlossinessTexture))

        local diffuseFactor = material.diffuseFactor or defaultMaterial.diffuseFactor
        local specularFactor = material.specularFactor or defaultMaterial.specularFactor
        state.shader:send("uDiffuseFactor", {
            diffuseFactor[1] or 1,
            diffuseFactor[2] or 1,
            diffuseFactor[3] or 1,
            diffuseFactor[4] or 1
        })
        state.shader:send("uSpecularFactor", {
            specularFactor[1] or 1,
            specularFactor[2] or 1,
            specularFactor[3] or 1
        })
        state.shader:send("uGlossinessFactor", tonumber(material.glossinessFactor) or 1)

        if not state.loggedMaterialDebug then
            log(string.format(
                "material bind: workflow=%s useBase=%s useMR=%s useSG=%s useNormal=%s useAO=%s useEmissive=%s alphaMode=%s",
                tostring(material.workflow or "metalRough"),
                tostring(useBase),
                tostring(useMetal),
                tostring(useSpecGlossTex),
                tostring(useNormal),
                tostring(useOcc),
                tostring(useEmissive),
                tostring(material.alphaMode or "OPAQUE")
            ))
            state.loggedMaterialDebug = true
        end

        local normalScale = (material.normalTexture and tonumber(material.normalTexture.scale)) or 1
        local occlusionStrength = (material.occlusionTexture and tonumber(material.occlusionTexture.strength)) or 1
        state.shader:send("uNormalScale", normalScale)
        state.shader:send("uOcclusionStrength", occlusionStrength)

        pcall(love.graphics.setMeshCullMode, material.doubleSided and "none" or "front")
        love.graphics.draw(mesh)
        return call.triangleCount or (mesh:getVertexCount() / 3)
    end

    local triangleCount = 0

    if state.depthSupported then
        love.graphics.setDepthMode("lequal", true)
        for _, call in ipairs(opaqueCalls) do
            triangleCount = triangleCount + drawCall(call)
        end

        if #transparentCalls > 0 then
            table.sort(transparentCalls, sortCallsBackToFront)
            love.graphics.setDepthMode("lequal", false)
            for _, call in ipairs(transparentCalls) do
                triangleCount = triangleCount + drawCall(call)
            end
            love.graphics.setDepthMode("lequal", true)
        end
    else
        local drawCalls = {}
        for i = 1, #opaqueCalls do
            drawCalls[#drawCalls + 1] = opaqueCalls[i]
        end
        for i = 1, #transparentCalls do
            drawCalls[#drawCalls + 1] = transparentCalls[i]
        end
        table.sort(drawCalls, sortCallsBackToFront)
        for _, call in ipairs(drawCalls) do
            triangleCount = triangleCount + drawCall(call)
        end
    end

    love.graphics.setShader()
    pcall(love.graphics.setMeshCullMode, "none")
    if state.depthSupported then
        love.graphics.setDepthMode("always", false)
    end

    if not state.loggedFirstFrame then
        log(string.format("first frame: objects=%d triangles=%d", #objects, triangleCount))
        state.loggedFirstFrame = true
    end
    shadowPass.finish()

    return true, triangleCount
end

return renderer
