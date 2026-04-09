#version 450

layout(set = 2, binding = 0) uniform sampler2D uCompositeTexture;
layout(set = 3, binding = 0, std140) uniform CompositeUniforms {
    vec4 uCameraRight;
    vec4 uCameraUp;
    vec4 uCameraForward;
    vec4 uProjectionParams;
    vec4 uCameraWorldPosition;
    vec4 uPlanetCenter;
    vec4 uPlanetParams;
    vec4 uLightDirection;
    vec4 uLightColor;
    vec4 uSkyColor;
    vec4 uGroundColor;
    vec4 uFogColor;
    vec4 uFogAndExposure;
};

layout(location = 0) in vec2 inTexCoord;
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

float hash13(vec3 p)
{
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453123);
}

float valueNoise3(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - (2.0 * f));

    float c000 = hash13(i + vec3(0.0, 0.0, 0.0));
    float c100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float c010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float c110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float c001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float c101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float c011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float c111 = hash13(i + vec3(1.0, 1.0, 1.0));

    float x00 = mix(c000, c100, f.x);
    float x10 = mix(c010, c110, f.x);
    float x01 = mix(c001, c101, f.x);
    float x11 = mix(c011, c111, f.x);
    float y0 = mix(x00, x10, f.y);
    float y1 = mix(x01, x11, f.y);
    return mix(y0, y1, f.z);
}

float fbm3(vec3 p)
{
    float amp = 1.0;
    float sum = 0.0;
    float weight = 0.0;
    float freq = 1.0;
    for (int index = 0; index < 5; ++index) {
        sum += valueNoise3(p * freq) * amp;
        weight += amp;
        amp *= 0.55;
        freq *= 2.05;
    }
    return weight > 0.0 ? sum / weight : 0.0;
}

float ridgeFbm(vec3 p)
{
    float amp = 1.0;
    float sum = 0.0;
    float weight = 0.0;
    float freq = 1.0;
    for (int index = 0; index < 5; ++index) {
        float n = (valueNoise3(p * freq) * 2.0) - 1.0;
        float ridge = 1.0 - abs(n);
        sum += ridge * ridge * amp;
        weight += amp;
        amp *= 0.6;
        freq *= 2.1;
    }
    return weight > 0.0 ? sum / weight : 0.0;
}

float raySphereNearestIntersection(vec3 origin, vec3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - (radius * radius);
    float discriminant = (b * b) - c;
    if (discriminant < 0.0) {
        return -1.0;
    }
    float root = sqrt(discriminant);
    float nearHit = -b - root;
    float farHit = -b + root;
    if (nearHit > 0.0) {
        return nearHit;
    }
    return farHit > 0.0 ? farHit : -1.0;
}

float raySphereFarIntersection(vec3 origin, vec3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - (radius * radius);
    float discriminant = (b * b) - c;
    if (discriminant < 0.0) {
        return -1.0;
    }
    return -b + sqrt(discriminant);
}

vec3 reconstructViewDirection(vec2 uv)
{
    vec2 ndc = vec2((uv.x * 2.0) - 1.0, 1.0 - (uv.y * 2.0));
    vec3 view =
        uCameraForward.xyz +
        (uCameraRight.xyz * (ndc.x * uProjectionParams.x)) +
        (uCameraUp.xyz * (ndc.y * uProjectionParams.y));
    return safeNormalize(view);
}

vec3 transmittanceToSun(vec3 samplePos, vec3 lightDir, float planetRadius, float outerRadius, float mieScaleHeight, vec3 betaM)
{
    float tAtmosphere = raySphereFarIntersection(samplePos, lightDir, outerRadius);
    float tGround = raySphereNearestIntersection(samplePos, lightDir, planetRadius);
    if (tAtmosphere <= 0.0 || (tGround > 0.0 && tGround < tAtmosphere)) {
        return vec3(0.0);
    }

    const int kSunSamples = 2;
    float stepSize = tAtmosphere / float(kSunSamples);
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;
    for (int index = 0; index < kSunSamples; ++index) {
        float t = (float(index) + 0.5) * stepSize;
        vec3 marchPos = samplePos + (lightDir * t);
        float altitude = max(length(marchPos) - planetRadius, 0.0);
        opticalDepthR += exp(-altitude / kRayleighScaleHeight) * stepSize;
        opticalDepthM += exp(-altitude / mieScaleHeight) * stepSize;
    }

    return exp(-((kRayleighBeta * opticalDepthR) + (betaM * opticalDepthM)));
}

