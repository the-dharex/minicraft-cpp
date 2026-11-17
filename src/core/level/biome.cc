#include "core/level/biome.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/level/level_noise.h"
#include "core/util/random.h"

namespace minicraft::level {

namespace {

// Matches Tile IDs in Tiles registry
constexpr uint16_t kTileGrass = 0;
constexpr uint16_t kTileDirt = 1;
constexpr uint16_t kTileFlower = 2;
constexpr uint16_t kTileStairsDown = 5;
constexpr uint16_t kTileWater = 6;
constexpr uint16_t kTileRock = 7;
constexpr uint16_t kTileTree = 8;
constexpr uint16_t kTileSand = 10;
constexpr uint16_t kTileCactus = 11;
constexpr uint16_t kTileIronOre = 13;
constexpr uint16_t kTileGoldOre = 14;
constexpr uint16_t kTileGemOre = 15;
constexpr uint16_t kTileLapis = 16;
constexpr uint16_t kTileLava = 17;
constexpr uint16_t kTileInfiniteFall = 23;
constexpr uint16_t kTileCloud = 24;
constexpr uint16_t kTileCloudCactus = 25;
constexpr uint16_t kTileObsidian = 31;
constexpr uint16_t kTileObsidianWall = 34;
constexpr uint16_t kTileRawObsidian = 44;

double RiverMetric(const LevelNoise& noise, int32_t x, int32_t y) {
  int32_t l = noise.Layers();
  double a = noise.GetScale128Noise(x, y, l - 1);
  double b = noise.GetScale128Noise(x, y, l - 2);
  return std::abs(a - b);
}

class OceanBiome : public Biome {
 public:
  OceanBiome() : Biome(BiomeId::kOcean, "Ocean", 0.0, -1.0, 0.2, 1.0) {}
  double GetGenerationWeight(double /*temp*/, double height,
                             double /*humidity*/, const LevelNoise& /*noise*/,
                             int32_t /*x*/, int32_t /*y*/) const override {
    return std::pow(-height * 4.0, 5.0);
  }
  GeneratedTile GenerateTile(const LevelNoise& /*noise*/, int32_t /*lx*/,
                             int32_t /*ly*/,
                             util::Random& /*rng*/) const override {
    return {kTileWater, 0};
  }
};

class RiverBiome : public Biome {
 public:
  RiverBiome() : Biome(BiomeId::kRiver, "River", 0.0, -0.1, 0.1, 0.2) {}
  double GetGenerationWeight(double /*temp*/, double /*height*/,
                             double /*humidity*/, const LevelNoise& noise,
                             int32_t x, int32_t y) const override {
    double d = RiverMetric(noise, x, y);
    if (d < 0.0001) return 10.0;
    return std::min(0.5 / d, 10.0);
  }
  GeneratedTile GenerateTile(const LevelNoise& /*noise*/, int32_t /*lx*/,
                             int32_t /*ly*/,
                             util::Random& /*rng*/) const override {
    return {kTileWater, 0};
  }
};

class RiverBankBiome : public Biome {
 public:
  RiverBankBiome()
      : Biome(BiomeId::kRiverBank, "River Bank", 0.0, -0.1, 0.1, 0.3) {}
  double GetGenerationWeight(double /*temp*/, double /*height*/,
                             double /*humidity*/, const LevelNoise& noise,
                             int32_t x, int32_t y) const override {
    double d = RiverMetric(noise, x, y);
    if (d < 0.0001) return 3.0;
    return std::min(0.55 / d, 3.0);
  }
  GeneratedTile GenerateTile(const LevelNoise& /*noise*/, int32_t /*lx*/,
                             int32_t /*ly*/,
                             util::Random& /*rng*/) const override {
    return {kTileSand, 0};
  }
};

class SurfaceBiome : public Biome {
 public:
  SurfaceBiome()
      : Biome(BiomeId::kSurface, "Plains", -0.25, 0.0, 0.3, 1.0) {}
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& rng) const override {
    double val = std::abs(noise.GetScale64Noise(lx, ly, 0) -
                          noise.GetScale64Noise(lx, ly, 1));
    double mval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 0) -
                                    noise.GetScale16Noise(lx, ly, 1)) -
                           noise.GetScale16Noise(lx, ly, 2));
    if (val > 0.75 && mval < 0.35) {
      if (val > 0.85 && mval < 0.25 && rng.NextDouble() < 0.005) {
        return {kTileStairsDown, 0};
      }
      return {kTileRock, 0};
    }
    if (noise.GetScale16Noise(lx, ly, 2) < -0.6 &&
        noise.GetTileNoise(lx, ly, 0) < 0.4) {
      return {kTileTree, 0};
    }
    if (noise.GetScale32Noise(lx, ly, 2) + noise.GetScale8Noise(lx, ly, 0) <
            -0.6 &&
        noise.GetTileNoise(lx, ly, 1) < -0.4) {
      int32_t flower_data =
          static_cast<int32_t>(std::abs(noise.GetScale8Noise(lx, ly, 1) * 24)) %
          12;
      return {kTileFlower, static_cast<int16_t>(flower_data)};
    }
    return {kTileGrass, 0};
  }
};

