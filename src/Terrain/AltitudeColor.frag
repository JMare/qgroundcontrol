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
    // Convert fragment position to grid coordinates
    float fragX = vTexCoord.x * float(gridCols);
    float fragY = vTexCoord.y * float(gridRows);

    // Distance from drone
    float dx = fragX - droneX;
    float dy = fragY - droneY;
    float distance = sqrt(dx * dx + dy * dy);

    // 🟢 If we're at the drone location, it's visible
    if (distance < 1.0) {
        fragColor = vec4(0.0, 1.0, 0.0, qt_Opacity);
        return;
    }

    // Normalize direction vector
    float stepCount = distance;
    float stepX = dx / stepCount;
    float stepY = dy / stepCount;

    float sampleX = droneX;
    float sampleY = droneY;
    float maxElevation = -9999.0;

    for (int i = 1; i < int(stepCount); ++i) {
        sampleX += stepX;
        sampleY += stepY;

        // Clamp sample to grid bounds
        int px = int(clamp(floor(sampleX), 0.0, float(gridCols - 1)));
        int py = int(clamp(floor(sampleY), 0.0, float(gridRows - 1)));

        vec2 texCoord = (vec2(px, py) + 0.5) / vec2(gridCols, gridRows);
        float gray = texture(altitudeTexture, texCoord).r;
        float terrainAlt = 200.0 + gray * 255.0;

        float distToSample = length(vec2(sampleX - droneX, sampleY - droneY));
        float elevationAngle = (terrainAlt - droneAlt) / distToSample;

        if (elevationAngle > maxElevation)
            maxElevation = elevationAngle;
    }

    // Final pixel altitude and angle
    float currentGray = texture(altitudeTexture, vTexCoord).r;
    float currentAlt = 200.0 + currentGray * 255.0;
    float pixelAngle = (currentAlt - droneAlt) / distance;

    if (pixelAngle >= maxElevation) {
        fragColor = vec4(0.0, 1.0, 0.0, qt_Opacity); // 🟢 Visible
    } else {
        fragColor = vec4(1.0, 0.0, 0.0, qt_Opacity); // 🔴 Blocked
    }
}
