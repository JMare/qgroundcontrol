// RadioLOS_Attitude.frag
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

    // RF params
    float freqMHz;
    float minDbm;
    float maxDbm;
    float systemLoss_dB;

    // Antenna axis in ENU
    float antAxisX;
    float antAxisY;
    float antAxisZ;

    // ---- DEBUG ----
    // 0 = normal RF
    // 1 = altitude grayscale (decoded meters)
    // 2 = raw packed bytes (R=low byte, G=high byte) shown as colors
    float debugMode;

    // Optional: clamp grayscale range for debugMode=1
    // If <= 0, uses terrainMinMeters / terrainMaxMeters
    float debugMinAlt;
    float debugMaxAlt;
};

layout(binding = 1) uniform sampler2D altitudeTexture;

// --- Helpers ---

float log10_safe(float x) {
    return log(max(x, 1e-30)) * 0.4342944819;
}

bool isTexInvalid(vec4 tex) {
    // Primary: alpha==0 means "no data" from your CPU packer
    // Secondary: fully transparent pixel safeguard
    if (tex.a < 0.5) return true;

    // Some pipelines can mangle alpha; keep this check anyway:
    // If the entire texel is basically zero, treat invalid.
    if (tex.r < 0.001 && tex.g < 0.001 && tex.b < 0.001 && tex.a < 0.001) return true;

    return false;
}

// Returns (altMeters, validFlag)
vec2 sampleTerrainAltValid(float x, float y) {
    int sx = int(clamp(floor(x), 0.0, gridCols - 1.0));
    int sy = int(clamp(floor(y), 0.0, gridRows - 1.0));
    vec2 uv = (vec2(sx, sy) + 0.5) / vec2(gridCols, gridRows);

    vec4 tex = texture(altitudeTexture, uv);

    // If invalid by alpha/zero test, report invalid
    if (isTexInvalid(tex)) {
        return vec2(0.0, 0.0);
    }

    // Decode 16-bit from packed bytes
    float r = tex.r * 255.0;
    float g = tex.g * 255.0;
    float q16 = floor(r + 0.5) + floor(g + 0.5) * 256.0;
    float norm = q16 / 65535.0;

    float alt = terrainMinMeters + norm * (terrainMaxMeters - terrainMinMeters);
    return vec2(alt, 1.0);
}

float sampleTerrainAlt(float x, float y) {
    return sampleTerrainAltValid(x, y).x;
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

        // If the sample is invalid, SKIP it (don’t let NaNs nuke LOS)
        // You can change this to "return 0.0;" if you want missing data to block.
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
        return 0.0; // receiver cell has no data -> treat as not drawable
    }

    float finalAlt = finalAltV.x;
    float finalSlope = (finalAlt - droneAlt) / max(dist2D, 1e-3);

    float diff = finalSlope - maxSlope;
    return smoothstep(-0.15, 0.01, diff);
}

// LTE-style discrete heatmap for RSRP-like values
vec4 lteHeatmap(float dbm) {
    if (dbm < minDbm) return vec4(0.0);
    dbm = clamp(dbm, minDbm, maxDbm);

    float t0 = -120.0;
    float t1 = -110.0;
    float t2 = -100.0;
    float t3 = -90.0;
    float t4 = -80.0;

    vec4 c0 = vec4(0.00, 0.20, 0.80, 1.0);
    vec4 c1 = vec4(0.00, 0.80, 0.80, 1.0);
    vec4 c2 = vec4(0.00, 0.85, 0.20, 1.0);
    vec4 c3 = vec4(1.00, 0.90, 0.00, 1.0);
    vec4 c4 = vec4(1.00, 0.20, 0.00, 1.0);

    float w = 2.0;

    if (dbm < t1) {
        float u = smoothstep(t0, t0 + w, dbm);
        return mix(vec4(0.0), c0, u);
    } else if (dbm < t2) {
        float u = smoothstep(t1, t1 + w, dbm);
        return mix(c0, c1, u);
    } else if (dbm < t3) {
        float u = smoothstep(t2, t2 + w, dbm);
        return mix(c1, c2, u);
    } else if (dbm < t4) {
        float u = smoothstep(t3, t3 + w, dbm);
        return mix(c2, c3, u);
    } else {
        float u = smoothstep(t4 - w, t4, dbm);
        return mix(c3, c4, u);
    }
}

