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

layout(set = 3, binding = 1, std140) uniform SceneObjectUniforms {
    vec4 uObjectFogRange;
};

layout(location = 0) in vec3 inRelativePosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec2 inFogRange;
layout(location = 5) in float inAlphaCutoff;
layout(location = 6) in float inWorldHeight;
layout(location = 0) out vec4 outColor;

const float kPi = 3.14159265358979323846;
const float kRayleighScaleHeight = 8000.0;
const float kBaseMieScaleHeight = 1200.0;
const vec3 kRayleighBeta = vec3(5.802e-6, 13.558e-6, 33.100e-6);

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

float rayleighPhase(float mu)
{
    return 3.0 * (1.0 + (mu * mu)) / (16.0 * kPi);
}

float hgPhase(float mu, float g)
{
    float g2 = g * g;
    float denom = max(1.0e-3, 1.0 + g2 - (2.0 * g * mu));
    return (1.0 - g2) / (4.0 * kPi * denom * sqrt(denom));
}

vec3 computeAerialPerspective(
    vec3 viewRay,
    float viewDistance,
    float cameraAltitude,
    float fragmentAltitude,
    out vec3 extinction)
{
    float haze = clamp((uFogAndExposure.w - 1.0) / 9.0, 0.0, 1.0);
    float humidity = clamp(uFogAndExposure.x * 4500.0, 0.0, 1.4);
    float mistScaleHeight = max(400.0, 1.0 / max(1.0e-5, uFogAndExposure.y));
    float mieScaleHeight = mix(kBaseMieScaleHeight, mistScaleHeight, clamp(humidity * 0.55, 0.0, 1.0));
    vec3 betaM = vec3(mix(7.0e-6, 3.0e-5, clamp((haze * 0.72) + (humidity * 0.45), 0.0, 1.0)));

    float averageAltitude = clamp(max(0.0, mix(cameraAltitude, fragmentAltitude, 0.5)), 0.0, 100000.0);
    float densityR = exp(-averageAltitude / kRayleighScaleHeight);
    float densityM = exp(-averageAltitude / mieScaleHeight);

    float skyView = clamp((viewRay.y * 0.5) + 0.5, 0.0, 1.0);
    float atmosphereStrength = clamp((haze * 0.35) + (humidity * 0.65), 0.08, 1.0);
    float horizonBoost = 1.0 + ((1.0 - skyView) * mix(0.35, 1.4, atmosphereStrength));
    float opticalDistance = max(0.0, viewDistance) * mix(0.045, 0.16, atmosphereStrength) * horizonBoost;
    vec3 opticalDepth = (kRayleighBeta * (densityR * opticalDistance)) + (betaM * (densityM * opticalDistance));
    extinction = exp(-max(opticalDepth, vec3(0.0)));

    float mu = dot(viewRay, safeNormalize(uLightDirection.xyz));
    float phaseR = rayleighPhase(mu);
    float phaseM = hgPhase(mu, mix(0.68, 0.84, haze));
    vec3 sunColor = max(uLightColor.xyz, vec3(0.001)) * mix(4.0, 9.0, atmosphereStrength);
    vec3 ambientSky = mix(uGroundColor.xyz, uSkyColor.xyz, skyView);
    vec3 atmosphereTint = mix(ambientSky, uFogColor.xyz, clamp((haze * 0.45) + (humidity * 0.55), 0.0, 1.0));
    vec3 atmosphereBase = atmosphereTint * mix(0.04, 0.14, atmosphereStrength);
    vec3 inscatter =
        sunColor *
        ((kRayleighBeta * phaseR * densityR) + (betaM * phaseM * densityM * 1.35)) *
        opticalDistance *
        mix(110.0, 280.0, atmosphereStrength);
    return (atmosphereBase + inscatter) * clamp(vec3(1.0) - extinction, vec3(0.0), vec3(1.0));
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
    vec3 viewRay = safeNormalize(inRelativePosition);
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

    vec3 extinction;
    vec3 airlight = computeAerialPerspective(
        viewRay,
        viewDistance,
        max(0.0, uCameraPosition.y),
        max(0.0, inWorldHeight),
        extinction);
    finalColor = (finalColor * extinction) + airlight;

    float fogSpan = max(0.001, uObjectFogRange.y - uObjectFogRange.x);
    float rangeFog = clamp((viewDistance - uObjectFogRange.x) / fogSpan, 0.0, 1.0);
    if (rangeFog > 0.0) {
        float humidity = clamp(uFogAndExposure.x * 4500.0, 0.0, 1.0);
        finalColor = mix(finalColor, airlight + (uFogColor.xyz * 0.10), rangeFog * mix(0.20, 0.48, humidity));
    }

    finalColor *= exp2(uFogAndExposure.z);
    finalColor = linearToSrgb(toneMapAces(finalColor));
    outColor = vec4(finalColor, shaded.a);
}
