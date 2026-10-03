// Informative screen bodies of the controller (character sheet, game over, Hall of Fame, bestiary, achievements).
#include <algorithm>
#include <array>
#include <format>

#include "domain/character.hpp"
#include "domain/formulas.hpp"
#include "presentation/controller.hpp"

namespace rpg::presentation {

namespace {

std::string join_or_dash(const std::vector<std::string>& values) {
	if (values.empty()) {
		return "—";
	}
	std::string result;
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i > 0) {
			result += ", ";
		}
		result += values[i];
	}
	return result;
}

std::string date_of(const std::string& timestamp) {
	return timestamp.substr(0, std::min<std::size_t>(10, timestamp.size()));
}

} // namespace

application::ProfileService Controller::profile_service() {
	if (session.has_value()) {
		return session->profile;
	}
	application::Profile loaded;
	try {
		loaded = services.repositories.profile->load();
	} catch (const std::exception& failure) {
		error = failure.what();
	}
	return application::ProfileService(*services.data, std::move(loaded));
}

std::vector<std::string> Controller::body() {
	switch (view) {
	case View::character:
		return character_sheet();
	case View::game_over:
		return game_over_summary();
	case View::hall_of_fame:
		return hall_of_fame();
	case View::bestiary:
		return bestiary();
	case View::achievements:
		return achievements();
	case View::merchant: {
		const domain::Player& player = session->state().player;
		return {t("merchant.welcome", {{"name", player.name}, {"gold", player.gold}})};
	}
	case View::sell:
		if (session->state().player.bag.empty()) {
			return {t("merchant.empty_bag")};
		}
		return {};
	case View::stock:
		if (session->state().merchant_stock.empty()) {
			return {t("merchant.empty_stock")};
		}
		return {};
	case View::potions: {
		const auto& potions = session->state().player.potions;
		const bool any = std::ranges::any_of(potions, [](const auto& entry) { return entry.second > 0; });
		if (any) {
			return {};
		}
		return {t("battle.no_potions")};
	}
	default:
		return {};
	}
}

std::vector<std::string> Controller::character_sheet() const {
	const domain::GameData& data = *services.data;
	const domain::Player& player = session->state().player;
	const domain::CharacterSheet sheet = domain::build_sheet(player, data);
	std::vector<std::string> lines{
	    t("character.level",
	        {{"level", player.level}, {"xp", player.xp}, {"next", domain::xp_for_level(player.level + 1)}}),
	    t("character.magic_level", {{"magicLevel", player.magic_level}, {"spent", player.mana_spent},
	                                   {"next", domain::mana_for_magic_level(player.magic_level, data.balance)}}),
	    t("character.hp_mp", {{"hp", player.hp}, {"maxHp", sheet.max_hp}, {"mp", player.mp}, {"maxMp", sheet.max_mp}}),
	    t("character.melee",
	        {{"min", sheet.melee_min}, {"max", sheet.melee_max}, {"element", t("element." + sheet.weapon_element)}}),
	};
	const std::array<std::pair<std::string_view, std::int64_t>, 11> stats{{
	    {"stat.armor", sheet.armor},
	    {"stat.hpRegen", sheet.hp_regen},
	    {"stat.mpRegen", sheet.mp_regen},
	    {"stat.critChance", sheet.crit_chance},
	    {"stat.critDamage", sheet.crit_damage},
	    {"stat.spellPower", sheet.spell_power},
	    {"stat.physicalDamage", sheet.physical_damage},
	    {"stat.dodge", sheet.dodge},
	    {"stat.parry", sheet.parry},
	    {"stat.lifeLeech", sheet.life_leech},
	    {"stat.manaLeech", sheet.mana_leech},
	}};
	for (const auto& [key, value] : stats) {
		if (value != 0) {
			lines.push_back(t("character.stat_line", {{"stat", t(key)}, {"value", value}}));
		}
	}
	for (const std::string_view element : domain::kElements) {
		if (const std::int64_t value = sheet.protection(element); value != 0) {
			lines.push_back(t("character.stat_line",
			    {{"stat", t("element." + std::string(element))}, {"value", std::format("{}%", value)}}));
		}
	}
	lines.emplace_back();
	lines.push_back(t("character.equipment"));
	for (const std::string_view slot : domain::kSlots) {
		const std::string slot_name = t("slot." + std::string(slot));
		if (const auto found = player.equipment.find(slot); found != player.equipment.end()) {
			lines.push_back(t("character.slot", {{"slot", slot_name}, {"item", data.item(found->second.item_id).name},
			                                        {"rarity", t("rarity." + found->second.rarity)}}));
		} else {
			lines.push_back(t("character.empty_slot", {{"slot", slot_name}}));
		}
	}
	lines.push_back(t("character.bag", {{"count", player.bag.size()}, {"capacity", data.balance.bag_capacity}}));
	return lines;
}

