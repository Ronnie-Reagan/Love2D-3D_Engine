#version 450

layout(set = 2, binding = 0) uniform sampler2D uHudTexture;

layout(location = 0) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(uHudTexture, inTexCoord);
}
