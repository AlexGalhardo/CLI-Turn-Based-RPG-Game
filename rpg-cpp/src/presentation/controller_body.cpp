// Informative screen bodies of the controller (character sheet, game over, victory, equipment screens, Hall of Fame,
// bestiary, achievements).
#include <algorithm>
#include <array>
#include <format>

#include "domain/character.hpp"
#include "domain/formulas.hpp"
#include "presentation/controller.hpp"
#include "presentation/render.hpp"

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
	case View::victory:
		return victory_summary();
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

std::string Controller::run_stats_line() const {
	const application::RunState& state = session->state();
	return t("gameover.stats",
	    {{"level", state.player.level}, {"damage", state.stats.damage_dealt}, {"kills", state.stats.total_kills()},
	        {"elites", state.stats.elites_killed}, {"bosses", state.stats.bosses_killed}});
}

std::vector<std::string> Controller::game_over_summary() const {
	const application::RunState& state = session->state();
	const std::string cause = state.death_cause.value_or("");
	infrastructure::Params params{
	    {"name", state.player.name}, {"vocation", t("vocation." + state.player.vocation_id)}, {"round", state.round}};
	std::string summary;
	if (!cause.empty()) {
		params.insert_or_assign("monster", services.data->creature(cause).name);
		summary = t("gameover.summary", params);
	} else if (state.won) {
		summary = t("gameover.won_summary", params);
	} else {
		params.insert_or_assign("monster", "?");
		summary = t("gameover.summary", params);
	}
	return {summary, run_stats_line()};
}

std::vector<std::string> Controller::victory_summary() const {
	const domain::GameData& data = *services.data;
	const application::RunState& state = session->state();
	const domain::MonsterDef& boss =
	    data.boss_of_tier(domain::round_info(state.round, data.balance, data.tier_count()).tier);
	return {
	    t("victory.summary", {{"name", state.player.name}, {"vocation", t("vocation." + state.player.vocation_id)},
	                             {"monster", boss.name}, {"round", state.round}}),
	    run_stats_line(),
	    "",
	    t("victory.choice"),
	};
}

// ── equipment screens (docs/tui.md "Equipment screen") ─────────────────────────────────────────────────────────────

std::vector<BodyLine> Controller::styled_body() const {
	switch (view) {
	case View::equipment:
		return equipment_body();
	case View::compare:
		return compare_body();
	default:
		return slot_body();
	}
}

std::vector<BodyLine> Controller::equipment_body() const {
	const domain::GameData& data = *services.data;
	const domain::Player& player = session->state().player;
	std::vector<BodyLine> lines{
	    {t("equipment.equipped_header", {{"score", domain::equipment_score(player, data)}}), ""}};
	for (const std::string_view slot : domain::kEquipmentSlotOrder) {
		const std::string slot_name = t("slot." + std::string(slot));
		const auto found = player.equipment.find(slot);
		if (found == player.equipment.end()) {
			lines.push_back({t("equipment.slot_empty", {{"slot", slot_name}}), std::string(style_warning)});
			continue;
		}
		const domain::ItemInstance& item = found->second;
		lines.push_back(
		    {t("equipment.slot_line",
		         {{"slot", slot_name}, {"name", data.item(item.item_id).name}, {"rarity", t("rarity." + item.rarity)},
		             {"level", domain::required_level(item, data)}, {"score", domain::item_score(item, data)}}),
		        item.rarity});
	}
	lines.push_back({"", ""});
	lines.push_back({t("equipment.bag_header"), ""});
	if (usable_bag().empty()) {
		lines.push_back({t("equipment.bag_empty"), ""});
	}
	return lines;
}

std::string Controller::compare_title() const {
	const domain::ItemInstance* item = compared_item();
	if (item == nullptr) {
		return t("merchant.equipment");
	}
	const domain::GameData& data = *services.data;
	const domain::Player& player = session->state().player;
	const domain::Slot& slot = data.item(item->item_id).slot;
	const auto current = player.equipment.find(slot);
	const std::string current_name =
	    current == player.equipment.end() ? t("equipment.empty") : data.item(current->second.item_id).name;
	return t("equipment.compare_title",
	    {{"slot", t("slot." + slot)}, {"current", current_name}, {"new", data.item(item->item_id).name}});
}

