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
} // namespace stat

// The protection stat of an element: "fire" → "protFire".
std::string protection_stat(std::string_view element);

// The run phase of the engine state machine. A real enum: unlike ids, phases are a closed set owned by the code.
enum class Phase { merchant, battle, game_over };

std::string_view to_string(Phase phase);
Phase phase_from_string(std::string_view text);

} // namespace rpg::domain
