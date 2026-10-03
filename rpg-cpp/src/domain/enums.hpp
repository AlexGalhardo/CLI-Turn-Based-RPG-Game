#pragma once

#include <array>
#include <string>
#include <string_view>

namespace rpg::domain {

// Elements, slots and stats are data-driven ids (they come from shared/data and go to saves as strings), so they are
// plain strings with named constants, like the string enums of the reference implementation.
using Element = std::string;
using Slot = std::string;
using Stat = std::string;

namespace element {
inline constexpr std::string_view physical = "physical";
inline constexpr std::string_view fire = "fire";
inline constexpr std::string_view ice = "ice";
inline constexpr std::string_view energy = "energy";
inline constexpr std::string_view earth = "earth";
inline constexpr std::string_view holy = "holy";
inline constexpr std::string_view death = "death";
} // namespace element

// Every element in canonical order (never iterate a map to make game decisions).
inline constexpr std::array<std::string_view, 7> kElements{
    element::physical,
    element::fire,
    element::ice,
    element::energy,
    element::earth,
    element::holy,
    element::death,
};

namespace slot {
inline constexpr std::string_view helmet = "helmet";
inline constexpr std::string_view armor = "armor";
inline constexpr std::string_view legs = "legs";
inline constexpr std::string_view boots = "boots";
inline constexpr std::string_view amulet = "amulet";
inline constexpr std::string_view ring = "ring";
inline constexpr std::string_view weapon = "weapon";
inline constexpr std::string_view shield = "shield";
} // namespace slot

// Every equipment slot in canonical order.
inline constexpr std::array<std::string_view, 8> kSlots{
    slot::helmet,
    slot::armor,
    slot::legs,
    slot::boots,
    slot::amulet,
    slot::ring,
    slot::weapon,
    slot::shield,
};

// The order of the equipment screen and of auto-equip (docs/game-design.md §8.1, docs/tui.md).
inline constexpr std::array<std::string_view, 8> kEquipmentSlotOrder{
    slot::weapon,
    slot::shield,
    slot::helmet,
    slot::armor,
    slot::legs,
    slot::boots,
    slot::ring,
    slot::amulet,
};

// Stat keys (docs/game-design.md §4).
namespace stat {
inline constexpr std::string_view attack = "attack";
inline constexpr std::string_view armor = "armor";
inline constexpr std::string_view max_hp = "maxHp";
inline constexpr std::string_view max_mp = "maxMp";
inline constexpr std::string_view hp_regen = "hpRegen";
inline constexpr std::string_view mp_regen = "mpRegen";
inline constexpr std::string_view crit_chance = "critChance";
inline constexpr std::string_view crit_damage = "critDamage";
inline constexpr std::string_view spell_power = "spellPower";
inline constexpr std::string_view physical_damage = "physicalDamage";
inline constexpr std::string_view dodge = "dodge";
inline constexpr std::string_view parry = "parry";
inline constexpr std::string_view life_leech = "lifeLeech";
inline constexpr std::string_view mana_leech = "manaLeech";
inline constexpr std::string_view prot_physical = "protPhysical";
inline constexpr std::string_view prot_fire = "protFire";
inline constexpr std::string_view prot_ice = "protIce";
inline constexpr std::string_view prot_energy = "protEnergy";
inline constexpr std::string_view prot_earth = "protEarth";
inline constexpr std::string_view prot_holy = "protHoly";
inline constexpr std::string_view prot_death = "protDeath";
} // namespace stat

// Every stat key in canonical order (the reference's `Stat` enum order).
inline constexpr std::array<std::string_view, 21> kStats{
    stat::attack,
    stat::armor,
    stat::max_hp,
    stat::max_mp,
    stat::hp_regen,
    stat::mp_regen,
    stat::crit_chance,
    stat::crit_damage,
    stat::spell_power,
    stat::physical_damage,
    stat::dodge,
    stat::parry,
    stat::life_leech,
    stat::mana_leech,
    stat::prot_physical,
    stat::prot_fire,
    stat::prot_ice,
    stat::prot_energy,
    stat::prot_earth,
    stat::prot_holy,
    stat::prot_death,
};

// Enemy classes (docs/game-design.md §3): ids of `balance.enemyClasses`, saved as strings.
namespace enemy_class {
inline constexpr std::string_view normal = "normal";
inline constexpr std::string_view elite = "elite";
inline constexpr std::string_view boss = "boss";
} // namespace enemy_class

inline constexpr std::array<std::string_view, 3> kEnemyClasses{
    enemy_class::normal,
    enemy_class::elite,
    enemy_class::boss,
};

// The protection stat of an element: "fire" → "protFire".
std::string protection_stat(std::string_view element);

// The run phase of the engine state machine. A real enum: unlike ids, phases are a closed set owned by the code.
enum class Phase { merchant, battle, victory, game_over };

std::string_view to_string(Phase phase);
Phase phase_from_string(std::string_view text);

} // namespace rpg::domain
