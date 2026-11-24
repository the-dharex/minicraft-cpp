#include "core/saveload/save.h"

#include <algorithm>
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
#include "core/item/recipes.h"
#include "core/level/chunk_manager.h"
#include "core/level/level.h"
#include "core/level/tiles.h"
#include "core/pak/fast_compress.h"
#include "core/platform/file_system.h"
#include "core/saveload/binary_codec.h"
#include "core/saveload/version.h"
#include "core/util/advancements.h"
#include "core/util/localization.h"
#include "core/util/logging.h"

namespace minicraft::saveload {

namespace {

void WriteBinaryItem(BinaryWriter &w, const item::Item *it, int32_t depth = 0) {
  if (!it || depth > kMaxNestedItemDepth) {
    w.WriteU8(0);
    return;
  }
  w.WriteU8(1);
  w.WriteStr16(it->Name());

  if (const auto *st = dynamic_cast<const item::StackableItem *>(it)) {
    w.WriteU8(1);
    w.WriteI32(st->count);
  } else if (const auto *tool = dynamic_cast<const item::ToolItem *>(it)) {
    w.WriteU8(2);
    w.WriteI32(tool->dur);
  } else if (const auto *wc = dynamic_cast<const item::WateringCanItem *>(it)) {
    w.WriteU8(3);
    w.WriteI32(wc->content);
  } else if (const auto *rod = dynamic_cast<const item::FishingRodItem *>(it)) {
    w.WriteU8(4);
    w.WriteI32(rod->uses);
  } else if (const auto *furn = dynamic_cast<const item::FurnitureItem *>(it);
             furn != nullptr && furn->furniture != nullptr) {
    if (const auto *ch =
            dynamic_cast<const entity::Chest *>(furn->furniture.get())) {
      w.WriteU8(5);
      if (const auto *dc = dynamic_cast<const entity::DeathChest *>(ch)) {
        w.WriteU8(1);
        w.WriteI32(dc->Time());
      } else if (const auto *dnc =
                     dynamic_cast<const entity::DungeonChest *>(ch)) {
        w.WriteU8(2);
        w.WriteU8(dnc->is_locked ? 1u : 0u);
      } else {
        w.WriteU8(0);
      }
      const auto inv = ch->GetInventory();
      const uint32_t cnt = inv ? static_cast<uint32_t>(std::min<size_t>(
                                     inv->InvSize(), kMaxInventoryItems))
                               : 0u;
      w.WriteU32(cnt);
      for (uint32_t i = 0; i < cnt; ++i) {
        WriteBinaryItem(w, inv->Get(i), depth + 1);
      }
    } else {
      w.WriteU8(0);
    }
  } else {
    w.WriteU8(0);
  }
}

bool ClassifyBinaryEntity(const entity::Entity &e, BinaryEntityKind *out_kind) {
  const std::string name = e.GetEntityTypeName();
  if (name == "Player" || name == "Particle" || name == "FireParticle" ||
      name == "BurnParticle" || name == "SmashParticle" ||
      name == "SandParticle" || name == "WaterParticle" ||
      name == "TextParticle" || name == "Arrow" || name == "Spark" ||
      name == "FireSpark" || name == "ItemEntity" ||
      name == "ExplosionTileTicker") {
    return false;
  }

  if (name == "Cow") {
    *out_kind = BinaryEntityKind::kCow;
    return true;
  }
  if (name == "Pig") {
    *out_kind = BinaryEntityKind::kPig;
    return true;
  }
  if (name == "Sheep") {
    *out_kind = BinaryEntityKind::kSheep;
    return true;
  }
  if (name == "Zombie") {
    *out_kind = BinaryEntityKind::kZombie;
    return true;
  }
  if (name == "Slime") {
    *out_kind = BinaryEntityKind::kSlime;
    return true;
  }
  if (name == "Creeper") {
    *out_kind = BinaryEntityKind::kCreeper;
    return true;
  }
  if (name == "Skeleton") {
    *out_kind = BinaryEntityKind::kSkeleton;
    return true;
  }
  if (name == "Knight") {
    *out_kind = BinaryEntityKind::kKnight;
    return true;
  }
  if (name == "Snake") {
    *out_kind = BinaryEntityKind::kSnake;
    return true;
  }
  if (name == "AirWizard") {
    *out_kind = BinaryEntityKind::kAirWizard;
    return true;
  }
  if (name == "ObsidianKnight") {
    *out_kind = BinaryEntityKind::kObsidianKnight;
    return true;
  }
  if (name == "Spawner") {
    *out_kind = BinaryEntityKind::kSpawner;
    return true;
  }
  if (name == "Workbench") {
    *out_kind = BinaryEntityKind::kWorkbench;
    return true;
  }
  if (name == "Anvil") {
    *out_kind = BinaryEntityKind::kAnvil;
    return true;
  }
  if (name == "Enchanter") {
    *out_kind = BinaryEntityKind::kEnchanter;
    return true;
  }
  if (name == "Loom") {
    *out_kind = BinaryEntityKind::kLoom;
    return true;
  }
  if (name == "Furnace") {
    *out_kind = BinaryEntityKind::kFurnace;
    return true;
  }
  if (name == "Oven") {
    *out_kind = BinaryEntityKind::kOven;
    return true;
  }
  if (name == "DyeVat") {
    *out_kind = BinaryEntityKind::kDyeVat;
    return true;
  }
  if (name == "Composter") {
    *out_kind = BinaryEntityKind::kComposter;
    return true;
  }
  if (name == "DeathChest") {
    *out_kind = BinaryEntityKind::kDeathChest;
    return true;
  }
  if (name == "DungeonChest") {
    *out_kind = BinaryEntityKind::kDungeonChest;
    return true;
  }
  if (name == "Chest") {
    *out_kind = BinaryEntityKind::kChest;
    return true;
  }
  if (dynamic_cast<const entity::Bed *>(&e) != nullptr || name == "Bed") {
    *out_kind = BinaryEntityKind::kBed;
    return true;
  }
  if (name == "Tnt") {
    *out_kind = BinaryEntityKind::kTnt;
    return true;
  }
  if (name == "Lantern") {
    *out_kind = BinaryEntityKind::kLantern;
    return true;
  }
  if (name == "KnightStatue") {
    *out_kind = BinaryEntityKind::kKnightStatue;
    return true;
  }
  if (name == "Boat") {
    *out_kind = BinaryEntityKind::kBoat;
    return true;
  }
  return false;
}

void WriteBinaryEntity(BinaryWriter &w, const entity::Entity &e,
                       BinaryEntityKind kind, uint8_t level_idx) {
  w.WriteU8(static_cast<uint8_t>(kind));
  w.WriteU8(level_idx);
  w.WriteI32(e.x);
  w.WriteI32(e.y);

  switch (kind) {
  case BinaryEntityKind::kCow:
  case BinaryEntityKind::kPig:
  case BinaryEntityKind::kAirWizard: {
    const auto *mob = static_cast<const entity::Mob *>(&e);
    w.WriteI32(mob->health);
    break;
  }
  case BinaryEntityKind::kSheep: {
    const auto *sheep = static_cast<const entity::Sheep *>(&e);
    w.WriteI32(sheep->health);
    w.WriteU8(sheep->cut ? 1u : 0u);
    w.WriteU8(static_cast<uint8_t>(sheep->color));
    break;
  }
  case BinaryEntityKind::kZombie:
  case BinaryEntityKind::kSlime:
  case BinaryEntityKind::kCreeper:
  case BinaryEntityKind::kSkeleton:
  case BinaryEntityKind::kKnight:
  case BinaryEntityKind::kSnake: {
    const auto *enemy = static_cast<const entity::EnemyMob *>(&e);
    w.WriteI32(enemy->health);
    w.WriteU8(static_cast<uint8_t>(std::clamp(enemy->lvl, 1, 4)));
    break;
  }
  case BinaryEntityKind::kObsidianKnight: {
    const auto *ok = static_cast<const entity::ObsidianKnight *>(&e);
    w.WriteI32(ok->health);
    w.WriteU8(static_cast<uint8_t>(std::clamp(ok->Phase(), 0, 1)));
    break;
  }
  case BinaryEntityKind::kSpawner: {
    const auto *sp = static_cast<const entity::Spawner *>(&e);
    w.WriteStr16(sp->MobType());
    w.WriteU8(static_cast<uint8_t>(std::clamp(sp->MobLvl(), 1, 4)));
    break;
  }
  case BinaryEntityKind::kWorkbench:
  case BinaryEntityKind::kAnvil:
  case BinaryEntityKind::kEnchanter:
  case BinaryEntityKind::kLoom:
  case BinaryEntityKind::kFurnace:
  case BinaryEntityKind::kOven:
  case BinaryEntityKind::kDyeVat:
  case BinaryEntityKind::kTnt:
    break;
  case BinaryEntityKind::kComposter: {
    const auto *comp = static_cast<const entity::Composter *>(&e);
    w.WriteI32(comp->compost);
    break;
  }
  case BinaryEntityKind::kChest:
  case BinaryEntityKind::kDeathChest:
  case BinaryEntityKind::kDungeonChest: {
    const auto *ch = static_cast<const entity::Chest *>(&e);
    if (kind == BinaryEntityKind::kDeathChest) {
      w.WriteI32(static_cast<const entity::DeathChest *>(&e)->Time());
    } else if (kind == BinaryEntityKind::kDungeonChest) {
      w.WriteU8(static_cast<const entity::DungeonChest *>(&e)->is_locked ? 1u
                                                                         : 0u);
    }
    const auto inv = ch->GetInventory();
    const uint32_t cnt = inv ? static_cast<uint32_t>(std::min<size_t>(
                                   inv->InvSize(), kMaxInventoryItems))
                             : 0u;
    w.WriteU32(cnt);
    for (uint32_t i = 0; i < cnt; ++i) {
      WriteBinaryItem(w, inv->Get(i), 1);
    }
    break;
  }
  case BinaryEntityKind::kBed: {
    const auto *bed = static_cast<const entity::Bed *>(&e);
    w.WriteStr16(bed->ColorName());
    break;
  }
  case BinaryEntityKind::kLantern: {
    const auto *lnt = static_cast<const entity::Lantern *>(&e);
    w.WriteU8(static_cast<uint8_t>(lnt->GetType()));
    break;
  }
  case BinaryEntityKind::kKnightStatue: {
    const auto *st = static_cast<const entity::KnightStatue *>(&e);
    w.WriteI32(st->BossHp());
    break;
  }
  case BinaryEntityKind::kBoat: {
    const auto *boat = static_cast<const entity::Boat *>(&e);
    w.WriteU8(static_cast<uint8_t>(boat->Dir()));
    break;
  }
  }
}

// Verifies whether an existing binary file on disk has a valid header and
// payload CRC before rotating it to `.bak`, preventing a corrupted file from
// overwriting a healthy backup.
bool VerifyExistingBinarySave(const platform::IFileSystem &fs,
                              std::string_view path) {
  std::vector<uint8_t> bytes;
  if (!fs.ReadBinaryFile(path, &bytes))
    return false;
  BinaryPreamble pre;
  if (!ParseBinaryPreambleAndHeader(bytes.data(), bytes.size(), &pre,
                                    nullptr)) {
    return false;
  }
  const uint64_t expected_total = static_cast<uint64_t>(kBinaryPreambleSize) +
                                  pre.header_size + pre.payload_stored_size;
  if (bytes.size() != expected_total)
    return false;
  const uint8_t *payload_ptr =
      bytes.data() + kBinaryPreambleSize + pre.header_size;
  return ComputeCrc32(payload_ptr, pre.payload_stored_size) ==
         pre.payload_stored_crc32;
}

void CleanupLegacyTextSaveArtifacts(const platform::IFileSystem &fs,
                                    std::string_view world_dir) {
  static constexpr const char *kLegacyFiles[] = {
      "Game.miniplussave",     "Player.miniplussave", "Inventory.miniplussave",
      "Entities.miniplussave", "advancements.json",   "signs.json",
  };
  for (const char *f : kLegacyFiles) {
    const std::string p = Save::JoinWorldPath(world_dir, f);
    if (fs.Exists(p))
      fs.RemoveFile(p);
  }
  for (int32_t i = 0; i < 6; ++i) {
    const std::string ls = "Level" + std::to_string(i);
    const std::string flat_t =
        Save::JoinWorldPath(world_dir, ls + ".miniplussave");
    const std::string flat_d =
        Save::JoinWorldPath(world_dir, ls + "data.miniplussave");
    const std::string lvl_dir = Save::JoinWorldPath(world_dir, ls);
    if (fs.Exists(flat_t))
      fs.RemoveFile(flat_t);
    if (fs.Exists(flat_d))
      fs.RemoveFile(flat_d);
    if (fs.IsDirectory(lvl_dir))
      fs.RemoveDirectoryAll(lvl_dir);
  }
}

} // namespace

std::string Save::JoinWorldPath(std::string_view base, std::string_view sub) {
  std::string out(base);
  if (!out.empty() && out.back() != '/' && out.back() != '\\') {
    out.push_back('/');
  }
  out.append(sub);
  return out;
}

std::vector<std::string> Save::WriteInventory(const item::Inventory &inv,
                                              const entity::Player *player) {
  std::vector<std::string> lines;
  lines.reserve(inv.InvSize() + 1);
  if (player && player->active_item) {
    lines.push_back("ACTIVE:" + player->active_item->GetData());
  }
  for (size_t i = 0; i < inv.InvSize(); ++i) {
    const item::Item *it = inv.Get(i);
    if (it) {
      lines.push_back(it->GetData());
    }
  }
  return lines;
}

std::string Save::WriteEntity(const entity::Entity &e, int32_t level_idx,
                              bool is_local_save) {
  (void)is_local_save;
  std::string name = e.GetEntityTypeName();
  if (name == "Player" || name == "Particle" || name == "FireParticle" ||
      name == "BurnParticle" || name == "SmashParticle" ||
      name == "SandParticle" || name == "WaterParticle" ||
      name == "TextParticle" || name == "Arrow" || name == "Spark" ||
      name == "FireSpark" || name == "ItemEntity" ||
      name == "ExplosionTileTicker") {
    return {};
  }

  std::vector<std::string> parts;
  parts.push_back(std::to_string(e.x));
  parts.push_back(std::to_string(e.y));

  if (const auto *mob = dynamic_cast<const entity::Mob *>(&e)) {
    parts.push_back(std::to_string(mob->health));
    if (const auto *enemy = dynamic_cast<const entity::EnemyMob *>(&e)) {
      parts.push_back(std::to_string(enemy->lvl));
    } else if (name == "AirWizard") {
      parts.push_back("1");
    } else if (const auto *knight =
                   dynamic_cast<const entity::ObsidianKnight *>(&e)) {
      parts.push_back(std::to_string(knight->Phase()));
    } else if (const auto *sheep = dynamic_cast<const entity::Sheep *>(&e)) {
      parts.push_back(sheep->cut ? "true" : "false");
    }
  } else if (const auto *bed = dynamic_cast<const entity::Bed *>(&e)) {
    name = bed->ColorName() + " Bed";
  } else if (const auto *chest = dynamic_cast<const entity::Chest *>(&e)) {
    if (const auto inv = chest->GetInventory()) {
      for (size_t i = 0; i < inv->InvSize(); ++i) {
        const item::Item *it = inv->Get(i);
        if (it)
          parts.push_back(it->GetData());
      }
    }
    if (const auto *dc = dynamic_cast<const entity::DeathChest *>(&e)) {
      parts.push_back(std::to_string(dc->Time()));
    } else if (const auto *dnc =
                   dynamic_cast<const entity::DungeonChest *>(&e)) {
      parts.push_back(dnc->is_locked ? "true" : "false");
    }
  } else if (const auto *spawner = dynamic_cast<const entity::Spawner *>(&e)) {
    parts.push_back(spawner->MobType());
    parts.push_back(std::to_string(spawner->MobLvl()));
  } else if (const auto *lantern = dynamic_cast<const entity::Lantern *>(&e)) {
    parts.push_back(std::to_string(static_cast<int32_t>(lantern->GetType())));
  } else if (const auto *statue =
                 dynamic_cast<const entity::KnightStatue *>(&e)) {
    parts.push_back(std::to_string(statue->BossHp()));
  } else if (const auto *boat = dynamic_cast<const entity::Boat *>(&e)) {
    parts.push_back(std::to_string(static_cast<int32_t>(boat->Dir())));
  }

  parts.push_back(std::to_string(level_idx));

  std::string line = name + "[";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0)
      line.push_back(':');
    line += parts[i];
  }
  line.push_back(']');
  return line;
}

