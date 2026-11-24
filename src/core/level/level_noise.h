#ifndef MINICRAFT_CORE_LEVEL_LEVEL_NOISE_H_
#define MINICRAFT_CORE_LEVEL_LEVEL_NOISE_H_

#include <array>
#include <cstdint>
#include <vector>

namespace minicraft::level {

// Multi-scale simplex noise grid.
class LevelNoise {
 public:
  static constexpr int32_t kDefaultLayers = 10;
  static constexpr int32_t kScaleCount = 10;
  static constexpr std::array<double, 10> kScaleFactors = {
      1.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0, 512.0, 2048.0, 8192.0};

  LevelNoise(int64_t seed, int32_t x_off, int32_t y_off, int32_t z_off = 0,
             int32_t w = 64, int32_t h = 64, int32_t layers = kDefaultLayers);

  int32_t Width() const { return w_; }
  int32_t Height() const { return h_; }
  int32_t Layers() const { return layers_; }

  double Sample(int32_t scale_idx, int32_t x, int32_t y, int32_t layer) const;
  double GetValue(int32_t scale_idx, int32_t x, int32_t y) const {
    return Sample(scale_idx, x, y, 0);
  }

  double GetTileNoise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(0, x, y, layer);
  }
  double GetScale4Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(1, x, y, layer);
  }
  double GetScale8Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(2, x, y, layer);
  }
  double GetScale16Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(3, x, y, layer);
  }
  double GetScale32Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(4, x, y, layer);
  }
  double GetScale64Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(5, x, y, layer);
  }
  double GetScale128Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(6, x, y, layer);
  }
  double GetScale512Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(7, x, y, layer);
  }
  double GetScale2048Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(8, x, y, layer);
  }
  double GetScale8192Noise(int32_t x, int32_t y, int32_t layer) const {
    return Sample(9, x, y, layer);
  }

  double Octave(int32_t x, int32_t y, int32_t layer,
                const std::array<double, 10>& weights) const;

  double GetTemperature(int32_t x, int32_t y) const;
  double GetHeight(int32_t x, int32_t y) const;
  double GetHumidity(int32_t x, int32_t y) const;

 private:
  int32_t w_;
  int32_t h_;
  int32_t layers_;
  // Contiguous storage: scale_idx * (w * h * layers) + layer * (w * h) + y * w + x
  std::vector<double> values_;
};

}  // namespace minicraft::level

#endif  // MINICRAFT_CORE_LEVEL_LEVEL_NOISE_H_
