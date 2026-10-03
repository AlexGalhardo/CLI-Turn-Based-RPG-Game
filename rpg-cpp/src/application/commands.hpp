#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "domain/enums.hpp"

namespace rpg::application {

// Player commands. Each one is its own small value type and Command is their closed union (std::variant), the
// C++ counterpart of the reference's frozen dataclasses.
struct Attack {
	bool operator==(const Attack&) const = default;
};
struct Cast {
	std::string spell_id;
	bool operator==(const Cast&) const = default;
};
struct UsePotion {
	std::string potion_id;
	bool operator==(const UsePotion&) const = default;
};
struct Defend {
	bool operator==(const Defend&) const = default;
};
struct NextFight {
	bool operator==(const NextFight&) const = default;
};
struct BuyPotion {
	std::string potion_id;
	std::int64_t quantity = 0;
	bool operator==(const BuyPotion&) const = default;
};
struct SellItem {
	std::int64_t uid = 0;
	bool operator==(const SellItem&) const = default;
};
struct Equip {
	std::int64_t uid = 0;
	bool operator==(const Equip&) const = default;
};
struct Unequip {
	domain::Slot slot;
	bool operator==(const Unequip&) const = default;
};
struct BuyStockItem {
	std::int64_t index = 0;
	bool operator==(const BuyStockItem&) const = default;
};

// Victory phase: close the won run (history + Hall of Fame).
struct EndRun {
	bool operator==(const EndRun&) const = default;
};
// Victory phase: keep playing endlessly after beating the final boss.
struct ContinueRun {
	bool operator==(const ContinueRun&) const = default;
};

using Command = std::variant<Attack, Cast, UsePotion, Defend, NextFight, BuyPotion, SellItem, Equip, Unequip,
    BuyStockItem, EndRun, ContinueRun>;

// Whether the command belongs to the battle phase.
bool is_battle(const Command& command);

// The JSON of a command, exactly like the reference (the `type` field plus the fields of that type).
nlohmann::json command_to_json(const Command& command);

// Parses a command object; throws std::invalid_argument for an unknown type.
Command command_from_json(const nlohmann::json& document);

// Helper for std::visit with one lambda per alternative.
template <typename... Lambdas> struct Overloaded : Lambdas... {
	using Lambdas::operator()...;
};

} // namespace rpg::application
