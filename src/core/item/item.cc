#include "core/item/item.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "core/entity/furniture.h"
#include "core/entity/mob.h"
#include "core/entity/particles.h"
#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/gfx/color.h"
#include "core/gfx/font.h"
#include "core/gfx/screen.h"
#include "core/io/sound.h"
#include "core/item/items.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/random.h"

namespace minicraft::item {

namespace {

constexpr std::array<int32_t, 16> kDyeRgb = {
    0x1D1D21, 0xB02E26, 0x5E7C16, 0x835432, 0x3C44AA, 0x8932B8,
    0x169C9C, 0x9D9D97, 0x474F52, 0xF38BAA, 0x80C71F, 0xFED83D,
    0x3AB3DA, 0xC74EBD, 0xF9801D, 0xF9FFFE};

constexpr std::array<const char*, 16> kDyeNames = {
    "Black",     "Red",   "Green", "Brown",  "Blue",       "Purple",
    "Cyan",      "Light Gray", "Gray", "Pink", "Lime",       "Yellow",
    "Light Blue","Magenta", "Orange", "White"};

constexpr std::array<const char*, 16> kDyePrefixes = {
    "black",      "red",   "green", "brown", "blue",       "purple",
    "cyan",       "light_gray", "gray", "pink", "lime",   "yellow",
    "light_blue", "magenta", "orange", "white"};

constexpr std::array<ToolSpec, 8> kToolSpecs = {{
    {0, 13, 34, false, "Shovel"},
    {1, 13, 30, false, "Hoe"},
    {2, 13, 52, false, "Sword"},
    {3, 13, 38, false, "Pickaxe"},
    {4, 13, 34, false, "Axe"},
    {5, 13, 30, false, "Bow"},
    {6, 13, 44, false, "Claymore"},
    {0, 12, 42, true, "Shears"},
}};

constexpr std::array<PotionSpec, 12> kPotionSpecs = {{
    {gfx::color::Get(1, 41, 51, 255), 0, "Awkward"},
    {gfx::color::Get(1, 105, 209, 105), 4200, "Speed"},
    {gfx::color::Get(1, 183, 183, 91), 6000, "Light"},
    {gfx::color::Get(1, 51, 51, 255), 4800, "Swim"},
    {gfx::color::Get(1, 237, 110, 78), 8400, "Energy"},
    {gfx::color::Get(1, 219, 70, 189), 1800, "Regen"},
    {gfx::color::Get(1, 194, 56, 84), 0, "Health"},
    {gfx::color::Get(1, 163), 1800, "Time"},
    {gfx::color::Get(1, 199, 58, 58), 7200, "Lava"},
    {gfx::color::Get(1, 84, 84, 204), 5400, "Shield"},
    {gfx::color::Get(1, 201, 71, 201), 4800, "Haste"},
    {gfx::color::Get(1, 222, 162, 162), 0, "Escape"},
}};

std::string ToLowerSnake(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c == ' ') {
      out.push_back('_');
    } else {
      out.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
  }
  return out;
}

std::string ToUpperAscii(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(
        static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

}  // namespace

int32_t DyeColorRgb(DyeColor color) {
  size_t i = static_cast<size_t>(color);
  return i < kDyeRgb.size() ? kDyeRgb[i] : 0xFFFFFF;
}

const char* DyeColorDisplayName(DyeColor color) {
  size_t i = static_cast<size_t>(color);
  return i < kDyeNames.size() ? kDyeNames[i] : "White";
}

const char* DyeColorSpritePrefix(DyeColor color) {
  size_t i = static_cast<size_t>(color);
  return i < kDyePrefixes.size() ? kDyePrefixes[i] : "white";
}

const ToolSpec& GetToolSpec(ToolType type) {
  size_t i = static_cast<size_t>(type);
  return i < kToolSpecs.size() ? kToolSpecs[i] : kToolSpecs[0];
}

const PotionSpec& GetPotionSpec(PotionType type) {
  size_t i = static_cast<size_t>(type);
  return i < kPotionSpecs.size() ? kPotionSpecs[i] : kPotionSpecs[0];
}

bool ApplyPotionEffect(entity::Player* player, PotionType type, bool add) {
  if (!player) return false;
  if (!add) {
    if (type != PotionType::kAwkward && type != PotionType::kHealth &&
        type != PotionType::kEscape) {
      player->potion_effects.erase(static_cast<int32_t>(type));
    }
    return true;
  }
  return ApplyPotionEffectWithTime(player, type, GetPotionSpec(type).duration);
}

bool ApplyPotionEffectWithTime(entity::Player* player, PotionType type,
                               int32_t time_ticks) {
  if (!player) return false;
  switch (type) {
    case PotionType::kAwkward:
      return false;
    case PotionType::kHealth:
      player->health = std::min(player->max_health, player->health + 5);
      return true;
    case PotionType::kEscape: {
      level::Level* cur = player->GetLevel();
      if (!cur || cur->depth == 0) {
        game::GameState::Instance().AddNotification(
            "You can't escape from here!");
        return false;
      }
      const int32_t depth_diff = cur->depth > 0 ? -1 : 1;
      game::GameState::Instance().ScheduleLevelChange(depth_diff, [player]() {
        if (!player) return;
        level::Level* plevel = player->GetLevel();
        if (plevel != nullptr &&
            !plevel->GetTile(player->x >> 4, player->y >> 4)
                 ->MayPass(*plevel, player->x >> 4, player->y >> 4, *player)) {
          player->FindStartPos(*plevel, 5000);
        }
      });
      return true;
    }
    case PotionType::kSpeed:
      player->AddPotionEffect(entity::Player::PotionEffect::kSpeed, time_ticks);
      return true;
    case PotionType::kLight:
      player->AddPotionEffect(entity::Player::PotionEffect::kLight, time_ticks);
      return true;
    case PotionType::kSwim:
      player->AddPotionEffect(entity::Player::PotionEffect::kSwim, time_ticks);
      return true;
    case PotionType::kEnergy:
      player->AddPotionEffect(entity::Player::PotionEffect::kEnergy,
                              time_ticks);
      return true;
    case PotionType::kRegen:
      player->AddPotionEffect(entity::Player::PotionEffect::kRegen, time_ticks);
      return true;
    case PotionType::kTime:
      player->AddPotionEffect(entity::Player::PotionEffect::kTime, time_ticks);
      return true;
    case PotionType::kLava:
      player->AddPotionEffect(entity::Player::PotionEffect::kLava, time_ticks);
      return true;
    case PotionType::kShield:
      player->AddPotionEffect(entity::Player::PotionEffect::kShield,
                              time_ticks);
      return true;
    case PotionType::kHaste:
      player->AddPotionEffect(entity::Player::PotionEffect::kHaste, time_ticks);
      return true;
    default:
      return false;
  }
}

// --- Item ---
Item::Item(std::string name)
    : sprite(gfx::SpriteType::kItem, ToLowerSnake(name)),
      name_(std::move(name)) {}

Item::Item(std::string name, gfx::LinkedSprite spr)
    : sprite(std::move(spr)), name_(std::move(name)) {}

void Item::RenderHud(gfx::Screen* screen, int32_t x, int32_t y,
                     int32_t font_color) const {
  if (!screen) return;
  gfx::LinkedSprite copy = sprite;
  screen->Render(x, y, copy);
  gfx::Font::Draw(GetDisplayName(), screen, x + 10, y, font_color);
}

void PowerGloveItem::RenderHud(gfx::Screen* screen, int32_t x, int32_t y,
                               int32_t font_color) const {
  if (!screen) return;
  gfx::Font::Draw(GetDisplayName(), screen, x + 10, y, font_color);
}

bool Item::InteractOn(level::Tile* /*tile*/, level::Level* /*level*/,
                      int32_t /*xt*/, int32_t /*yt*/, entity::Player* /*player*/,
                      entity::Direction /*attack_dir*/) {
  return false;
}

bool Item::Equals(const Item& other) const { return name_ == other.name_; }

std::string Item::GetDisplayName() const {
  return " " + util::Localization::GetLocalized(name_);
}

util::ItemSnapshot Item::ToSnapshot() const {
  util::ItemSnapshot snap;
  snap.name = name_;
  snap.count = 1;
  snap.is_stackable = IsStackable();
  return snap;
}

// --- StackableItem ---
StackableItem::StackableItem(std::string name, gfx::LinkedSprite sprite,
                             int32_t count)
    : Item(std::move(name), std::move(sprite)), count(count) {}

bool StackableItem::StacksWith(const Item& other) const {
  auto* st = dynamic_cast<const StackableItem*>(&other);
  return st != nullptr && name_ == st->name_;
}

std::string StackableItem::GetData() const {
  return name_ + "_" + std::to_string(count);
}

std::string StackableItem::GetDisplayName() const {
  std::string c = std::to_string(std::min(count, 999));
  return " " + c + " " + util::Localization::GetLocalized(name_);
}

util::ItemSnapshot StackableItem::ToSnapshot() const {
  util::ItemSnapshot snap;
  snap.name = name_;
  snap.count = count;
  snap.is_stackable = true;
  return snap;
}

bool StackableItem::ConsumeOnSuccess(bool success) {
  if (success && !game::GameState::Instance().IsCreativeMode()) {
    --count;
  }
  return success;
}

// --- FoodItem ---
FoodItem::FoodItem(std::string name, gfx::LinkedSprite sprite, int32_t feed,
                   int32_t count)
    : StackableItem(std::move(name), std::move(sprite), count), feed_(feed) {}

bool FoodItem::InteractOn(level::Tile* /*tile*/, level::Level* /*level*/,
                          int32_t /*xt*/, int32_t /*yt*/,
                          entity::Player* player,
                          entity::Direction /*attack_dir*/) {
  if (!player) return false;
  if (player->hunger < player->max_hunger && player->PayStamina(2)) {
    player->hunger = std::min(player->max_hunger, player->hunger + feed_);
    io::Sound::Play("pickup");
    return ConsumeOnSuccess(true);
  }
  return false;
}

// --- ArmorItem ---
ArmorItem::ArmorItem(std::string name, gfx::LinkedSprite sprite,
                     float armor_ratio, int32_t level, int32_t count)
    : StackableItem(std::move(name), std::move(sprite), count),
      level(level),
      armor_(armor_ratio) {}

bool ArmorItem::InteractOn(level::Tile* /*tile*/, level::Level* /*level*/,
                           int32_t /*xt*/, int32_t /*yt*/,
                           entity::Player* player,
                           entity::Direction /*attack_dir*/) {
  if (!player || player->cur_armor != nullptr || !player->PayStamina(9)) {
    return false;
  }
  player->cur_armor = std::make_shared<ArmorItem>(*this);
  player->cur_armor->count = 1;
  player->armor = static_cast<int32_t>(armor_ * entity::Player::kMaxArmor);
  io::Sound::Play("craft");
  return ConsumeOnSuccess(true);
}

// --- ClothingItem ---
ClothingItem::ClothingItem(std::string name, gfx::LinkedSprite sprite,
                           int32_t player_col, int32_t count)
    : StackableItem(std::move(name), std::move(sprite), count),
      player_col_(player_col) {}

bool ClothingItem::InteractOn(level::Tile* /*tile*/, level::Level* /*level*/,
                              int32_t /*xt*/, int32_t /*yt*/,
                              entity::Player* player,
                              entity::Direction /*attack_dir*/) {
  if (!player || player->shirt_color == player_col_) return false;
  if (!game::GameState::Instance().IsCreativeMode()) {
    std::unique_ptr<Item> last_clothing;
    for (const auto& tmpl : Items::GetAllTemplates()) {
      if (auto* c = dynamic_cast<const ClothingItem*>(tmpl.get())) {
        if (c->PlayerColor() == player->shirt_color) {
          last_clothing = c->Clone();
          break;
        }
      }
    }
    if (!last_clothing) {
      last_clothing = Items::Get("Reg Clothes", true);
    }
    if (auto* st = dynamic_cast<StackableItem*>(last_clothing.get())) {
      st->count = 1;
    }
    player->TryAddToInvOrDrop(std::move(last_clothing));
  }
  player->shirt_color = player_col_;
  io::Sound::Play("craft");
  return ConsumeOnSuccess(true);
}

// --- PotionItem ---
PotionItem::PotionItem(PotionType type, int32_t count)
    : StackableItem(std::string(GetPotionSpec(type).name) + " Potion",
                    gfx::LinkedSprite(gfx::SpriteType::kItem, "potion"), count),
      type(type) {
  sprite.SetColor(GetPotionSpec(type).disp_color);
}

bool PotionItem::InteractOn(level::Tile* /*tile*/, level::Level* /*level*/,
                            int32_t /*xt*/, int32_t /*yt*/,
                            entity::Player* player,
                            entity::Direction /*attack_dir*/) {
  if (!player) return false;
  if (type == PotionType::kLava) {
    util::AchievementManager::Instance().SetAchievement(
        "minicraft.achievement.lava", true,
        game::GameState::Instance().IsCreativeMode());
  }
  const bool success = ApplyPotionEffect(player, type, true);
  if (success) {
    if (!game::GameState::Instance().IsCreativeMode()) {
      player->TryAddToInvOrDrop(Items::Get("Glass Bottle"));
    }
    io::Sound::Play("pickup");
  }
  return ConsumeOnSuccess(success);
}

bool PotionItem::Equals(const Item& other) const {
  auto* p = dynamic_cast<const PotionItem*>(&other);
  return p != nullptr && p->type == type;
}

// --- HeartItem ---
HeartItem::HeartItem(std::string name, gfx::LinkedSprite sprite,
                     int32_t health_add, int32_t count)
    : StackableItem(std::move(name), std::move(sprite), count),
      health_(health_add) {}

bool HeartItem::InteractOn(level::Tile* /*tile*/, level::Level* /*level*/,
                           int32_t /*xt*/, int32_t /*yt*/,
                           entity::Player* player,
                           entity::Direction /*attack_dir*/) {
  if (!player) return false;
  if (player->max_health < entity::Player::kMaxTotalHealth) {
    const int32_t inc =
        std::min(health_, entity::Player::kMaxTotalHealth - player->max_health);
    player->max_health += inc;
    player->health += inc;
    io::Sound::Play("pickup");
    return ConsumeOnSuccess(true);
  }
  game::GameState::Instance().AddNotification("Health increase is at max!");
  return false;
}

// --- DyeItem ---
DyeItem::DyeItem(std::string name, gfx::LinkedSprite sprite, DyeColor col,
                 int32_t count)
    : StackableItem(std::move(name), std::move(sprite), count), color(col) {}

// --- BookItem ---
BookItem::BookItem(std::string title, gfx::LinkedSprite sprite,
                   ContentProvider book, bool has_title_page)
    : Item(std::move(title), std::move(sprite)),
      book_(std::move(book)),
      has_title_page_(has_title_page) {}

bool BookItem::InteractOn(level::Tile* /*tile*/, level::Level* /*level*/,
                          int32_t /*xt*/, int32_t /*yt*/,
                          entity::Player* /*player*/,
                          entity::Direction /*attack_dir*/) {
  std::string text = book_ ? book_() : "";
  game::GameState::Instance().OpenBook(name_, text, has_title_page_);
  return true;
}

// --- BucketItem ---
BucketItem::BucketItem(BucketFill fill, int32_t count)
    : StackableItem(fill == BucketFill::kWater
                        ? "Water Bucket"
                        : (fill == BucketFill::kLava ? "Lava Bucket"
                                                     : "Empty Bucket"),
                    gfx::LinkedSprite(
                        gfx::SpriteType::kItem,
                        fill == BucketFill::kWater
                            ? "water_bucket"
                            : (fill == BucketFill::kLava ? "lava_bucket"
                                                         : "bucket")),
                    count),
      filling(fill) {}

bool BucketItem::Equals(const Item& other) const {
  auto* b = dynamic_cast<const BucketItem*>(&other);
  return b != nullptr && b->filling == filling;
}

std::unique_ptr<BucketItem> BucketItem::EditBucket(entity::Player* player,
                                                   BucketFill new_fill) {
  if (count <= 1) {
    return std::make_unique<BucketItem>(new_fill, 1);
  }
  --count;
  if (player) {
    auto leftover =
        player->GetInventory().Add(std::make_unique<BucketItem>(new_fill, 1));
    if (leftover && player->GetLevel()) {
      player->GetLevel()->DropItem(player->x, player->y, std::move(leftover));
    }
  }
  return nullptr;
}

bool BucketItem::InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                            int32_t yt, entity::Player* player,
                            entity::Direction /*attack_dir*/) {
  if (!tile || !level || !player) return false;
  auto apply_fill = [&](BucketFill new_fill) {
    if (!game::GameState::Instance().IsCreativeMode()) {
      auto b = EditBucket(player, new_fill);
      if (b) {
        filling = b->filling;
        name_ = b->Name();
        sprite = b->sprite;
      }
    }
  };
  if (filling == BucketFill::kEmpty) {
    if (tile->id == level::Tiles::kWater) {
      level->SetTile(xt, yt, level::Tiles::kHole, 0);
      apply_fill(BucketFill::kWater);
      return true;
    }
    if (tile->id == level::Tiles::kLava) {
      level->SetTile(xt, yt, level::Tiles::kHole, 0);
      apply_fill(BucketFill::kLava);
      return true;
    }
  } else {
    if (tile->id == level::Tiles::kHole) {
      level->SetTile(xt, yt,
                     filling == BucketFill::kWater ? level::Tiles::kWater
                                                   : level::Tiles::kLava,
                     0);
      apply_fill(BucketFill::kEmpty);
      return true;
    }
    if (tile->id == level::Tiles::kLava && filling == BucketFill::kWater) {
      level->SetTile(xt, yt, level::Tiles::kObsidian, 0);
      apply_fill(BucketFill::kEmpty);
      return true;
    }
  }
  return false;
}

// --- TileItem ---
TileItem::TileItem(std::string name, gfx::LinkedSprite sprite, TileModel model,
                   std::vector<std::string> valid_tiles, int32_t count)
    : StackableItem(std::move(name), std::move(sprite), count),
      model(std::move(model)),
      valid_tiles(std::move(valid_tiles)) {}

bool TileItem::Equals(const Item& other) const {
  auto* t = dynamic_cast<const TileItem*>(&other);
  return t != nullptr && name_ == t->name_;
}

bool TileItem::InteractOn(level::Tile* tile, level::Level* level, int32_t xt,
                          int32_t yt, entity::Player* player,
                          entity::Direction attack_dir) {
  if (!tile || !level) return false;
  for (const std::string& vt : valid_tiles) {
    const level::Tile* target = level::Tiles::Find(vt);
    if (target && tile->id == target->id) {
      const level::Tile* placed = level::Tiles::Find(model.tile_name);
      if (placed) {
        int32_t data = 0;
        if (model.data_getter) {
          data = model.data_getter(*placed, *tile, level, xt, yt, player,
                                   attack_dir);
        } else if (model.keep_data) {
          data = tile->id;
        } else if (model.fixed_data >= 0) {
          data = model.fixed_data;
        }
        level->SetTile(xt, yt, placed->id, static_cast<int16_t>(data));
        io::Sound::Play("craft");
        return ConsumeOnSuccess(true);
      }
    }
  }
  if (!model.tile_name.empty()) {
    const std::string upper_tile = ToUpperAscii(model.tile_name);
    std::string note;
    if (upper_tile.find("WALL") != std::string::npos ||
        upper_tile.find("DOOR") != std::string::npos) {
      if (!valid_tiles.empty()) {
        const level::Tile* required = level::Tiles::Find(valid_tiles.front());
        const std::string required_name = util::Localization::GetLocalized(
            required ? required->name : valid_tiles.front());
        note = util::Localization::GetLocalized(
            "minicraft.notification.invalid_placement", required_name);
      }
    } else if (upper_tile.find("BRICK") != std::string::npos ||
               upper_tile.find("PLANK") != std::string::npos ||
               upper_tile == "STONE" ||
               upper_tile == "OBSIDIAN" ||
               upper_tile == "RAW OBSIDIAN" ||
               upper_tile.find("ORNATE") != std::string::npos) {
      note = util::Localization::GetLocalized("minicraft.notification.dig_hole");
    }
    if (!note.empty()) {
      game::GameState::Instance().AddNotification(note);
    }
  }
  return false;
}

// --- WoolItem ---
WoolItem::WoolItem(std::string name, gfx::LinkedSprite sprite, DyeColor col,
                   int32_t count)
    : TileItem(std::move(name), std::move(sprite),
               TileModel{col == DyeColor::kWhite
                             ? "White Wool"
                             : std::string(DyeColorDisplayName(col)) + " Wool",
                         nullptr, false, -1},
               {"Hole", "Water"}, count),
      color(col) {}

// --- ToolItem ---
ToolItem::ToolItem(ToolType type, int32_t level)
    : Item(std::string(level == 0
                           ? "Wood"
                           : (level == 1
                                  ? "Rock"
                                  : (level == 2
                                         ? "Iron"
                                         : (level == 3 ? "Gold" : "Gem")))) +
               " " + GetToolSpec(type).name,
           gfx::LinkedSprite(
               gfx::SpriteType::kItem,
               type == ToolType::kShears
                   ? "shears"
                   : std::string(level == 0
                                     ? "wooden"
                                     : (level == 1
                                            ? "stone"
                                            : (level == 2
                                                   ? "iron"
                                                   : (level == 3 ? "gold"
                                                                 : "gem")))) +
                         "_" + ToLowerSnake(GetToolSpec(type).name))),
      type(type),
      level(level),
      dur(GetToolSpec(type).durability * (level + 1)),
      damage(level * 5 + 10) {
  if (GetToolSpec(type).no_level) {
    name_ = GetToolSpec(type).name;
  }
}

ToolItem::ToolItem(ToolType type) : ToolItem(type, 0) {}

std::string ToolItem::GetDisplayName() const {
  const ToolSpec& spec = GetToolSpec(type);
  if (!spec.no_level) {
    static constexpr const char* kLevelNames[5] = {"Wood", "Rock", "Iron",
                                                   "Gold", "Gem"};
    const char* lvl_name =
        (level >= 0 && level < 5) ? kLevelNames[level] : "Wood";
    return " " + util::Localization::GetLocalized(lvl_name) + " " +
           util::Localization::GetLocalized(spec.name);
  }
  return " " + util::Localization::GetLocalized(spec.name);
}

bool ToolItem::IsDepleted() const {
  return dur <= 0 && GetToolSpec(type).durability > 0;
}

bool ToolItem::PayDurability() {
  if (game::GameState::Instance().IsCreativeMode()) return true;
  if (dur <= 0) return false;
  --dur;
  return true;
}

int32_t ToolItem::GetDamage() const {
  return util::GlobalRandom().NextInt(5) + damage;
}

int32_t ToolItem::GetAttackDamageBonus(const entity::Entity* e) {
  if (!PayDurability()) return 0;
  if (dynamic_cast<const entity::Mob*>(e) != nullptr) {
    auto& rng = util::GlobalRandom();
    if (type == ToolType::kAxe) {
      return (level + 1) * 2 + rng.NextInt(4);
    }
    if (type == ToolType::kSword) {
      return (level + 1) * 3 + rng.NextInt(2 + level * level);
    }
    if (type == ToolType::kClaymore) {
      return (level + 1) * 3 + rng.NextInt(4 + level * level * 3);
    }
    if (type == ToolType::kPickaxe) {
      return (level + 1) + rng.NextInt(2);
    }
    return 1;
  }
  return 0;
}

std::string ToolItem::GetData() const {
  return name_ + "_" + std::to_string(dur);
}

bool ToolItem::Equals(const Item& other) const {
  auto* t = dynamic_cast<const ToolItem*>(&other);
  return t != nullptr && t->type == type && t->level == level;
}

util::ItemSnapshot ToolItem::ToSnapshot() const {
  util::ItemSnapshot snap;
  snap.name = name_;
  snap.count = 1;
  snap.is_stackable = false;
  return snap;
}

std::unique_ptr<Item> ToolItem::Clone() const {
  auto c = std::make_unique<ToolItem>(type, level);
  c->dur = dur;
  c->damage = damage;
  return c;
}

// --- FishingRodItem ---
FishingRodItem::FishingRodItem(int32_t level)
    : Item(std::string(level == 0
                           ? "Wood"
                           : (level == 1 ? "Iron"
                                         : (level == 2 ? "Gold" : "Gem"))) +
               " Fishing Rod",
           gfx::LinkedSprite(
               gfx::SpriteType::kItem,
               level <= 0 ? "wooden_fishing_rod"
                          : (level == 1 ? "iron_fishing_rod"
                                        : (level == 2 ? "gold_fishing_rod"
                                                      : "gem_fishing_rod")))),
      level(std::clamp(level, 0, 3)) {}

int32_t FishingRodItem::GetChance(int32_t idx, int32_t level) {
  static constexpr int32_t kLevelChances[4][4] = {
      {44, 14, 9, 4},   // Wood
      {24, 14, 9, 4},   // Iron
      {59, 49, 9, 4},   // Gold
      {79, 69, 59, 4},  // Gem
  };
  const int32_t l = std::clamp(level, 0, 3);
  const int32_t i = std::clamp(idx, 0, 3);
  return kLevelChances[l][i];
}

bool FishingRodItem::InteractOn(level::Tile* tile, level::Level* level_ptr,
                                int32_t /*xt*/, int32_t /*yt*/,
                                entity::Player* player,
                                entity::Direction /*attack_dir*/) {
  if (!tile || !level_ptr || !player) return false;
  if (tile->id == level::Tiles::kWater && !player->IsSwimming()) {
    ++uses;
    player->is_fishing = true;
    player->fishing_level = level;
    return true;
  }
  return false;
}

bool FishingRodItem::IsDepleted() const {
  if (util::GlobalRandom().NextInt(100) > 120 - uses + level * 6) {
    game::GameState::Instance().AddNotification("Your Fishing rod broke.");
    return true;
  }
  return false;
}

std::unique_ptr<Item> FishingRodItem::Clone() const {
  auto c = std::make_unique<FishingRodItem>(level);
  c->uses = uses;
  return c;
}

// --- WateringCanItem ---
WateringCanItem::WateringCanItem(std::string name)
    : Item(std::move(name),
           gfx::LinkedSprite(gfx::SpriteType::kItem, "watering_can")) {}

void WateringCanItem::UpdateSprite() {
  sprite = gfx::LinkedSprite(
      gfx::SpriteType::kItem,
      content > 0 ? "watering_can_filled" : "watering_can");
}

bool WateringCanItem::InteractOn(level::Tile* tile, level::Level* level_ptr,
                                 int32_t xt, int32_t yt,
                                 entity::Player* player,
                                 entity::Direction attack_dir) {
  if (!tile || !level_ptr) return false;
  if (tile->id == level::Tiles::kWater) {
    content = kCapacity;
    UpdateSprite();
    return true;
  }
  if (content > 0) {
    --content;
    UpdateSprite();
    ++rendering_tick_;
    auto& rng = util::GlobalRandom();
    if (player && rendering_tick_ >= 8) {
      static constexpr const char* kSplashKeys[4] = {
          "splash_0", "splash_1", "splash_2", "splash_3"};
      const int32_t dx = entity::DirX(attack_dir);
      const int32_t dy = entity::DirY(attack_dir);
      for (int32_t i = 0; i < 4; ++i) {
        gfx::LinkedSprite splash(gfx::SpriteType::kEntity,
                                   kSplashKeys[rng.NextInt(4)]);
        const int32_t dest_x =
            player->x - 2 + 12 * dx + rng.NextInt(9) - 4;
        const int32_t dest_y =
            player->y - 2 + 12 * dy + rng.NextInt(9) - 4;
        const int32_t sx = player->x - 2 + 4 * dx + rng.NextInt(5) - 2;
        const int32_t sy = player->y - 2 + 4 * dy + rng.NextInt(5) - 2;
        level_ptr->Add(std::make_shared<entity::WaterParticle>(
            sx, sy, 80 + rng.NextInt(61) - 30, splash, dest_x, dest_y));
      }
      rendering_tick_ = 0;
    }

    auto spawn_glint = [&](int32_t tx, int32_t ty) {
      const double px = tx * 16.0 + 8.0 + (rng.NextGaussian() * 0.5) * 8.0;
      const double py = ty * 16.0 + 8.0 + (rng.NextGaussian() * 0.5) * 8.0;
      level_ptr->Add(std::make_shared<entity::Particle>(
          static_cast<int32_t>(px), static_cast<int32_t>(py),
          120 + rng.NextInt(21) - 40,
          gfx::LinkedSprite(gfx::SpriteType::kEntity, "glint")));
    };

    const bool is_crop = tile->id == level::Tiles::kWheat ||
                         tile->id == level::Tiles::kPotato ||
                         tile->id == level::Tiles::kTomato ||
                         tile->id == level::Tiles::kCarrot ||
                         tile->id == level::Tiles::kHeavenlyBerries ||
                         tile->id == level::Tiles::kHellishBerries;
    if (tile->id == level::Tiles::kFarmland) {
      const uint16_t d = static_cast<uint16_t>(level_ptr->GetData(xt, yt));
      level_ptr->SetData(xt, yt, static_cast<int16_t>((d & ~0b111u) | 7u));
    } else if (is_crop) {
      const uint16_t d = static_cast<uint16_t>(level_ptr->GetData(xt, yt));
      const uint16_t fert = d >> 7;
      if (fert < 150) {
        level_ptr->SetData(
            xt, yt, static_cast<int16_t>(((fert + 1u) << 7) | (d & 0x7Fu) | 7u));
      } else if ((d & 0b111u) < 7u) {
        level_ptr->SetData(xt, yt, static_cast<int16_t>((d & ~0b111u) | 7u));
      }
      if (rng.NextInt(5) == 0) {
        spawn_glint(xt, yt);
      }
    } else if (tile->id == level::Tiles::kDirt ||
               tile->id == level::Tiles::kGrass) {
      if (tile->id == level::Tiles::kGrass) {
        if (rng.NextInt(15) == 0) {
          spawn_glint(xt, yt);
        }
        if (rng.NextInt(60) == 0) {
          level_ptr->SetTile(xt, yt, level::Tiles::kFlower,
                             static_cast<int16_t>(rng.NextInt(19)));
        }
      }

      for (const gfx::Point& p : level_ptr->GetAreaTilePositions(xt, yt, 1)) {
        const level::Tile* nt = level_ptr->GetTile(p.x, p.y);
        if (!nt) continue;
        if (tile->id == level::Tiles::kDirt) {
          if (nt->id == level::Tiles::kGrass) {
            if (rng.NextInt(5) == 0) spawn_glint(xt, yt);
            if (rng.NextInt(10) == 0) {
              level_ptr->SetTile(xt, yt, level::Tiles::kGrass, 0);
            }
            break;
          }
        } else {
          if (nt->id == level::Tiles::kDirt) {
            if (rng.NextInt(5) == 0) spawn_glint(xt, yt);
            if (rng.NextInt(15) == 0) {
              level_ptr->SetTile(p.x, p.y, level::Tiles::kGrass, 0);
            }
            break;
          }
        }
      }
    }

    return true;
  }
  return false;
}

std::string WateringCanItem::GetData() const {
  return name_ + "_" + std::to_string(content);
}

std::unique_ptr<Item> WateringCanItem::Clone() const {
  auto c = std::make_unique<WateringCanItem>(name_);
  c->content = content;
  c->UpdateSprite();
  return c;
}

// --- SummonItem ---
SummonItem::SummonItem(std::string name, gfx::LinkedSprite sprite,
                       std::string mob_name, int32_t count)
    : StackableItem(std::move(name), std::move(sprite), count),
      mob_(std::move(mob_name)) {}

bool SummonItem::InteractOn(level::Tile* /*tile*/, level::Level* level_ptr,
                            int32_t /*xt*/, int32_t /*yt*/,
                            entity::Player* player,
                            entity::Direction /*attack_dir*/) {
  if (!level_ptr || !player) return false;
  auto& gs = game::GameState::Instance();
  bool success = false;

  if (mob_ == "Air Wizard" || mob_ == "AirWizard") {
    if (level_ptr->depth == 1) {
      bool boss_active = false;
      for (const auto& e : level_ptr->Entities()) {
        if (e && !e->removed &&
            dynamic_cast<const entity::AirWizard*>(e.get()) != nullptr) {
          boss_active = true;
          break;
        }
      }
      if (!boss_active) {
        if (player->PayStamina(2)) {
          level_ptr->Add(std::make_shared<entity::AirWizard>(), player->x + 8,
                         player->y + 8, false);
          io::Sound::Play("craft");
          success = true;
        }
      } else {
        gs.AddNotification(
            util::Localization::GetLocalized("minicraft.notification.boss_limit"));
      }
    } else {
      gs.AddNotification(util::Localization::GetLocalized(
          "minicraft.notification.wrong_level_sky"));
    }
  } else if (mob_ == "Obsidian Knight" || mob_ == "ObsidianKnight") {
    if (level_ptr->depth == -4) {
      const int32_t tx = player->x >> 4;
      const int32_t ty = player->y >> 4;
      const int32_t cx = level_ptr->w / 2 - 3;
      const int32_t cy = level_ptr->h / 2 - 3;
      if (tx >= cx && tx < cx + 7 && ty >= cy && ty < cy + 7) {
        bool knight_active = gs.obsidian_knight_summoned;
        bool statue_exists = false;
        for (const auto& e : level_ptr->Entities()) {
          if (!e || e->removed) continue;
          if (dynamic_cast<const entity::ObsidianKnight*>(e.get()) != nullptr) {
            knight_active = true;
          }
          if (dynamic_cast<const entity::KnightStatue*>(e.get()) != nullptr) {
            statue_exists = true;
          }
        }
        if (!knight_active) {
          if (!statue_exists) {
            if (player->PayStamina(2)) {
              level_ptr->Add(std::make_shared<entity::KnightStatue>(5000),
                             level_ptr->w / 2, level_ptr->h / 2, true);
              io::Sound::Play("craft");
              success = true;
            }
          } else {
            gs.AddNotification(util::Localization::GetLocalized(
                "minicraft.notification.knight_statue_exists"));
          }
        } else {
          gs.AddNotification(util::Localization::GetLocalized(
              "minicraft.notification.boss_limit"));
        }
      } else {
        gs.AddNotification(util::Localization::GetLocalized(
            "minicraft.notification.spawn_on_boss_tile"));
      }
    } else {
      gs.AddNotification(util::Localization::GetLocalized(
          "minicraft.notification.wrong_level_dungeon"));
    }
  }

  return ConsumeOnSuccess(success);
}

// --- EntitySummonItem ---
EntitySummonItem::EntitySummonItem(std::string name, gfx::LinkedSprite sprite,
                                   EntityFactory factory, int32_t count)
    : StackableItem(std::move(name), std::move(sprite), count),
      factory_(std::move(factory)) {}

bool EntitySummonItem::InteractOn(level::Tile* /*tile*/,
                                  level::Level* level_ptr, int32_t /*xt*/,
                                  int32_t /*yt*/, entity::Player* player,
                                  entity::Direction attack_dir) {
  if (!level_ptr || !player || !factory_) return false;
  std::unique_ptr<entity::Entity> e = factory_(attack_dir);
  if (e) {
    std::shared_ptr<entity::Entity> sp(std::move(e));
    level_ptr->Add(std::move(sp), player->x + 12 * entity::DirX(attack_dir),
                   player->y + 12 * entity::DirY(attack_dir));
    io::Sound::Play("craft");
    return ConsumeOnSuccess(true);
  }
  return false;
}

// --- FurnitureItem ---
FurnitureItem::FurnitureItem(std::unique_ptr<entity::Furniture> furn)
    : Item(furn ? furn->name : "Furniture",
           furn ? furn->item_sprite
                : gfx::LinkedSprite(gfx::SpriteType::kItem, "chest")),
      furniture(std::move(furn)) {}

FurnitureItem::FurnitureItem(const FurnitureItem& other)
    : Item(other.name_, other.sprite),
      furniture(other.furniture ? other.furniture->Clone() : nullptr),
      placed(other.placed) {}

FurnitureItem& FurnitureItem::operator=(const FurnitureItem& other) {
  if (this != &other) {
    name_ = other.name_;
    sprite = other.sprite;
    furniture = other.furniture ? other.furniture->Clone() : nullptr;
    placed = other.placed;
  }
  return *this;
}

FurnitureItem::~FurnitureItem() = default;

bool FurnitureItem::InteractOn(level::Tile* tile, level::Level* level_ptr,
                               int32_t xt, int32_t yt,
                               entity::Player* /*player*/,
                               entity::Direction /*attack_dir*/) {
  if (!tile || !level_ptr || !furniture) return false;
  if (tile->MayPass(*level_ptr, xt, yt, *furniture)) {
    std::shared_ptr<entity::Entity> sp(furniture->Clone());
    level_ptr->Add(std::move(sp), xt * 16 + 8, yt * 16 + 8);
    if (!game::GameState::Instance().IsCreativeMode()) {
      placed = true;
    }
    io::Sound::Play("craft");
    return true;
  }
  return false;
}

bool FurnitureItem::Equals(const Item& other) const {
  auto* f = dynamic_cast<const FurnitureItem*>(&other);
  return f != nullptr && name_ == f->name_;
}

}  // namespace minicraft::item
