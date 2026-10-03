#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "domain/definitions.hpp"
#include "domain/enums.hpp"

namespace rpg::domain {

// Ordered maps: deterministic iteration and sorted JSON keys. std::less<> allows lookups by std::string_view.
using CountMap = std::map<std::string, std::int64_t, std::less<>>;

// Reads a counter without inserting it: operator[] on a std::map would add a zero entry, and that entry would then
// show up in the save file (`"spellUses": {"x": 0}`) and break parity with the reference.
std::int64_t count_of(const CountMap& counts, std::string_view key);

// A status currently affecting a creature.
struct ActiveStatus {
	std::string status_id;
	std::int64_t turns = 0;
	std::int64_t per_turn = 0;

	bool operator==(const ActiveStatus&) const = default;
};

// One rolled affix of an item.
struct AffixRoll {
	Stat stat;
	std::int64_t value = 0;

	bool operator==(const AffixRoll&) const = default;
};

// A concrete item (base + rarity + affixes).
struct ItemInstance {
	std::int64_t uid = 0;
	std::string item_id;
	std::string rarity;
	std::int64_t tier = 0;
	std::vector<AffixRoll> affixes;

	bool operator==(const ItemInstance&) const = default;
};

// The player's mutable state. A plain value type: copying a Player copies everything (no shared references).
struct Player {
	std::string name;
	std::string vocation_id;
	std::int64_t hp = 0;
	std::int64_t mp = 0;
	std::int64_t gold = 0;
	std::int64_t level = 1;
	std::int64_t xp = 0;
	std::int64_t magic_level = 1;
	std::int64_t mana_spent = 0;
	CountMap potions;
	std::map<Slot, ItemInstance, std::less<>> equipment;
	std::vector<ItemInstance> bag;
	CountMap spell_uses;
	std::vector<ActiveStatus> statuses;
	std::int64_t stun_cooldown = 0;
	bool defending = false;

	[[nodiscard]] std::int64_t potion_count(std::string_view potion_id) const { return count_of(potions, potion_id); }

	bool operator==(const Player&) const = default;
};

// A spawned monster with stats already scaled for the round and difficulty.
struct MonsterInstance {
	std::string creature_id;
	bool is_boss = false;
	std::int64_t hp = 0;
	std::int64_t max_hp = 0;
	std::int64_t xp = 0;
	std::int64_t gold_min = 0;
	std::int64_t gold_max = 0;
	std::vector<MonsterAttack> attacks;
	std::vector<ActiveStatus> statuses;
	std::int64_t stun_cooldown = 0;
	std::int64_t boss_actions = 0;

	[[nodiscard]] const MonsterAttack& attack(std::string_view attack_id) const;

	bool operator==(const MonsterInstance&) const = default;
};

} // namespace rpg::domain
