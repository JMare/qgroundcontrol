#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float droneAlt; // ✅ Moved inside the block
    float droneX;
    float droneY;
    float gridCols;
    float gridRows;
};

layout(binding = 1) uniform sampler2D altitudeTexture;

void main() {
    float targetX = vTexCoord.x * float(gridCols);
    float targetY = vTexCoord.y * float(gridRows);

    float dx = targetX - droneX;
    float dy = targetY - droneY;

    const int steps = 50;
    bool blocked = false;

    float targetGray = texture(altitudeTexture, vec2(targetX / float(gridCols), targetY / float(gridRows))).r;
    float targetAlt = 200.0 + targetGray * 255.0;

    for (int i = 1; i < steps; ++i) {
        float t = float(i) / float(steps);

        float sampleX = droneX + dx * t;
        float sampleY = droneY + dy * t;

        vec2 sampleCoord = vec2(sampleX / float(gridCols), sampleY / float(gridRows));
        float gray = texture(altitudeTexture, sampleCoord).r;
        float terrainAlt = 200.0 + gray * 255.0;

        float expectedAlt = mix(droneAlt, targetAlt, t); // LOS slope

        if (terrainAlt > expectedAlt) {
            blocked = true;
            break;
        }
    }

    fragColor = blocked ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
    fragColor *= qt_Opacity;
}