class ForestBiome : public Biome {
 public:
  ForestBiome() : Biome(BiomeId::kForest, "Forest", -0.1, 0.0, 0.5, 1.0) {}
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& /*rng*/) const override {
    if (noise.GetTileNoise(lx, ly, 0) < 0.6) {
      return {kTileTree, 0};
    }
    return {kTileGrass, 0};
  }
};

class DesertBiome : public Biome {
 public:
  DesertBiome() : Biome(BiomeId::kDesert, "Desert", 0.3, 0.0, -0.6, 1.0) {}
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& rng) const override {
    double val = std::abs(noise.GetScale64Noise(lx, ly, 0) -
                          noise.GetScale64Noise(lx, ly, 1));
    double mval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 0) -
                                    noise.GetScale16Noise(lx, ly, 1)) -
                           noise.GetScale16Noise(lx, ly, 2));
    if (val > 0.5 && mval < 0.5) {
      if (val > 0.65 && mval < 0.35 && rng.NextDouble() < 0.005) {
        return {kTileStairsDown, 0};
      }
      return {kTileRock, 0};
    }
    if (noise.GetScale8Noise(lx, ly, 0) < -0.25 &&
        noise.GetTileNoise(lx, ly, 1) < -0.4) {
      return {kTileCactus, 0};
    }
    return {kTileSand, 0};
  }
};

class RockBiome : public Biome {
 public:
  RockBiome() : Biome(BiomeId::kRock, "Mountains", 0.0, 1.2, 0.0, 0.6) {}
  double GetGenerationWeight(double /*temp*/, double height,
                             double /*humidity*/, const LevelNoise& /*noise*/,
                             int32_t /*x*/, int32_t /*y*/) const override {
    double a = height * 3.0 - 1.0;
    return std::pow(a, 13.0);
  }
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& rng) const override {
    if (noise.GetHeight(lx, ly) > 0.35 && rng.NextDouble() < 0.002) {
      return {kTileStairsDown, 0};
    }
    return {kTileRock, 0};
  }
};

class IronCaveBiome : public Biome {
 public:
  IronCaveBiome()
      : Biome(BiomeId::kIronCave, "Iron Cave", 0.0, 0.0, 0.0, 1.0) {}
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& rng) const override {
    double val = std::abs(noise.GetScale32Noise(lx, ly, 0) -
                          noise.GetScale32Noise(lx, ly, 1));
    double mval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 0) -
                                    noise.GetScale16Noise(lx, ly, 1)) -
                           noise.GetScale16Noise(lx, ly, 2));
    double nval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 3) -
                                    noise.GetScale16Noise(lx, ly, 4)) -
                           noise.GetScale16Noise(lx, ly, 5));
    double wval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 6) -
                                    noise.GetScale16Noise(lx, ly, 7)) -
                           noise.GetScale16Noise(lx, ly, 8));
    if (val > 0.25 && wval < 0.35) return {kTileWater, 0};
    if ((val > 0.125 && mval < 0.20) || nval < 0.25) return {kTileDirt, 0};
    if (noise.GetScale16Noise(lx, ly, 9) > 0.65) return {kTileIronOre, 0};
    if (noise.GetScale32Noise(lx, ly, 3) > 0.8) return {kTileLapis, 0};
    if (rng.NextDouble() < 0.003) return {kTileStairsDown, 0};
    return {kTileRock, 0};
  }
};

