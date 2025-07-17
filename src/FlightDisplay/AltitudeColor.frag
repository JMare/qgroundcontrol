#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D altitudeTexture;

void main()
{
    float val = texture(altitudeTexture, qt_TexCoord0).r;
    vec3 color = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), val);
    fragColor = vec4(color, 0.5);
}
