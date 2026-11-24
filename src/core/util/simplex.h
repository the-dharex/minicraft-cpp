#ifndef MINICRAFT_CORE_UTIL_SIMPLEX_H_
#define MINICRAFT_CORE_UTIL_SIMPLEX_H_

#include <cstdint>

namespace minicraft {
namespace util {

// Port of K.jpg's OpenSimplex2 (faster variant) Minicraft+ worldgen.
class Simplex {
 public:
  explicit Simplex(int64_t seed = 0) : seed_(seed) {}

  void SetSeed(int64_t seed) { seed_ = seed; }

  float Noise2(double x, double y) const;
  float Noise2ImproveX(double x, double y) const;
  float Noise3(double x, double y, double z) const {
    return Noise3ImproveXY(x, y, z);
  }
  float Noise3ImproveXY(double x, double y, double z) const;
  float Noise3ImproveXZ(double x, double y, double z) const;

 private:
  float Noise2UnskewedBase(double xs, double ys) const;
  float Noise3UnrotatedBase(double xr, double yr, double zr) const;
  float Grad2(int64_t xsvp, int64_t ysvp, float dx, float dy) const;
  float Grad3(int64_t xrvp, int64_t yrvp, int64_t zrvp, float dx, float dy,
              float dz, int64_t seed_override) const;

  int64_t seed_ = 0;
};

}  // namespace util
}  // namespace minicraft

#endif  // MINICRAFT_CORE_UTIL_SIMPLEX_H_
