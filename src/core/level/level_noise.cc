#include "core/level/level_noise.h"

#include <algorithm>
#include <cmath>

#include "core/util/simplex.h"

namespace minicraft::level {

namespace {

constexpr int32_t kNoiseLayerDiff = 100;

constexpr std::array<double, 10> kTempWeights = {
    0.01, 0.02, 0.04, 0.08, 0.16, 0.32, 0.64, 0.05, 0.04, 0.01};
constexpr std::array<double, 10> kHeightWeights = {
    0.005, 0.01, 0.02, 0.01, 0.02, 0.05, 0.1, 0.2, 0.7, 0.2};
constexpr std::array<double, 10> kHumidityWeights = {
    0.02, 0.04, 0.07, 0.1, 0.4, 0.3, 0.1, 0.05, 0.02, 0.01};

}  // namespace

LevelNoise::LevelNoise(int64_t seed, int32_t x_off, int32_t y_off,
                       int32_t z_off, int32_t w, int32_t h, int32_t layers)
    : w_(w), h_(h), layers_(std::max(3, layers)) {
  const size_t grid_size = static_cast<size_t>(w_) * h_ * layers_;
  values_.resize(static_cast<size_t>(kScaleCount) * grid_size);

  util::Simplex simplex(seed);

  for (int32_t s = 0; s < kScaleCount; ++s) {
    const double inv_scale = 1.0 / kScaleFactors[s];
    double* scale_buf = values_.data() + static_cast<size_t>(s) * grid_size;
    for (int32_t z = 0; z < layers_; ++z) {
      const double nz =
          static_cast<double>(z + z_off + layers_ * s) * kNoiseLayerDiff;
      double* layer_buf = scale_buf + static_cast<size_t>(z) * w_ * h_;
      for (int32_t y = 0; y < h_; ++y) {
        const double ny = static_cast<double>(y + y_off) * inv_scale;
        for (int32_t x = 0; x < w_; ++x) {
          const double nx = static_cast<double>(x + x_off) * inv_scale;
          layer_buf[y * w_ + x] = simplex.Noise3(nx, ny, nz);
        }
      }
    }
  }
}

double LevelNoise::Sample(int32_t scale_idx, int32_t x, int32_t y,
                          int32_t layer) const {
  if (scale_idx < 0 || scale_idx >= kScaleCount || layer < 0 ||
      layer >= layers_ || w_ <= 0 || h_ <= 0) {
    return 0.0;
  }
  const int32_t lx = x & (w_ - 1);
  const int32_t ly = y & (h_ - 1);
  const size_t grid_size = static_cast<size_t>(w_) * h_ * layers_;
  const size_t idx = static_cast<size_t>(scale_idx) * grid_size +
                     static_cast<size_t>(layer) * w_ * h_ +
                     static_cast<size_t>(ly) * w_ + lx;
  return values_[idx];
}

double LevelNoise::Octave(int32_t x, int32_t y, int32_t layer,
                          const std::array<double, 10>& weights) const {
  double val = 0.0;
  for (int32_t s = 0; s < kScaleCount; ++s) {
    val += Sample(s, x, y, layer) * weights[s];
  }
  return val;
}

double LevelNoise::GetTemperature(int32_t x, int32_t y) const {
  return Octave(x, y, layers_ - 1, kTempWeights);
}

double LevelNoise::GetHeight(int32_t x, int32_t y) const {
  return Octave(x, y, layers_ - 2, kHeightWeights);
}

double LevelNoise::GetHumidity(int32_t x, int32_t y) const {
  return Octave(x, y, layers_ - 3, kHumidityWeights);
}

}  // namespace minicraft::level
