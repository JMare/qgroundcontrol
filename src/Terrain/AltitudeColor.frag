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
    // 📌 SNAP fragment to center of its grid cell (avoids subpixel aliasing)
    int col = int(floor(vTexCoord.x * float(gridCols)));
    int row = int(floor(vTexCoord.y * float(gridRows)));

    vec2 snappedTexCoord = (vec2(col, row) + 0.5) / vec2(gridCols, gridRows);
    float fragX = float(col);
    float fragY = float(row);

    // ⛓️ Vector from drone to this point
    float dx = fragX - droneX;
    float dy = fragY - droneY;
    float distance = sqrt(dx * dx + dy * dy);

    if (distance < 0.5) {
        fragColor = vec4(0.0, 0.0, 1.0, 1.0); // 🔵 Drone pixel
        return;
    }

    // ⚡ Raytrace setup
    float steps = distance;
    float stepX = dx / steps;
    float stepY = dy / steps;

    float maxSlope = -99999.0;
    float sampleX = droneX;
    float sampleY = droneY;
    float obstruction = 0.0;

    for (int i = 1; i < int(steps); ++i) {
        sampleX += stepX;
        sampleY += stepY;

        int sx = int(clamp(floor(sampleX), 0.0, float(gridCols - 1)));
        int sy = int(clamp(floor(sampleY), 0.0, float(gridRows - 1)));

        vec2 texCoord = (vec2(sx, sy) + 0.5) / vec2(gridCols, gridRows);
        float gray = texture(altitudeTexture, texCoord).r;
        float terrainAlt = 200.0 + gray * 255.0;

        float dist = length(vec2(sampleX - droneX, sampleY - droneY));
        float slope = (terrainAlt - droneAlt) / dist;

        if (slope > maxSlope) {
            maxSlope = slope;
        }
    }

    // 🎯 Final pixel terrain altitude
    float finalGray = texture(altitudeTexture, snappedTexCoord).r;
    float finalAlt = 200.0 + finalGray * 255.0;
    float finalSlope = (finalAlt - droneAlt) / distance;

    float diff = finalSlope - maxSlope;

    if (diff >= 0.01) {
        fragColor = vec4(0.0, 1.0, 0.0, qt_Opacity); // 🟢 Visible
    } else if (diff >= -0.02) {
        fragColor = vec4(1.0, 0.5, 0.0, qt_Opacity); // 🟠 Near blocked
    } else {
        float strength = clamp(-diff * 10.0, 0.0, 1.0); // redder = more obstructed
        fragColor = vec4(strength, 0.0, 0.0, qt_Opacity); // 🟥 Obstructed gradient
    }
}
