#version 440
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D altitudeTexture;

void main() {
    vec4 tex = texture(altitudeTexture, vTexCoord);
    //fragColor = vec4(tex.rgb, tex.a) * qt_Opacity;
    //fragColor = vec4(1.0, 0.0, 0.0, 1.0); // Solid red
    //fragColor = vec4(vTexCoord.x, vTexCoord.y, 0.0, 1.0);
    fragColor = vec4(tex.r, tex.r, tex.r, 1.0);
}
