#ifndef MINICRAFT_CORE_UTIL_RANDOM_H_
#define MINICRAFT_CORE_UTIL_RANDOM_H_

#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>

namespace minicraft {
namespace util {

// Deterministic Java-compatible 48-bit LCG PRNG.
class Random {
 public:
  explicit Random(int64_t seed = 0x123456789ABCDEFLL) { SetSeed(seed); }

  void SetSeed(int64_t seed) {
    seed_ = (static_cast<uint64_t>(seed) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1);
    have_next_gaussian_ = false;
  }

  int32_t Next(int bits) {
    seed_ = (seed_ * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
    return static_cast<int32_t>(seed_ >> (48 - bits));
  }

  int32_t NextInt() { return Next(32); }

  int32_t NextInt(int32_t bound) {
    if (bound <= 0) return 0;
    if ((bound & -bound) == bound) {
      return static_cast<int32_t>((static_cast<int64_t>(bound) * Next(31)) >>
                                  31);
    }
    int32_t bits, val;
    do {
      bits = Next(31);
      val = bits % bound;
    } while (bits - val + (bound - 1) < 0);
    return val;
  }

  int64_t NextLong() {
    return (static_cast<int64_t>(Next(32)) << 32) + Next(32);
  }

  bool NextBoolean() { return Next(1) != 0; }

  float NextFloat() {
    return static_cast<float>(Next(24)) / static_cast<float>(1 << 24);
  }

  double NextDouble() {
    int64_t a = (static_cast<int64_t>(Next(26)) << 27) + Next(27);
    return static_cast<double>(a) / static_cast<double>(1LL << 53);
  }

  double NextGaussian() {
    if (have_next_gaussian_) {
      have_next_gaussian_ = false;
      return next_gaussian_;
    }
    double v1, v2, s;
    do {
      v1 = 2.0 * NextDouble() - 1.0;
      v2 = 2.0 * NextDouble() - 1.0;
      s = v1 * v1 + v2 * v2;
    } while (s >= 1.0 || s == 0.0);
    double multiplier = std::sqrt(-2.0 * std::log(s) / s);
    next_gaussian_ = v2 * multiplier;
    have_next_gaussian_ = true;
    return v1 * multiplier;
  }

 private:
  uint64_t seed_ = 0;
  double next_gaussian_ = 0.0;
  bool have_next_gaussian_ = false;
};

// Shared global gameplay PRNG helper seeded once from entropy + clock.
inline Random& GlobalRandom() {
  static Random rng([]() {
    std::random_device rd;
    const uint64_t r0 = static_cast<uint64_t>(rd());
    const uint64_t r1 = static_cast<uint64_t>(rd());
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    uint64_t x = (r0 << 32) ^ r1 ^ now ^ reinterpret_cast<uintptr_t>(&rd);
    // SplitMix64 avalanche so closely-spaced launches produce distinct seeds.
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x ^= (x >> 31);
    return static_cast<int64_t>(x);
  }());
  return rng;
}

}  // namespace util
}  // namespace minicraft

#endif  // MINICRAFT_CORE_UTIL_RANDOM_H_
