#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domain/enums.hpp"

namespace rpg::domain {

// Thrown when a definition id is not present in the loaded data (a bug in the code or the data, never a player error).
class UnknownIdError : public std::out_of_range {
public:
	explicit UnknownIdError(std::string_view id) : std::out_of_range("unknown id: " + std::string(id)) {}
};

// A status an attack may apply.
struct StatusOnHit {
	std::string status;
	std::int64_t chance = 0;
	std::int64_t damage_pct = 0;

	bool operator==(const StatusOnHit&) const = default;
};

// One attack of a monster (already scaled when stored in a MonsterInstance).
struct MonsterAttack {
	std::string id;
	Element element;
	std::int64_t min = 0;
	std::int64_t max = 0;
	std::int64_t weight = 0;
	std::optional<StatusOnHit> status;

	bool operator==(const MonsterAttack&) const = default;
};

// A monster or a boss.
struct MonsterDef {
	std::string id;
	std::string name;
	std::int64_t tier = 0;
	std::string family;
	std::int64_t hp = 0;
	std::int64_t xp = 0;
	std::int64_t gold_min = 0;
	std::int64_t gold_max = 0;
	std::vector<MonsterAttack> attacks;
	std::map<Element, std::int64_t, std::less<>> resistances;
	bool is_boss = false;
	std::optional<std::string> charge_attack;

	// Damage-taken percentage for an element (default 100).
	[[nodiscard]] std::int64_t resistance(std::string_view element) const;
	[[nodiscard]] const MonsterAttack& attack(std::string_view attack_id) const;
};

// The extra effect of a spell at level 3.
struct Level3Bonus {
	std::optional<std::string> status;
	std::int64_t chance = 0;
	bool cleanse = false;
};

struct SpellDef {
	std::string id;
	std::string name;
	std::string words;
	std::string kind; // "attack" or "heal"
	Element element;
	std::int64_t mana = 0;
	std::int64_t min = 0;
	std::int64_t max = 0;
	std::int64_t per_level = 0;
	std::int64_t per_magic_level = 0;
	Level3Bonus level3_bonus;
};

struct VocationDef {
	std::string id;
	std::string name;
	std::int64_t start_hp = 0;
	std::int64_t start_mp = 0;
	std::int64_t hp_per_level = 0;
	std::int64_t mp_per_level = 0;
	std::int64_t hp_regen = 0;
	std::int64_t mp_regen = 0;
	std::int64_t melee_min = 0;
	std::int64_t melee_max = 0;
	std::int64_t melee_per_level = 0;
	std::vector<std::string> weapon_types;
	std::vector<std::string> shield_types;
	std::string starter_weapon;
	std::vector<std::string> spells;
};

struct PotionDef {
	std::string id;
	std::string name;
	std::string resource; // "hp" or "mp"
	std::int64_t min = 0;
	std::int64_t max = 0;
	std::int64_t price = 0;
	std::int64_t unlock_round = 0;
};

struct StatusDef {
	std::string id;
	std::string kind; // "dot" or "stun"
	Element element;
	std::int64_t turns = 0;
};

// An equipment base item. Stats keep the file order (a vector of pairs, not a map).
struct ItemDef {
	std::string id;
	std::string name;
	Slot slot;
	std::string type;
	std::int64_t tier = 0;
	std::optional<Element> element;
	std::vector<std::pair<Stat, std::int64_t>> stats;
	std::int64_t value = 0;
};

struct AffixDef {
	std::string id;
	Stat stat;
	std::int64_t min = 0;
	std::int64_t max = 0;
	std::int64_t per_tier = 0;
	std::vector<Slot> slots;
};

struct AchievementDef {
	std::string id;
	std::string type;
	std::int64_t value = 0;
};

struct DifficultyDef {
	std::string id;
	std::int64_t hp_pct = 0;
	std::int64_t damage_pct = 0;
	std::int64_t gold_pct = 0;
	std::int64_t xp_pct = 0;
	std::int64_t non_common_weight_pct = 0;
};

struct RarityDef {
	std::string id;
	std::int64_t stat_pct = 0;
	std::int64_t value_pct = 0;
	std::int64_t affix_min = 0;
	std::int64_t affix_max = 0;
};

struct SpellLevelDef {
	std::int64_t level = 0;
	std::int64_t uses = 0;
	std::int64_t effect_pct = 0;
	std::int64_t mana_pct = 0;
};

struct Caps {
	std::int64_t crit_chance = 0;
	std::int64_t dodge = 0;
	std::int64_t parry = 0;
	std::int64_t leech = 0;
	std::int64_t protection = 0;
};

// A potion id with a quantity (starting kit).
struct PotionStack {
	std::string potion_id;
	std::int64_t quantity = 0;
};

// The global knobs of balance.json.
struct Balance {
	std::int64_t rounds_per_tier = 0;
	std::int64_t cycle_stat_pct = 0;
	std::int64_t cycle_reward_pct = 0;
	std::int64_t position_pct = 0;
	std::vector<DifficultyDef> difficulties;
	std::int64_t crit_multiplier_pct = 0;
	std::int64_t defend_damage_pct = 0;
	std::int64_t boss_telegraph_every = 0;
	std::int64_t boss_charge_damage_pct = 0;
	Caps caps;
	std::int64_t magic_level_base = 0;
	std::int64_t magic_level_growth_pct = 0;
	std::vector<SpellLevelDef> spell_levels;
	std::int64_t starting_gold = 0;
	std::vector<PotionStack> starting_potions;
	std::int64_t bag_capacity = 0;
	std::int64_t drop_chance_pct = 0;
	std::int64_t boss_drops = 0;
	std::vector<RarityDef> rarities;
	// Read only by rarity id (weights are combined in the order of `rarities`), so a map is safe here.
	std::map<std::string, std::map<std::string, std::int64_t, std::less<>>, std::less<>> rarity_weights;
	std::int64_t merchant_stock_size = 0;
	std::int64_t merchant_markup_pct = 0;
	std::int64_t spell_status_damage_pct = 0;

