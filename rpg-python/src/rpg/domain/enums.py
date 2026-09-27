from enum import StrEnum


class Element(StrEnum):
	PHYSICAL = "physical"
	FIRE = "fire"
	ICE = "ice"
	ENERGY = "energy"
	EARTH = "earth"
	HOLY = "holy"
	DEATH = "death"


class Slot(StrEnum):
	HELMET = "helmet"
	ARMOR = "armor"
	LEGS = "legs"
	BOOTS = "boots"
	AMULET = "amulet"
	RING = "ring"
	WEAPON = "weapon"
	SHIELD = "shield"


class Phase(StrEnum):
	MERCHANT = "merchant"
	BATTLE = "battle"
	GAME_OVER = "game_over"


class Resource(StrEnum):
	HP = "hp"
	MP = "mp"


class SpellKind(StrEnum):
	ATTACK = "attack"
	HEAL = "heal"


class StatusKind(StrEnum):
	DOT = "dot"
	STUN = "stun"


class Target(StrEnum):
	PLAYER = "player"
	MONSTER = "monster"


class Stat(StrEnum):
	ATTACK = "attack"
	ARMOR = "armor"
	MAX_HP = "maxHp"
	MAX_MP = "maxMp"
	HP_REGEN = "hpRegen"
	MP_REGEN = "mpRegen"
	CRIT_CHANCE = "critChance"
	CRIT_DAMAGE = "critDamage"
	SPELL_POWER = "spellPower"
	PHYSICAL_DAMAGE = "physicalDamage"
	DODGE = "dodge"
	PARRY = "parry"
	LIFE_LEECH = "lifeLeech"
	MANA_LEECH = "manaLeech"
	PROT_PHYSICAL = "protPhysical"
	PROT_FIRE = "protFire"
	PROT_ICE = "protIce"
	PROT_ENERGY = "protEnergy"
	PROT_EARTH = "protEarth"
	PROT_HOLY = "protHoly"
	PROT_DEATH = "protDeath"


PROTECTION_BY_ELEMENT: dict[Element, Stat] = {
	Element.PHYSICAL: Stat.PROT_PHYSICAL,
	Element.FIRE: Stat.PROT_FIRE,
	Element.ICE: Stat.PROT_ICE,
	Element.ENERGY: Stat.PROT_ENERGY,
	Element.EARTH: Stat.PROT_EARTH,
	Element.HOLY: Stat.PROT_HOLY,
	Element.DEATH: Stat.PROT_DEATH,
}
