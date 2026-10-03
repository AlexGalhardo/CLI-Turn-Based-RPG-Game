#include "infrastructure/data_loader.hpp"

#include <format>

#include <nlohmann/json.hpp>

namespace rpg::infrastructure {

// ordered_json keeps the key order of the file, which matters for item stats.
using Json = nlohmann::ordered_json;

namespace {

std::int64_t integer(const Json& document, const char* key) { return document.at(key).get<std::int64_t>(); }

std::string text(const Json& document, const char* key) { return document.at(key).get<std::string>(); }

std::vector<std::string> strings(const Json& document, const char* key) {
	return document.at(key).get<std::vector<std::string>>();
}

std::optional<std::string> optional_text(const Json& document, const char* key) {
	const auto found = document.find(key);
	if (found == document.end() || found->is_null()) {
		return std::nullopt;
	}
	return found->get<std::string>();
}

std::vector<domain::MonsterAttack> attacks(const Json& raw) {
	std::vector<domain::MonsterAttack> result;
	for (const Json& attack : raw) {
		domain::MonsterAttack converted{
		    .id = text(attack, "id"),
		    .element = text(attack, "element"),
		    .min = integer(attack, "min"),
		    .max = integer(attack, "max"),
		    .weight = integer(attack, "weight"),
		    .status = std::nullopt,
		};
		if (const auto status = attack.find("status"); status != attack.end() && !status->is_null()) {
			converted.status =
			    domain::StatusOnHit{text(*status, "id"), integer(*status, "chance"), integer(*status, "damagePct")};
		}
		result.push_back(std::move(converted));
	}
	return result;
}

std::vector<domain::MonsterDef> creatures(const Json& raw, bool is_boss) {
	std::vector<domain::MonsterDef> result;
	for (const Json& creature : raw) {
		domain::MonsterDef converted{
		    .id = text(creature, "id"),
		    .name = text(creature, "name"),
		    .tier = integer(creature, "tier"),
		    .family = text(creature, "family"),
		    .hp = integer(creature, "hp"),
		    .xp = integer(creature, "xp"),
		    .gold_min = integer(creature.at("gold"), "min"),
		    .gold_max = integer(creature.at("gold"), "max"),
		    .attacks = attacks(creature.at("attacks")),
		    .resistances = {},
		    .is_boss = is_boss,
		    .charge_attack = optional_text(creature, "chargeAttack"),
		};
		if (const auto resistances = creature.find("resistances"); resistances != creature.end()) {
			for (const auto& [element, value] : resistances->items()) {
				converted.resistances.emplace(element, value.get<std::int64_t>());
			}
		}
		result.push_back(std::move(converted));
	}
	return result;
}

domain::RarityWeights weights_of(const Json& raw) {
	domain::RarityWeights result;
	for (const auto& [rarity, weight] : raw.items()) {
		result.emplace(rarity, weight.get<std::int64_t>());
	}
	return result;
}

domain::EnemyClassDef enemy_class(std::string_view class_id, const Json& raw) {
	return domain::EnemyClassDef{
	    .id = std::string(class_id),
	    .stat_pct = integer(raw, "statPct"),
	    .reward_pct = integer(raw, "rewardPct"),
	    .dodge = integer(raw, "dodge"),
	    .parry = integer(raw, "parry"),
	    .crit = integer(raw, "crit"),
	    .heal = integer(raw, "heal"),
	    .drop_chance_pct = integer(raw, "dropChancePct"),
	    .drops = integer(raw, "drops"),
	    .potion_drop_pct = integer(raw, "potionDropPct"),
	    .rarity_weights = weights_of(raw.at("rarityWeights")),
	};
}

domain::AutoBattleDef auto_battle(const Json& raw) {
	domain::AutoBattleDef result{
	    .heal_below_pct = integer(raw, "healBelowPct"),
	    .mana_below_pct = integer(raw, "manaBelowPct"),
	    .emergency_heal_below_pct = integer(raw, "emergencyHealBelowPct"),
	    .modes = {},
	};
	for (const auto& [mode_id, mode] : raw.at("modes").items()) {
		result.modes.push_back(
		    domain::AutoBattleModeDef{mode_id, text(mode, "offense"), integer(mode, "supportEvery")});
	}
	return result;
}

domain::Balance balance(const Json& raw) {
	domain::Balance result{
	    .rounds_per_tier = integer(raw, "roundsPerTier"),
	    .cycle_stat_pct = integer(raw, "cycleStatPct"),
	    .cycle_reward_pct = integer(raw, "cycleRewardPct"),
	    .position_pct = integer(raw, "positionPct"),
	    .final_round = integer(raw, "finalRound"),
	    .elite_chance_pct = integer(raw, "eliteChancePct"),
	    .difficulties = {},
	    .enemy_classes = {},
	    .crit_multiplier_pct = integer(raw, "critMultiplierPct"),
	    .defend_damage_pct = integer(raw, "defendDamagePct"),
	    .parry_reflect_pct = integer(raw, "parryReflectPct"),
	    .monster_heal_pct = integer(raw, "monsterHealPct"),
	    .boss_telegraph_every = integer(raw, "bossTelegraphEvery"),
	    .boss_charge_damage_pct = integer(raw, "bossChargeDamagePct"),
	    .caps =
	        domain::Caps{
	            .crit_chance = integer(raw.at("caps"), "critChance"),
	            .dodge = integer(raw.at("caps"), "dodge"),
	            .parry = integer(raw.at("caps"), "parry"),
	            .leech = integer(raw.at("caps"), "leech"),
	            .protection = integer(raw.at("caps"), "protection"),
	        },
	    .magic_level_base = integer(raw.at("magicLevel"), "base"),
	    .magic_level_growth_pct = integer(raw.at("magicLevel"), "growthPct"),
	    .spell_levels = {},
	    .starting_gold = integer(raw, "startingGold"),
	    .starting_potions = {},
	    .bag_capacity = integer(raw, "bagCapacity"),
	    .item_level_per_tier = integer(raw, "itemLevelPerTier"),
	    .item_score_weights = {},
	    .rarities = {},
	    .rarity_weights = {},
	    .merchant_stock_size = integer(raw, "merchantStockSize"),
	    .merchant_markup_pct = integer(raw, "merchantMarkupPct"),
	    .spell_status_damage_pct = integer(raw, "spellStatusDamagePct"),
	    .auto_battle = auto_battle(raw.at("autoBattle")),
	};
	for (const Json& difficulty : raw.at("difficulties")) {
		result.difficulties.push_back(domain::DifficultyDef{text(difficulty, "id"), integer(difficulty, "hpPct"),
		    integer(difficulty, "damagePct"), integer(difficulty, "goldPct"), integer(difficulty, "xpPct")});
	}
	// In the order of the EnemyClass enum, like the reference.
	for (const std::string_view class_id : domain::kEnemyClasses) {
		result.enemy_classes.push_back(enemy_class(class_id, raw.at("enemyClasses").at(std::string(class_id))));
	}
	for (const auto& [stat, weight] : raw.at("itemScoreWeights").items()) {
		result.item_score_weights.emplace(stat, weight.get<std::int64_t>());
	}
	for (const Json& level : raw.at("spellLevels")) {
		result.spell_levels.push_back(domain::SpellLevelDef{
		    integer(level, "level"), integer(level, "uses"), integer(level, "effectPct"), integer(level, "manaPct")});
	}
	for (const Json& stack : raw.at("startingPotions")) {
		result.starting_potions.push_back(domain::PotionStack{text(stack, "potionId"), integer(stack, "quantity")});
	}
	for (const Json& rarity : raw.at("rarities")) {
		result.rarities.push_back(domain::RarityDef{text(rarity, "id"), integer(rarity, "statPct"),
		    integer(rarity, "valuePct"), integer(rarity, "affixMin"), integer(rarity, "affixMax")});
	}
	for (const auto& [table, weights] : raw.at("rarityWeights").items()) {
		result.rarity_weights.emplace(table, weights_of(weights));
	}
	return result;
}

void load_vocations(domain::GameData& data, const Json& raw) {
	for (const Json& vocation : raw) {
		data.vocations.push_back(domain::VocationDef{
		    .id = text(vocation, "id"),
		    .name = text(vocation, "name"),
		    .start_hp = integer(vocation, "startHp"),
		    .start_mp = integer(vocation, "startMp"),
		    .hp_per_level = integer(vocation, "hpPerLevel"),
		    .mp_per_level = integer(vocation, "mpPerLevel"),
		    .hp_regen = integer(vocation, "hpRegen"),
		    .mp_regen = integer(vocation, "mpRegen"),
		    .melee_min = integer(vocation, "meleeMin"),
		    .melee_max = integer(vocation, "meleeMax"),
		    .melee_per_level = integer(vocation, "meleePerLevel"),
		    .weapon_types = strings(vocation, "weaponTypes"),
		    .shield_types = strings(vocation, "shieldTypes"),
		    .starter_weapon = text(vocation, "starterWeapon"),
		    .spells = strings(vocation, "spells"),
		});
	}
}

void load_spells(domain::GameData& data, const Json& raw) {
	for (const Json& spell : raw) {
		domain::Level3Bonus bonus;
		if (const auto found = spell.find("level3Bonus"); found != spell.end()) {
			bonus.status = optional_text(*found, "status");
			bonus.chance = found->value("chance", std::int64_t{0});
			bonus.cleanse = found->value("cleanse", false);
		}
		data.spells.push_back(domain::SpellDef{
		    .id = text(spell, "id"),
		    .name = text(spell, "name"),
		    .words = text(spell, "words"),
		    .kind = text(spell, "kind"),
		    .element = text(spell, "element"),
		    .mana = integer(spell, "mana"),
		    .min = integer(spell, "min"),
		    .max = integer(spell, "max"),
		    .per_level = integer(spell, "perLevel"),
		    .per_magic_level = integer(spell, "perMagicLevel"),
		    .level3_bonus = bonus,
		});
	}
}

void load_items(domain::GameData& data, const Json& raw) {
	for (const Json& item : raw) {
		domain::ItemDef converted{
		    .id = text(item, "id"),
		    .name = text(item, "name"),
		    .slot = text(item, "slot"),
		    .type = text(item, "type"),
		    .tier = integer(item, "tier"),
		    .element = optional_text(item, "element"),
		    .stats = {},
		    .value = integer(item, "value"),
		};
		for (const auto& [stat, value] : item.at("stats").items()) {
			converted.stats.emplace_back(stat, value.get<std::int64_t>());
		}
		data.items.push_back(std::move(converted));
	}
}

void load_potions(domain::GameData& data, const Json& raw) {
	for (const Json& potion : raw) {
		data.potions.push_back(domain::PotionDef{text(potion, "id"), text(potion, "name"), text(potion, "resource"),
		    integer(potion, "min"), integer(potion, "max"), integer(potion, "price"), integer(potion, "unlockRound")});
	}
}

void load_statuses(domain::GameData& data, const Json& raw) {
	for (const Json& status : raw) {
		data.statuses.push_back(domain::StatusDef{
		    text(status, "id"), text(status, "kind"), text(status, "element"), integer(status, "turns")});
	}
}

void load_affixes(domain::GameData& data, const Json& raw) {
	for (const Json& affix : raw) {
		data.affixes.push_back(domain::AffixDef{text(affix, "id"), text(affix, "stat"), integer(affix, "min"),
		    integer(affix, "max"), integer(affix, "perTier"), strings(affix, "slots")});
	}
}

void load_achievements(domain::GameData& data, const Json& raw) {
	for (const Json& achievement : raw) {
		data.achievements.push_back(
		    domain::AchievementDef{text(achievement, "id"), text(achievement, "type"), integer(achievement, "value")});
	}
}

} // namespace

domain::GameData load_game_data(const assets::SharedFs& shared) {
	// Each document is parsed and converted inside its own try block so the error names the file.
	const auto document = [&](const std::string& name) {
		const auto found = shared.find("data/" + name + ".json");
		if (found == shared.end()) {
			throw DataError(std::format("invalid game data: {}.json is missing", name));
		}
		try {
			return Json::parse(found->second);
		} catch (const nlohmann::json::exception& error) {
			throw DataError(std::format("invalid game data: {}.json: {}", name, error.what()));
		}
	};
	const auto convert = [](const std::string& name, const auto& action) {
		try {
			action();
		} catch (const nlohmann::json::exception& error) {
			throw DataError(std::format("invalid game data: {}.json: {}", name, error.what()));
		}
	};

	domain::GameData data;
	const Json balance_json = document("balance");
	convert("balance", [&] { data.balance = balance(balance_json); });
	const Json vocations = document("vocations");
	convert("vocations", [&] { load_vocations(data, vocations.at("vocations")); });
	const Json spells = document("spells");
	convert("spells", [&] { load_spells(data, spells.at("spells")); });
	const Json monsters = document("monsters");
	convert("monsters", [&] { data.monsters = creatures(monsters.at("monsters"), false); });
	const Json bosses = document("bosses");
	convert("bosses", [&] { data.bosses = creatures(bosses.at("bosses"), true); });
	const Json items = document("items");
	convert("items", [&] { load_items(data, items.at("items")); });
	const Json families = document("families");
	convert("families", [&] { data.families = strings(families, "families"); });
	const Json potions = document("potions");
	convert("potions", [&] { load_potions(data, potions.at("potions")); });
	const Json statuses = document("statuses");
	convert("statuses", [&] { load_statuses(data, statuses.at("statuses")); });
	const Json affixes = document("affixes");
	convert("affixes", [&] { load_affixes(data, affixes.at("affixes")); });
	const Json achievements = document("achievements");
	convert("achievements", [&] { load_achievements(data, achievements.at("achievements")); });

	if (data.vocations.empty() || data.bosses.empty() || data.balance.spell_levels.empty()) {
		throw DataError("invalid game data: vocations, bosses and spell levels are required");
	}
	data.index();
	return data;
}

} // namespace rpg::infrastructure
