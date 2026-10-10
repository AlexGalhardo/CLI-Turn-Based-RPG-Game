// Immutable game definitions loaded from shared/data (see docs/data-format.md).
//
// Every list keeps the file order. The small inner lists (a monster's attacks, a vocation's spells) are fixed-size
// arrays, so a definition is one plain struct; the loader rejects data that does not fit. Mappings keyed by a closed
// set become arrays indexed by the enum (`resistances[ELEMENT_FIRE]`, `stats[STAT_ARMOR]`).
#ifndef RPG_DOMAIN_DEFINITIONS_H
#define RPG_DOMAIN_DEFINITIONS_H

#include "domain/base.h"
#include "domain/enums.h"

// Implementation limits (not balance numbers): the loader fails when the data files exceed them.
#define MAX_ATTACKS 8
#define MAX_TYPES 8
#define MAX_VOCATION_SPELLS 16
#define MAX_RARITIES 8
#define MAX_DIFFICULTIES 8
#define MAX_SPELL_LEVELS 8
#define MAX_STARTING_POTIONS 8
#define MAX_AUTO_BATTLE_MODES 8
#define MAX_TIERS 32

#define DEFAULT_RESISTANCE 100

typedef struct {
	Id status;
	int64_t chance;
	int64_t damage_pct;
} StatusOnHit;

typedef struct {
	Id id;
	Element element;
	int64_t min;
	int64_t max;
	int64_t weight;
	bool has_status;
	StatusOnHit status;
} MonsterAttack;

typedef struct {
	Id id;
	char name[NAME_SIZE];
	int64_t tier;
	Id family;
	int64_t hp;
	int64_t xp;
	int64_t gold_min;
	int64_t gold_max;
	int attack_count;
	MonsterAttack attacks[MAX_ATTACKS];
	// Damage taken in percent, per element (100 when the file does not list it, 0 = immune).
	int64_t resistances[ELEMENT_COUNT];
	bool is_boss;
	bool has_charge_attack;
	Id charge_attack;
} MonsterDef;

typedef struct {
	bool has_status;
	Id status;
	int64_t chance;
	bool cleanse;
} Level3Bonus;

typedef struct {
	Id id;
	char name[NAME_SIZE];
	char words[NAME_SIZE];
	SpellKind kind;
	Element element;
	int64_t mana;
	int64_t min;
	int64_t max;
	int64_t per_level;
	int64_t per_magic_level;
	Level3Bonus level3_bonus;
} SpellDef;

typedef struct {
	Id id;
	char name[NAME_SIZE];
	int64_t start_hp;
	int64_t start_mp;
	int64_t hp_per_level;
	int64_t mp_per_level;
	int64_t hp_regen;
	int64_t mp_regen;
	int64_t melee_min;
	int64_t melee_max;
	int64_t melee_per_level;
	int weapon_type_count;
	Id weapon_types[MAX_TYPES];
	int shield_type_count;
	Id shield_types[MAX_TYPES];
	Id starter_weapon;
	int spell_count;
	Id spells[MAX_VOCATION_SPELLS];
} VocationDef;

typedef struct {
	Id id;
	char name[NAME_SIZE];
	Resource resource;
	int64_t min;
	int64_t max;
	int64_t price;
	int64_t unlock_round;
} PotionDef;

typedef struct {
	Id id;
	StatusKind kind;
	Element element;
	int64_t turns;
} StatusDef;

typedef struct {
	Id id;
	char name[NAME_SIZE];
	Slot slot;
	Id type;
	int64_t tier;
	bool has_element;
	Element element;
	// A stat listed in the file is "present" even when its value is 0 (the UI lists present stats).
	bool has_stat[STAT_COUNT];
	int64_t stats[STAT_COUNT];
	int64_t value;
} ItemDef;

typedef struct {
	Id id;
	Stat stat;
	int64_t min;
	int64_t max;
	int64_t per_tier;
	bool slots[SLOT_COUNT];
} AffixDef;

typedef struct {
	Id id;
	Id type;
	int64_t value;
} AchievementDef;

typedef struct {
	Id id;
	int64_t hp_pct;
	int64_t damage_pct;
	int64_t gold_pct;
	int64_t xp_pct;
} DifficultyDef;

typedef struct {
	Id id;
	int64_t stat_pct;
	int64_t value_pct;
	int64_t affix_min;
	int64_t affix_max;
} RarityDef;

// Weights of a rarity table, parallel to `balance.rarities` (0 for a rarity the table does not list).
typedef struct {
	int64_t weights[MAX_RARITIES];
} RarityWeights;

// A row of `balance.enemyClasses`: multipliers, combat chances and drop table (docs/game-design.md §3).
typedef struct {
	EnemyClass id;
	int64_t stat_pct;
	int64_t reward_pct;
	int64_t dodge;
	int64_t parry;
	int64_t crit;
	int64_t heal;
	int64_t drop_chance_pct;
	int64_t drops;
	int64_t potion_drop_pct;
	RarityWeights rarity_weights;
} EnemyClassDef;

