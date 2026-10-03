package domain

// Element is a damage element (Tibia elements).
type Element string

// Elements in their canonical order.
const (
	Physical Element = "physical"
	Fire     Element = "fire"
	Ice      Element = "ice"
	Energy   Element = "energy"
	Earth    Element = "earth"
	Holy     Element = "holy"
	Death    Element = "death"
)

// Elements lists every element in canonical order (never iterate a map to make game decisions).
var Elements = []Element{Physical, Fire, Ice, Energy, Earth, Holy, Death}

// Slot is an equipment slot.
type Slot string

// Equipment slots in canonical order.
const (
	SlotHelmet Slot = "helmet"
	SlotArmor  Slot = "armor"
	SlotLegs   Slot = "legs"
	SlotBoots  Slot = "boots"
	SlotAmulet Slot = "amulet"
	SlotRing   Slot = "ring"
	SlotWeapon Slot = "weapon"
	SlotShield Slot = "shield"
)

// Slots lists every slot in canonical order.
var Slots = []Slot{SlotHelmet, SlotArmor, SlotLegs, SlotBoots, SlotAmulet, SlotRing, SlotWeapon, SlotShield}

// Phase is the run phase of the engine state machine.
type Phase string

// Run phases.
const (
	PhaseMerchant Phase = "merchant"
	PhaseBattle   Phase = "battle"
	PhaseVictory  Phase = "victory"
	PhaseGameOver Phase = "game_over"
)

// EnemyClass is the class of a spawned monster (docs/game-design.md §3).
type EnemyClass string

// Enemy classes in the order of balance.enemyClasses.
const (
	EnemyNormal EnemyClass = "normal"
	EnemyElite  EnemyClass = "elite"
	EnemyBoss   EnemyClass = "boss"
)

// EnemyClasses lists every enemy class in canonical order.
var EnemyClasses = []EnemyClass{EnemyNormal, EnemyElite, EnemyBoss}

// EquipmentSlotOrder is the slot order of auto-equip and the equipment screen (docs/game-design.md §8.1).
var EquipmentSlotOrder = []Slot{SlotWeapon, SlotShield, SlotHelmet, SlotArmor, SlotLegs, SlotBoots, SlotRing, SlotAmulet}

// Stat is an equipment stat key.
type Stat string

// Stat keys (docs/game-design.md §4).
const (
	StatAttack         Stat = "attack"
	StatArmor          Stat = "armor"
	StatMaxHp          Stat = "maxHp"
	StatMaxMp          Stat = "maxMp"
	StatHpRegen        Stat = "hpRegen"
	StatMpRegen        Stat = "mpRegen"
	StatCritChance     Stat = "critChance"
	StatCritDamage     Stat = "critDamage"
	StatSpellPower     Stat = "spellPower"
	StatPhysicalDamage Stat = "physicalDamage"
	StatDodge          Stat = "dodge"
	StatParry          Stat = "parry"
	StatLifeLeech      Stat = "lifeLeech"
	StatManaLeech      Stat = "manaLeech"
	StatProtPhysical   Stat = "protPhysical"
	StatProtFire       Stat = "protFire"
	StatProtIce        Stat = "protIce"
	StatProtEnergy     Stat = "protEnergy"
	StatProtEarth      Stat = "protEarth"
	StatProtHoly       Stat = "protHoly"
	StatProtDeath      Stat = "protDeath"
)

// Stats lists every stat key in canonical order (the order of the equipment comparison).
var Stats = []Stat{
	StatAttack, StatArmor, StatMaxHp, StatMaxMp, StatHpRegen, StatMpRegen, StatCritChance, StatCritDamage,
	StatSpellPower, StatPhysicalDamage, StatDodge, StatParry, StatLifeLeech, StatManaLeech, StatProtPhysical,
	StatProtFire, StatProtIce, StatProtEnergy, StatProtEarth, StatProtHoly, StatProtDeath,
}

// ProtectionByElement maps each element to its protection stat.
var ProtectionByElement = map[Element]Stat{
	Physical: StatProtPhysical,
	Fire:     StatProtFire,
	Ice:      StatProtIce,
	Energy:   StatProtEnergy,
	Earth:    StatProtEarth,
	Holy:     StatProtHoly,
	Death:    StatProtDeath,
}
