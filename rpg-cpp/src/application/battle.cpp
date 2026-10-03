#include "application/battle.hpp"

#include <algorithm>
#include <stdexcept>

#include "domain/formulas.hpp"

namespace rpg::application {

namespace {

constexpr std::string_view stun_status = "stun";
constexpr std::int64_t stun_cooldown_turns = 2;
constexpr std::string_view target_player = "player";
constexpr std::string_view target_monster = "monster";

auto find_status(std::vector<domain::ActiveStatus>& statuses, std::string_view status_id) {
	return std::ranges::find(statuses, status_id, &domain::ActiveStatus::status_id);
}

bool has_status(const std::vector<domain::ActiveStatus>& statuses, std::string_view status_id) {
	return std::ranges::contains(statuses, status_id, &domain::ActiveStatus::status_id);
}

// Removes a stun if present and reports whether there was one.
bool consume_stun(std::vector<domain::ActiveStatus>& statuses) {
	const auto found = find_status(statuses, stun_status);
	if (found == statuses.end()) {
		return false;
	}
	statuses.erase(found);
	return true;
}

} // namespace

domain::MonsterInstance& Battle::monster() {
	if (!state_->monster.has_value()) {
		throw std::logic_error("battle without a monster");
	}
	return *state_->monster;
}

std::int64_t Battle::monster_resistance(std::string_view element) const {
	return data_->creature(state_->monster->creature_id).resistance(element);
}

std::int64_t Battle::spell_cost(const domain::SpellDef& spell) const {
	const auto& level =
	    domain::spell_level_for_uses(domain::count_of(player().spell_uses, spell.id), data_->balance.spell_levels);
	return domain::pct(spell.mana, level.mana_pct);
}

std::optional<Event> Battle::validate(const Command& command) const {
	if (const auto* cast = std::get_if<Cast>(&command)) {
		const auto& vocation = data_->vocation(player().vocation_id);
		if (!std::ranges::contains(vocation.spells, cast->spell_id)) {
			return error_event(error_code::unknown_spell);
		}
		if (player().mp < spell_cost(data_->spell(cast->spell_id))) {
			return error_event(error_code::not_enough_mana);
		}
	} else if (const auto* potion = std::get_if<UsePotion>(&command)) {
		if (!data_->has_potion(potion->potion_id)) {
			return error_event(error_code::unknown_potion);
		}
		if (player().potion_count(potion->potion_id) <= 0) {
			return error_event(error_code::no_potion);
		}
	}
	return std::nullopt;
}

std::pair<std::vector<Event>, BattleOutcome> Battle::play_turn(const Command& command) {
	std::vector<Event> events;
	player_action(command, events);
	if (monster().hp <= 0) {
		return {std::move(events), BattleOutcome::victory};
	}
	while (true) {
		if (monster_phase(events)) {
			return {std::move(events), BattleOutcome::victory};
		}
		if (player().hp <= 0) {
			return {std::move(events), BattleOutcome::defeat};
		}
		if (end_of_turn(events)) {
			return {std::move(events), BattleOutcome::defeat};
		}
		if (!consume_stun(player().statuses)) {
			return {std::move(events), BattleOutcome::ongoing};
		}
		player().stun_cooldown = stun_cooldown_turns;
		events.emplace_back("player_stunned");
	}
}

// ── step 1: player action ─────────────────────────────────────────────────────────────────────────────────────────

void Battle::player_action(const Command& command, std::vector<Event>& events) {
	std::visit(Overloaded{
	               [&](const Attack&) { melee(events); },
	               [&](const Cast& cast_command) { cast(data_->spell(cast_command.spell_id), events); },
	               [&](const UsePotion& potion) { drink(potion.potion_id, events); },
	               [&](const Defend&) {
		               player().defending = true;
		               events.emplace_back("player_defended");
	               },
	               [](const auto&) {},
	           },
	    command);
}

void Battle::melee(std::vector<Event>& events) {
	const domain::CharacterSheet current = sheet();
	const std::int64_t base =
	    domain::pct(rng_->roll(current.melee_min, current.melee_max), 100 + current.physical_damage);
	auto [damage, crit] = roll_crit(base, current);
	damage = resisted(damage, current.weapon_element);
	hit_monster(damage);
	events.push_back(
	    Event{"player_attacked", {{"damage", damage}, {"crit", crit}, {"element", current.weapon_element}}});
	leech(damage, current, events);
}

void Battle::cast(const domain::SpellDef& spell, std::vector<Event>& events) {
	domain::Player& caster = player();
	const domain::CharacterSheet current = sheet();
	const auto& level =
	    domain::spell_level_for_uses(domain::count_of(caster.spell_uses, spell.id), data_->balance.spell_levels);
	const std::int64_t cost = domain::pct(spell.mana, level.mana_pct);
	caster.mp -= cost;
	const std::int64_t bonus = caster.level * spell.per_level + caster.magic_level * spell.per_magic_level;
	const std::int64_t amount = domain::pct(
	    domain::pct(rng_->roll(spell.min + bonus, spell.max + bonus), level.effect_pct), 100 + current.spell_power);

	if (spell.kind == "attack") {
		auto [damage, crit] = roll_crit(amount, current);
		damage = resisted(damage, spell.element);
		hit_monster(damage);
		events.push_back(Event{"spell_cast",
		    {{"spellId", spell.id}, {"damage", damage}, {"crit", crit}, {"element", spell.element}, {"mana", cost}}});
		leech(damage, current, events);

		const auto& bonus_effect = spell.level3_bonus;
		if (level.level == 3 && bonus_effect.status.has_value() && rng_->chance(bonus_effect.chance)) {
			const std::int64_t per_turn =
			    std::max<std::int64_t>(1, domain::pct(damage, data_->balance.spell_status_damage_pct));
			apply_status(target_monster, *bonus_effect.status, per_turn, events);
		}
	} else {
		const std::int64_t healed = std::min(amount, current.max_hp - caster.hp);
		caster.hp += healed;
		events.push_back(Event{"spell_healed", {{"spellId", spell.id}, {"amount", healed}, {"mana", cost}}});
		if (level.level == 3 && spell.level3_bonus.cleanse) {
			const std::vector<domain::ActiveStatus> removed = std::exchange(caster.statuses, {});
			for (const domain::ActiveStatus& status : removed) {
				events.push_back(
				    Event{"status_expired", {{"target", std::string(target_player)}, {"status", status.status_id}}});
			}
		}
	}

	for (Event& event : progression_.after_cast(caster, spell, cost)) {
		events.push_back(std::move(event));
	}
}

void Battle::drink(const std::string& potion_id, std::vector<Event>& events) {
	domain::Player& drinker = player();
	const domain::CharacterSheet current = sheet();
	const domain::PotionDef& potion = data_->potion(potion_id);
	drinker.potions[potion_id] -= 1;
	const std::int64_t amount = rng_->roll(potion.min, potion.max);
	std::int64_t restored = 0;
	if (potion.resource == "hp") {
		restored = std::min(amount, current.max_hp - drinker.hp);
		drinker.hp += restored;
	} else {
		restored = std::min(amount, current.max_mp - drinker.mp);
		drinker.mp += restored;
	}
	events.push_back(
	    Event{"potion_used", {{"potionId", potion_id}, {"amount", restored}, {"resource", potion.resource}}});
}

std::pair<std::int64_t, bool> Battle::roll_crit(std::int64_t damage, const domain::CharacterSheet& current) {
	if (rng_->chance(current.crit_chance)) {
		return {domain::pct(damage, data_->balance.crit_multiplier_pct + current.crit_damage), true};
	}
	return {damage, false};
}

std::int64_t Battle::resisted(std::int64_t damage, std::string_view element) const {
	const std::int64_t resistance = monster_resistance(element);
	if (resistance == 0) {
		return 0;
	}
	return std::max<std::int64_t>(1, domain::pct(damage, resistance));
}

void Battle::hit_monster(std::int64_t damage) { monster().hp = std::max<std::int64_t>(0, monster().hp - damage); }

void Battle::leech(std::int64_t damage, const domain::CharacterSheet& current, std::vector<Event>& events) {
	domain::Player& leecher = player();
	const std::int64_t hp_gain = std::min(domain::pct(damage, current.life_leech), current.max_hp - leecher.hp);
	const std::int64_t mp_gain = std::min(domain::pct(damage, current.mana_leech), current.max_mp - leecher.mp);
	if (hp_gain <= 0 && mp_gain <= 0) {
		return;
	}
	leecher.hp += std::max<std::int64_t>(0, hp_gain);
	leecher.mp += std::max<std::int64_t>(0, mp_gain);
	events.push_back(
	    Event{"leeched", {{"hp", std::max<std::int64_t>(0, hp_gain)}, {"mp", std::max<std::int64_t>(0, mp_gain)}}});
}

// ── step 3: monster phase ─────────────────────────────────────────────────────────────────────────────────────────

// True when the monster died from its own status ticks.
bool Battle::monster_phase(std::vector<Event>& events) {
	domain::MonsterInstance& foe = monster();
	tick(target_monster, foe.statuses, events);
	if (foe.hp <= 0) {
		return true;
	}

	if (consume_stun(foe.statuses)) {
		foe.stun_cooldown = stun_cooldown_turns;
		events.emplace_back("monster_stunned");
		return false;
	}

	if (foe.is_boss) {
		const std::int64_t every = data_->balance.boss_telegraph_every;
		const std::int64_t position = foe.boss_actions % (every + 1);
		foe.boss_actions += 1;
		if (const auto& charge_id = data_->creature(foe.creature_id).charge_attack; charge_id.has_value()) {
			if (position == every - 1) {
				const domain::MonsterAttack& charge = foe.attack(*charge_id);
				events.push_back(Event{"boss_telegraph", {{"attackId", charge.id}, {"element", charge.element}}});
				return false;
			}
			if (position == every) {
				// A copy: resolving the attack must not depend on the monster's attack list staying put.
				const domain::MonsterAttack charge = foe.attack(*charge_id);
				resolve_monster_attack(charge, true, events);
				return false;
			}
		}
	}

	std::vector<std::int64_t> weights;
	weights.reserve(foe.attacks.size());
	for (const domain::MonsterAttack& attack : foe.attacks) {
		weights.push_back(attack.weight);
	}
	const domain::MonsterAttack attack = foe.attacks[rng_->weighted(weights)];
	resolve_monster_attack(attack, false, events);
	return false;
}

void Battle::resolve_monster_attack(const domain::MonsterAttack& attack, bool charged, std::vector<Event>& events) {
	domain::Player& target = player();
	const domain::CharacterSheet current = sheet();
	const bool physical = attack.element == domain::element::physical;

	if (rng_->chance(current.dodge)) {
		events.push_back(Event{"attack_dodged", {{"attackId", attack.id}}});
		return;
	}
	if (physical && rng_->chance(current.parry)) {
		events.push_back(Event{"attack_parried", {{"attackId", attack.id}}});
		return;
	}

	std::int64_t damage = rng_->roll(attack.min, attack.max);
	if (charged) {
		damage = domain::pct(damage, data_->balance.boss_charge_damage_pct);
	}
	if (physical) {
		damage = domain::armor_mitigation(damage, current.armor);
	}
	damage = domain::pct(damage, 100 - current.protection(attack.element));
	if (target.defending) {
		damage = domain::pct(damage, data_->balance.defend_damage_pct);
	}
	damage = std::max<std::int64_t>(1, damage);
	target.hp = std::max<std::int64_t>(0, target.hp - damage);
	events.push_back(Event{"monster_attacked",
	    {{"attackId", attack.id}, {"damage", damage}, {"element", attack.element}, {"charged", charged}}});

	if (attack.status.has_value() && rng_->chance(attack.status->chance)) {
		const std::int64_t per_turn = std::max<std::int64_t>(1, domain::pct(damage, attack.status->damage_pct));
		apply_status(target_player, attack.status->status, per_turn, events);
	}
}

// ── step 5: end of turn ───────────────────────────────────────────────────────────────────────────────────────────

// True when the player died from status ticks.
bool Battle::end_of_turn(std::vector<Event>& events) {
	domain::Player& hero = player();
	tick(target_player, hero.statuses, events);
	if (hero.hp <= 0) {
		return true;
	}

	const domain::CharacterSheet current = sheet();
	const std::int64_t hp_gain = std::max<std::int64_t>(0, std::min(current.hp_regen, current.max_hp - hero.hp));
	const std::int64_t mp_gain = std::max<std::int64_t>(0, std::min(current.mp_regen, current.max_mp - hero.mp));
	hero.hp += hp_gain;
	hero.mp += mp_gain;
	if (hp_gain > 0 || mp_gain > 0) {
		events.push_back(Event{"regenerated", {{"hp", hp_gain}, {"mp", mp_gain}}});
	}

	hero.defending = false;
	hero.stun_cooldown = std::max<std::int64_t>(0, hero.stun_cooldown - 1);
	monster().stun_cooldown = std::max<std::int64_t>(0, monster().stun_cooldown - 1);
	state_->turn += 1;
	return false;
}

// ── statuses ──────────────────────────────────────────────────────────────────────────────────────────────────────

void Battle::apply_status(
    std::string_view target, const std::string& status_id, std::int64_t per_turn, std::vector<Event>& events) {
	const domain::StatusDef& definition = data_->status(status_id);
	const bool on_player = target == target_player;
	if (!on_player && monster_resistance(definition.element) == 0) {
		return;
	}
	std::vector<domain::ActiveStatus>& statuses = on_player ? player().statuses : monster().statuses;
	const std::int64_t cooldown = on_player ? player().stun_cooldown : monster().stun_cooldown;

	if (definition.kind == stun_status) {
		if (cooldown > 0 || has_status(statuses, stun_status)) {
			return;
		}
		statuses.push_back(domain::ActiveStatus{std::string(stun_status), definition.turns, 0});
		events.push_back(Event{"status_applied", {{"target", std::string(target)}, {"status", std::string(stun_status)},
		                                             {"turns", definition.turns}, {"perTurn", std::int64_t{0}}}});
		return;
	}

	auto found = find_status(statuses, status_id);
	if (found == statuses.end()) {
		statuses.push_back(domain::ActiveStatus{status_id, definition.turns, per_turn});
		found = std::prev(statuses.end());
	} else {
		found->turns = definition.turns;
		found->per_turn = std::max(found->per_turn, per_turn);
	}
	events.push_back(Event{"status_applied", {{"target", std::string(target)}, {"status", status_id},
	                                             {"turns", found->turns}, {"perTurn", found->per_turn}}});
}

void Battle::tick(std::string_view target, std::vector<domain::ActiveStatus>& statuses, std::vector<Event>& events) {
	// Iterates a copy: expired statuses are erased from the live list while ticking.
	const std::vector<domain::ActiveStatus> snapshot = statuses;
	for (const domain::ActiveStatus& status : snapshot) {
		const domain::StatusDef& definition = data_->status(status.status_id);
		if (definition.kind != "dot") {
			continue;
		}
		const std::int64_t damage = status_damage(target, status.per_turn, definition.element);
		if (target == target_player) {
			player().hp = std::max<std::int64_t>(0, player().hp - damage);
		} else {
			hit_monster(damage);
		}
		events.push_back(Event{
		    "status_ticked", {{"target", std::string(target)}, {"status", status.status_id}, {"damage", damage}}});

		const auto live = find_status(statuses, status.status_id);
		live->turns -= 1;
		if (live->turns <= 0) {
			statuses.erase(live);
			events.push_back(Event{"status_expired", {{"target", std::string(target)}, {"status", status.status_id}}});
		}
	}
}

std::int64_t Battle::status_damage(std::string_view target, std::int64_t per_turn, std::string_view element) const {
	if (target == target_player) {
		return std::max<std::int64_t>(1, domain::pct(per_turn, 100 - sheet().protection(element)));
	}
	const std::int64_t resistance = monster_resistance(element);
	if (resistance == 0) {
		return 0;
	}
	return std::max<std::int64_t>(1, domain::pct(per_turn, resistance));
}

} // namespace rpg::application