bool Save::CaptureWorldSnapshot(WorldSaveContext &ctx, WorldSaveSnapshot *out) {
  if (!out || ctx.world_dir.empty() || !ctx.levels || !ctx.player ||
      !*ctx.player) {
    return false;
  }

  auto &gs = game::GameState::Instance();
  auto report = [&](std::string_view msg) {
    if (ctx.on_progress)
      ctx.on_progress(std::string(msg));
  };

  WorldSaveSnapshot snap;
  snap.world_dir = ctx.world_dir;
  snap.directories.push_back(ctx.world_dir);

  const auto &player = **ctx.player;
  const int32_t cur_lvl_idx =
      std::clamp(ctx.current_level_idx ? *ctx.current_level_idx : 3, 0, 5);

  // Build compact metadata header (also read independently by ListWorlds).
  report("minicraft.displays.loading.message.world");
  BinaryWorldHeader hdr;
  hdr.version_str = std::string(Version::kCurrentVersion);
  hdr.mode_str = io::Settings::GetString("mode");
  hdr.diff_str = io::Settings::GetString("diff");
  hdr.score = player.GetScore();
  hdr.game_time = gs.game_time;
  hdr.tick_count = gs.tick_count;
  hdr.score_time = gs.score_time;
  hdr.scoretime_setting = io::Settings::GetInt("scoretime");
  hdr.saved_lvl_idx = static_cast<uint8_t>(cur_lvl_idx);
  hdr.world_flags = 0;
  if (gs.air_wizard_beaten)
    hdr.world_flags |= kWorldFlagAirWizardBeaten;
  if (gs.obsidian_knight_beaten) {
    hdr.world_flags |= kWorldFlagObsidianKnightBeaten;
  }
  if (gs.obsidian_knight_summoned) {
    hdr.world_flags |= kWorldFlagObsidianKnightSummoned;
  }
  if (gs.past_day1)
    hdr.world_flags |= kWorldFlagPastDay1;
  if (io::Settings::GetBool("quests"))
    hdr.world_flags |= kWorldFlagQuests;
  if (io::Settings::GetBool("tutorials")) {
    hdr.world_flags |= kWorldFlagTutorials;
  }

  const std::vector<uint8_t> hdr_bytes = SerializeBinaryWorldHeader(hdr);
  const uint32_t hdr_crc = ComputeCrc32(hdr_bytes.data(), hdr_bytes.size());

  // Build binary payload.
  BinaryWriter pw(128 * 1024);

  // Tile ID -> Name palette for forward compatibility.
  level::Tiles::InitTiles();
  std::vector<std::pair<uint16_t, std::string_view>> palette;
  palette.reserve(80);
  for (uint16_t tid = 0; tid < 256; ++tid) {
    if (level::Tiles::ContainsTile(tid)) {
      const level::Tile *t = level::Tiles::Get(tid);
      if (t)
        palette.emplace_back(tid, t->name);
    }
  }
  pw.WriteU16(static_cast<uint16_t>(palette.size()));
  for (const auto &[tid, tname] : palette) {
    pw.WriteU16(tid);
    pw.WriteStr16(tname);
  }

  // Levels 0..5 with deterministic chunk ordering & uniform/zero flags.
  report("minicraft.displays.loading.message.levels");
  for (int32_t i = 0; i < 6; ++i) {
    const auto &lvl = (*ctx.levels)[i];
    if (!lvl) {
      pw.WriteU8(0);
      continue;
    }
    pw.WriteU8(1);
    pw.WriteI32(lvl->w);
    pw.WriteI32(lvl->h);
    pw.WriteI64(lvl->seed);
    pw.WriteI32(lvl->depth);

    auto &cm = lvl->GetChunkManager();
    std::vector<level::Chunk *> sorted_chunks;
    sorted_chunks.reserve(cm.ChunksMutable().size());
    for (auto &[key, chunk_ptr] : cm.ChunksMutable()) {
      (void)key;
      if (chunk_ptr) {
        chunk_ptr->dirty = false;
        sorted_chunks.push_back(chunk_ptr.get());
      }
    }
    std::sort(sorted_chunks.begin(), sorted_chunks.end(),
              [](const level::Chunk *a, const level::Chunk *b) {
                return std::tie(a->chunk_x, a->chunk_y) <
                       std::tie(b->chunk_x, b->chunk_y);
              });

    const uint32_t ccount = static_cast<uint32_t>(
        std::min<size_t>(sorted_chunks.size(), kMaxChunksPerLevel));
    pw.WriteU32(ccount);
    for (uint32_t ci = 0; ci < ccount; ++ci) {
      const level::Chunk *ch = sorted_chunks[ci];
      pw.WriteI32(ch->chunk_x);
      pw.WriteI32(ch->chunk_y);
      pw.WriteI32(ch->stage);

      const uint16_t first_tid = ch->tile_ids[0];
      bool uniform_tiles = true;
      for (size_t k = 1; k < level::Chunk::kArea; ++k) {
        if (ch->tile_ids[k] != first_tid) {
          uniform_tiles = false;
          break;
        }
      }
      bool zero_data = true;
      for (size_t k = 0; k < level::Chunk::kArea; ++k) {
        if (ch->tile_data[k] != 0) {
          zero_data = false;
          break;
        }
      }

      uint8_t cflags = 0;
      if (uniform_tiles)
        cflags |= kChunkFlagTilesUniform;
      if (zero_data)
        cflags |= kChunkFlagDataAllZero;
      pw.WriteU8(cflags);

      if (uniform_tiles) {
        pw.WriteU16(first_tid);
      } else {
        for (size_t k = 0; k < level::Chunk::kArea; ++k) {
          pw.WriteU16(ch->tile_ids[k]);
        }
      }
      if (!zero_data) {
        for (size_t k = 0; k < level::Chunk::kArea; ++k) {
          pw.WriteI16(ch->tile_data[k]);
        }
      }
    }
  }

  // Player state, active potions, unlocked recipes, and inventory.
  report("minicraft.displays.loading.message.entities");
  pw.WriteI32(player.x);
  pw.WriteI32(player.y);
  pw.WriteI32(player.spawnx);
  pw.WriteI32(player.spawny);
  pw.WriteI32(player.spawn_depth);
  pw.WriteI32(player.health);
  pw.WriteI32(player.max_health);
  pw.WriteI32(player.hunger);
  pw.WriteI32(player.stamina);
  pw.WriteI32(player.armor);
  pw.WriteI32(player.armor_damage_buffer);
  pw.WriteI32(player.shirt_color);
  pw.WriteU8(player.skinon ? 1u : 0u);
  pw.WriteI32(player.GetScore());
  pw.WriteU8(static_cast<uint8_t>(cur_lvl_idx));

  WriteBinaryItem(pw, player.cur_armor.get());
  WriteBinaryItem(pw, player.active_item.get());

  std::vector<std::pair<int32_t, int32_t>> active_potions;
  for (const auto &[eid, dur] : player.potion_effects) {
    if (dur > 0)
      active_potions.emplace_back(eid, dur);
  }
  std::sort(active_potions.begin(), active_potions.end());
  const uint16_t pcount =
      static_cast<uint16_t>(std::min<size_t>(active_potions.size(), 64u));
  pw.WriteU16(pcount);
  for (uint16_t pi = 0; pi < pcount; ++pi) {
    pw.WriteI32(active_potions[pi].first);
    pw.WriteI32(active_potions[pi].second);
  }

  if (ctx.unlocked_recipes) {
    const uint32_t rcount = static_cast<uint32_t>(
        std::min<size_t>(ctx.unlocked_recipes->size(), 4096u));
    pw.WriteU32(rcount);
    for (uint32_t ri = 0; ri < rcount; ++ri) {
      const auto &r = (*ctx.unlocked_recipes)[ri];
      auto prod = r.GetProduct();
      const std::string prod_name = prod ? prod->Name() : r.Product();
      pw.WriteStr16(prod_name + "_" + std::to_string(r.Amount()));
      const auto &costs = r.Costs();
      const uint16_t cost_cnt =
          static_cast<uint16_t>(std::min<size_t>(costs.size(), 64u));
      pw.WriteU16(cost_cnt);
      uint16_t written_costs = 0;
      for (const auto &[cost_name, cost_amt] : costs) {
        if (written_costs >= cost_cnt)
          break;
        pw.WriteStr16(cost_name + "_" + std::to_string(cost_amt));
        ++written_costs;
      }
    }
  } else {
    pw.WriteU32(0);
  }

  const auto &inv = player.GetInventory();
  const uint32_t inv_cnt = static_cast<uint32_t>(
      std::min<size_t>(inv.InvSize(), kMaxInventoryItems));
  pw.WriteU32(inv_cnt);
  for (uint32_t ii = 0; ii < inv_cnt; ++ii) {
    WriteBinaryItem(pw, inv.Get(ii));
  }

  // World entities across all 6 levels.
  struct PendingEntityRef {
    const entity::Entity *ptr;
    BinaryEntityKind kind;
    uint8_t lvl_idx;
  };
  std::vector<PendingEntityRef> ent_refs;
  for (int32_t i = 0; i < 6; ++i) {
    const auto &lvl = (*ctx.levels)[i];
    if (!lvl)
      continue;
    for (const auto &e : lvl->Entities()) {
      if (!e || e->removed)
        continue;
      BinaryEntityKind kind{};
      if (ClassifyBinaryEntity(*e, &kind)) {
        ent_refs.push_back({e.get(), kind, static_cast<uint8_t>(i)});
      }
    }
  }
  const uint32_t ent_cnt = static_cast<uint32_t>(
      std::min<size_t>(ent_refs.size(), kMaxWorldEntities));
  pw.WriteU32(ent_cnt);
  for (uint32_t ei = 0; ei < ent_cnt; ++ei) {
    WriteBinaryEntity(pw, *ent_refs[ei].ptr, ent_refs[ei].kind,
                      ent_refs[ei].lvl_idx);
  }

  // Advancements & signs.
  pw.WriteStr32(util::AdvancementSystem::Instance().SaveAdvancementsJson());
  if (ctx.signs) {
    const uint32_t sign_cnt = static_cast<uint32_t>(
        std::min<size_t>(ctx.signs->size(), kMaxWorldSigns));
    pw.WriteU32(sign_cnt);
    uint32_t written_signs = 0;
    for (const auto &[coord, lines] : *ctx.signs) {
      if (written_signs >= sign_cnt)
        break;
      pw.WriteI32(coord.level);
      pw.WriteI32(coord.x);
      pw.WriteI32(coord.y);
      const uint8_t lcnt =
          static_cast<uint8_t>(std::min<size_t>(lines.size(), kMaxSignLines));
      pw.WriteU8(lcnt);
      for (uint8_t li = 0; li < lcnt; ++li) {
        pw.WriteStr16(lines[li]);
      }
      ++written_signs;
    }
  } else {
    pw.WriteU32(0);
  }

  const std::vector<uint8_t> raw_payload = pw.TakeBuffer();
  if (raw_payload.size() > kMaxBinaryPayloadSize ||
      hdr_bytes.size() > kMaxBinaryHeaderSize) {
    util::log::kSaveLoad.Error("World snapshot exceeds safe binary bounds");
    return false;
  }

  const uint32_t raw_crc = ComputeCrc32(raw_payload.data(), raw_payload.size());
  std::vector<uint8_t> packed_payload =
      pak::CompressFastLz(raw_payload.data(), raw_payload.size());

  const bool use_compressed =
      !packed_payload.empty() && packed_payload.size() < raw_payload.size();
  const std::vector<uint8_t> &stored_payload =
      use_compressed ? packed_payload : raw_payload;
  const uint32_t stored_crc =
      use_compressed
          ? ComputeCrc32(stored_payload.data(), stored_payload.size())
          : raw_crc;

  // Assemble final container (`kBinaryPreambleSize` + header + payload).
  BinaryWriter container(kBinaryPreambleSize + hdr_bytes.size() +
                         stored_payload.size());
  container.WriteBytes(kBinaryMagic.data(), kBinaryMagic.size());
  container.WriteU32(kBinaryFormatVersion);
  container.WriteU32(use_compressed ? kContainerFlagCompressed : 0u);
  container.WriteU32(static_cast<uint32_t>(hdr_bytes.size()));
  container.WriteU32(hdr_crc);
  container.WriteU32(static_cast<uint32_t>(stored_payload.size()));
  container.WriteU32(static_cast<uint32_t>(raw_payload.size()));
  container.WriteU32(stored_crc);
  container.WriteU32(raw_crc);
  container.WriteBytes(hdr_bytes.data(), hdr_bytes.size());
  container.WriteBytes(stored_payload.data(), stored_payload.size());

  snap.binary_world = container.TakeBuffer();
  *out = std::move(snap);
  return true;
}