typedef struct {
	Id id;
	Id offense;
	int64_t support_every;
} AutoBattleModeDef;

typedef struct {
	int64_t heal_below_pct;
	int64_t mana_below_pct;
	int64_t emergency_heal_below_pct;
	// Menu order = the key order of `autoBattle.modes` in balance.json.
	int mode_count;
	AutoBattleModeDef modes[MAX_AUTO_BATTLE_MODES];
} AutoBattleDef;

typedef struct {
	int64_t level;
	int64_t uses;
	int64_t effect_pct;
	int64_t mana_pct;
} SpellLevelDef;

typedef struct {
	int64_t crit_chance;
	int64_t dodge;
	int64_t parry;
	int64_t leech;
	int64_t protection;
} Caps;

typedef struct {
	Id potion_id;
	int64_t quantity;
} StartingPotion;

typedef struct {
	int64_t rounds_per_tier;
	int64_t cycle_stat_pct;
	int64_t cycle_reward_pct;
	int64_t position_pct;
	int64_t final_round;
	int64_t elite_chance_pct;
	int difficulty_count;
	DifficultyDef difficulties[MAX_DIFFICULTIES];
	EnemyClassDef enemy_classes[ENEMY_CLASS_COUNT];
	int64_t crit_multiplier_pct;
	int64_t defend_damage_pct;
	int64_t parry_reflect_pct;
	int64_t monster_heal_pct;
	int64_t boss_telegraph_every;
	int64_t boss_charge_damage_pct;
	Caps caps;
	int64_t magic_level_base;
	int64_t magic_level_growth_pct;
	int spell_level_count;
	SpellLevelDef spell_levels[MAX_SPELL_LEVELS];
	int64_t starting_gold;
	int starting_potion_count;
	StartingPotion starting_potions[MAX_STARTING_POTIONS];
	int64_t bag_capacity;
	int64_t item_level_per_tier;
	int64_t item_score_weights[STAT_COUNT];
	int rarity_count;
	RarityDef rarities[MAX_RARITIES];
	// `rarityWeights.merchant`; drops use the enemy class tables.
	RarityWeights merchant_rarity_weights;
	int64_t merchant_stock_size;
	int64_t merchant_markup_pct;
	int64_t spell_status_damage_pct;
	AutoBattleDef auto_battle;
} Balance;

typedef struct {
	Balance balance;
	MonsterDef *monsters;
	int monster_count;
	MonsterDef *bosses;
	int boss_count;
	VocationDef *vocations;
	int vocation_count;
	SpellDef *spells;
	int spell_count;
	PotionDef *potions;
	int potion_count;
	StatusDef *statuses;
	int status_count;
	ItemDef *items;
	int item_count;
	AffixDef *affixes;
	int affix_count;
	AchievementDef *achievements;
	int achievement_count;
	Id *families;
	int family_count;
} GameData;

void game_data_free(GameData *data);

// `find_*` return NULL for an unknown id; the plain lookups treat it as a bug (the data tests rule it out).
const VocationDef *find_vocation(const GameData *data, const char *id);
const DifficultyDef *find_difficulty(const Balance *balance, const char *id);
const SpellDef *find_spell(const GameData *data, const char *id);
const PotionDef *find_potion(const GameData *data, const char *id);
const ItemDef *find_item(const GameData *data, const char *id);
const MonsterDef *find_creature(const GameData *data, const char *id);
const RarityDef *find_rarity(const Balance *balance, const char *id);
const AutoBattleModeDef *find_auto_battle_mode(const AutoBattleDef *config, const char *id);

const VocationDef *data_vocation(const GameData *data, const char *id);
const SpellDef *data_spell(const GameData *data, const char *id);
// Monsters and bosses share one id space.
const MonsterDef *data_creature(const GameData *data, const char *id);
const PotionDef *data_potion(const GameData *data, const char *id);
const StatusDef *data_status(const GameData *data, const char *id);
const ItemDef *data_item(const GameData *data, const char *id);
const DifficultyDef *balance_difficulty(const Balance *balance, const char *id);
const EnemyClassDef *balance_enemy_class(const Balance *balance, EnemyClass enemy_class);
const RarityDef *balance_rarity(const Balance *balance, const char *id);

int data_tier_count(const GameData *data);
// Fills `out` (room for `data->monster_count` pointers) with the monsters of a tier sorted by id; returns how many.
int data_monsters_in_tier(const GameData *data, int64_t tier, const MonsterDef **out);
const MonsterDef *data_boss_of_tier(const GameData *data, int64_t tier);

const MonsterAttack *monster_def_attack(const MonsterDef *monster, const char *attack_id);
bool vocation_has_spell(const VocationDef *vocation, const char *spell_id);

#endif
