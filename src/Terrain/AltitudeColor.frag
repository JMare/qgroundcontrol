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

    // Pixel-to-meter scale (you added these in QML)
    float metersPerPixelX; // meters per pixel in +X (east / lon direction)
    float metersPerPixelY; // meters per pixel in +Y (north / lat direction)

    // RF params
    float freqMHz;        // e.g. 1800.0
    float minDbm;         // e.g. -120.0
    float maxDbm;         // e.g. -80.0
    float systemLoss_dB;  // e.g. 30.0 (LTE-ish normalization / margins)

    // NEW: antenna axis in ENU (east,north,up), normalized in QML ideally
    float antAxisX;
    float antAxisY;
    float antAxisZ;
};

layout(binding = 1) uniform sampler2D altitudeTexture;

// --- Helpers ---

float log10_safe(float x) {
    // log10(x) = ln(x) / ln(10); 1/ln(10) ≈ 0.4342944819
    return log(max(x, 1e-30)) * 0.4342944819;
}

// Terrain decode: assumes your provider encodes altitude as gray*255.
// If you actually normalize real meters into 0..1, replace this with
// fragAlt = terrainMin + gray*(terrainMax-terrainMin).
float sampleTerrainAlt(float x, float y) {
    int sx = int(clamp(floor(x), 0.0, gridCols - 1.0));
    int sy = int(clamp(floor(y), 0.0, gridRows - 1.0));
    vec2 uv = (vec2(sx, sy) + 0.5) / vec2(gridCols, gridRows);
    float gray = texture(altitudeTexture, uv).r;
    return gray * 255.0;
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

// LTE-style discrete heatmap for RSRP-like values
vec4 lteHeatmap(float dbm) {
    if (dbm < minDbm) return vec4(0.0);
    dbm = clamp(dbm, minDbm, maxDbm);

    // Common LTE RSRP bins (dBm)
    float t0 = -120.0;
    float t1 = -110.0;
    float t2 = -100.0;
    float t3 = -90.0;
    float t4 = -80.0;

    // Colors: weak (blue) -> strong (red)
    vec4 c0 = vec4(0.00, 0.20, 0.80, 1.0); // blue
    vec4 c1 = vec4(0.00, 0.80, 0.80, 1.0); // cyan
    vec4 c2 = vec4(0.00, 0.85, 0.20, 1.0); // green
    vec4 c3 = vec4(1.00, 0.90, 0.00, 1.0); // yellow
    vec4 c4 = vec4(1.00, 0.20, 0.00, 1.0); // red

    float w = 2.0; // smoothing width in dB

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
    // Snap to raster cell center
    int col = int(floor(vTexCoord.x * gridCols));
    int row = int(floor(vTexCoord.y * gridRows));
    float fragX = float(col);
    float fragY = float(row);

    // NaN/degenerate guards (portable)
    if (droneX != droneX || droneY != droneY || droneAlt != droneAlt) discard;
    if (gridCols <= 1.0 || gridRows <= 1.0) discard;

    // LOS
    float vis = computeVisibility(fragX, fragY);
    if (vis <= 0.001) discard;

    // Receiver point altitude (ground)
    float fragAlt = sampleTerrainAlt(fragX, fragY);

    // Convert raster delta -> meters (ENU-ish)
    float dxm = (fragX - droneX) * metersPerPixelX; // east-ish
    float dym = (fragY - droneY) * metersPerPixelY; // north-ish
    float dzm = fragAlt - droneAlt;                 // up-ish

    float d_m = sqrt(dxm*dxm + dym*dym + dzm*dzm);
    d_m = max(d_m, 10.0); // avoid near-field dominance (10m clamp)

    // Direction unit vector in ENU (same basis as your axis uniforms)
    vec3 dirN = vec3(dxm, dym, dzm) / d_m;

    // Antenna axis in ENU from QML (fallback to vertical)
    vec3 axis = vec3(antAxisX, antAxisY, antAxisZ);
    float axisLen = length(axis);
    // NaN check: NaN != NaN
    if (axis.x != axis.x || axis.y != axis.y || axis.z != axis.z || axisLen < 0.5) {
        axis = vec3(0.0, 0.0, 1.0);
    } else {
        axis /= axisLen;
    }

    // Dipole power pattern: sin^2(theta) where theta is angle from axis
    float cosTheta = abs(dot(dirN, axis));
    float p = 1.0 - cosTheta * cosTheta;
    p = clamp(p, 0.0, 1.0);

    // Convert relative pattern power -> dBi (peak ~ 2.15 dBi)
    float dipole_dBi = 2.15 + 10.0 * log10_safe(max(p, 1e-6));

    // FSPL (dB), distance in km, frequency in MHz
    float d_km = d_m * 0.001;
    float fspl_dB = 32.44
                  + 20.0 * log10_safe(max(freqMHz, 1e-3))
                  + 20.0 * log10_safe(max(d_km, 1e-6));

    // 1 W TX power
    float pt_dBm = 30.0;

    // RSRP-like received level with a tunable system loss term
    float pr_dBm = pt_dBm + dipole_dBi - fspl_dB - systemLoss_dB;

    // Heatmap coloring
    vec4 hm = lteHeatmap(pr_dBm);
    if (hm.a <= 0.0) discard;

    // Alpha: include LOS confidence
    float alpha = qt_Opacity * 0.85 * hm.a * clamp(vis, 0.0, 1.0);

    fragColor = vec4(hm.rgb, alpha);
}
