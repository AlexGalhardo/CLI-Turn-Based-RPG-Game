// Derived character stats: vocation base + equipment (docs/game-design.md §4 and §8).
#ifndef RPG_DOMAIN_CHARACTER_H
#define RPG_DOMAIN_CHARACTER_H

#include "domain/entities.h"

// Final stats of an item: base stats scaled by the rarity, plus affixes. `present` (optional) tells which stats
// the item has at all, since a present stat can be 0.
void item_stats(const ItemInstance *item, const GameData *data, int64_t values[STAT_COUNT], bool present[STAT_COUNT]);
int64_t item_value(const ItemInstance *item, const GameData *data);
// Sum of the item's final stats weighted by `balance.itemScoreWeights` (like Diablo's item power).
int64_t item_score(const ItemInstance *item, const GameData *data);
// Uses the instance tier: the round tier the item was generated for (docs/game-design.md §8).
int64_t required_level(const ItemInstance *item, const GameData *data);
int64_t equipment_score(const Player *player, const GameData *data);

typedef struct {
	int64_t max_hp;
	int64_t max_mp;
	int64_t hp_regen;
	int64_t mp_regen;
	int64_t melee_min;
	int64_t melee_max;
	Element weapon_element;
	int64_t armor;
	int64_t crit_chance;
	int64_t crit_damage;
	int64_t spell_power;
	int64_t physical_damage;
	int64_t dodge;
	int64_t parry;
	int64_t life_leech;
	int64_t mana_leech;
	int64_t protections[ELEMENT_COUNT];
} CharacterSheet;

CharacterSheet build_sheet(const Player *player, const GameData *data);

#endif
