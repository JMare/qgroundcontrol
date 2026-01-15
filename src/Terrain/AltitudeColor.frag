// RadioLOS_Attitude.frag  (LOS-only, no RF)
// #version must match your pipeline (you had 440)
#version 440

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;

    // TX (drone) in raster/grid space + altitude in meters (AMSL)
    float droneAlt;
    float droneX;
    float droneY;

    // Raster grid size (pixels)
    float gridCols;
    float gridRows;

    float terrainMinMeters;
    float terrainMaxMeters;

    // Pixel-to-meter scale
    float metersPerPixelX;
    float metersPerPixelY;

    // RF params (unused in LOS-only mode, but kept for layout compatibility)
    float freqMHz;
    float minDbm;
    float maxDbm;
    float systemLoss_dB;

    // Antenna axis in ENU (unused here, kept for layout compatibility)
    float antAxisX;
    float antAxisY;
    float antAxisZ;

    // ---- DEBUG ----
    // 0 = LOS-only view
    // 1 = altitude grayscale (decoded meters)
    // 2 = raw packed bytes (R=low byte, G=high byte) shown as colors (+ magenta for invalid)
    float debugMode;

    // Optional: clamp grayscale range for debugMode=1
    // If <= 0, uses terrainMinMeters / terrainMaxMeters
    float debugMinAlt;
    float debugMaxAlt;
};

layout(binding = 1) uniform sampler2D altitudeTexture;

// --- Helpers ---

bool isTexInvalid(vec4 tex) {
    // Primary: alpha==0 means "no data" from your CPU packer (NaN -> transparent)
    if (tex.a < 0.5) return true;

    // Secondary: fully transparent pixel safeguard
    if (tex.r < 0.001 && tex.g < 0.001 && tex.b < 0.001 && tex.a < 0.001) return true;

    return false;
}

// Returns (altMeters, validFlag)
vec2 sampleTerrainAltValid(float x, float y) {
    int sx = int(clamp(floor(x), 0.0, gridCols - 1.0));
    int sy = int(clamp(floor(y), 0.0, gridRows - 1.0));
    vec2 uv = (vec2(sx, sy) + 0.5) / vec2(gridCols, gridRows);

    vec4 tex = texture(altitudeTexture, uv);
    if (isTexInvalid(tex)) {
        return vec2(0.0, 0.0);
    }

    // Decode 16-bit from packed bytes (low byte in R, high byte in G)
    float r = tex.r * 255.0;
    float g = tex.g * 255.0;
    float q16 = floor(r + 0.5) + floor(g + 0.5) * 256.0;
    float norm = q16 / 65535.0;

    float alt = terrainMinMeters + norm * (terrainMaxMeters - terrainMinMeters);
    return vec2(alt, 1.0);
}

// LOS visibility along ray drone->frag. Returns 0..1.
float computeVisibility(float fragX, float fragY) {
    float dx = fragX - droneX;
    float dy = fragY - droneY;
    float dist2D = sqrt(dx * dx + dy * dy);
    if (dist2D < 0.5) return 1.0;

    float stepCount = dist2D;
    float stepX = dx / stepCount;
    float stepY = dy / stepCount;

    float maxSlope = -1e9;
    float sampleX = droneX;
    float sampleY = droneY;

    for (int i = 1; i < int(stepCount); ++i) {
        sampleX += stepX;
        sampleY += stepY;

        vec2 altV = sampleTerrainAltValid(sampleX, sampleY);

        // Missing sample along ray:
        // - current behavior: SKIP (don't let holes nuke LOS)
        // If you prefer "missing blocks LOS", replace 'continue' with 'return 0.0;'
        if (altV.y < 0.5) {
            continue;
        }

        float terrainAlt = altV.x;

        float d = length(vec2(sampleX - droneX, sampleY - droneY));
        d = max(d, 1e-3);

        float slope = (terrainAlt - droneAlt) / d;
        maxSlope = max(maxSlope, slope);
    }

    vec2 finalAltV = sampleTerrainAltValid(fragX, fragY);
    if (finalAltV.y < 0.5) {
        return 0.0; // receiver cell has no data -> treat as not visible/drawable
    }

    float finalAlt = finalAltV.x;
    float finalSlope = (finalAlt - droneAlt) / max(dist2D, 1e-3);

    float diff = finalSlope - maxSlope;
    return smoothstep(-0.15, 0.01, diff);
}

void main() {
    if (gridCols <= 1.0 || gridRows <= 1.0) discard;

    // Snap to raster cell center
    int col = int(floor(vTexCoord.x * gridCols));
    int row = int(floor(vTexCoord.y * gridRows));
    float fragX = float(col);
    float fragY = float(row);

    // Sample the raw texel ONCE (for debug mode 2)
    int sx = int(clamp(floor(fragX), 0.0, gridCols - 1.0));
    int sy = int(clamp(floor(fragY), 0.0, gridRows - 1.0));
    vec2 uv = (vec2(sx, sy) + 0.5) / vec2(gridCols, gridRows);
    vec4 tex = texture(altitudeTexture, uv);

    // Decode altitude + validity
    vec2 altV = sampleTerrainAltValid(fragX, fragY);
    float fragAlt = altV.x;
    float valid = altV.y;

    // -------- DEBUG MODES --------

    // Mode 2: raw packed bytes view
    if (debugMode > 1.5) {
        vec3 raw = vec3(tex.r, tex.g, 0.0);
        if (isTexInvalid(tex)) {
            raw = mix(raw, vec3(1.0, 0.0, 1.0), 0.85);
        }
        fragColor = vec4(raw, qt_Opacity);
        return;
    }

    // Mode 1: altitude grayscale (decoded meters)
    if (debugMode > 0.5) {
        if (valid < 0.5) {
            fragColor = vec4(1.0, 0.0, 1.0, qt_Opacity); // missing = magenta
            return;
        }

        float lo = (debugMinAlt > 0.0) ? debugMinAlt : terrainMinMeters;
        float hi = (debugMaxAlt > 0.0) ? debugMaxAlt : terrainMaxMeters;
        hi = max(hi, lo + 1.0);

        float t = clamp((fragAlt - lo) / (hi - lo), 0.0, 1.0);
        fragColor = vec4(vec3(t), qt_Opacity);
        return;
    }

    // -------- LOS-ONLY MODE (debugMode == 0) --------

    // NaN/degenerate guards
    if (droneX != droneX || droneY != droneY || droneAlt != droneAlt) discard;

    // If the receiver cell has no terrain data, show it as magenta (so you can see holes)
    // If you prefer "treat missing as blocked red", change this to red.
    if (valid < 0.5) {
        fragColor = vec4(1.0, 0.0, 1.0, qt_Opacity);
        return;
    }

    float vis = computeVisibility(fragX, fragY);

    // Want: transparent if LOS, red if blocked
    if (vis > 0.5) {
        discard; // transparent
    } else {
        fragColor = vec4(1.0, 0.0, 0.0, qt_Opacity*0.5); // blocked = red
    }
}