vec3 sampleSky(vec3 origin, vec3 viewDir, float planetRadius, float outerRadius)
{
    float tAtmosphere = raySphereFarIntersection(origin, viewDir, outerRadius);
    if (tAtmosphere <= 0.0) {
        return vec3(0.0);
    }

    float tGround = raySphereNearestIntersection(origin, viewDir, planetRadius);
    float tMax = (tGround > 0.0) ? min(tGround, tAtmosphere) : tAtmosphere;
    float altitude = max(length(origin) - planetRadius, 0.0);

    float haze = clamp((uFogAndExposure.w - 1.0) / 9.0, 0.0, 1.0);
    float humidity = clamp(uFogAndExposure.x * 4500.0, 0.0, 1.4);
    float mistScaleHeight = max(400.0, 1.0 / max(1.0e-5, uFogAndExposure.y));
    float mieScaleHeight = mix(kBaseMieScaleHeight, mistScaleHeight, clamp(humidity * 0.55, 0.0, 1.0));
    vec3 betaM = vec3(mix(7.0e-6, 3.0e-5, clamp((haze * 0.72) + (humidity * 0.45), 0.0, 1.0)));
    vec3 lightDir = safeNormalize(uLightDirection.xyz);
    vec3 sunColor = max(uLightColor.xyz, vec3(0.001)) * 22.0;
    float mu = dot(viewDir, lightDir);
    float phaseR = rayleighPhase(mu);
    float phaseM = hgPhase(mu, mix(0.68, 0.84, haze));

    const int kViewSamples = 4;
    float stepSize = tMax / float(kViewSamples);
    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;
    vec3 sky = vec3(0.0);
    for (int index = 0; index < kViewSamples; ++index) {
        float t = (float(index) + 0.5) * stepSize;
        vec3 samplePos = origin + (viewDir * t);
        float sampleAltitude = max(length(samplePos) - planetRadius, 0.0);
        float densityR = exp(-sampleAltitude / kRayleighScaleHeight);
        float densityM = exp(-sampleAltitude / mieScaleHeight);
        opticalDepthR += densityR * stepSize;
        opticalDepthM += densityM * stepSize;

        vec3 sunTransmittance = transmittanceToSun(samplePos, lightDir, planetRadius, outerRadius, mieScaleHeight, betaM);
        vec3 viewTransmittance = exp(-((kRayleighBeta * opticalDepthR) + (betaM * opticalDepthM)));
        sky +=
            viewTransmittance *
            sunTransmittance *
            ((kRayleighBeta * densityR * phaseR) + (betaM * densityM * phaseM * 1.35)) *
            stepSize;
    }

    float skyView = clamp((viewDir.y * 0.5) + 0.5, 0.0, 1.0);
    vec3 ambientSky = mix(uGroundColor.xyz, uSkyColor.xyz, skyView);
    vec3 hazeTint = mix(ambientSky, uFogColor.xyz, clamp((haze * 0.42) + (humidity * 0.58), 0.0, 1.0));
    sky = (sky * sunColor * 24.0) + (hazeTint * (0.18 + (0.18 * humidity)));
    sky = mix(sky, vec3(0.0014, 0.0024, 0.0052), clamp((altitude - (outerRadius - planetRadius) * 0.6) / max(1.0, (outerRadius - planetRadius) * 0.8), 0.0, 1.0) * 0.82);
    sky *= exp2(uFogAndExposure.z);
    return linearToSrgb(toneMapAces(sky));
}

