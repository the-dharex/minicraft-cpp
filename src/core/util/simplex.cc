#include "core/util/simplex.h"

#include <array>

namespace minicraft {
namespace util {

namespace {

constexpr int64_t kPrimeX = 0x5205402B9270C86FLL;
constexpr int64_t kPrimeY = 0x598CD327003817B5LL;
constexpr int64_t kPrimeZ = 0x5BCC226E9FA0BACBLL;
constexpr int64_t kHashMultiplier = 0x53A3F72DEEC546F5LL;
constexpr int64_t kSeedFlip3D = -0x52D547B2E96ED629LL;

constexpr double kRoot2Over2 = 0.7071067811865476;
constexpr double kSkew2D = 0.366025403784439;
constexpr double kUnskew2D = -0.21132486540518713;

constexpr double kRoot3Over3 = 0.577350269189626;
constexpr double kRotate3DOrthogonalizer = kUnskew2D;

constexpr int kNGrads2DExponent = 7;
constexpr int kNGrads3DExponent = 8;
constexpr int kNGrads2D = 1 << kNGrads2DExponent;
constexpr int kNGrads3D = 1 << kNGrads3DExponent;

constexpr double kNormalizer2D = 0.01001634121365712;
constexpr double kNormalizer3D = 0.07969837668935331;

constexpr float kRSquared2D = 0.5f;
constexpr float kRSquared3D = 0.6f;

inline int FastFloor(double x) {
  int xi = static_cast<int>(x);
  return x < xi ? xi - 1 : xi;
}

inline int FastRound(double x) {
  return x < 0 ? static_cast<int>(x - 0.5) : static_cast<int>(x + 0.5);
}

const std::array<float, kNGrads2D * 2>& GetGradients2D() {
  static const auto kTable = []() {
    std::array<float, kNGrads2D * 2> out{};
    const float base[] = {
        0.38268343236509f,   0.923879532511287f,
        0.923879532511287f,  0.38268343236509f,
        0.923879532511287f, -0.38268343236509f,
        0.38268343236509f,  -0.923879532511287f,
       -0.38268343236509f,  -0.923879532511287f,
       -0.923879532511287f, -0.38268343236509f,
       -0.923879532511287f,  0.38268343236509f,
       -0.38268343236509f,   0.923879532511287f,
        0.130526192220052f,  0.99144486137381f,
        0.608761429008721f,  0.793353340291235f,
        0.793353340291235f,  0.608761429008721f,
        0.99144486137381f,   0.130526192220051f,
        0.99144486137381f,  -0.130526192220051f,
        0.793353340291235f, -0.60876142900872f,
        0.608761429008721f, -0.793353340291235f,
        0.130526192220052f, -0.99144486137381f,
       -0.130526192220052f, -0.99144486137381f,
       -0.608761429008721f, -0.793353340291235f,
       -0.793353340291235f, -0.608761429008721f,
       -0.99144486137381f,  -0.130526192220052f,
       -0.99144486137381f,   0.130526192220051f,
       -0.793353340291235f,  0.608761429008721f,
       -0.608761429008721f,  0.793353340291235f,
       -0.130526192220052f,  0.99144486137381f,
    };
    constexpr size_t kBaseCount = sizeof(base) / sizeof(base[0]);
    for (size_t i = 0, j = 0; i < out.size(); ++i, ++j) {
      if (j == kBaseCount) j = 0;
      out[i] = static_cast<float>(base[j] / kNormalizer2D);
    }
    return out;
  }();
  return kTable;
}

const std::array<float, kNGrads3D * 4>& GetGradients3D() {
  static const auto kTable = []() {
    std::array<float, kNGrads3D * 4> out{};
    const float base[] = {
        2.22474487139f,       2.22474487139f,      -1.0f,                 0.0f,
        2.22474487139f,       2.22474487139f,       1.0f,                 0.0f,
        3.0862664687972017f,  1.1721513422464978f,  0.0f,                 0.0f,
        1.1721513422464978f,  3.0862664687972017f,  0.0f,                 0.0f,
       -2.22474487139f,       2.22474487139f,      -1.0f,                 0.0f,
       -2.22474487139f,       2.22474487139f,       1.0f,                 0.0f,
       -1.1721513422464978f,  3.0862664687972017f,  0.0f,                 0.0f,
       -3.0862664687972017f,  1.1721513422464978f,  0.0f,                 0.0f,
       -1.0f,                -2.22474487139f,      -2.22474487139f,       0.0f,
        1.0f,                -2.22474487139f,      -2.22474487139f,       0.0f,
        0.0f,                -3.0862664687972017f, -1.1721513422464978f,  0.0f,
        0.0f,                -1.1721513422464978f, -3.0862664687972017f,  0.0f,
       -1.0f,                -2.22474487139f,       2.22474487139f,       0.0f,
        1.0f,                -2.22474487139f,       2.22474487139f,       0.0f,
        0.0f,                -1.1721513422464978f,  3.0862664687972017f,  0.0f,
        0.0f,                -3.0862664687972017f,  1.1721513422464978f,  0.0f,
       -2.22474487139f,      -2.22474487139f,      -1.0f,                 0.0f,
       -2.22474487139f,      -2.22474487139f,       1.0f,                 0.0f,
       -3.0862664687972017f, -1.1721513422464978f,  0.0f,                 0.0f,
       -1.1721513422464978f, -3.0862664687972017f,  0.0f,                 0.0f,
       -2.22474487139f,      -1.0f,                -2.22474487139f,       0.0f,
       -2.22474487139f,       1.0f,                -2.22474487139f,       0.0f,
       -1.1721513422464978f,  0.0f,                -3.0862664687972017f,  0.0f,
       -3.0862664687972017f,  0.0f,                -1.1721513422464978f,  0.0f,
       -2.22474487139f,      -1.0f,                 2.22474487139f,       0.0f,
       -2.22474487139f,       1.0f,                 2.22474487139f,       0.0f,
       -3.0862664687972017f,  0.0f,                 1.1721513422464978f,  0.0f,
       -1.1721513422464978f,  0.0f,                 3.0862664687972017f,  0.0f,
       -1.0f,                 2.22474487139f,      -2.22474487139f,       0.0f,
        1.0f,                 2.22474487139f,      -2.22474487139f,       0.0f,
        0.0f,                 1.1721513422464978f, -3.0862664687972017f,  0.0f,
        0.0f,                 3.0862664687972017f, -1.1721513422464978f,  0.0f,
       -1.0f,                 2.22474487139f,       2.22474487139f,       0.0f,
        1.0f,                 2.22474487139f,       2.22474487139f,       0.0f,
        0.0f,                 3.0862664687972017f,  1.1721513422464978f,  0.0f,
        0.0f,                 1.1721513422464978f,  3.0862664687972017f,  0.0f,
        2.22474487139f,      -2.22474487139f,      -1.0f,                 0.0f,
        2.22474487139f,      -2.22474487139f,       1.0f,                 0.0f,
        1.1721513422464978f, -3.0862664687972017f,  0.0f,                 0.0f,
        3.0862664687972017f, -1.1721513422464978f,  0.0f,                 0.0f,
        2.22474487139f,      -1.0f,                -2.22474487139f,       0.0f,
        2.22474487139f,       1.0f,                -2.22474487139f,       0.0f,
        3.0862664687972017f,  0.0f,                -1.1721513422464978f,  0.0f,
        1.1721513422464978f,  0.0f,                -3.0862664687972017f,  0.0f,
        2.22474487139f,      -1.0f,                 2.22474487139f,       0.0f,
        2.22474487139f,       1.0f,                 2.22474487139f,       0.0f,
        1.1721513422464978f,  0.0f,                 3.0862664687972017f,  0.0f,
        3.0862664687972017f,  0.0f,                 1.1721513422464978f,  0.0f,
    };
    constexpr size_t kBaseCount = sizeof(base) / sizeof(base[0]);
    for (size_t i = 0, j = 0; i < out.size(); ++i, ++j) {
      if (j == kBaseCount) j = 0;
      out[i] = static_cast<float>(base[j] / kNormalizer3D);
    }
    return out;
  }();
  return kTable;
}

}  // namespace

float Simplex::Noise2(double x, double y) const {
  double s = kSkew2D * (x + y);
  return Noise2UnskewedBase(x + s, y + s);
}

float Simplex::Noise2ImproveX(double x, double y) const {
  double xx = x * kRoot2Over2;
  double yy = y * (kRoot2Over2 * (1.0 + 2.0 * kSkew2D));
  return Noise2UnskewedBase(yy + xx, yy - xx);
}

float Simplex::Noise2UnskewedBase(double xs, double ys) const {
  int xsb = FastFloor(xs);
  int ysb = FastFloor(ys);
  float xi = static_cast<float>(xs - xsb);
  float yi = static_cast<float>(ys - ysb);

  int64_t xsbp = static_cast<int64_t>(xsb) * kPrimeX;
  int64_t ysbp = static_cast<int64_t>(ysb) * kPrimeY;

  float t = (xi + yi) * static_cast<float>(kUnskew2D);
  float dx0 = xi + t;
  float dy0 = yi + t;

  float value = 0.0f;
  float a0 = kRSquared2D - dx0 * dx0 - dy0 * dy0;
  if (a0 > 0.0f) {
    value = (a0 * a0) * (a0 * a0) * Grad2(xsbp, ysbp, dx0, dy0);
  }

  float a1 = static_cast<float>(2.0 * (1.0 + 2.0 * kUnskew2D) *
                                (1.0 / kUnskew2D + 2.0)) *
                 t +
             (static_cast<float>(-2.0 * (1.0 + 2.0 * kUnskew2D) *
                                 (1.0 + 2.0 * kUnskew2D)) +
              a0);
  if (a1 > 0.0f) {
    float dx1 = dx0 - static_cast<float>(1.0 + 2.0 * kUnskew2D);
    float dy1 = dy0 - static_cast<float>(1.0 + 2.0 * kUnskew2D);
    value +=
        (a1 * a1) * (a1 * a1) * Grad2(xsbp + kPrimeX, ysbp + kPrimeY, dx1, dy1);
  }

  if (dy0 > dx0) {
    float dx2 = dx0 - static_cast<float>(kUnskew2D);
    float dy2 = dy0 - static_cast<float>(kUnskew2D + 1.0);
    float a2 = kRSquared2D - dx2 * dx2 - dy2 * dy2;
    if (a2 > 0.0f) {
      value += (a2 * a2) * (a2 * a2) * Grad2(xsbp, ysbp + kPrimeY, dx2, dy2);
    }
  } else {
    float dx2 = dx0 - static_cast<float>(kUnskew2D + 1.0);
    float dy2 = dy0 - static_cast<float>(kUnskew2D);
    float a2 = kRSquared2D - dx2 * dx2 - dy2 * dy2;
    if (a2 > 0.0f) {
      value += (a2 * a2) * (a2 * a2) * Grad2(xsbp + kPrimeX, ysbp, dx2, dy2);
    }
  }
  return value;
}

float Simplex::Noise3ImproveXY(double x, double y, double z) const {
  double xy = x + y;
  double s2 = xy * kRotate3DOrthogonalizer;
  double zz = z * kRoot3Over3;
  double xr = x + s2 + zz;
  double yr = y + s2 + zz;
  double zr = xy * -kRoot3Over3 + zz;
  return Noise3UnrotatedBase(xr, yr, zr);
}

float Simplex::Noise3ImproveXZ(double x, double y, double z) const {
  double xz = x + z;
  double s2 = xz * kRotate3DOrthogonalizer;
  double yy = y * kRoot3Over3;
  double xr = x + s2 + yy;
  double zr = z + s2 + yy;
  double yr = xz * -kRoot3Over3 + yy;
  return Noise3UnrotatedBase(xr, yr, zr);
}

float Simplex::Noise3UnrotatedBase(double xr, double yr, double zr) const {
  int xrb = FastRound(xr);
  int yrb = FastRound(yr);
  int zrb = FastRound(zr);
  float xri = static_cast<float>(xr - xrb);
  float yri = static_cast<float>(yr - yrb);
  float zri = static_cast<float>(zr - zrb);

  int x_n_sign = static_cast<int>(-1.0f - xri) | 1;
  int y_n_sign = static_cast<int>(-1.0f - yri) | 1;
  int z_n_sign = static_cast<int>(-1.0f - zri) | 1;

  float ax0 = x_n_sign * -xri;
  float ay0 = y_n_sign * -yri;
  float az0 = z_n_sign * -zri;

  int64_t xrbp = static_cast<int64_t>(xrb) * kPrimeX;
  int64_t yrbp = static_cast<int64_t>(yrb) * kPrimeY;
  int64_t zrbp = static_cast<int64_t>(zrb) * kPrimeZ;

  float value = 0.0f;
  float a = (kRSquared3D - xri * xri) - (yri * yri + zri * zri);
  int64_t cur_seed = seed_;

  for (int l = 0;; ++l) {
    if (a > 0.0f) {
      value += (a * a) * (a * a) *
               Grad3(xrbp, yrbp, zrbp, xri, yri, zri, cur_seed);
    }

    if (ax0 >= ay0 && ax0 >= az0) {
      float b = a + ax0 + ax0;
      if (b > 1.0f) {
        b -= 1.0f;
        value += (b * b) * (b * b) *
                 Grad3(xrbp - x_n_sign * kPrimeX, yrbp, zrbp, xri + x_n_sign,
                       yri, zri, cur_seed);
      }
    } else if (ay0 > ax0 && ay0 >= az0) {
      float b = a + ay0 + ay0;
      if (b > 1.0f) {
        b -= 1.0f;
        value += (b * b) * (b * b) *
                 Grad3(xrbp, yrbp - y_n_sign * kPrimeY, zrbp, xri,
                       yri + y_n_sign, zri, cur_seed);
      }
    } else {
      float b = a + az0 + az0;
      if (b > 1.0f) {
        b -= 1.0f;
        value += (b * b) * (b * b) *
                 Grad3(xrbp, yrbp, zrbp - z_n_sign * kPrimeZ, xri, yri,
                       zri + z_n_sign, cur_seed);
      }
    }

    if (l == 1) break;

    ax0 = 0.5f - ax0;
    ay0 = 0.5f - ay0;
    az0 = 0.5f - az0;

    xri = x_n_sign * ax0;
    yri = y_n_sign * ay0;
    zri = z_n_sign * az0;

    a += (0.75f - ax0) - (ay0 + az0);

    xrbp += (x_n_sign >> 1) & kPrimeX;
    yrbp += (y_n_sign >> 1) & kPrimeY;
    zrbp += (z_n_sign >> 1) & kPrimeZ;

    x_n_sign = -x_n_sign;
    y_n_sign = -y_n_sign;
    z_n_sign = -z_n_sign;

    cur_seed ^= kSeedFlip3D;
  }

  return value;
}

float Simplex::Grad2(int64_t xsvp, int64_t ysvp, float dx, float dy) const {
  int64_t hash = seed_ ^ xsvp ^ ysvp;
  hash = static_cast<int64_t>(static_cast<uint64_t>(hash) *
                              static_cast<uint64_t>(kHashMultiplier));
  hash ^= hash >> (64 - kNGrads2DExponent + 1);
  int gi = static_cast<int>(hash) & ((kNGrads2D - 1) << 1);
  const auto& g2 = GetGradients2D();
  return g2[gi | 0] * dx + g2[gi | 1] * dy;
}

float Simplex::Grad3(int64_t xrvp, int64_t yrvp, int64_t zrvp, float dx,
                     float dy, float dz, int64_t seed_override) const {
  int64_t hash = (seed_override ^ xrvp) ^ (yrvp ^ zrvp);
  hash = static_cast<int64_t>(static_cast<uint64_t>(hash) *
                              static_cast<uint64_t>(kHashMultiplier));
  hash ^= hash >> (64 - kNGrads3DExponent + 2);
  int gi = static_cast<int>(hash) & ((kNGrads3D - 1) << 2);
  const auto& g3 = GetGradients3D();
  return g3[gi | 0] * dx + g3[gi | 1] * dy + g3[gi | 2] * dz;
}

}  // namespace util
}  // namespace minicraft