bool Save::WriteWorldSnapshot(const platform::IFileSystem &fs,
                              const WorldSaveSnapshot &snapshot,
                              bool notify_on_complete,
                              const std::function<void(float)> &on_percent) {
  auto &gs = game::GameState::Instance();
  gs.saving = true;

  if (snapshot.world_dir.empty()) {
    gs.saving = false;
    return false;
  }

  fs.CreateDirectories(snapshot.world_dir);
  for (const auto &dir : snapshot.directories) {
    fs.CreateDirectories(dir);
  }

  if (!snapshot.binary_world.empty()) {
    const std::string final_path =
        JoinWorldPath(snapshot.world_dir, kBinaryWorldFileName);
    const std::string bak_path =
        JoinWorldPath(snapshot.world_dir, kBinaryWorldBackupFileName);
    const std::string tmp_path =
        JoinWorldPath(snapshot.world_dir, kBinaryWorldTempFileName);

    if (on_percent)
      on_percent(20.0f);

    // Write complete binary image to temporary file first so a crash or I/O
    // failure never truncates or corrupts the live save.
    if (!fs.WriteBinaryFile(tmp_path, snapshot.binary_world.data(),
                            snapshot.binary_world.size())) {
      util::log::kSaveLoad.Error("Failed writing temporary binary save: " +
                                 tmp_path);
      fs.RemoveFile(tmp_path);
      gs.saving = false;
      return false;
    }

    if (on_percent)
      on_percent(70.0f);

    // Rotate existing healthy `world.mcbin` -> `world.mcbin.bak` before
    // promoting `.tmp` -> `world.mcbin`.
    bool rotated_to_bak = false;
    if (fs.Exists(final_path)) {
      if (VerifyExistingBinarySave(fs, final_path)) {
        rotated_to_bak = fs.Rename(final_path, bak_path);
      }
    }

    // 3. Atomically promote `world.mcbin.tmp` -> `world.mcbin`.
    if (!fs.Rename(tmp_path, final_path)) {
      util::log::kSaveLoad.Error("Failed promoting binary save: " + final_path);
      if (rotated_to_bak && !fs.Exists(final_path) && fs.Exists(bak_path)) {
        fs.Rename(bak_path, final_path);
      }
      fs.RemoveFile(tmp_path);
      gs.saving = false;
      return false;
    }

    // Clean up legacy text `.miniplussave` artifacts if this folder was
    // migrated from a Java or older text save.
    CleanupLegacyTextSaveArtifacts(fs, snapshot.world_dir);

    if (on_percent)
      on_percent(100.0f);
  }

  // Support any auxiliary explicit file entries if present.
  const size_t total = snapshot.files.size();
  for (size_t i = 0; i < total; ++i) {
    const auto &entry = snapshot.files[i];
    const bool ok =
        entry.is_binary
            ? fs.WriteBinaryFile(entry.path, entry.binary_content.data(),
                                 entry.binary_content.size())
            : fs.WriteTextFile(entry.path, entry.content);
    if (!ok && entry.critical) {
      util::log::kSaveLoad.Error("Failed writing critical world file: " +
                                 entry.path);
      gs.saving = false;
      return false;
    }
    if (on_percent && snapshot.binary_world.empty() && total > 0) {
      on_percent(100.0f * static_cast<float>(i + 1) /
                 static_cast<float>(total));
    }
  }

  gs.saving = false;
  if (notify_on_complete) {
    gs.NotifyAll("minicraft.notification.world_saved");
  }
  return true;
}

