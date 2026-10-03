#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "application/events.hpp"
#include "assets/shared_files.hpp"
#include "infrastructure/i18n.hpp"
#include "presentation/event_text.hpp"
#include "support/helpers.hpp"

using namespace rpg;
using application::Event;

namespace {

std::string format(const Event& event, const application::RunState& state) {
	const infrastructure::Translator translator(assets::embedded_shared());
	return presentation::EventFormatter(rpg::testing::test_data(), translator).format(event, state);
}

} // namespace

TEST_CASE("events format to English sentences", "[unit][event_text]") {
	using S = std::string;
	using I = std::int64_t;
	const auto [event, expected] = GENERATE(table<Event, std::string>({
	    {Event{"player_attacked", {{"damage", I{12}}, {"crit", false}, {"element", S("fire")}}},
	        "You hit for 12 fire damage."},
	    {Event{"player_attacked", {{"damage", I{30}}, {"crit", true}, {"element", S("physical")}}},
	        "CRITICAL! You hit for 30 physical damage."},
	    {Event{"spell_cast", {{"spellId", S("flame_strike")}, {"damage", I{9}}, {"crit", false}, {"element", S("fire")},
	                             {"mana", I{20}}}},
	        "Flame Strike deals 9 fire damage."},
	    {Event{"potion_used", {{"potionId", S("mana_potion")}, {"amount", I{80}}, {"resource", S("mp")}}},
	        "Mana Potion restores 80 MP."},
	    {Event{"status_applied", {{"target", S("player")}, {"status", S("burn")}, {"turns", I{3}}, {"perTurn", I{2}}}},
	        "You are burning (3 turns)."},
	    {Event{"monster_killed", {{"monsterId", S("dragon")}, {"isBoss", false}}}, "You defeated Dragon!"},
	    {Event{"round_started", {{"round", I{10}}, {"tier", I{0}}, {"cycle", I{0}}, {"monsterId", S("munster")},
	                                {"isBoss", true}, {"hp", I{5}}}},
	        "Round 10: the boss Munster challenges you! (5 HP)"},
	    {Event{"item_sold", {{"uid", I{3}}, {"itemId", S("sword")}, {"gold", I{25}}}}, "You sold Sword for 25 gold."},
	    {application::error_event("not_enough_mana"), "Not enough mana."},
	}));
	const auto engine = rpg::testing::new_engine(rpg::testing::test_data());
	REQUIRE(format(event, engine.state) == expected);
}

TEST_CASE("unknown ids are shown as they are", "[unit][event_text]") {
	const auto engine = rpg::testing::new_engine(rpg::testing::test_data());
	const Event event{"spell_cast", {{"spellId", std::string("ghost_spell")}, {"damage", std::int64_t{1}},
	                                    {"crit", false}, {"element", std::string("fire")}, {"mana", std::int64_t{1}}}};
	REQUIRE(format(event, engine.state).find("ghost_spell") != std::string::npos);
}

TEST_CASE("the monster name comes from the state", "[unit][event_text]") {
	const auto& data = rpg::testing::test_data();
	auto engine = rpg::testing::new_engine(data);
	rpg::testing::fight(engine);
	const std::string name = data.creature(engine.state.monster->creature_id).name;
	REQUIRE(format(Event{"monster_stunned"}, engine.state).starts_with(name));
}

TEST_CASE("item names are found by uid", "[unit][event_text]") {
	auto engine = rpg::testing::new_engine(rpg::testing::test_data());
	engine.state.player.bag.push_back(domain::ItemInstance{77, "bow", "rare", 0, {}});
	REQUIRE(format(Event{"item_dropped",
	                   {{"uid", std::int64_t{77}}, {"itemId", std::string("bow")}, {"rarity", std::string("rare")}}},
	            engine.state) == "Loot: Bow [Rare]!");
	REQUIRE(format(Event{"item_equipped", {{"uid", std::int64_t{999}}, {"slot", std::string("ring")}}}, engine.state)
	            .find("#999") != std::string::npos);
	REQUIRE(format(Event{"item_equipped", {{"uid", std::int64_t{1}}, {"slot", std::string("weapon")}}}, engine.state)
	            .find("Sword") != std::string::npos);
}
