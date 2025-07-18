#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float droneAlt; // ✅ Moved inside the block
};

layout(binding = 1) uniform sampler2D altitudeTexture;

void main() {
    float gray = texture(altitudeTexture, vTexCoord).r;
    float terrainAlt = 200.0 + gray * 255.0;

    if (terrainAlt > droneAlt) {
        fragColor = vec4(1.0, 0.0, 0.0, 1.0); // 🔴 Above
    } else {
        fragColor = vec4(0.0, 1.0, 0.0, 1.0); // 🟢 Below
    }

    fragColor *= qt_Opacity;
}
