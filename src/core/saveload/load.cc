#include "core/saveload/load.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <json.hpp>
#include <string>
#include <vector>

#include "core/entity/boat.h"
#include "core/entity/direction.h"
#include "core/entity/entity.h"
#include "core/entity/furniture.h"
#include "core/entity/mob.h"
#include "core/entity/player.h"
#include "core/game/game_state.h"
#include "core/io/input_handler.h"
#include "core/io/settings.h"
#include "core/item/inventory.h"
#include "core/item/item.h"
#include "core/item/items.h"
#include "core/item/recipes.h"
#include "core/level/chunk_manager.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/pak/fast_compress.h"
#include "core/platform/file_system.h"
#include "core/saveload/binary_codec.h"
#include "core/saveload/legacy_load.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/logging.h"

namespace minicraft::saveload {

namespace {

std::string_view TrimSv(std::string_view s) {
  while (!s.empty() &&
         std::isspace(static_cast<unsigned char>(s.front()))) {
    s.remove_prefix(1);
  }
  while (!s.empty() &&
         std::isspace(static_cast<unsigned char>(s.back()))) {
    s.remove_suffix(1);
  }
  return s;
}

int32_t ParseInt(std::string_view s, int32_t def = 0) {
  s = TrimSv(s);
  int32_t val = def;
  std::from_chars(s.data(), s.data() + s.size(), val);
  return val;
}

bool ParseBool(std::string_view s) {
  s = TrimSv(s);
  return s == "true" || s == "TRUE" || s == "1";
}

bool TryReadBinaryWorldMetadata(const platform::IFileSystem& fs,
                                std::string_view file_path,
                                WorldMetadata* out) {
  std::array<uint8_t, 4096> hdr_buf{};
  size_t bytes_read = 0;
  if (!fs.ReadBinaryHeader(file_path, hdr_buf.data(), hdr_buf.size(),
                           &bytes_read)) {
    return false;
  }
  BinaryPreamble pre;
  BinaryWorldHeader hdr;
  if (!ParseBinaryPreambleAndHeader(hdr_buf.data(), bytes_read, &pre, &hdr)) {
    std::vector<uint8_t> full_bytes;
    if (!fs.ReadBinaryFile(file_path, &full_bytes) ||
        !ParseBinaryPreambleAndHeader(full_bytes.data(), full_bytes.size(),
                                      &pre, &hdr)) {
      return false;
    }
  }
  Version ver(hdr.version_str, false);
  if (!ver.IsValid()) return false;
  out->version = ver;
  out->mode = hdr.mode_str;
  out->score = hdr.score;
  out->game_time = hdr.tick_count;
  out->valid = true;
  return true;
}

bool ReadBinaryItem(BinaryReader& r, const Version& world_ver,
                    std::unique_ptr<item::Item>* out, int32_t depth = 0) {
  if (!out || depth > kMaxNestedItemDepth) {
    r.Fail();
    return false;
  }
  out->reset();
  const uint8_t present = r.ReadU8();
  if (!r.Ok()) return false;
  if (present == 0) return true;
  if (present != 1) {
    r.Fail();
    return false;
  }

  std::string raw_name;
  if (!r.ReadStr16(&raw_name)) return false;
  const uint8_t state_kind = r.ReadU8();
  if (!r.Ok()) return false;

  int32_t val_i32 = 0;
  uint8_t chest_sub = 0;
  uint8_t dnc_locked = 1;
  std::vector<std::unique_ptr<item::Item>> nested_items;

  switch (state_kind) {
    case 0:
      break;
    case 1:
    case 2:
    case 3:
    case 4:
      val_i32 = r.ReadI32();
      break;
    case 5: {
      chest_sub = r.ReadU8();
      if (chest_sub == 1) {
        val_i32 = r.ReadI32();
      } else if (chest_sub == 2) {
        dnc_locked = r.ReadU8();
      } else if (chest_sub != 0) {
        r.Fail();
        return false;
      }
      const uint32_t cnt = r.ReadU32();
      if (!r.Ok() || cnt > kMaxInventoryItems || cnt > r.Remaining()) {
        r.Fail();
        return false;
      }
      nested_items.reserve(cnt);
      for (uint32_t i = 0; i < cnt; ++i) {
        std::unique_ptr<item::Item> child;
        if (!ReadBinaryItem(r, world_ver, &child, depth + 1)) return false;
        if (child) nested_items.push_back(std::move(child));
      }
      break;
    }
    default:
      r.Fail();
      return false;
  }
  if (!r.Ok()) return false;

  const std::string mapped_name = LegacyLoad::SubOldName(raw_name, world_ver);
  if (mapped_name.find("Power Glove") != std::string::npos ||
      mapped_name.find("Totem of Wind") != std::string::npos) {
    return true;
  }

  auto it = item::Items::Get(mapped_name, true);
  if (!it) return true;

  switch (state_kind) {
    case 1:
      if (auto* st = dynamic_cast<item::StackableItem*>(it.get())) {
        st->count = std::clamp(val_i32, 1, 99999);
      }
      break;
    case 2:
      if (auto* tool = dynamic_cast<item::ToolItem*>(it.get())) {
        tool->dur = val_i32;
      }
      break;
    case 3:
      if (auto* wc = dynamic_cast<item::WateringCanItem*>(it.get())) {
        wc->content = std::clamp(val_i32, 0, item::WateringCanItem::kCapacity);
        wc->UpdateSprite();
      }
      break;
    case 4:
      if (auto* rod = dynamic_cast<item::FishingRodItem*>(it.get())) {
        rod->uses = std::max(0, val_i32);
      }
      break;
    case 5:
      if (auto* furn = dynamic_cast<item::FurnitureItem*>(it.get());
          furn != nullptr && furn->furniture != nullptr) {
        if (auto* ch = dynamic_cast<entity::Chest*>(furn->furniture.get())) {
          if (chest_sub == 1) {
            if (auto* dc = dynamic_cast<entity::DeathChest*>(ch)) {
              dc->SetTime(std::max(0, val_i32));
            }
          } else if (chest_sub == 2) {
            if (auto* dnc = dynamic_cast<entity::DungeonChest*>(ch)) {
              dnc->is_locked = (dnc_locked != 0);
            }
          }
          if (auto inv = ch->GetInventory()) {
            inv->ClearInv();
            for (auto& child : nested_items) {
              if (child) inv->Add(std::move(child));
            }
          }
        }
      }
      break;
    default:
      break;
  }

  *out = std::move(it);
  return true;
}

bool ReadBinaryEntity(
    BinaryReader& r, const Version& world_ver,
    std::array<std::unique_ptr<level::Level>, 6>& target_levels) {
  const uint8_t kind_raw = r.ReadU8();
  const uint8_t lvl_idx = r.ReadU8();
  const int32_t x = r.ReadI32();
  const int32_t y = r.ReadI32();
  if (!r.Ok() || kind_raw < 1 || kind_raw > 28) {
    r.Fail();
    return false;
  }

  const auto kind = static_cast<BinaryEntityKind>(kind_raw);
  std::shared_ptr<entity::Entity> ent;

  switch (kind) {
    case BinaryEntityKind::kCow: {
      auto c = std::make_shared<entity::Cow>();
      c->health = r.ReadI32();
      ent = std::move(c);
      break;
    }
    case BinaryEntityKind::kPig: {
      auto p = std::make_shared<entity::Pig>();
      p->health = r.ReadI32();
      ent = std::move(p);
      break;
    }
    case BinaryEntityKind::kSheep: {
      const int32_t hp = r.ReadI32();
      const uint8_t cut = r.ReadU8();
      const uint8_t col = r.ReadU8();
      const auto dye = static_cast<item::DyeColor>(col < 16 ? col : 15);
      auto s = std::make_shared<entity::Sheep>(dye);
      s->health = hp;
      s->cut = (cut != 0);
      ent = std::move(s);
      break;
    }
    case BinaryEntityKind::kZombie:
    case BinaryEntityKind::kSlime:
    case BinaryEntityKind::kCreeper:
    case BinaryEntityKind::kSkeleton:
    case BinaryEntityKind::kKnight:
    case BinaryEntityKind::kSnake: {
      const int32_t hp = r.ReadI32();
      const int32_t lvl = std::clamp<int32_t>(r.ReadU8(), 1, 4);
      if (kind == BinaryEntityKind::kZombie) {
        ent = std::make_shared<entity::Zombie>(lvl);
      } else if (kind == BinaryEntityKind::kSlime) {
        ent = std::make_shared<entity::Slime>(lvl);
      } else if (kind == BinaryEntityKind::kCreeper) {
        ent = std::make_shared<entity::Creeper>(lvl);
      } else if (kind == BinaryEntityKind::kSkeleton) {
        ent = std::make_shared<entity::Skeleton>(lvl);
      } else if (kind == BinaryEntityKind::kKnight) {
        ent = std::make_shared<entity::Knight>(lvl);
      } else {
        ent = std::make_shared<entity::Snake>(lvl);
      }
      static_cast<entity::EnemyMob*>(ent.get())->health = hp;
      break;
    }
    case BinaryEntityKind::kAirWizard: {
      auto aw = std::make_shared<entity::AirWizard>();
      aw->health = r.ReadI32();
      ent = std::move(aw);
      break;
    }
    case BinaryEntityKind::kObsidianKnight: {
      auto ok = std::make_shared<entity::ObsidianKnight>();
      ok->health = r.ReadI32();
      ok->SetPhase(std::clamp<int32_t>(r.ReadU8(), 0, 1));
      ent = std::move(ok);
      break;
    }
    case BinaryEntityKind::kSpawner: {
      std::string mob_type;
      if (!r.ReadStr16(&mob_type)) return false;
      const int32_t mlvl = std::clamp<int32_t>(r.ReadU8(), 1, 4);
      ent = std::make_shared<entity::Spawner>(std::move(mob_type), mlvl);
      break;
    }
    case BinaryEntityKind::kWorkbench:
      ent = std::make_shared<entity::Crafter>(entity::Crafter::Type::kWorkbench);
      break;
    case BinaryEntityKind::kAnvil:
      ent = std::make_shared<entity::Crafter>(entity::Crafter::Type::kAnvil);
      break;
    case BinaryEntityKind::kEnchanter:
      ent = std::make_shared<entity::Crafter>(entity::Crafter::Type::kEnchanter);
      break;
    case BinaryEntityKind::kLoom:
      ent = std::make_shared<entity::Crafter>(entity::Crafter::Type::kLoom);
      break;
    case BinaryEntityKind::kFurnace:
      ent = std::make_shared<entity::Crafter>(entity::Crafter::Type::kFurnace);
      break;
    case BinaryEntityKind::kOven:
      ent = std::make_shared<entity::Crafter>(entity::Crafter::Type::kOven);
      break;
    case BinaryEntityKind::kDyeVat:
      ent = std::make_shared<entity::Crafter>(entity::Crafter::Type::kDyeVat);
      break;
    case BinaryEntityKind::kComposter: {
      auto comp = std::make_shared<entity::Composter>();
      comp->compost =
          std::clamp(r.ReadI32(), 0, entity::Composter::kMaxCompost);
      comp->UpdateVisual();
      ent = std::move(comp);
      break;
    }
    case BinaryEntityKind::kChest:
    case BinaryEntityKind::kDeathChest:
    case BinaryEntityKind::kDungeonChest: {
      std::shared_ptr<entity::Chest> ch;
      if (kind == BinaryEntityKind::kDeathChest) {
        auto dc = std::make_shared<entity::DeathChest>();
        dc->SetTime(std::max(0, r.ReadI32()));
        ch = std::move(dc);
      } else if (kind == BinaryEntityKind::kDungeonChest) {
        const bool locked = (r.ReadU8() != 0);
        auto dnc = std::make_shared<entity::DungeonChest>(false, locked);
        dnc->sprite = gfx::LinkedSprite(
            gfx::SpriteType::kEntity, locked ? "white_chest" : "dungeon_chest");
        if (locked && lvl_idx < 6 && target_levels[lvl_idx]) {
          target_levels[lvl_idx]->chest_count++;
        }
        ch = std::move(dnc);
      } else {
        ch = std::make_shared<entity::Chest>();
      }
      const uint32_t cnt = r.ReadU32();
      if (!r.Ok() || cnt > kMaxInventoryItems || cnt > r.Remaining()) {
        r.Fail();
        return false;
      }
      for (uint32_t i = 0; i < cnt; ++i) {
        std::unique_ptr<item::Item> it;
        if (!ReadBinaryItem(r, world_ver, &it, 1)) return false;
        if (it && ch->GetInventory()) {
          ch->GetInventory()->Add(std::move(it));
        }
      }
      ent = std::move(ch);
      break;
    }
    case BinaryEntityKind::kBed: {
      std::string color_name;
      if (!r.ReadStr16(&color_name)) return false;
      ent = std::make_shared<entity::Bed>(
          LegacyLoad::NormalizeBedColor(color_name));
      break;
    }
    case BinaryEntityKind::kTnt:
      ent = std::make_shared<entity::Tnt>();
      break;
    case BinaryEntityKind::kLantern: {
      const int32_t lt = std::clamp<int32_t>(r.ReadU8(), 0, 2);
      ent = std::make_shared<entity::Lantern>(
          static_cast<entity::Lantern::Type>(lt));
      break;
    }
    case BinaryEntityKind::kKnightStatue:
      ent = std::make_shared<entity::KnightStatue>(
          std::clamp(r.ReadI32(), 1, 5000));
      break;
    case BinaryEntityKind::kBoat: {
      const int32_t d = std::clamp<int32_t>(r.ReadU8(), 0, 4);
      ent = std::make_shared<entity::Boat>(static_cast<entity::Direction>(d));
      break;
    }
  }

  if (!r.Ok()) return false;
  if (ent && lvl_idx < 6 && target_levels[lvl_idx]) {
    target_levels[lvl_idx]->Add(ent, x, y, false);
  }
  return true;
}

// Fully validates and parses a binary world save into staging objects before
// mutating any live GameState or WorldSaveContext state.
bool TryLoadBinaryWorldFile(const platform::IFileSystem& fs,
                            std::string_view file_path, WorldSaveContext& ctx) {
  std::vector<uint8_t> file_bytes;
  if (!fs.ReadBinaryFile(file_path, &file_bytes)) return false;

  BinaryPreamble pre;
  BinaryWorldHeader hdr;
  if (!ParseBinaryPreambleAndHeader(file_bytes.data(), file_bytes.size(), &pre,
                                    &hdr)) {
    return false;
  }

  const uint64_t expected_total =
      static_cast<uint64_t>(kBinaryPreambleSize) + pre.header_size +
      pre.payload_stored_size;
  if (file_bytes.size() != expected_total) return false;

  const Version world_ver(hdr.version_str, false);
  if (!world_ver.IsValid()) return false;

  const uint8_t* stored_ptr =
      file_bytes.data() + kBinaryPreambleSize + pre.header_size;
  if (ComputeCrc32(stored_ptr, pre.payload_stored_size) !=
      pre.payload_stored_crc32) {
    return false;
  }

  std::vector<uint8_t> decompressed_buf;
  const uint8_t* raw_ptr = nullptr;
  size_t raw_size = pre.payload_raw_size;
  if ((pre.container_flags & kContainerFlagCompressed) != 0) {
    if (!pak::DecompressFastLz(stored_ptr, pre.payload_stored_size,
                               pre.payload_raw_size, &decompressed_buf)) {
      return false;
    }
    raw_ptr = decompressed_buf.data();
  } else {
    raw_ptr = stored_ptr;
  }

  if (ComputeCrc32(raw_ptr, raw_size) != pre.payload_raw_crc32) {
    return false;
  }

  auto report = [&](std::string_view msg) {
    if (ctx.on_progress) ctx.on_progress(std::string(msg));
  };

  BinaryReader r(raw_ptr, raw_size);

  // 1. Tile palette remap table.
  level::Tiles::InitTiles();
  std::array<uint16_t, 256> tile_remap{};
  for (uint16_t id = 0; id < 256; ++id) {
    tile_remap[id] = level::Tiles::ContainsTile(id) ? id : 0;
  }
  const uint16_t palette_cnt = r.ReadU16();
  if (!r.Ok() || palette_cnt > 512) return false;
  for (uint16_t pi = 0; pi < palette_cnt; ++pi) {
    const uint16_t saved_id = r.ReadU16();
    std::string saved_name;
    if (!r.ReadStr16(&saved_name)) return false;
    if (saved_id < 256) {
      const level::Tile* cur = level::Tiles::Get(saved_id);
      if (cur && cur->name == saved_name) {
        tile_remap[saved_id] = saved_id;
      } else if (const level::Tile* found = level::Tiles::Find(saved_name)) {
        tile_remap[saved_id] = found->id;
      } else {
        tile_remap[saved_id] = 0;
      }
    }
  }

  // 2. Staged levels 0..5.
  report("minicraft.displays.loading.message.levels");
  static constexpr std::array<int32_t, 6> kDepths = {-3, -2, -1, 0, 1, -4};
  std::array<std::unique_ptr<level::Level>, 6> staged_levels;
  for (int32_t i = 0; i < 6; ++i) {
    const uint8_t present = r.ReadU8();
    if (!r.Ok()) return false;
    if (present == 0) {
      staged_levels[i] = std::make_unique<level::Level>(
          128, 128, 0, kDepths[i], nullptr, /*skip_gen=*/false);
      continue;
    }
    if (present != 1) return false;

    const int32_t w = r.ReadI32();
    const int32_t h = r.ReadI32();
    const int64_t seed = r.ReadI64();
    const int32_t depth = r.ReadI32();
    const uint32_t ccount = r.ReadU32();
    if (!r.Ok() || w < 16 || w > 65536 || h < 16 || h > 65536 ||
        ccount > kMaxChunksPerLevel ||
        static_cast<size_t>(ccount) * 13u > r.Remaining()) {
      return false;
    }

    auto lvl = std::make_unique<level::Level>(
        w, h, seed, depth, nullptr, /*skip_gen=*/ccount > 0);
    auto& cm = lvl->GetChunkManager();
    for (uint32_t ci = 0; ci < ccount; ++ci) {
      const int32_t cx = r.ReadI32();
      const int32_t cy = r.ReadI32();
      const int32_t stage = r.ReadI32();
      const uint8_t cflags = r.ReadU8();
      if (!r.Ok() ||
          (cflags & ~(kChunkFlagTilesUniform | kChunkFlagDataAllZero)) != 0) {
        return false;
      }

      auto chunk = std::make_unique<level::Chunk>();
      chunk->chunk_x = cx;
      chunk->chunk_y = cy;
      chunk->stage = stage;

      if ((cflags & kChunkFlagTilesUniform) != 0) {
        const uint16_t raw_tid = r.ReadU16();
        if (!r.Ok()) return false;
        chunk->tile_ids.fill(raw_tid < 256 ? tile_remap[raw_tid] : 0);
      } else {
        if (r.Remaining() < level::Chunk::kArea * sizeof(uint16_t)) {
          return false;
        }
        for (size_t k = 0; k < level::Chunk::kArea; ++k) {
          const uint16_t raw_tid = r.ReadU16();
          chunk->tile_ids[k] = raw_tid < 256 ? tile_remap[raw_tid] : 0;
        }
      }

      if ((cflags & kChunkFlagDataAllZero) != 0) {
        chunk->tile_data.fill(0);
      } else {
        if (r.Remaining() < level::Chunk::kArea * sizeof(int16_t)) {
          return false;
        }
        for (size_t k = 0; k < level::Chunk::kArea; ++k) {
          chunk->tile_data[k] = r.ReadI16();
        }
      }
      if (!r.Ok()) return false;

      chunk->dirty = false;
      cm.InsertLoadedChunk(std::move(chunk));
      cm.SetChunkStage(cx, cy, stage);
    }
    staged_levels[i] = std::move(lvl);
  }

  for (int32_t i = 0; i < 4; ++i) {
    if (staged_levels[i]) {
      staged_levels[i]->parent_level = staged_levels[i + 1].get();
    }
  }
  if (staged_levels[5]) {
    staged_levels[5]->parent_level = staged_levels[0].get();
    if (!staged_levels[5]->GetChunkManager().Chunks().empty()) {
      staged_levels[5]->SetBossRoomGenerated(true);
    }
  }

  // 3. Staged player & inventory.
  report("minicraft.displays.loading.message.entities");
  auto staged_player = std::make_shared<entity::Player>(*ctx.player, nullptr);
  staged_player->GetInventory().ClearInv();
  staged_player->potion_effects.clear();
  staged_player->cur_armor.reset();
  staged_player->active_item.reset();

  staged_player->x = r.ReadI32();
  staged_player->y = r.ReadI32();
  staged_player->spawnx = r.ReadI32();
  staged_player->spawny = r.ReadI32();
  staged_player->spawn_depth = r.ReadI32();
  staged_player->health = r.ReadI32();
  staged_player->max_health = std::clamp(r.ReadI32(), entity::Player::kMaxHealth,
                                         entity::Player::kMaxTotalHealth);
  staged_player->hunger = r.ReadI32();
  staged_player->stamina = r.ReadI32();
  staged_player->armor = r.ReadI32();
  staged_player->armor_damage_buffer = r.ReadI32();
  staged_player->shirt_color = r.ReadI32();
  staged_player->skinon = (r.ReadU8() != 0);
  staged_player->SetScore(r.ReadI32());
  const int32_t saved_lvl_idx = std::clamp<int32_t>(r.ReadU8(), 0, 5);
  if (!r.Ok()) return false;

  std::unique_ptr<item::Item> cur_armor_item;
  std::unique_ptr<item::Item> active_item_ptr;
  if (!ReadBinaryItem(r, world_ver, &cur_armor_item) ||
      !ReadBinaryItem(r, world_ver, &active_item_ptr)) {
    return false;
  }
  if (auto* arm = dynamic_cast<item::ArmorItem*>(cur_armor_item.get())) {
    cur_armor_item.release();
    staged_player->cur_armor.reset(arm);
  }
  if (active_item_ptr) {
    staged_player->active_item =
        std::shared_ptr<item::Item>(std::move(active_item_ptr));
  }

  const uint16_t potion_cnt = r.ReadU16();
  if (!r.Ok() || potion_cnt > 64 ||
      static_cast<size_t>(potion_cnt) * 8u > r.Remaining()) {
    return false;
  }
  for (uint16_t pi = 0; pi < potion_cnt; ++pi) {
    const int32_t eid = r.ReadI32();
    const int32_t dur = r.ReadI32();
    if (eid >= 0 && dur > 0) {
      staged_player->potion_effects[eid] = dur;
    }
  }

  const uint32_t recipe_cnt = r.ReadU32();
  if (!r.Ok() || recipe_cnt > 4096 ||
      static_cast<size_t>(recipe_cnt) * 4u > r.Remaining()) {
    return false;
  }
  std::vector<item::Recipe> staged_recipes;
  staged_recipes.reserve(recipe_cnt);
  for (uint32_t ri = 0; ri < recipe_cnt; ++ri) {
    std::string prod_key;
    if (!r.ReadStr16(&prod_key)) return false;
    const uint16_t cost_cnt = r.ReadU16();
    if (!r.Ok() || cost_cnt > 64 ||
        static_cast<size_t>(cost_cnt) * 2u > r.Remaining()) {
      return false;
    }
    std::vector<std::string> costs;
    costs.reserve(cost_cnt);
    for (uint16_t ci = 0; ci < cost_cnt; ++ci) {
      std::string cstr;
      if (!r.ReadStr16(&cstr)) return false;
      costs.push_back(std::move(cstr));
    }
    staged_recipes.emplace_back(prod_key, costs);
  }

  const uint32_t inv_cnt = r.ReadU32();
  if (!r.Ok() || inv_cnt > kMaxInventoryItems || inv_cnt > r.Remaining()) {
    return false;
  }
  auto overflow_chest = std::make_unique<entity::DeathChest>();
  for (uint32_t ii = 0; ii < inv_cnt; ++ii) {
    std::unique_ptr<item::Item> it;
    if (!ReadBinaryItem(r, world_ver, &it)) return false;
    if (!it) continue;
    auto leftover = staged_player->GetInventory().Add(std::move(it));
    if (leftover && overflow_chest->GetInventory()) {
      overflow_chest->GetInventory()->Add(std::move(leftover));
    }
  }

  if (staged_levels[saved_lvl_idx]) {
    staged_levels[saved_lvl_idx]->Add(staged_player, staged_player->x,
                                      staged_player->y, false);
    if (overflow_chest && overflow_chest->GetInventory() &&
        overflow_chest->GetInventory()->InvSize() > 0) {
      staged_levels[saved_lvl_idx]->Add(
          std::shared_ptr<entity::DeathChest>(std::move(overflow_chest)),
          staged_player->x, staged_player->y, false);
    }
  }

  // 4. Staged world entities.
  const uint32_t ent_cnt = r.ReadU32();
  if (!r.Ok() || ent_cnt > kMaxWorldEntities ||
      static_cast<size_t>(ent_cnt) * 10u > r.Remaining()) {
    return false;
  }
  for (uint32_t ei = 0; ei < ent_cnt; ++ei) {
    if (!ReadBinaryEntity(r, world_ver, staged_levels)) return false;
  }

  // 5. Advancements & signs.
  std::string staged_adv_json;
  if (!r.ReadStr32(&staged_adv_json)) return false;

  const uint32_t sign_cnt = r.ReadU32();
  if (!r.Ok() || sign_cnt > kMaxWorldSigns ||
      static_cast<size_t>(sign_cnt) * 13u > r.Remaining()) {
    return false;
  }
  SignMap staged_signs;
  for (uint32_t si = 0; si < sign_cnt; ++si) {
    SignCoord sc;
    sc.level = r.ReadI32();
    sc.x = r.ReadI32();
    sc.y = r.ReadI32();
    const uint8_t lcnt = r.ReadU8();
    if (!r.Ok() || lcnt > kMaxSignLines) return false;
    std::vector<std::string> lines;
    lines.reserve(lcnt);
    for (uint8_t li = 0; li < lcnt; ++li) {
      std::string line;
      if (!r.ReadStr16(&line)) return false;
      lines.push_back(std::move(line));
    }
    staged_signs[sc] = std::move(lines);
  }

  if (!r.Ok() || r.Remaining() != 0) return false;

  // 6. Commit validated state atomically to GameState, Settings, and ctx.
  auto& gs = game::GameState::Instance();
  gs.ResetForNewWorld();

  if (!hdr.mode_str.empty()) io::Settings::Set("mode", hdr.mode_str);
  if (!hdr.diff_str.empty()) io::Settings::Set("diff", hdr.diff_str);
  gs.score_time = hdr.score_time;
  if (hdr.game_time >= game::GameState::kDayLength) {
    gs.game_time = 0;
    gs.past_day1 = true;
  } else {
    gs.SetGameTime(std::max<int32_t>(0, hdr.game_time));
  }
  gs.tick_count = std::max<int32_t>(0, hdr.tick_count);
  gs.past_day1 = gs.past_day1 || (gs.tick_count > 65000) ||
                 ((hdr.world_flags & kWorldFlagPastDay1) != 0);
  gs.air_wizard_beaten = (hdr.world_flags & kWorldFlagAirWizardBeaten) != 0;
  gs.obsidian_knight_beaten =
      (hdr.world_flags & kWorldFlagObsidianKnightBeaten) != 0;
  gs.obsidian_knight_summoned =
      (hdr.world_flags & kWorldFlagObsidianKnightSummoned) != 0;
  gs.score = staged_player->GetScore();
  io::Settings::Set("scoretime", hdr.scoretime_setting);
  io::Settings::Set("quests", (hdr.world_flags & kWorldFlagQuests) != 0);
  io::Settings::Set("tutorials", (hdr.world_flags & kWorldFlagTutorials) != 0);

  *ctx.levels = std::move(staged_levels);
  *ctx.player = std::move(staged_player);
  if (ctx.current_level_idx) *ctx.current_level_idx = saved_lvl_idx;
  if (ctx.unlocked_recipes) {
    *ctx.unlocked_recipes = std::move(staged_recipes);
  }
  if (ctx.signs) {
    *ctx.signs = std::move(staged_signs);
  }

  for (int32_t i = 0; i < 6; ++i) {
    if ((*ctx.levels)[i]) {
      (*ctx.levels)[i]->FlushPendingEntities();
      (*ctx.levels)[i]->CheckChestCount();
      (*ctx.levels)[i]->CheckAirWizard();
    }
  }

  util::AdvancementSystem::Instance().ResetAll();
  if (!staged_adv_json.empty()) {
    util::AdvancementSystem::Instance().LoadAdvancementsJson(staged_adv_json);
  }

  gs.has_world = true;
  return true;
}

}  // namespace

std::string Load::SubOldName(std::string_view name_sv,
                             const Version& world_ver) {
  return LegacyLoad::SubOldName(name_sv, world_ver);
}

void Load::LoadInventory(const std::vector<std::string>& lines,
                         item::Inventory& inv, const Version& world_ver,
                         entity::DeathChest* overflow_chest) {
  LegacyLoad::LoadInventory(lines, inv, world_ver, overflow_chest);
}

std::shared_ptr<entity::Entity> Load::LoadEntity(std::string_view entity_data,
                                                 const Version& world_ver,
                                                 WorldSaveContext& ctx,
                                                 bool is_java_save) {
  return LegacyLoad::LoadEntity(entity_data, world_ver, ctx, is_java_save);
}

std::vector<WorldMetadata> Load::ListWorlds(const platform::IFileSystem& fs,
                                            std::string_view saves_dir) {
  std::vector<WorldMetadata> worlds;
  if (!fs.Exists(saves_dir)) return worlds;

  for (const auto& entry : fs.ListDirectory(saves_dir)) {
    const std::string world_dir = Save::JoinWorldPath(saves_dir, entry);
    if (!fs.IsDirectory(world_dir)) continue;

    WorldMetadata meta;
    meta.folder_name = entry;
    meta.world_dir = world_dir;

    const std::string bin_file =
        Save::JoinWorldPath(world_dir, kBinaryWorldFileName);
    const std::string bak_file =
        Save::JoinWorldPath(world_dir, kBinaryWorldBackupFileName);
    if ((fs.Exists(bin_file) &&
         TryReadBinaryWorldMetadata(fs, bin_file, &meta)) ||
        (fs.Exists(bak_file) &&
         TryReadBinaryWorldMetadata(fs, bak_file, &meta))) {
      worlds.push_back(std::move(meta));
      continue;
    }

    if (LegacyLoad::IsLegacyWorldDir(fs, world_dir) &&
        LegacyLoad::ReadWorldMetadata(fs, world_dir, &meta)) {
      worlds.push_back(std::move(meta));
    }
  }

  std::sort(worlds.begin(), worlds.end(),
            [](const WorldMetadata& a, const WorldMetadata& b) {
              return a.folder_name < b.folder_name;
            });
  return worlds;
}

bool Load::LoadPrefs(const platform::IFileSystem& fs,
                     std::string_view game_dir, io::InputHandler* input,
                     std::vector<std::string>* out_resource_packs) {
  bool loaded_any = false;
  auto& gs = game::GameState::Instance();

  std::string prefs_text;
  if (fs.ReadTextFile(Save::JoinWorldPath(game_dir, "Preferences.json"),
                      &prefs_text)) {
    try {
      auto j = nlohmann::json::parse(prefs_text);
      auto read_json_bool = [&](const char* key, bool* out) -> bool {
        if (!j.contains(key)) return false;
        const auto& val = j[key];
        if (val.is_boolean()) {
          *out = val.get<bool>();
          return true;
        }
        if (val.is_string()) {
          *out = ParseBool(val.get<std::string>());
          return true;
        }
        return false;
      };
      auto read_json_int = [&](const char* key, int32_t* out) -> bool {
        if (!j.contains(key)) return false;
        const auto& val = j[key];
        if (val.is_number_integer()) {
          *out = val.get<int32_t>();
          return true;
        }
        if (val.is_string()) {
          *out = ParseInt(val.get<std::string>(), *out);
          return true;
        }
        return false;
      };
      bool bval = false;
      int32_t ival = 0;
      if (read_json_bool("sound", &bval)) io::Settings::Set("sound", bval);
      if (read_json_bool("autosave", &bval)) io::Settings::Set("autosave", bval);
      if (read_json_int("fps", &ival)) io::Settings::SetMaxFps(ival);
      if (j.contains("lang") && j["lang"].is_string()) {
        util::Localization::Instance().ChangeLanguage(
            j["lang"].get<std::string>());
      }
      if (j.contains("diff") && j["diff"].is_string()) {
        io::Settings::Set("diff", j["diff"].get<std::string>());
      }
      if (read_json_bool("fullscreen", &bval)) {
        io::Settings::Set("fullscreen", bval);
      }
      if (read_json_bool("tutorials", &bval)) {
        io::Settings::Set("tutorials", bval);
      }
      if (read_json_bool("showquests", &bval)) {
        io::Settings::Set("showquests", bval);
      }
      if (read_json_int("skinIdx", &ival)) {
        gs.selected_skin_index = ival;
      }
      if (j.contains("skin") && j["skin"].is_string()) {
        gs.selected_skin_name = j["skin"].get<std::string>();
      }
      if (input && j.contains("keymap") && j["keymap"].is_array()) {
        for (const auto& kv : j["keymap"]) {
          if (!kv.is_string()) continue;
          std::string s = kv.get<std::string>();
          size_t sep = s.find(';');
          if (sep != std::string::npos) {
            input->SetKey(std::string_view(s).substr(0, sep),
                          std::string_view(s).substr(sep + 1));
          }
        }
      }
      if (out_resource_packs && j.contains("resourcePacks") &&
          j["resourcePacks"].is_array()) {
        out_resource_packs->clear();
        for (const auto& p : j["resourcePacks"]) {
          if (p.is_string()) out_resource_packs->push_back(p.get<std::string>());
        }
      }
      loaded_any = true;
    } catch (const std::exception& e) {
      util::log::kSaveLoad.Error(std::string("Failed parsing Preferences.json: ") +
                                 e.what());
    }
  }

  std::string unlocks_text;
  if (fs.ReadTextFile(Save::JoinWorldPath(game_dir, "Unlocks.json"),
                      &unlocks_text)) {
    try {
      auto j = nlohmann::json::parse(unlocks_text);
      const char* ach_key = j.contains("unlockedAchievements")
                                ? "unlockedAchievements"
                                : "achievements";
      if (j.contains(ach_key) && j[ach_key].is_array()) {
        std::vector<std::string> ids;
        for (const auto& a : j[ach_key]) {
          if (a.is_string()) ids.push_back(a.get<std::string>());
        }
        util::AchievementManager::Instance().UnlockFromList(ids);
      }
      if (j.contains("unlockedRecipes") && j["unlockedRecipes"].is_object()) {
        util::AdvancementSystem::Instance().LoadRecipeUnlocksJson(
            j["unlockedRecipes"].dump());
      }
      if (j.contains("visibleScoreTimes") &&
          j["visibleScoreTimes"].is_array()) {
        if (auto* st_entry = io::Settings::GetEntry("scoretime")) {
          for (const auto& v : j["visibleScoreTimes"]) {
            if (!v.is_number_integer()) continue;
            const int32_t score_val = v.get<int32_t>();
            for (size_t i = 0; i < st_entry->int_options.size(); ++i) {
              if (st_entry->int_options[i] == score_val &&
                  i < st_entry->option_visible.size()) {
                st_entry->option_visible[i] = true;
              }
            }
          }
        }
      }
      loaded_any = true;
    } catch (const std::exception& e) {
      util::log::kSaveLoad.Error(std::string("Failed parsing Unlocks.json: ") +
                                 e.what());
    }
  }

  return loaded_any;
}

bool Load::LoadWorld(const platform::IFileSystem& fs, WorldSaveContext& ctx) {
  auto report = [&](std::string_view msg) {
    if (ctx.on_progress) ctx.on_progress(std::string(msg));
  };

  if (ctx.world_dir.empty() || !fs.IsDirectory(ctx.world_dir) || !ctx.levels ||
      !ctx.player) {
    return false;
  }

  // Native C++ binary container (`world.mcbin`) with `.bak` recovery.
  const std::string bin_path =
      Save::JoinWorldPath(ctx.world_dir, kBinaryWorldFileName);
  const std::string bak_path =
      Save::JoinWorldPath(ctx.world_dir, kBinaryWorldBackupFileName);
  const bool has_bin = fs.Exists(bin_path);
  const bool has_bak = fs.Exists(bak_path);
  if (has_bin || has_bak) {
    report("minicraft.displays.loading.message.world");
    if (has_bin && TryLoadBinaryWorldFile(fs, bin_path, ctx)) {
      return true;
    }
    if (has_bin) {
      util::log::kSaveLoad.Warn(
          "Primary binary save failed validation in " + ctx.world_dir +
          (has_bak ? "; attempting backup recovery" : ""));
    }
    if (has_bak && TryLoadBinaryWorldFile(fs, bak_path, ctx)) {
      util::log::kSaveLoad.Warn("Recovered world from backup: " + bak_path);
      return true;
    }
    util::log::kSaveLoad.Error("Binary save corrupt and unrecoverable in " +
                               ctx.world_dir);
    return false;
  }

  // Delegate legacy Java `.miniplussave` / text world folders to LegacyLoad.
  if (LegacyLoad::IsLegacyWorldDir(fs, ctx.world_dir)) {
    return LegacyLoad::LoadWorld(fs, ctx);
  }

  util::log::kSaveLoad.Error("No valid world save found in " + ctx.world_dir);
  return false;
}

}  // namespace minicraft::saveload
