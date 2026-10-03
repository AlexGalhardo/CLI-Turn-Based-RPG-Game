#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "application/commands.hpp"
#include "application/run_state.hpp"
#include "domain/definitions.hpp"

namespace rpg::application {

// Auto-battle modes (docs/game-design.md §13); their ids are the keys of `balance.autoBattle.modes`.
enum class AutoBattleMode { melee, spells, balanced };

inline constexpr std::array<AutoBattleMode, 3> kAutoBattleModes{
    AutoBattleMode::melee, AutoBattleMode::spells, AutoBattleMode::balanced};

std::string_view to_string(AutoBattleMode mode);

// Picks the player's battle commands from the run state only. It lives in the application layer, not in the engine:
// the commands it returns are ordinary commands, so a fight played by the policy replays like any other. Every
// command it returns is valid (affordable spells, owned potions).
class AutoBattlePolicy {
public:
	AutoBattlePolicy(const domain::GameData& data, AutoBattleMode mode);

	[[nodiscard]] Command choose(const RunState& state) const;

private:
	const domain::GameData* data_;
	const domain::AutoBattleModeDef* mode_;

	[[nodiscard]] Command offense(const RunState& state) const;
	[[nodiscard]] std::optional<Command> heal(const RunState& state) const;
	[[nodiscard]] std::vector<const domain::SpellDef*> affordable(const RunState& state, std::string_view kind) const;
	[[nodiscard]] const domain::PotionDef* best_potion(const RunState& state, std::string_view resource) const;
	[[nodiscard]] bool telegraph_pending(const RunState& state) const;
};

} // namespace rpg::application
