--- Closed sets of the rules. Members are the JSON strings themselves; each set also lists its members in order.

local enums = {}

local function enum(members)
	local set = { values = {} }
	for _, member in ipairs(members) do
		set[member[1]] = member[2]
		set.values[#set.values + 1] = member[2]
	end
	return set
end

--- True when `value` is a member of `set` (Python's `Enum(value)` raises otherwise).
function enums.is_member(set, value)
	for _, member in ipairs(set.values) do
		if member == value then
			return true
		end
	end
	return false
end

enums.Element = enum({
	{ "PHYSICAL", "physical" },
	{ "FIRE", "fire" },
	{ "ICE", "ice" },
	{ "ENERGY", "energy" },
	{ "EARTH", "earth" },
	{ "HOLY", "holy" },
	{ "DEATH", "death" },
})

enums.Slot = enum({
	{ "HELMET", "helmet" },
	{ "ARMOR", "armor" },
	{ "LEGS", "legs" },
	{ "BOOTS", "boots" },
	{ "AMULET", "amulet" },
	{ "RING", "ring" },
	{ "WEAPON", "weapon" },
	{ "SHIELD", "shield" },
})

enums.Phase = enum({
	{ "MERCHANT", "merchant" },
	{ "BATTLE", "battle" },
	{ "VICTORY", "victory" },
	{ "GAME_OVER", "game_over" },
})

enums.EnemyClass = enum({
	{ "NORMAL", "normal" },
	{ "ELITE", "elite" },
	{ "BOSS", "boss" },
})

enums.EQUIPMENT_SLOT_ORDER = { "weapon", "shield", "helmet", "armor", "legs", "boots", "ring", "amulet" }

enums.Resource = enum({ { "HP", "hp" }, { "MP", "mp" } })

enums.SpellKind = enum({ { "ATTACK", "attack" }, { "HEAL", "heal" } })

enums.StatusKind = enum({ { "DOT", "dot" }, { "STUN", "stun" } })

enums.Target = enum({ { "PLAYER", "player" }, { "MONSTER", "monster" } })

enums.Stat = enum({
	{ "ATTACK", "attack" },
	{ "ARMOR", "armor" },
	{ "MAX_HP", "maxHp" },
	{ "MAX_MP", "maxMp" },
	{ "HP_REGEN", "hpRegen" },
	{ "MP_REGEN", "mpRegen" },
	{ "CRIT_CHANCE", "critChance" },
	{ "CRIT_DAMAGE", "critDamage" },
	{ "SPELL_POWER", "spellPower" },
	{ "PHYSICAL_DAMAGE", "physicalDamage" },
	{ "DODGE", "dodge" },
	{ "PARRY", "parry" },
	{ "LIFE_LEECH", "lifeLeech" },
	{ "MANA_LEECH", "manaLeech" },
	{ "PROT_PHYSICAL", "protPhysical" },
	{ "PROT_FIRE", "protFire" },
	{ "PROT_ICE", "protIce" },
	{ "PROT_ENERGY", "protEnergy" },
	{ "PROT_EARTH", "protEarth" },
	{ "PROT_HOLY", "protHoly" },
	{ "PROT_DEATH", "protDeath" },
})

enums.PROTECTION_BY_ELEMENT = {
	physical = "protPhysical",
	fire = "protFire",
	ice = "protIce",
	energy = "protEnergy",
	earth = "protEarth",
	holy = "protHoly",
	death = "protDeath",
}

return enums
