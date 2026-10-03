#include "application/commands.hpp"

#include <stdexcept>

namespace rpg::application {

bool is_battle(const Command& command) {
	return std::holds_alternative<Attack>(command) || std::holds_alternative<Cast>(command) ||
	       std::holds_alternative<UsePotion>(command) || std::holds_alternative<Defend>(command);
}

nlohmann::json command_to_json(const Command& command) {
	return std::visit(
	    Overloaded{
	        [](const Attack&) { return nlohmann::json{{"type", "attack"}}; },
	        [](const Cast& cast) { return nlohmann::json{{"type", "cast"}, {"spellId", cast.spell_id}}; },
	        [](const UsePotion& potion) { return nlohmann::json{{"type", "potion"}, {"potionId", potion.potion_id}}; },
	        [](const Defend&) { return nlohmann::json{{"type", "defend"}}; },
	        [](const NextFight&) { return nlohmann::json{{"type", "next_fight"}}; },
	        [](const BuyPotion& buy) {
		        return nlohmann::json{{"type", "buy_potion"}, {"potionId", buy.potion_id}, {"quantity", buy.quantity}};
	        },
	        [](const SellItem& sell) { return nlohmann::json{{"type", "sell_item"}, {"uid", sell.uid}}; },
	        [](const Equip& equip) { return nlohmann::json{{"type", "equip"}, {"uid", equip.uid}}; },
	        [](const Unequip& unequip) { return nlohmann::json{{"type", "unequip"}, {"slot", unequip.slot}}; },
	        [](const BuyStockItem& buy) { return nlohmann::json{{"type", "buy_stock_item"}, {"index", buy.index}}; },
	    },
	    command);
}

Command command_from_json(const nlohmann::json& document) {
	const auto type = document.at("type").get<std::string>();
	if (type == "attack") {
		return Attack{};
	}
	if (type == "cast") {
		return Cast{document.at("spellId").get<std::string>()};
	}
	if (type == "potion") {
		return UsePotion{document.at("potionId").get<std::string>()};
	}
	if (type == "defend") {
		return Defend{};
	}
	if (type == "next_fight") {
		return NextFight{};
	}
	if (type == "buy_potion") {
		return BuyPotion{document.at("potionId").get<std::string>(), document.at("quantity").get<std::int64_t>()};
	}
	if (type == "sell_item") {
		return SellItem{document.at("uid").get<std::int64_t>()};
	}
	if (type == "equip") {
		return Equip{document.at("uid").get<std::int64_t>()};
	}
	if (type == "unequip") {
		return Unequip{document.at("slot").get<std::string>()};
	}
	if (type == "buy_stock_item") {
		return BuyStockItem{document.at("index").get<std::int64_t>()};
	}
	throw std::invalid_argument("unknown command type: " + type);
}

} // namespace rpg::application
