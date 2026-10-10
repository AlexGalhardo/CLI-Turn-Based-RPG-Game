// Closed sets of the rules. Each enum has a `*_name()` (the string used in JSON and i18n keys) and a `*_parse()`
// that returns false for an unknown name. The enumerator order is the reference order (it drives UI listings).
#ifndef RPG_DOMAIN_ENUMS_H
#define RPG_DOMAIN_ENUMS_H

#include "domain/base.h"

typedef enum {
	ELEMENT_PHYSICAL,
	ELEMENT_FIRE,
	ELEMENT_ICE,
	ELEMENT_ENERGY,
	ELEMENT_EARTH,
	ELEMENT_HOLY,
	ELEMENT_DEATH,
	ELEMENT_COUNT
} Element;

typedef enum {
	SLOT_HELMET,
	SLOT_ARMOR,
	SLOT_LEGS,
	SLOT_BOOTS,
	SLOT_AMULET,
	SLOT_RING,
	SLOT_WEAPON,
	SLOT_SHIELD,
	SLOT_COUNT
} Slot;

typedef enum { PHASE_MERCHANT, PHASE_BATTLE, PHASE_VICTORY, PHASE_GAME_OVER, PHASE_COUNT } Phase;

typedef enum { ENEMY_NORMAL, ENEMY_ELITE, ENEMY_BOSS, ENEMY_CLASS_COUNT } EnemyClass;

typedef enum { RESOURCE_HP, RESOURCE_MP, RESOURCE_COUNT } Resource;

typedef enum { SPELL_ATTACK, SPELL_HEAL, SPELL_KIND_COUNT } SpellKind;

typedef enum { STATUS_DOT, STATUS_STUN, STATUS_KIND_COUNT } StatusKind;

typedef enum { TARGET_PLAYER, TARGET_MONSTER, TARGET_COUNT } Target;

typedef enum {
	STAT_ATTACK,
	STAT_ARMOR,
	STAT_MAX_HP,
	STAT_MAX_MP,
	STAT_HP_REGEN,
	STAT_MP_REGEN,
	STAT_CRIT_CHANCE,
	STAT_CRIT_DAMAGE,
	STAT_SPELL_POWER,
	STAT_PHYSICAL_DAMAGE,
	STAT_DODGE,
	STAT_PARRY,
	STAT_LIFE_LEECH,
	STAT_MANA_LEECH,
	STAT_PROT_PHYSICAL,
	STAT_PROT_FIRE,
	STAT_PROT_ICE,
	STAT_PROT_ENERGY,
	STAT_PROT_EARTH,
	STAT_PROT_HOLY,
	STAT_PROT_DEATH,
	STAT_COUNT
} Stat;

// Order in which equipment slots are listed and auto-equipped.
extern const Slot EQUIPMENT_SLOT_ORDER[SLOT_COUNT];
// Slots sorted by name: the key order of `player.equipment` in a save.
extern const Slot SLOTS_BY_NAME[SLOT_COUNT];

const char *element_name(Element value);
const char *slot_name(Slot value);
const char *phase_name(Phase value);
const char *enemy_class_name(EnemyClass value);
const char *resource_name(Resource value);
const char *spell_kind_name(SpellKind value);
const char *status_kind_name(StatusKind value);
const char *target_name(Target value);
const char *stat_name(Stat value);

bool element_parse(const char *name, Element *out);
bool slot_parse(const char *name, Slot *out);
bool phase_parse(const char *name, Phase *out);
bool enemy_class_parse(const char *name, EnemyClass *out);
bool resource_parse(const char *name, Resource *out);
bool spell_kind_parse(const char *name, SpellKind *out);
bool status_kind_parse(const char *name, StatusKind *out);
bool stat_parse(const char *name, Stat *out);

// The protection stat that reduces damage of `element`.
Stat protection_stat(Element element);

#endif