vec3 samplePlanetSurfaceColor(vec3 normal, vec3 lightDir)
{
    float continental = (fbm3(normal * 1.35) * 2.0) - 1.0;
    float macro = (fbm3(normal * 5.4) * 2.0) - 1.0;
    float ridges = ridgeFbm(normal * 13.0);
    float landMask = smoothstep(-0.08, 0.18, continental + (macro * 0.22));
    float coastMask = smoothstep(0.04, 0.18, landMask);
    float latitudeSnow = smoothstep(0.62, 0.92, abs(normal.y));
    float mountainSnow = smoothstep(0.42, 0.82, ridges);
    float snow = clamp((latitudeSnow * 0.72) + (mountainSnow * 0.52), 0.0, 1.0);

    vec3 deepOcean = vec3(0.018, 0.055, 0.108);
    vec3 shallowOcean = vec3(0.042, 0.170, 0.255);
    vec3 coast = vec3(0.33, 0.35, 0.24);
    vec3 lowland = vec3(0.11, 0.22, 0.10);
    vec3 highland = vec3(0.28, 0.25, 0.17);
    vec3 rock = vec3(0.44, 0.42, 0.40);
    vec3 snowColor = vec3(0.88, 0.90, 0.94);

    vec3 ocean = mix(deepOcean, shallowOcean, clamp((macro * 0.5) + 0.5, 0.0, 1.0));
    vec3 land = mix(lowland, highland, clamp((macro * 0.7) + (ridges * 0.6), 0.0, 1.0));
    land = mix(land, rock, clamp(ridges * 1.2, 0.0, 1.0));
    land = mix(land, snowColor, snow * 0.88);

    vec3 base = mix(ocean, mix(coast, land, coastMask), landMask);
    float nDotL = max(dot(normal, lightDir), 0.0);
    float fresnel = pow(1.0 - max(dot(normal, safeNormalize(-uCameraForward.xyz)), 0.0), 5.0);
    vec3 lit = base * ((uGroundColor.xyz * 0.12) + (uLightColor.xyz * (0.12 + (nDotL * 0.88))));
    vec3 specular = mix(vec3(0.0), uLightColor.xyz * (0.12 + (fresnel * 0.08)), (1.0 - landMask) * pow(nDotL, 28.0));
    vec3 nightLights =
        vec3(1.0, 0.72, 0.42) *
        smoothstep(0.60, 0.94, continental + (macro * 0.2)) *
        smoothstep(0.10, 0.0, nDotL) *
        0.18;
    return lit + specular + nightLights;
}

vec4 sampleCloudLayer(vec3 origin, vec3 viewDir, float planetRadius)
{
    float cloudRadius = planetRadius + 9000.0;
    float tCloud = raySphereNearestIntersection(origin, viewDir, cloudRadius);
    if (tCloud <= 0.0) {
        return vec4(0.0);
    }

    vec3 cloudPos = origin + (viewDir * tCloud);
    vec3 cloudNormal = safeNormalize(cloudPos);
    float cloudNoise = fbm3(cloudNormal * 24.0) * 0.7 + (fbm3(cloudNormal * 48.0) * 0.3);
    float coverage = smoothstep(0.56, 0.72, cloudNoise);
    if (coverage <= 0.0) {
        return vec4(0.0);
    }

    float nDotL = max(dot(cloudNormal, safeNormalize(uLightDirection.xyz)), 0.0);
    vec3 color = mix(vec3(0.84), vec3(1.0), nDotL * 0.75) * (0.65 + (coverage * 0.35));
    return vec4(color, coverage * 0.78);
}

void main()
{
    if (uProjectionParams.w > 0.5) {
        outColor = texture(uCompositeTexture, inTexCoord);
        return;
    }

    vec4 sceneColor = texture(uCompositeTexture, inTexCoord);
    vec3 viewDir = reconstructViewDirection(inTexCoord);

    if (uPlanetParams.z < 0.5) {
        vec3 skyOnly = sampleSky(vec3(0.0, uPlanetParams.x, 0.0), viewDir, max(1.0, uPlanetParams.x), max(1.0, uPlanetParams.x + uPlanetParams.y));
        float coverage = clamp(sceneColor.a, 0.0, 1.0);
        outColor = vec4(sceneColor.rgb + (skyOnly * (1.0 - coverage)), 1.0);
        return;
    }

    vec3 origin = uCameraWorldPosition.xyz - uPlanetCenter.xyz;
    float planetRadius = max(1.0, uPlanetParams.x);
    float outerRadius = planetRadius + max(1000.0, uPlanetParams.y);
    vec3 lightDir = safeNormalize(uLightDirection.xyz);

    vec3 sky = sampleSky(origin, viewDir, planetRadius, outerRadius);
    vec3 background = sky;

    float tSurface = raySphereNearestIntersection(origin, viewDir, planetRadius);
    if (tSurface > 0.0) {
        vec3 surfacePos = origin + (viewDir * tSurface);
        vec3 surfaceNormal = safeNormalize(surfacePos);
        vec3 surfaceColor = samplePlanetSurfaceColor(surfaceNormal, lightDir);
        float limb = pow(1.0 - max(dot(surfaceNormal, -viewDir), 0.0), 3.0);
        background = mix(surfaceColor, sky, clamp(limb * 0.82, 0.0, 0.78));
    }

    vec4 cloudLayer = sampleCloudLayer(origin, viewDir, planetRadius);
    background = mix(background, cloudLayer.rgb, cloudLayer.a);

    float coverage = clamp(sceneColor.a, 0.0, 1.0);
    outColor = vec4(sceneColor.rgb + (background * (1.0 - coverage)), 1.0);
}