	[[nodiscard]] const DifficultyDef& difficulty(std::string_view id) const;
	[[nodiscard]] const RarityDef& rarity(std::string_view id) const;
	[[nodiscard]] bool has_difficulty(std::string_view id) const;
};

// The immutable, loaded game content with lookups. Call index() after changing any definition vector.
class GameData {
public:
	Balance balance;
	std::vector<VocationDef> vocations;
	std::vector<SpellDef> spells;
	std::vector<MonsterDef> monsters;
	std::vector<MonsterDef> bosses;
	std::vector<PotionDef> potions;
	std::vector<StatusDef> statuses;
	std::vector<ItemDef> items;
	std::vector<AffixDef> affixes;
	std::vector<AchievementDef> achievements;
	std::vector<std::string> families;

	// Builds the lookup tables. The tables store positions, not pointers, so copying GameData stays safe.
	GameData& index();

	[[nodiscard]] std::int64_t tier_count() const { return static_cast<std::int64_t>(bosses.size()); }

	[[nodiscard]] const VocationDef& vocation(std::string_view id) const;
	[[nodiscard]] const SpellDef& spell(std::string_view id) const;
	[[nodiscard]] const MonsterDef& creature(std::string_view id) const;
	[[nodiscard]] const PotionDef& potion(std::string_view id) const;
	[[nodiscard]] const StatusDef& status(std::string_view id) const;
	[[nodiscard]] const ItemDef& item(std::string_view id) const;

	[[nodiscard]] bool has_vocation(std::string_view id) const { return vocation_index_.contains(id); }
	[[nodiscard]] bool has_spell(std::string_view id) const { return spell_index_.contains(id); }
	[[nodiscard]] bool has_creature(std::string_view id) const { return creature_index_.contains(id); }
	[[nodiscard]] bool has_potion(std::string_view id) const { return potion_index_.contains(id); }
	[[nodiscard]] bool has_item(std::string_view id) const { return item_index_.contains(id); }

	// The monsters of a tier sorted by id (code-point order).
	[[nodiscard]] std::vector<const MonsterDef*> monsters_in_tier(std::int64_t tier) const;
	[[nodiscard]] const MonsterDef& boss_of_tier(std::int64_t tier) const;

private:
	using Index = std::map<std::string, std::size_t, std::less<>>;

	Index vocation_index_;
	Index spell_index_;
	// Creatures: position in `monsters`, or in `bosses` when the flag is set.
	std::map<std::string, std::pair<bool, std::size_t>, std::less<>> creature_index_;
	Index potion_index_;
	Index status_index_;
	Index item_index_;
};

} // namespace rpg::domain
