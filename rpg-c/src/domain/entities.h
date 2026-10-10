// Mutable run entities. Serialised with camelCase keys: the save format is shared by every implementation.
//
// Player and MonsterInstance are plain values except for the two Counter maps inside Player, which own heap memory
// (see player_free / player_clone).
#ifndef RPG_DOMAIN_ENTITIES_H
#define RPG_DOMAIN_ENTITIES_H

#include "domain/definitions.h"
#include "domain/json_types.h"

// Implementation limits; loading a save that exceeds them fails with an error.
#define MAX_STATUSES 16
#define MAX_AFFIXES 8
#define MAX_BAG 64
#define MAX_MERCHANT_STOCK 16

// A string → integer map kept sorted by key (the order saves are written in). A missing key counts as 0.
typedef struct {
	Id key;
	int64_t value;
} CounterEntry;

typedef struct {
	CounterEntry *entries;
	int count;
	int capacity;
} Counter;

int64_t counter_get(const Counter *counter, const char *key);
bool counter_has(const Counter *counter, const char *key);
void counter_set(Counter *counter, const char *key, int64_t value);
void counter_add(Counter *counter, const char *key, int64_t delta);
int64_t counter_total(const Counter *counter);
void counter_free(Counter *counter);
Counter counter_clone(const Counter *counter);
JsonValue *counter_to_json(const Counter *counter);
void counter_from_json(const JsonValue *raw, Counter *out, JsonError *error);

typedef struct {
	Id status_id;
	int64_t turns;
	int64_t per_turn;
} ActiveStatus;

typedef struct {
	int count;
	ActiveStatus items[MAX_STATUSES];
} StatusList;

void status_list_remove(StatusList *list, int index);
void status_list_push(StatusList *list, ActiveStatus status);

typedef struct {
	Stat stat;
	int64_t value;
} AffixRoll;

typedef struct {
	int64_t uid;
	Id item_id;
	Id rarity;
	int64_t tier;
	int affix_count;
	AffixRoll affixes[MAX_AFFIXES];
} ItemInstance;

JsonValue *item_instance_to_json(const ItemInstance *item);
void item_instance_from_json(const JsonValue *raw, ItemInstance *out, JsonError *error);

typedef struct {
	char name[NAME_SIZE];
	Id vocation_id;
	int64_t hp;
	int64_t mp;
	int64_t gold;
	int64_t level;
	int64_t xp;
	int64_t magic_level;
	int64_t mana_spent;
	Counter potions;
	bool equipped[SLOT_COUNT];
	ItemInstance equipment[SLOT_COUNT];
	int bag_count;
	ItemInstance bag[MAX_BAG];
	Counter spell_uses;
	StatusList statuses;
	int64_t stun_cooldown;
	bool defending;
} Player;

// The item worn in `slot`, or NULL.
const ItemInstance *player_equipped(const Player *player, Slot slot);
// Index of the bag item with `uid`, or -1.
int player_bag_index(const Player *player, int64_t uid);
void player_bag_remove(Player *player, int index);
void player_bag_push(Player *player, const ItemInstance *item);
void player_free(Player *player);
Player player_clone(const Player *player);
JsonValue *player_to_json(const Player *player);
void player_from_json(const JsonValue *raw, Player *out, JsonError *error);

// A spawned monster: definition id plus stats already scaled for the round and difficulty.
typedef struct {
	Id creature_id;
	bool is_boss;
	EnemyClass enemy_class;
	int64_t hp;
	int64_t max_hp;
	int64_t xp;
	int64_t gold_min;
	int64_t gold_max;
	int attack_count;
	MonsterAttack attacks[MAX_ATTACKS];
	StatusList statuses;
	int64_t stun_cooldown;
	int64_t boss_actions;
} MonsterInstance;

const MonsterAttack *monster_attack(const MonsterInstance *monster, const char *attack_id);
JsonValue *monster_instance_to_json(const MonsterInstance *monster);
void monster_instance_from_json(const JsonValue *raw, MonsterInstance *out, JsonError *error);
void monster_attack_from_json(const JsonValue *raw, MonsterAttack *out, JsonError *error);

#endif
