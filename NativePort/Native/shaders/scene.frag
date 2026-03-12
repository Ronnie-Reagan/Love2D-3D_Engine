#version 450

layout(set = 2, binding = 0) uniform sampler2D uBaseColorTexture;
layout(set = 3, binding = 0, std140) uniform SceneLightingUniforms {
    vec4 uLightDirection;
    vec4 uLightColor;
    vec4 uSkyColor;
    vec4 uGroundColor;
    vec4 uFogColor;
    vec4 uCameraPosition;
    vec4 uAmbientAndGi;
    vec4 uFogAndExposure;
    vec4 uShadowParams;
};

layout(location = 0) in vec3 inRelativePosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec2 inFogRange;
layout(location = 5) in float inAlphaCutoff;
layout(location = 6) in float inWorldHeight;
layout(location = 0) out vec4 outColor;

vec3 safeNormalize(vec3 value)
{
    float len2 = dot(value, value);
    if (len2 <= 1e-10) {
        return vec3(0.0, 0.0, 1.0);
    }
    return normalize(value);
}

vec3 linearToSrgb(vec3 color)
{
    return pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 toneMapAces(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    color = max(color, vec3(0.0));
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main()
{
    vec4 shaded = texture(uBaseColorTexture, inTexCoord) * inColor;
    if (inAlphaCutoff >= 0.0 && shaded.a < inAlphaCutoff) {
        discard;
    }

    vec3 baseColor = max(shaded.rgb, vec3(0.0));
    vec3 normal = safeNormalize(inNormal);
    vec3 lightDir = safeNormalize(uLightDirection.xyz);
    vec3 viewDir = safeNormalize(-inRelativePosition);
    vec3 halfVector = safeNormalize(lightDir + viewDir);

    float nDotL = max(dot(normal, lightDir), 0.0);
    float nDotV = max(dot(normal, viewDir), 0.0);
    float nDotH = max(dot(normal, halfVector), 0.0);

    float skyMix = clamp(normal.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemiColor = mix(uGroundColor.xyz, uSkyColor.xyz, skyMix);
    vec3 ambient = baseColor * hemiColor * max(0.0, uAmbientAndGi.x);

    float sunAbove = clamp(lightDir.y, 0.0, 1.0);
    float upFacing = clamp(normal.y, 0.0, 1.0);
    vec3 bounce = baseColor * uGroundColor.xyz * (max(0.0, uAmbientAndGi.z) * sunAbove * upFacing);

    float specularPower = mix(20.0, 72.0, clamp(1.0 - uAmbientAndGi.y, 0.0, 1.0));
    float directSpecular = pow(nDotH, specularPower) * (0.08 + max(0.0, uAmbientAndGi.y) * 0.18);
    vec3 ambientSpecular =
        uSkyColor.xyz *
        (0.04 + baseColor * 0.02) *
        max(0.0, uAmbientAndGi.y) *
        (0.35 + 0.65 * pow(1.0 - nDotV, 2.0));

    vec3 direct = (baseColor + vec3(directSpecular)) * uLightColor.xyz * nDotL;

    float viewDistance = length(inRelativePosition);
    if (uShadowParams.x > 0.5) {
        float heightMask = clamp((inWorldHeight + 18.0) / max(10.0, 70.0 * max(0.4, uShadowParams.y)), 0.0, 1.0);
        float shadowReach = 1.0 - smoothstep(max(10.0, uShadowParams.z * 0.82), max(20.0, uShadowParams.z), viewDistance);
        float pseudoShadow = mix(1.0, mix(0.62, 1.0, heightMask), clamp(shadowReach, 0.0, 1.0));
        direct *= pseudoShadow;
    }

    vec3 finalColor = max(ambient + bounce + ambientSpecular + direct, vec3(0.0));

    float fogSpan = max(0.001, inFogRange.y - inFogRange.x);
    float rangeFog = clamp((viewDistance - inFogRange.x) / fogSpan, 0.0, 1.0);
    float exponentialFog = 0.0;
    if (uFogAndExposure.x > 1e-6) {
        float heightTerm = exp(-max(inWorldHeight, 0.0) * max(0.0, uFogAndExposure.y));
        exponentialFog = 1.0 - exp(-viewDistance * uFogAndExposure.x * max(0.05, heightTerm));
    }
    float fogFactor = max(rangeFog, clamp(exponentialFog, 0.0, 1.0));
    finalColor = mix(finalColor, uFogColor.xyz, fogFactor);

    finalColor *= exp2(uFogAndExposure.z);
    finalColor = linearToSrgb(toneMapAces(finalColor));
    outColor = vec4(finalColor, shaded.a);
}
