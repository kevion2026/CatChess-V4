#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "geo45.h"
#ifdef CATCHESS_LITE2
#include "lite2_config.h"
#endif

namespace flash2 {

constexpr int BoardInputs = 14 * 90;
#ifndef V2_BOARD_WIDTH
#ifdef CATCHESS_PRO2
#define V2_BOARD_WIDTH 128
#else
#define V2_BOARD_WIDTH 64
#endif
#endif
#ifndef V2_HIDDEN_WIDTH
#ifdef CATCHESS_PRO2
#define V2_HIDDEN_WIDTH 64
#else
#define V2_HIDDEN_WIDTH 32
#endif
#endif
constexpr int BoardWidth = V2_BOARD_WIDTH, GeoWidth = 32, HiddenWidth = V2_HIDDEN_WIDTH;
constexpr int HeadWidth = BoardWidth + GeoWidth;
static_assert(BoardWidth > 0 && BoardWidth % 16 == 0 && HiddenWidth > 0);
#ifdef CATCHESS_LITE2
constexpr float BoardScale = 128.f;
#else
constexpr float BoardScale = 2048.f;
#endif
constexpr float GeoScale = 256.f;
constexpr float ValueScale = 400.f;
constexpr int Material[8] = {0, 0, 140, 140, 390, 800, 390, 90};

inline int feature(int piece, int sq, int side) {
    const int p = piece * side;
    return (std::abs(p) - 1 + (p < 0 ? 7 : 0)) * 90 + (side == 1 ? sq : 89 - sq);
}

// Each token contributes weighted raw moments in the three original GeoLite
// coordinate spaces. Integer sums are exactly reversible on make/unmake.
#ifdef CATCHESS_LITE2
using GeoRow = std::array<int16_t, GeoWidth>;
// Build the compact rows at compile time; accumulators remain 32-bit.
inline constexpr int geoRound(float x) { return int(x + (x < 0 ? -.5f : .5f)); }
inline constexpr auto geoRows = [] {
#else
using GeoRow = std::array<int32_t, GeoWidth>;
inline int geoRound(float x) { return std::lround(x); }
inline const auto geoRows = [] {
#endif
    std::array<GeoRow, BoardInputs> table{};
    constexpr float role[8] = {0, 1.40f, .80f, .85f, 1.f, 1.25f, 1.10f, .95f};
    for (int id = 0; id < BoardInputs; ++id) {
        const int type = (id / 90) % 7 + 1;
        const int owner = id < 7 * 90 ? 1 : -1;
        const int sq = id % 90;
        const bool advanced = owner == 1 ? sq < 45 : sq >= 45;
        auto& row = table[id];
        row[28] = Material[type] * owner;
        row[29] = Material[type];
        row[30] = type == 7 && advanced ? owner : 0;
        row[31] = type == 7 && advanced ? 1 : 0;
        const int slot = GEO_SLOT[id];
        if (slot < 0) continue;
        const float w = role[type] * (advanced ? 1.15f : 1.f) * GeoScale;
        row[0] = geoRound(w);
        for (int space = 0; space < 3; ++space) {
            const float* xyz = GEO_XYZ + (space * GEO_NVEC + slot) * 3;
            const float x = xyz[0], y = xyz[1], z = xyz[2];
            const float moments[] = {x, y, z, x*x, y*y, z*z, x*y, x*z, y*z};
            for (int k = 0; k < 9; ++k)
                row[1 + 9 * space + k] = geoRound(w * moments[k]);
        }
    }
    return table;
}();

#ifdef CATCHESS_LITE2
static_assert([] {
    // Bound every weighted coordinate product before narrowing a row.
    for (float v : GEO_XYZ) if (v > 8.f || v < -8.f) return false;
    return true;
}(), "Lite geometry row exceeds its int16 range");
#endif

inline void geometry(const int32_t* sum, float* out) {
    out[0] = sum[0] / (GeoScale * 16.f);
    const float iw = 1.f / std::max(sum[0], 1);
    for (int sp = 0; sp < 3; ++sp) {
        const int i = 1 + sp * 9;
        const float x = sum[i] * iw, y = sum[i+1] * iw, z = sum[i+2] * iw;
        out[i] = x;
        out[i+1] = y;
        out[i+2] = z;
        out[i+3] = sum[i+3] * iw - x*x;
        out[i+4] = sum[i+4] * iw - y*y;
        out[i+5] = sum[i+5] * iw - z*z;
        out[i+6] = sum[i+6] * iw - x*y;
        out[i+7] = sum[i+7] * iw - x*z;
        out[i+8] = sum[i+8] * iw - y*z;
    }
    out[28] = sum[28] / (ValueScale * 4.f);
    out[29] = sum[29] / (ValueScale * 12.f);
    out[30] = sum[30] / 3.f;
    out[31] = sum[31] / 3.f;
}

// Keep the learned correction anchored to a material term that generalizes
// to unseen positions. Phase reduces the prior in sparse, often drawn endings.
inline float prior(const int32_t* sum) {
    return sum[28] * (0.62f + 0.38f * std::min(sum[29] / 3600.f, 1.f)) / ValueScale;
}

#ifdef V2_PHASE_HEAD
inline float phase(const int32_t* sum) {
    return std::clamp((sum[29] - 1200.f) / 3800.f, 0.f, 1.f) - .5f;
}
#endif

} // namespace flash2
