#include "domain/enums.h"

#include <string.h>

static const char *const ELEMENT_NAMES[ELEMENT_COUNT] = {"physical", "fire", "ice", "energy", "earth", "holy", "death"};
static const char *const SLOT_NAMES[SLOT_COUNT] = {
    "helmet", "armor", "legs", "boots", "amulet", "ring", "weapon", "shield"};
static const char *const PHASE_NAMES[PHASE_COUNT] = {"merchant", "battle", "victory", "game_over"};
static const char *const ENEMY_CLASS_NAMES[ENEMY_CLASS_COUNT] = {"normal", "elite", "boss"};
static const char *const RESOURCE_NAMES[RESOURCE_COUNT] = {"hp", "mp"};
static const char *const SPELL_KIND_NAMES[SPELL_KIND_COUNT] = {"attack", "heal"};
static const char *const STATUS_KIND_NAMES[STATUS_KIND_COUNT] = {"dot", "stun"};
static const char *const TARGET_NAMES[TARGET_COUNT] = {"player", "monster"};
static const char *const STAT_NAMES[STAT_COUNT] = {
    "attack",
    "armor",
    "maxHp",
    "maxMp",
    "hpRegen",
    "mpRegen",
    "critChance",
    "critDamage",
    "spellPower",
    "physicalDamage",
    "dodge",
    "parry",
    "lifeLeech",
    "manaLeech",
    "protPhysical",
    "protFire",
    "protIce",
    "protEnergy",
    "protEarth",
    "protHoly",
    "protDeath",
};

const Slot EQUIPMENT_SLOT_ORDER[SLOT_COUNT] = {
    SLOT_WEAPON,
    SLOT_SHIELD,
    SLOT_HELMET,
    SLOT_ARMOR,
    SLOT_LEGS,
    SLOT_BOOTS,
    SLOT_RING,
    SLOT_AMULET,
};

const Slot SLOTS_BY_NAME[SLOT_COUNT] = {
    SLOT_AMULET,
    SLOT_ARMOR,
    SLOT_BOOTS,
    SLOT_HELMET,
    SLOT_LEGS,
    SLOT_RING,
    SLOT_SHIELD,
    SLOT_WEAPON,
};

static bool parse(const char *const *names, int count, const char *name, int *out) {
	for (int i = 0; i < count; i++) {
		if (strcmp(names[i], name) == 0) {
			*out = i;
			return true;
		}
	}
	return false;
}

// One name function and one parse function per enum, all backed by the tables above.
#define ENUM_FUNCTIONS(Type, prefix, names, count)                                                                     \
	const char *prefix##_name(Type value) {                                                                            \
		if ((int)value < 0 || (int)value >= (count)) {                                                                 \
			fatal("invalid " #Type " value %d", (int)value);                                                           \
		}                                                                                                              \
		return (names)[value];                                                                                         \
	}                                                                                                                  \
	bool prefix##_parse(const char *name, Type *out) {                                                                 \
		int index = 0;                                                                                                 \
		if (!parse((names), (count), name, &index)) {                                                                  \
			return false;                                                                                              \
		}                                                                                                              \
		*out = (Type)index;                                                                                            \
		return true;                                                                                                   \
	}

ENUM_FUNCTIONS(Element, element, ELEMENT_NAMES, ELEMENT_COUNT)
ENUM_FUNCTIONS(Slot, slot, SLOT_NAMES, SLOT_COUNT)
ENUM_FUNCTIONS(Phase, phase, PHASE_NAMES, PHASE_COUNT)
ENUM_FUNCTIONS(EnemyClass, enemy_class, ENEMY_CLASS_NAMES, ENEMY_CLASS_COUNT)
ENUM_FUNCTIONS(Resource, resource, RESOURCE_NAMES, RESOURCE_COUNT)
ENUM_FUNCTIONS(SpellKind, spell_kind, SPELL_KIND_NAMES, SPELL_KIND_COUNT)
ENUM_FUNCTIONS(StatusKind, status_kind, STATUS_KIND_NAMES, STATUS_KIND_COUNT)
ENUM_FUNCTIONS(Stat, stat, STAT_NAMES, STAT_COUNT)

const char *target_name(Target value) { return TARGET_NAMES[value]; }

Stat protection_stat(Element element) { return (Stat)(STAT_PROT_PHYSICAL + (int)element); }