class GoldCaveBiome : public Biome {
 public:
  GoldCaveBiome()
      : Biome(BiomeId::kGoldCave, "Gold Cave", 0.0, 0.0, 0.0, 1.0) {}
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& rng) const override {
    double val = std::abs(noise.GetScale32Noise(lx, ly, 0) -
                          noise.GetScale32Noise(lx, ly, 1));
    double mval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 0) -
                                    noise.GetScale16Noise(lx, ly, 1)) -
                           noise.GetScale16Noise(lx, ly, 2));
    double nval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 3) -
                                    noise.GetScale16Noise(lx, ly, 4)) -
                           noise.GetScale16Noise(lx, ly, 5));
    double wval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 6) -
                                    noise.GetScale16Noise(lx, ly, 7)) -
                           noise.GetScale16Noise(lx, ly, 8));
    if (val > 0.25 && wval < 0.45) return {kTileWater, 0};
    if ((val > 0.125 && mval < 0.20) || nval < 0.25) return {kTileDirt, 0};
    if (noise.GetScale16Noise(lx, ly, 9) > 0.675) return {kTileGoldOre, 0};
    if (noise.GetScale32Noise(lx, ly, 3) > 0.825) return {kTileLapis, 0};
    if (rng.NextDouble() < 0.0025) return {kTileStairsDown, 0};
    return {kTileRock, 0};
  }
};

class GemCaveBiome : public Biome {
 public:
  GemCaveBiome()
      : Biome(BiomeId::kGemCave, "Gem Cave", 0.0, 0.0, 0.0, 1.0) {}
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& rng) const override {
    double val = std::abs(noise.GetScale32Noise(lx, ly, 0) -
                          noise.GetScale32Noise(lx, ly, 1));
    double mval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 0) -
                                    noise.GetScale16Noise(lx, ly, 1)) -
                           noise.GetScale16Noise(lx, ly, 2));
    double nval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 3) -
                                    noise.GetScale16Noise(lx, ly, 4)) -
                           noise.GetScale16Noise(lx, ly, 5));
    double wval = std::abs(std::abs(noise.GetScale16Noise(lx, ly, 6) -
                                    noise.GetScale16Noise(lx, ly, 7)) -
                           noise.GetScale16Noise(lx, ly, 8));
    if (val > 0.25 && wval < 0.45) return {kTileLava, 0};
    if ((val > 0.125 && mval < 0.20) || nval < 0.25) return {kTileDirt, 0};
    if (noise.GetScale16Noise(lx, ly, 9) > 0.7) return {kTileGemOre, 0};
    if (noise.GetScale32Noise(lx, ly, 3) > 0.85) return {kTileLapis, 0};
    if (rng.NextDouble() < 0.002) return {kTileStairsDown, 0};
    return {kTileRock, 0};
  }
};

class SkyBiome : public Biome {
 public:
  SkyBiome() : Biome(BiomeId::kSky, "Sky", 0.0, 0.0, 0.0, 1.0) {}
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& rng) const override {
    double val = std::abs(noise.GetScale8Noise(lx, ly, 0) -
                          noise.GetScale8Noise(lx, ly, 1));
    if (val < 0.15) return {kTileInfiniteFall, 0};
    if (noise.GetScale8Noise(lx, ly, 3) < -0.65) return {kTileCloudCactus, 0};
    if (rng.NextDouble() < 0.0005) return {kTileStairsDown, 0};
    return {kTileCloud, 0};
  }
};

class DungeonBiome : public Biome {
 public:
  DungeonBiome()
      : Biome(BiomeId::kDungeon, "Dungeon", 0.0, 0.0, 0.0, 1.0) {}
  GeneratedTile GenerateTile(const LevelNoise& noise, int32_t lx, int32_t ly,
                             util::Random& rng) const override {
    double val = std::abs(noise.GetScale16Noise(lx, ly, 0) -
                          noise.GetScale16Noise(lx, ly, 1));
    bool generate_decor = rng.NextDouble() < 0.001;
    if (val < 0.15) return {kTileObsidianWall, 0, generate_decor};
    if (val < 0.18) return {kTileLava, 0, generate_decor};

    int32_t selection =
        static_cast<int32_t>(std::abs(noise.GetScale16Noise(lx, ly, 2) * 9)) %
        3;
    if (rng.NextDouble() < 0.0001) return {kTileStairsDown, 0, false};
    if (selection == 0) return {kTileObsidian, 0, false};
    if (selection == 1) return {kTileRawObsidian, 0, false};
    return {kTileDirt, 0, false};
  }
};

}  // namespace

std::vector<std::unique_ptr<Biome>> Biomes::registry_;
bool Biomes::initialized_ = false;

