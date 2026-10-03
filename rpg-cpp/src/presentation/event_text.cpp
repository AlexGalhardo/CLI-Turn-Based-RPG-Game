#include "presentation/event_text.hpp"

#include <array>
#include <utility>

#include "application/commands.hpp"

namespace rpg::presentation {

namespace {

// Event fields holding ids, replaced by display names before formatting.
constexpr std::array<std::pair<std::string_view, std::string_view>, 4> name_fields{{
    {"spellId", "spell"},
    {"potionId", "potion"},
    {"monsterId", "monster"},
    {"itemId", "item"},
}};

// Event fields replaced by their translated labels.
constexpr std::array<std::string_view, 4> label_fields{"element", "status", "rarity", "resource"};

std::string value_text(const application::EventValue& value) {
	return std::visit(application::Overloaded{
	                      [](std::int64_t number) { return std::to_string(number); },
	                      [](const std::string& text) { return text; },
	                      [](bool flag) { return std::string(flag ? "true" : "false"); },
	                  },
	    value);
}

std::string event_key(const application::Event& event) {
	if (event.type == "error") {
		return "error." + event.text("code");
	}
	std::string variant;
	if (event.flag("crit")) {
		variant = "_crit";
	} else if (event.flag("charged")) {
		variant = "_charged";
	} else if (event.type == "round_started" && event.flag("isBoss")) {
		variant = "_boss";
	} else if (!event.text("target").empty()) {
		variant = "_" + event.text("target");
	}
	return "event." + event.type + variant;
}

std::string item_name_by_uid(const domain::GameData& data, const std::string& uid, const application::RunState& state) {
	std::vector<const domain::ItemInstance*> items;
	for (const auto& item : state.player.bag) {
		items.push_back(&item);
	}
	for (const std::string_view slot : domain::kSlots) {
		if (const auto found = state.player.equipment.find(slot); found != state.player.equipment.end()) {
			items.push_back(&found->second);
		}
	}
	for (const auto& item : state.merchant_stock) {
		items.push_back(&item);
	}
	for (const domain::ItemInstance* item : items) {
		if (std::to_string(item->uid) == uid) {
			return data.item(item->item_id).name;
		}
	}
	return "#" + uid;
}

} // namespace

std::string EventFormatter::format(const application::Event& event, const application::RunState& state) const {
	infrastructure::Params params;
	for (const auto& [field, value] : event.fields) {
		params.insert_or_assign(field, value_text(value));
	}
	for (const auto& [field, name] : name_fields) {
		if (const auto found = event.fields.find(field); found != event.fields.end()) {
			params.insert_or_assign(std::string(name), display_name(field, value_text(found->second)));
		}
	}
	for (const std::string_view field : label_fields) {
		if (const auto found = event.fields.find(field); found != event.fields.end()) {
			params.insert_or_assign(
			    std::string(field), translator_->t(std::string(field) + "." + value_text(found->second)));
		}
	}
	if (!params.contains("monster") && state.monster.has_value()) {
		params.insert_or_assign("monster", data_->creature(state.monster->creature_id).name);
	}
	if (const auto uid = event.fields.find("uid"); uid != event.fields.end() && !params.contains("item")) {
		params.insert_or_assign("item", item_name_by_uid(*data_, value_text(uid->second), state));
	}
	return translator_->t(event_key(event), params);
}

std::string EventFormatter::display_name(std::string_view field, const std::string& identifier) const {
	if (field == "spellId") {
		return data_->has_spell(identifier) ? data_->spell(identifier).name : identifier;
	}
	if (field == "potionId") {
		return data_->has_potion(identifier) ? data_->potion(identifier).name : identifier;
	}
	if (field == "monsterId") {
		return data_->has_creature(identifier) ? data_->creature(identifier).name : identifier;
	}
	return data_->has_item(identifier) ? data_->item(identifier).name : identifier;
}

} // namespace rpg::presentation
