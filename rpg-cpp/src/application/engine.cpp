#include "application/engine.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "application/auto_equip.hpp"
#include "application/battle.hpp"
#include "application/loot.hpp"
#include "application/merchant.hpp"
#include "application/progression.hpp"
#include "application/spawner.hpp"
#include "domain/character.hpp"
#include "domain/formulas.hpp"

namespace rpg::application {

namespace {

void append(std::vector<Event>& target, std::vector<Event> source) {
	target.insert(target.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
}

std::vector<Event> single(Event event) {
	std::vector<Event> events;
	events.push_back(std::move(event));
	return events;
}

} // namespace

std::expected<std::pair<GameEngine, std::vector<Event>>, std::string> GameEngine::new_run(
    const domain::GameData& data, const RunConfig& config, std::uint64_t seed) {
	if (!data.has_vocation(config.vocation_id)) {
		return std::unexpected(std::format("invalid run config: unknown vocation '{}'", config.vocation_id));
	}
	if (!data.balance.has_difficulty(config.difficulty_id)) {
		return std::unexpected(std::format("invalid run config: unknown id: {}", config.difficulty_id));
	}

	const domain::VocationDef& vocation = data.vocation(config.vocation_id);
	RunState state{
	    .seed = seed,
	    .config = config,
	    .player =
	        domain::Player{
	            .name = config.name,
	            .vocation_id = vocation.id,
	            .hp = vocation.start_hp,
	            .mp = vocation.start_mp,
	            .gold = data.balance.starting_gold,
	            .level = 1,
	            .xp = 0,
	            .magic_level = 1,
	            .mana_spent = 0,
	            .potions = {},
	            .equipment = {},
	            .bag = {},
	            .spell_uses = {},
	            .statuses = {},
	            .stun_cooldown = 0,
	            .defending = false,
	        },
	    .phase = domain::Phase::merchant,
	    .round = 0,
	    .turn = 0,
	    .monster = std::nullopt,
	    .merchant_stock = {},
	    .next_item_uid = 1,
	    .death_cause = std::nullopt,
	    .won = false,
	    .stats = {},
	};
	for (const domain::PotionStack& stack : data.balance.starting_potions) {
		state.player.potions[stack.potion_id] = stack.quantity;
	}
	const domain::ItemDef& starter = data.item(vocation.starter_weapon);
	state.player.equipment.emplace(starter.slot, domain::ItemInstance{.uid = state.take_item_uid(),
	                                                 .item_id = starter.id,
	                                                 .rarity = "common",
	                                                 .tier = starter.tier,
	                                                 .affixes = {}});

	GameEngine engine(data, std::move(state), static_cast<std::uint32_t>(seed));
	std::vector<Event> events = single(Event{"run_started",
	    {{"seed", static_cast<std::int64_t>(seed)}, {"vocation", vocation.id}, {"difficulty", config.difficulty_id}}});
	append(events, Merchant(data, engine.rng_, engine.state).enter());
	return std::pair{std::move(engine), std::move(events)};
}

GameEngine GameEngine::restore(const domain::GameData& data, RunState state, std::uint32_t rng_state) {
	return GameEngine(data, std::move(state), rng_state);
}

std::vector<Event> GameEngine::step(const Command& command) {
	std::vector<Event> events = dispatch(command);
	state.stats.record(events, state.round);
	return events;
}

std::vector<Event> GameEngine::dispatch(const Command& command) {
	const domain::Phase phase = state.phase;
	if (is_battle(command)) {
		if (phase != domain::Phase::battle) {
			return single(error_event(error_code::invalid_phase));
		}
		return battle_turn(command);
	}
	if (std::holds_alternative<EndRun>(command) || std::holds_alternative<ContinueRun>(command)) {
		if (phase != domain::Phase::victory) {
			return single(error_event(error_code::invalid_phase));
		}
		return std::holds_alternative<EndRun>(command) ? end_run() : continue_run();
	}
	if (phase != domain::Phase::merchant) {
		return single(error_event(error_code::invalid_phase));
	}
	if (std::holds_alternative<NextFight>(command)) {
		return next_fight();
	}
	return Merchant(*data_, rng_, state).handle(command);
}

std::vector<Event> GameEngine::next_fight() {
	state.round += 1;
	const domain::DifficultyDef& difficulty = data_->balance.difficulty(state.config.difficulty_id);
	auto [monster, info] = spawn_monster(*data_, rng_, state.round, difficulty);
	state.monster = std::move(monster);
	state.phase = domain::Phase::battle;
	state.turn = 1;
	state.merchant_stock.clear();
	return single(
	    Event{"round_started", {{"round", state.round}, {"tier", info.tier}, {"cycle", info.cycle},
	                               {"monsterId", state.monster->creature_id}, {"isBoss", state.monster->is_boss},
	                               {"enemyClass", state.monster->enemy_class}, {"hp", state.monster->hp}}});
}

std::vector<Event> GameEngine::battle_turn(const Command& command) {
	Battle battle(*data_, rng_, state);
	if (auto invalid = battle.validate(command)) {
		return single(std::move(*invalid));
	}
	auto [events, outcome] = battle.play_turn(command);
	switch (outcome) {
	case BattleOutcome::victory:
		append(events, victory());
		break;
	case BattleOutcome::defeat:
		append(events, defeat());
		break;
	case BattleOutcome::ongoing:
		break;
	}
	return std::move(events);
}

std::vector<Event> GameEngine::victory() {
	if (!state.monster.has_value()) {
		throw std::logic_error("victory without a monster");
	}
	domain::Player& player = state.player;
	const domain::MonsterInstance monster = *state.monster;
	std::vector<Event> events = single(Event{"monster_killed",
	    {{"monsterId", monster.creature_id}, {"isBoss", monster.is_boss}, {"enemyClass", monster.enemy_class}}});
	append(events, Progression(*data_).gain_experience(player, monster.xp));

	const std::int64_t gold = rng_.roll(monster.gold_min, monster.gold_max);
	player.gold += gold;
	events.push_back(Event{"gold_looted", {{"amount", gold}}});
	append(events, drops(monster));
	if (state.config.auto_equip) {
		append(events, auto_equip(state, *data_));
	}

	player.statuses.clear();
	player.stun_cooldown = 0;
	player.defending = false;
	const domain::CharacterSheet sheet = domain::build_sheet(player, *data_);
	player.hp = std::min(player.hp, sheet.max_hp);
	player.mp = std::min(player.mp, sheet.max_mp);
	state.monster.reset();
	state.turn = 0;
	if (state.round == data_->balance.final_round) {
		state.won = true;
		state.phase = domain::Phase::victory;
		events.push_back(Event{"run_won", {{"round", state.round}}});
		return events;
	}
	state.phase = domain::Phase::merchant;
	append(events, Merchant(*data_, rng_, state).enter());
	return events;
}

// One rule for the three classes: chance(100) and chance(0) consume nothing (docs/game-design.md §8).
std::vector<Event> GameEngine::drops(const domain::MonsterInstance& monster) {
	const domain::EnemyClassDef& row = data_->balance.enemy_class(monster.enemy_class);
	std::vector<Event> events;
	if (rng_.chance(row.drop_chance_pct)) {
		for (std::int64_t i = 0; i < row.drops; ++i) {
			append(events, drop_item(row));
		}
	}
	if (rng_.chance(row.potion_drop_pct)) {
		append(events, drop_potion());
	}
	return events;
}

std::vector<Event> GameEngine::drop_item(const domain::EnemyClassDef& row) {
	const domain::Balance& balance = data_->balance;
	const ItemRequest request{
	    .vocation = &data_->vocation(state.player.vocation_id),
	    .tier = domain::round_info(state.round, balance, data_->tier_count()).tier,
	    .weights = &row.rarity_weights,
	    .uid = state.next_item_uid,
	};
	auto item = generate_item(*data_, rng_, request);
	if (!item.has_value()) {
		return {};
	}
	state.take_item_uid();
	std::vector<Event> events =
	    single(Event{"item_dropped", {{"uid", item->uid}, {"itemId", item->item_id}, {"rarity", item->rarity}}});
	if (std::cmp_greater_equal(state.player.bag.size(), balance.bag_capacity)) {
		const std::int64_t value = domain::item_value(*item, *data_);
		state.player.gold += value;
		events.push_back(Event{"item_auto_sold", {{"uid", item->uid}, {"itemId", item->item_id}, {"gold", value}}});
	} else {
		state.player.bag.push_back(std::move(*item));
	}
	return events;
}

std::vector<Event> GameEngine::drop_potion() {
	std::vector<const domain::PotionDef*> unlocked;
	for (const domain::PotionDef& potion : data_->potions) {
		if (potion.unlock_round <= state.round) {
			unlocked.push_back(&potion);
		}
	}
	if (unlocked.empty()) {
		return {};
	}
	const domain::PotionDef& potion = *rng_.pick(unlocked);
	state.player.potions[potion.id] = state.player.potion_count(potion.id) + 1;
	return single(Event{"potion_dropped", {{"potionId", potion.id}}});
}

std::vector<Event> GameEngine::end_run() {
	state.phase = domain::Phase::game_over;
	state.death_cause.reset();
	return single(Event{"run_ended", {{"won", state.won}}});
}

std::vector<Event> GameEngine::continue_run() {
	state.phase = domain::Phase::merchant;
	return Merchant(*data_, rng_, state).enter();
}

std::vector<Event> GameEngine::defeat() {
	const std::string monster_id = state.monster.has_value() ? state.monster->creature_id : std::string{};
	state.phase = domain::Phase::game_over;
	state.death_cause = monster_id;
	return single(Event{"player_died", {{"monsterId", monster_id}, {"round", state.round}}});
}

} // namespace rpg::application