std::string Controller::affix_list(const domain::ItemInstance* item) const {
	if (item == nullptr) {
		return {};
	}
	std::vector<std::string> parts;
	for (const domain::AffixRoll& affix : item->affixes) {
		parts.push_back(t("equipment.affix", {{"value", affix.value}, {"stat", t("stat." + affix.stat)}}));
	}
	std::string result;
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i > 0) {
			result += ", ";
		}
		result += parts[i];
	}
	return result;
}

std::vector<BodyLine> Controller::compare_body() const {
	const domain::ItemInstance* item = compared_item();
	if (item == nullptr) {
		return {};
	}
	const domain::GameData& data = *services.data;
	const domain::Player& player = session->state().player;
	const auto equipped = player.equipment.find(data.item(item->item_id).slot);
	const domain::ItemInstance* current = equipped == player.equipment.end() ? nullptr : &equipped->second;
	const domain::StatTotals new_stats = domain::item_stats(*item, data);
	const domain::StatTotals old_stats = current == nullptr ? domain::StatTotals{} : domain::item_stats(*current, data);
	const auto value_of = [](const domain::StatTotals& stats, std::string_view stat) {
		const auto found = stats.find(stat);
		return found == stats.end() ? std::int64_t{0} : found->second;
	};

	std::vector<BodyLine> lines;
	for (const std::string_view stat : domain::kStats) {
		if (!new_stats.contains(stat) && !old_stats.contains(stat)) {
			continue;
		}
		const std::int64_t old_value = value_of(old_stats, stat);
		const std::int64_t new_value = value_of(new_stats, stat);
		lines.push_back(
		    {t("equipment.stat_delta", {{"stat", t("stat." + std::string(stat))}, {"current", old_value},
		                                   {"new", new_value}, {"delta", format_delta(new_value - old_value)}}),
		        std::string(delta_style(new_value - old_value))});
	}
	if (const std::string gained = affix_list(item); !gained.empty()) {
		lines.push_back({t("equipment.affixes_gained", {{"affixes", gained}}), std::string(style_gain)});
	}
	if (const std::string lost = affix_list(current); !lost.empty()) {
		lines.push_back({t("equipment.affixes_lost", {{"affixes", lost}}), std::string(style_loss)});
	}
	const std::int64_t old_score = current == nullptr ? 0 : domain::item_score(*current, data);
	const std::int64_t new_score = domain::item_score(*item, data);
	lines.push_back({t("equipment.score_delta",
	                     {{"current", old_score}, {"new", new_score}, {"delta", format_delta(new_score - old_score)}}),
	    std::string(delta_style(new_score - old_score))});
	if (const std::int64_t level = domain::required_level(*item, data); level > player.level) {
		lines.push_back(
		    {t("equipment.level_needed", {{"level", level}, {"current", player.level}}), std::string(style_loss)});
	}
	return lines;
}

std::vector<BodyLine> Controller::slot_body() const {
	const domain::GameData& data = *services.data;
	const auto& equipment = session->state().player.equipment;
	const auto found = equipment.find(slot_);
	if (found == equipment.end()) {
		return {{t("equipment.empty"), std::string(style_warning)}};
	}
	const domain::ItemInstance& item = found->second;
	std::vector<BodyLine> lines{
	    {t("equipment.item_title",
	         {{"name", data.item(item.item_id).name}, {"rarity", t("rarity." + item.rarity)},
	             {"level", domain::required_level(item, data)}, {"score", domain::item_score(item, data)}}),
	        item.rarity},
	};
	const domain::StatTotals stats = domain::item_stats(item, data);
	for (const std::string_view stat : domain::kStats) {
		if (const auto value = stats.find(stat); value != stats.end()) {
			lines.push_back(
			    {t("character.stat_line", {{"stat", t("stat." + std::string(stat))}, {"value", value->second}}), ""});
		}
	}
	return lines;
}

std::vector<std::string> Controller::hall_of_fame() {
	const std::vector<application::HallOfFameEntry> hall = profile_service().profile.hall_of_fame;
	if (hall.empty()) {
		return {t("hall.empty")};
	}
	std::vector<std::string> lines;
	for (std::size_t index = 0; index < hall.size(); ++index) {
		const application::HallOfFameEntry& entry = hall[index];
		lines.push_back(t(entry.won ? "hall.entry_won" : "hall.entry",
		    {{"position", index + 1}, {"name", entry.name}, {"vocation", t("vocation." + entry.vocation)},
		        {"difficulty", t("difficulty." + entry.difficulty)}, {"round", entry.round}, {"level", entry.level},
		        {"date", date_of(entry.ended_at)}}));
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