std::vector<std::string> Controller::game_over_summary() const {
	const application::RunState& state = session->state();
	const std::string cause = state.death_cause.value_or("");
	const std::string monster = cause.empty() ? std::string("?") : services.data->creature(cause).name;
	return {
	    t("gameover.summary", {{"name", state.player.name}, {"vocation", t("vocation." + state.player.vocation_id)},
	                              {"round", state.round}, {"monster", monster}}),
	    t("gameover.stats", {{"level", state.player.level}, {"damage", state.stats.damage_dealt},
	                            {"kills", state.stats.total_kills()}, {"bosses", state.stats.bosses_killed}}),
	};
}

std::vector<std::string> Controller::hall_of_fame() {
	const std::vector<application::HallOfFameEntry> hall = profile_service().profile.hall_of_fame;
	if (hall.empty()) {
		return {t("hall.empty")};
	}
	std::vector<std::string> lines;
	for (std::size_t index = 0; index < hall.size(); ++index) {
		const application::HallOfFameEntry& entry = hall[index];
		lines.push_back(t(
		    "hall.entry", {{"position", index + 1}, {"name", entry.name}, {"vocation", t("vocation." + entry.vocation)},
		                      {"difficulty", t("difficulty." + entry.difficulty)}, {"round", entry.round},
		                      {"level", entry.level}, {"date", date_of(entry.ended_at)}}));
	}
	return lines;
}

std::vector<std::string> Controller::bestiary() {
	const domain::GameData& data = *services.data;
	const application::ProfileService profile = profile_service();
	std::vector<const domain::MonsterDef*> creatures;
	for (const auto& monster : data.monsters) {
		creatures.push_back(&monster);
	}
	for (const auto& boss : data.bosses) {
		creatures.push_back(&boss);
	}
	// Tier, then regular monsters before the boss, then name (byte order, like the reference's default sort).
	std::ranges::stable_sort(creatures, {}, [](const domain::MonsterDef* creature) {
		return std::tuple{creature->tier, creature->is_boss, creature->name};
	});

	std::vector<std::string> lines;
	for (const domain::MonsterDef* creature : creatures) {
		const auto known = profile.profile.bestiary.find(creature->id);
		if (known == profile.profile.bestiary.end()) {
			lines.push_back(t("bestiary.unknown", {{"tier", creature->tier + 1}}));
		} else if (profile.revealed(creature->id)) {
			lines.push_back(t("bestiary.entry_revealed",
			    {{"name", creature->name}, {"tier", creature->tier + 1}, {"kills", known->second.kills},
			        {"weak", join_or_dash(weak_elements(*creature, true))},
			        {"strong", join_or_dash(weak_elements(*creature, false))}}));
		} else {
			lines.push_back(t("bestiary.entry",
			    {{"name", creature->name}, {"tier", creature->tier + 1}, {"kills", known->second.kills}}));
		}
	}
	return lines;
}

std::vector<std::string> Controller::achievements() {
	const application::ProfileService profile = profile_service();
	std::vector<std::string> lines;
	for (const domain::AchievementDef& achievement : services.data->achievements) {
		const std::string achievement_name = t("achievement." + achievement.id + ".name");
		const std::string description =
		    t("achievement." + achievement.id + ".description", {{"value", achievement.value}});
		if (const auto unlock = profile.profile.achievements.find(achievement.id);
		    unlock != profile.profile.achievements.end()) {
			lines.push_back(t("achievements.unlocked", {{"name", achievement_name}, {"description", description},
			                                               {"date", date_of(unlock->second.unlocked_at)}}));
		} else {
			lines.push_back(t("achievements.locked", {{"name", achievement_name}, {"description", description}}));
		}
	}
	return lines;
}

} // namespace rpg::presentation
