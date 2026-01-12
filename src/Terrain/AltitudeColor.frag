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

    float metersPerPixelX;
    float metersPerPixelY;

    float freqMHz;

    // LTE-ish display range (RSRP)
    float minDbm;   // recommend -120.0 (or -130.0)
    float maxDbm;   // recommend -80.0
};

layout(binding = 1) uniform sampler2D altitudeTexture;

float sampleTerrainAlt(float x, float y) {
    int sx = int(clamp(floor(x), 0.0, gridCols - 1.0));
    int sy = int(clamp(floor(y), 0.0, gridRows - 1.0));
    vec2 uv = (vec2(sx, sy) + 0.5) / vec2(gridCols, gridRows);
    float gray = texture(altitudeTexture, uv).r;
    return gray * 255.0;
}

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

        float terrainAlt = sampleTerrainAlt(sampleX, sampleY);

        float d = length(vec2(sampleX - droneX, sampleY - droneY));
        d = max(d, 1e-3);

        float slope = (terrainAlt - droneAlt) / d;
        maxSlope = max(maxSlope, slope);
    }

    float finalAlt = sampleTerrainAlt(fragX, fragY);
    float finalSlope = (finalAlt - droneAlt) / max(dist2D, 1e-3);

    float diff = finalSlope - maxSlope;
    return smoothstep(-0.15, 0.01, diff);
}

float log10_safe(float x) {
    return log(max(x, 1e-30)) * 0.4342944819;
}

// LTE-style discrete heatmap for RSRP-like dBm values.
// Thresholds based on common planning bins:
// >= -80 excellent, -80..-90 good, -90..-100 fair, -100..-110 poor, -110..-120 very poor.
vec4 lteHeatmap(float dbm) {
    // Below min: transparent
    if (dbm < minDbm) return vec4(0.0);

    // Clamp for safety
    dbm = clamp(dbm, minDbm, maxDbm);

    // Bin edges (dBm)
    float t0 = -120.0; // very poor
    float t1 = -110.0; // poor
    float t2 = -100.0; // fair
    float t3 = -90.0;  // good
    float t4 = -80.0;  // excellent

    // If your min/max differ from these, keep bins aligned by shifting,
    // but for LTE this set is a good default.

    // Colors (RGBA) - common RF map style: blue weak -> green/yellow -> red strong
    vec4 c0 = vec4(0.00, 0.20, 0.80, 1.0); // blue
    vec4 c1 = vec4(0.00, 0.80, 0.80, 1.0); // cyan
    vec4 c2 = vec4(0.00, 0.85, 0.20, 1.0); // green
    vec4 c3 = vec4(1.00, 0.90, 0.00, 1.0); // yellow
    vec4 c4 = vec4(1.00, 0.20, 0.00, 1.0); // red

    // Smooth transitions between bins (2 dB smoothing)
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

    // NaN guard
    if (droneX != droneX || droneY != droneY || droneAlt != droneAlt) discard;
    if (gridCols <= 1.0 || gridRows <= 1.0) discard;

    float vis = computeVisibility(fragX, fragY);
    if (vis <= 0.001) discard;

    float fragAlt = sampleTerrainAlt(fragX, fragY);

    float dxm = (fragX - droneX) * metersPerPixelX;
    float dym = (fragY - droneY) * metersPerPixelY;
    float dzm = fragAlt - droneAlt;

    float d_m = sqrt(dxm*dxm + dym*dym + dzm*dzm);
    d_m = max(d_m, 1.0);

    // Dipole (vertical axis)
    float cosTheta = abs(dzm) / d_m;
    float p = 1.0 - cosTheta * cosTheta;
    p = clamp(p, 0.0, 1.0);

    // Peak dipole gain ~2.15 dBi; relative power -> dB
    float dipole_dBi = 2.15 + 10.0 * log10_safe(max(p, 1e-6));

    // FSPL
    float d_km = d_m * 0.001;
    float fspl_dB = 32.44
        + 20.0 * log10_safe(max(freqMHz, 1e-3))
        + 20.0 * log10_safe(max(d_km, 1e-6));

    // 1 W TX power
    float pt_dBm = 30.0;
    float systemLoss_dB = 30.0;   // LTE system / RSRP normalization
    float pr_dBm = pt_dBm + dipole_dBi - fspl_dB - systemLoss_dB;

    // Heatmap color by RSRP-like strength
    vec4 hm = lteHeatmap(pr_dBm);
    if (hm.a <= 0.0) discard;

    // Use LOS as alpha multiplier (optional: convert to dB loss instead)
    float alpha = qt_Opacity * 0.85 * hm.a * clamp(vis, 0.0, 1.0);

    fragColor = vec4(hm.rgb, alpha);
}
