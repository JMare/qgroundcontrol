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
    // Convert texture coordinates to grid coordinates
    float fragX = vTexCoord.x * float(gridCols);
    float fragY = vTexCoord.y * float(gridRows);

    // Vector from drone to current pixel
    float dx = fragX - droneX;
    float dy = fragY - droneY;
    float distance = sqrt(dx * dx + dy * dy);

    // Small threshold for early exit (avoid self-intersection)
    if (distance < 1.0) {
        fragColor = vec4(0.0, 1.0, 0.0, qt_Opacity); // Green if we're at the drone location
        return;
    }

    // Normalize direction
    float stepCount = distance;
    float stepX = dx / stepCount;
    float stepY = dy / stepCount;

    // Start at drone
    float sampleX = droneX;
    float sampleY = droneY;
    float clear = 1.0;

    // Ray trace along the path
    for (int i = 0; i < int(stepCount); ++i) {
        sampleX += stepX;
        sampleY += stepY;

        // Clamp to avoid sampling out of bounds
        int px = int(clamp(floor(sampleX), 0.0, float(gridCols - 1)));
        int py = int(clamp(floor(sampleY), 0.0, float(gridRows - 1)));

        vec2 texCoord = vec2(float(px) + 0.5, float(py) + 0.5) / vec2(float(gridCols), float(gridRows));
        float gray = texture(altitudeTexture, texCoord).r;

        float terrainAlt = 200.0 + gray * 255.0;

        // Linearly interpolate height along LOS
        float t = float(i) / stepCount;
        float rayHeight = mix(droneAlt, droneAlt, t); // Flat for now, modify if needed

        if (terrainAlt > rayHeight) {
            clear = 0.0; // Obstructed
            break;
        }
    }

    // Color based on visibility
    fragColor = clear > 0.5 ? vec4(0.0, 1.0, 0.0, qt_Opacity) : vec4(1.0, 0.0, 0.0, qt_Opacity);
}