Biome::Biome(BiomeId id, std::string name, double temp, double height,
             double humidity, double rarity)
    : id_(id),
      name_(std::move(name)),
      temperature_(temp),
      height_(height),
      humidity_(humidity),
      rarity_(rarity) {}

double Biome::GetGenerationWeight(double temp, double height, double humidity,
                                  const LevelNoise& /*noise*/, int32_t /*x*/,
                                  int32_t /*y*/) const {
  double dx = temp - temperature_;
  double dy = height - height_;
  double dz = humidity - humidity_;
  double dist_sq = dx * dx + dy * dy + dz * dz;
  if (dist_sq < 1e-6) return rarity_ * 1e6;
  return rarity_ / dist_sq;
}

void Biomes::Init() {
  if (initialized_) return;
  registry_.clear();
  registry_.resize(12);
  registry_[static_cast<size_t>(BiomeId::kOcean)] =
      std::make_unique<OceanBiome>();
  registry_[static_cast<size_t>(BiomeId::kRiver)] =
      std::make_unique<RiverBiome>();
  registry_[static_cast<size_t>(BiomeId::kRiverBank)] =
      std::make_unique<RiverBankBiome>();
  registry_[static_cast<size_t>(BiomeId::kSurface)] =
      std::make_unique<SurfaceBiome>();
  registry_[static_cast<size_t>(BiomeId::kForest)] =
      std::make_unique<ForestBiome>();
  registry_[static_cast<size_t>(BiomeId::kDesert)] =
      std::make_unique<DesertBiome>();
  registry_[static_cast<size_t>(BiomeId::kRock)] =
      std::make_unique<RockBiome>();
  registry_[static_cast<size_t>(BiomeId::kIronCave)] =
      std::make_unique<IronCaveBiome>();
  registry_[static_cast<size_t>(BiomeId::kGoldCave)] =
      std::make_unique<GoldCaveBiome>();
  registry_[static_cast<size_t>(BiomeId::kGemCave)] =
      std::make_unique<GemCaveBiome>();
  registry_[static_cast<size_t>(BiomeId::kSky)] = std::make_unique<SkyBiome>();
  registry_[static_cast<size_t>(BiomeId::kDungeon)] =
      std::make_unique<DungeonBiome>();
  initialized_ = true;
}

const Biome& Biomes::Get(BiomeId id) {
  if (!initialized_) Init();
  return *registry_[static_cast<size_t>(id)];
}

const Biome& Biomes::Get(uint8_t id) {
  if (!initialized_) Init();
  if (id >= registry_.size()) return *registry_[static_cast<size_t>(BiomeId::kSurface)];
  return *registry_[id];
}

const Biome& Biomes::PickSurfaceBiome(const LevelNoise& noise, int32_t lx,
                                      int32_t ly) {
  if (!initialized_) Init();
  const double temp = noise.GetTemperature(lx, ly);
  const double height = noise.GetHeight(lx, ly);
  const double humidity = noise.GetHumidity(lx, ly);

  constexpr BiomeId kSurfaceBiomes[] = {
      BiomeId::kOcean,     BiomeId::kRiver,  BiomeId::kRiverBank,
      BiomeId::kSurface,   BiomeId::kForest, BiomeId::kDesert,
      BiomeId::kRock,
  };

  const Biome* best = registry_[static_cast<size_t>(BiomeId::kSurface)].get();
  double best_weight = -std::numeric_limits<double>::infinity();
  for (BiomeId bid : kSurfaceBiomes) {
    const Biome* b = registry_[static_cast<size_t>(bid)].get();
    double w = b->GetGenerationWeight(temp, height, humidity, noise, lx, ly);
    if (w > best_weight) {
      best_weight = w;
      best = b;
    }
  }
  return *best;
}

const Biome& Biomes::GetBiomeForDepth(int32_t depth, const LevelNoise& noise,
                                      int32_t lx, int32_t ly) {
  if (!initialized_) Init();
  switch (depth) {
    case 1:
      return Get(BiomeId::kSky);
    case 0:
      return PickSurfaceBiome(noise, lx, ly);
    case -1:
      return Get(BiomeId::kIronCave);
    case -2:
      return Get(BiomeId::kGoldCave);
    case -3:
      return Get(BiomeId::kGemCave);
    case -4:
    default:
      return Get(BiomeId::kDungeon);
  }
}

}  // namespace minicraft::level