bool Save::SaveWorld(const platform::IFileSystem &fs, WorldSaveContext &ctx) {
  auto &gs = game::GameState::Instance();
  gs.saving = true;

  WorldSaveSnapshot snapshot;
  if (!CaptureWorldSnapshot(ctx, &snapshot)) {
    gs.saving = false;
    return false;
  }

  return WriteWorldSnapshot(fs, snapshot, true);
}

bool Save::SavePrefs(const platform::IFileSystem &fs, std::string_view game_dir,
                     const io::InputHandler *input,
                     const std::vector<std::string> *resource_packs) {
  if (game_dir.empty())
    return false;
  fs.CreateDirectories(game_dir);

  auto &gs = game::GameState::Instance();

  nlohmann::json prefs;
  prefs["version"] = std::string(Version::kCurrentVersion);
  prefs["sound"] = io::Settings::GetBool("sound");
  prefs["autosave"] = io::Settings::GetBool("autosave");
  prefs["fps"] = io::Settings::GetMaxFps();
  prefs["lang"] = util::Localization::Instance().SelectedLocaleTag();
  prefs["diff"] = io::Settings::GetString("diff");
  prefs["fullscreen"] = io::Settings::GetBool("fullscreen");
  prefs["tutorials"] = io::Settings::GetBool("tutorials");
  prefs["showquests"] = io::Settings::GetBool("showquests");
  prefs["skinIdx"] = gs.selected_skin_index;
  prefs["skin"] = gs.selected_skin_name;

  if (input) {
    prefs["keymap"] = input->GetKeyPrefs();
  }
  if (resource_packs) {
    prefs["resourcePacks"] = *resource_packs;
  }

  const bool ok_prefs = fs.WriteTextFile(
      JoinWorldPath(game_dir, "Preferences.json"), prefs.dump(2));

  nlohmann::json unlocks;
  const auto unlocked_ids =
      util::AchievementManager::Instance().GetUnlockedIds();
  unlocks["achievements"] = unlocked_ids;
  unlocks["unlockedAchievements"] = unlocked_ids;
  try {
    unlocks["unlockedRecipes"] = nlohmann::json::parse(
        util::AdvancementSystem::Instance().SaveRecipeUnlocksJson());
  } catch (...) {
    unlocks["unlockedRecipes"] = nlohmann::json::object();
  }
  nlohmann::json visible_score_times = nlohmann::json::array();
  if (const auto *st_entry = io::Settings::GetEntry("scoretime")) {
    for (size_t i = 0; i < st_entry->int_options.size(); ++i) {
      if (st_entry->IsOptionVisible(i)) {
        visible_score_times.push_back(st_entry->int_options[i]);
      }
    }
  }
  unlocks["visibleScoreTimes"] = std::move(visible_score_times);

  const bool ok_unlocks = fs.WriteTextFile(
      JoinWorldPath(game_dir, "Unlocks.json"), unlocks.dump(2));

  return ok_prefs && ok_unlocks;
}

} // namespace minicraft::saveload
