#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float droneAlt;
    float droneX;
    float droneY;
    float droneAlt2;
    float droneX2;
    float droneY2;
    float gridCols;
    float gridRows;
};

layout(binding = 1) uniform sampler2D altitudeTexture;

// 🔁 LOS visibility function for a given drone
float computeVisibility(float droneX, float droneY, float droneAlt, float fragX, float fragY) {
    float dx = fragX - droneX;
    float dy = fragY - droneY;
    float distance = sqrt(dx * dx + dy * dy);
    if (distance < 0.5) return 1.0;

    float stepCount = distance;
    float stepX = dx / stepCount;
    float stepY = dy / stepCount;

    float maxSlope = -1e9;
    float sampleX = droneX;
    float sampleY = droneY;

    for (int i = 1; i < int(stepCount); ++i) {
        sampleX += stepX;
        sampleY += stepY;

        int sx = int(clamp(floor(sampleX), 0.0, gridCols - 1.0));
        int sy = int(clamp(floor(sampleY), 0.0, gridRows - 1.0));

        vec2 sampleCoord = (vec2(sx, sy) + 0.5) / vec2(gridCols, gridRows);
        float gray = texture(altitudeTexture, sampleCoord).r;
        float terrainAlt = gray * 255.0;

        float dist = length(vec2(sampleX - droneX, sampleY - droneY));
        float slope = (terrainAlt - droneAlt) / dist;
        float epsilon = 1e-4;
        if (slope > maxSlope + epsilon) {
            maxSlope = slope;
        }
    }

    vec2 finalCoord = (vec2(fragX, fragY) + 0.5) / vec2(gridCols, gridRows);
    float finalGray = texture(altitudeTexture, finalCoord).r;
    float finalAlt = finalGray * 255.0;
    float finalSlope = (finalAlt - droneAlt) / distance;

    float diff = finalSlope - maxSlope;
    float visibility = smoothstep(-0.15, 0.01, diff);
    return visibility;
}

void main() {
    // 🔲 Snap fragment to grid pixel center
    int col = int(floor(vTexCoord.x * gridCols));
    int row = int(floor(vTexCoord.y * gridRows));
    float fragX = float(col);
    float fragY = float(row);

    // 🔍 Compute visibility for both drones
    float t1 = computeVisibility(droneX, droneY, droneAlt, fragX, fragY);
    float t2 = computeVisibility(droneX2, droneY2, droneAlt2, fragX, fragY);

    // ❌ Discard if not visible to either
    if (t1 <= 0.0 && t2 <= 0.0) {
        discard;
    }

    // 🎨 Define colors
    vec3 color1 = vec3(0.0, 1.0, 1.0); // cyan (Drone 1)
    vec3 color2 = vec3(1.0, 0.5, 0.0); // orange (Drone 2)

    // 📏 Distance to each drone
    float dx1 = fragX - droneX;
    float dy1 = fragY - droneY;
    float d1 = sqrt(dx1 * dx1 + dy1 * dy1);

    float dx2 = fragX - droneX2;
    float dy2 = fragY - droneY2;
    float d2 = sqrt(dx2 * dx2 + dy2 * dy2);

    // 🧠 Pick the closer visible drone
    vec3 color;
    float t;
    if (t1 > 0.0 && t2 > 0.0) {
        if (d1 <= d2) {
            color = color1;
            t = t1;
        } else {
            color = color2;
            t = t2;
        }
    } else if (t1 > 0.0) {
        color = color1;
        t = t1;
    } else {
        color = color2;
        t = t2;
    }

    // 🫧 Reduced opacity
    float alpha = qt_Opacity * 0.4 * t;
    fragColor = vec4(color, alpha);
}