void main() {
    int col = int(floor(vTexCoord.x * gridCols));
    int row = int(floor(vTexCoord.y * gridRows));
    float fragX = float(col);
    float fragY = float(row);

    if (gridCols <= 1.0 || gridRows <= 1.0) discard;

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
    // Mode 2: raw packed bytes view (does not depend on alpha)
    // You should see a colorful-ish map; if it's all magenta/flat, binding/sampling is wrong.
    if (debugMode > 1.5) {
        // Visualize low/high bytes as RGB
        vec3 raw = vec3(tex.r, tex.g, 0.0);

        // If alpha says invalid, tint magenta on top so you see holes
        if (isTexInvalid(tex)) {
            raw = mix(raw, vec3(1.0, 0.0, 1.0), 0.85);
        }

        fragColor = vec4(raw, qt_Opacity);
        return;
    }

    // Mode 1: altitude grayscale (decoded meters)
    if (debugMode > 0.5) {
        if (valid < 0.5) {
            // Missing data = magenta (so it stands out)
            fragColor = vec4(1.0, 0.0, 1.0, qt_Opacity);
            return;
        }

        float lo = (debugMinAlt > 0.0) ? debugMinAlt : terrainMinMeters;
        float hi = (debugMaxAlt > 0.0) ? debugMaxAlt : terrainMaxMeters;
        hi = max(hi, lo + 1.0);

        float t = clamp((fragAlt - lo) / (hi - lo), 0.0, 1.0);
        fragColor = vec4(vec3(t), qt_Opacity);
        return;
    }

    // -------- NORMAL RF MODE (debugMode == 0) --------

    if (droneX != droneX || droneY != droneY || droneAlt != droneAlt) discard;
    if (valid < 0.5) discard;

    float vis = computeVisibility(fragX, fragY);
    if (vis <= 0.001) discard;

    float dxm = (fragX - droneX) * metersPerPixelX;
    float dym = (fragY - droneY) * metersPerPixelY;
    float dzm = fragAlt - droneAlt;

    float d_m = sqrt(dxm*dxm + dym*dym + dzm*dzm);
    d_m = max(d_m, 10.0);

    vec3 dirN = vec3(dxm, dym, dzm) / d_m;

    vec3 axis = vec3(antAxisX, antAxisY, antAxisZ);
    float axisLen = length(axis);
    if (axis.x != axis.x || axis.y != axis.y || axis.z != axis.z || axisLen < 0.5) {
        axis = vec3(0.0, 0.0, 1.0);
    } else {
        axis /= axisLen;
    }

    float cosTheta = abs(dot(dirN, axis));
    float p = 1.0 - cosTheta * cosTheta;
    p = clamp(p, 0.0, 1.0);

    float dipole_dBi = 2.15 + 10.0 * log10_safe(max(p, 1e-6));

    float d_km = d_m * 0.001;
    float fspl_dB = 32.44
                  + 20.0 * log10_safe(max(freqMHz, 1e-3))
                  + 20.0 * log10_safe(max(d_km, 1e-6));

    float pt_dBm = 30.0;
    float pr_dBm = pt_dBm + dipole_dBi - fspl_dB - systemLoss_dB;

    vec4 hm = lteHeatmap(pr_dBm);
    if (hm.a <= 0.0) discard;

    float alpha = qt_Opacity * 0.85 * hm.a * clamp(vis, 0.0, 1.0);
    fragColor = vec4(hm.rgb, alpha);
}
