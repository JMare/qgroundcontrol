#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float droneAlt;
    float droneX;
    float droneY;
    float gridCols;
    float gridRows;
};

layout(binding = 1) uniform sampler2D altitudeTexture;

void main() {
    // 🔲 Snap fragment to grid pixel center
    int col = int(floor(vTexCoord.x * gridCols));
    int row = int(floor(vTexCoord.y * gridRows));

    vec2 snappedTexCoord = (vec2(col, row) + 0.5) / vec2(gridCols, gridRows);
    float fragX = float(col);
    float fragY = float(row);

    // 📏 Vector from drone to pixel
    float dx = fragX - droneX;
    float dy = fragY - droneY;
    float distance = sqrt(dx * dx + dy * dy);

    if (distance < 0.5) {
        fragColor = vec4(0.0, 0.0, 1.0, 1.0); // 🔵 Drone location
        return;
    }

    float stepCount = distance;
    float stepX = dx / stepCount;
    float stepY = dy / stepCount;

    float maxSlope = -1e9;
    float sampleX = droneX;
    float sampleY = droneY;

    // 🔁 Walk along the ray
    for (int i = 1; i < int(stepCount); ++i) {
        sampleX += stepX;
        sampleY += stepY;

        int sx = int(clamp(floor(sampleX), 0.0, gridCols - 1.0));
        int sy = int(clamp(floor(sampleY), 0.0, gridRows - 1.0));

        vec2 sampleCoord = (vec2(sx, sy) + 0.5) / vec2(gridCols, gridRows);
        float gray = texture(altitudeTexture, sampleCoord).r;
        float terrainAlt = gray * 255;

        float dist = length(vec2(sampleX - droneX, sampleY - droneY));
        float slope = (terrainAlt - droneAlt) / dist;
        float epsilon = 1e-4; // Small fudge factor to ignore precision noise
        if (slope > maxSlope + epsilon) {
            maxSlope = slope;
        }
    }

    // 🎯 Final point visibility check
    int fx = col;
    int fy = row;
    vec2 finalCoord = (vec2(fx, fy) + 0.5) / vec2(gridCols, gridRows);
    float finalGray = texture(altitudeTexture, finalCoord).r;

    float finalAlt = finalGray * 255 + 4.0;
    float finalSlope = (finalAlt - droneAlt) / distance;

    // Difference between final slope and max occluding slope
    float diff = finalSlope - maxSlope;

    // 🎨 Smooth transition from blocked to clear
    // Red → Orange → Green depending on how clear it is
    //float t = smoothstep(-0.05, 0.02, diff); // t = 0 → blocked, t = 1 → clear
    float t = smoothstep(-0.15, 0.01, diff);

    vec3 green = vec3(0.0, 1.0, 0.0);
    vec3 color = green;

    float alpha = (t < 0.05) ? 0.0 : qt_Opacity * t;
    if (t <= 0.0) {
        discard; // Skip rendering this fragment entirely
    }
    fragColor = vec4(color, alpha);
}
