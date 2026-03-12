#version 450

layout(set = 1, binding = 0, std140) uniform SceneUniforms {
    mat4 uViewProjection;
    vec4 uWorldOrigin;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec2 inFogRange;
layout(location = 5) in float inAlphaCutoff;

layout(location = 0) out vec3 outRelativePosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outColor;
layout(location = 3) out vec2 outTexCoord;
layout(location = 4) out vec2 outFogRange;
layout(location = 5) out float outAlphaCutoff;
layout(location = 6) out float outWorldHeight;

void main()
{
    vec3 relativePosition = inPosition - uWorldOrigin.xyz;
    gl_Position = uViewProjection * vec4(relativePosition, 1.0);
    outRelativePosition = relativePosition;
    outNormal = normalize(inNormal);
    outColor = inColor;
    outTexCoord = inTexCoord;
    outFogRange = inFogRange;
    outAlphaCutoff = inAlphaCutoff;
    outWorldHeight = inPosition.y;
}
