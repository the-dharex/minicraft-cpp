#ifndef MINICRAFT_CORE_LEVEL_BIOME_H_
#define MINICRAFT_CORE_LEVEL_BIOME_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minicraft::util {
class Random;
}

namespace minicraft::level {

class LevelNoise;

enum class BiomeId : uint8_t {
  kOcean = 0,
  kRiver = 1,
  kRiverBank = 2,
  kSurface = 3,
  kForest = 4,
  kDesert = 5,
  kRock = 6,
  kIronCave = 7,
  kGoldCave = 8,
  kGemCave = 9,
  kSky = 10,
  kDungeon = 11,
};

struct GeneratedTile {
  uint16_t tile_id = 0;
  int16_t data = 0;
  bool generate_ornate_lava_pool = false;
};

class Biome {
 public:
  Biome(BiomeId id, std::string name, double temp, double height,
        double humidity, double rarity);
  virtual ~Biome() = default;

  BiomeId Id() const { return id_; }
  const std::string& Name() const { return name_; }

  virtual double GetGenerationWeight(double temp, double height,
                                     double humidity, const LevelNoise& noise,
                                     int32_t x, int32_t y) const;

  virtual GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx,
                                     int32_t ly, util::Random& rng) const = 0;

 protected:
  BiomeId id_;
  std::string name_;
  double temperature_;
  double height_;
  double humidity_;
  double rarity_;
};

class Biomes {
 public:
  static void Init();
  static const Biome& Get(BiomeId id);
  static const Biome& Get(uint8_t id);
  static const Biome& PickSurfaceBiome(const LevelNoise& noise, int32_t lx,
                                       int32_t ly);
  static const Biome& GetBiomeForDepth(int32_t depth, const LevelNoise& noise,
                                       int32_t lx, int32_t ly);

 private:
  static std::vector<std::unique_ptr<Biome>> registry_;
  static bool initialized_;
};

}  // namespace minicraft::level

#endif  // MINICRAFT_CORE_LEVEL_BIOME_H_
