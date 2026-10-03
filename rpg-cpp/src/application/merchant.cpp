#include "application/merchant.hpp"

#include <algorithm>

#include "application/loot.hpp"
#include "domain/character.hpp"
#include "domain/formulas.hpp"

namespace rpg::application {

namespace {

constexpr std::int64_t max_potions_per_purchase = 99;

std::vector<Event> single(Event event) {
	std::vector<Event> events;
	events.push_back(std::move(event));
	return events;
}

} // namespace

std::int64_t stock_price(const domain::ItemInstance& item, const domain::GameData& data) {
	return domain::pct(domain::item_value(item, data), data.balance.merchant_markup_pct);
}

std::vector<std::string> available_potions(const RunState& state, const domain::GameData& data) {
	const std::int64_t next_round = state.round + 1;
	std::vector<std::string> result;
	for (const domain::PotionDef& potion : data.potions) {
		if (potion.unlock_round <= next_round) {
			result.push_back(potion.id);
		}
	}
	return result;
}

std::vector<Event> Merchant::enter() {
	RunState& state = *state_;
	const domain::DifficultyDef& difficulty = data_->balance.difficulty(state.config.difficulty_id);
	const domain::VocationDef& vocation = data_->vocation(state.player.vocation_id);
	const std::int64_t tier = domain::round_info(state.round + 1, data_->balance, data_->tier_count()).tier;
	state.merchant_stock.clear();
	for (std::int64_t i = 0; i < data_->balance.merchant_stock_size; ++i) {
		const ItemRequest request{
		    .vocation = &vocation,
		    .tier = tier,
		    .table = "merchant",
		    .difficulty = &difficulty,
		    .uid = state.next_item_uid,
		};
		if (auto item = generate_item(*data_, *rng_, request)) {
			state.take_item_uid();
			state.merchant_stock.push_back(std::move(*item));
		}
	}
	return single(Event{"merchant_entered", {{"round", state.round}}});
}

std::vector<Event> Merchant::handle(const Command& command) {
	return std::visit(Overloaded{
	                      [&](const BuyPotion& buy) { return buy_potion(buy.potion_id, buy.quantity); },
	                      [&](const SellItem& sell_item) { return sell(sell_item.uid); },
	                      [&](const Equip& equip_item) { return equip(equip_item.uid); },
	                      [&](const Unequip& unequip_item) { return unequip(unequip_item.slot); },
	                      [&](const BuyStockItem& buy) { return buy_stock(buy.index); },
	                      [](const auto&) { return single(error_event(error_code::invalid_phase)); },
	                  },
	    command);
}

std::vector<Event> Merchant::buy_potion(const std::string& potion_id, std::int64_t quantity) {
	domain::Player& player = state_->player;
	if (!data_->has_potion(potion_id)) {
		return single(error_event(error_code::unknown_potion));
	}
	if (!std::ranges::contains(available_potions(*state_, *data_), potion_id)) {
		return single(error_event(error_code::potion_locked));
	}
	if (quantity < 1 || quantity > max_potions_per_purchase) {
		return single(error_event(error_code::invalid_quantity));
	}
	const std::int64_t cost = data_->potion(potion_id).price * quantity;
	if (player.gold < cost) {
		return single(error_event(error_code::not_enough_gold));
	}
	player.gold -= cost;
	player.potions[potion_id] += quantity;
	return single(Event{"potion_bought", {{"potionId", potion_id}, {"quantity", quantity}, {"gold", cost}}});
}

std::vector<Event> Merchant::sell(std::int64_t uid) {
	domain::Player& player = state_->player;
	const auto found = std::ranges::find(player.bag, uid, &domain::ItemInstance::uid);
	if (found == player.bag.end()) {
		return single(error_event(error_code::invalid_item));
	}
	const domain::ItemInstance item = *found;
	const std::int64_t value = domain::item_value(item, *data_);
	player.bag.erase(found);
	player.gold += value;
	return single(Event{"item_sold", {{"uid", uid}, {"itemId", item.item_id}, {"gold", value}}});
}

std::vector<Event> Merchant::equip(std::int64_t uid) {
	domain::Player& player = state_->player;
	const auto found = std::ranges::find(player.bag, uid, &domain::ItemInstance::uid);
	if (found == player.bag.end()) {
		return single(error_event(error_code::invalid_item));
	}
	const domain::ItemDef& definition = data_->item(found->item_id);
	if (!can_use(definition, data_->vocation(player.vocation_id))) {
		return single(error_event(error_code::cannot_equip));
	}

	std::vector<Event> events;
	const domain::ItemInstance item = *found;
	player.bag.erase(found);
	if (const auto previous = player.equipment.find(definition.slot); previous != player.equipment.end()) {
		const domain::ItemInstance unequipped = previous->second;
		player.equipment.erase(previous);
		player.bag.push_back(unequipped);
		events.push_back(Event{
		    "item_unequipped", {{"uid", unequipped.uid}, {"itemId", unequipped.item_id}, {"slot", definition.slot}}});
	}
	player.equipment.insert_or_assign(definition.slot, item);
	events.push_back(Event{"item_equipped", {{"uid", item.uid}, {"itemId", item.item_id}, {"slot", definition.slot}}});
	clamp_resources();
	return events;
}

std::vector<Event> Merchant::unequip(const domain::Slot& slot) {
	domain::Player& player = state_->player;
	const auto found = player.equipment.find(slot);
	if (found == player.equipment.end()) {
		return single(error_event(error_code::invalid_item));
	}
	if (std::cmp_greater_equal(player.bag.size(), data_->balance.bag_capacity)) {
		return single(error_event(error_code::bag_full));
	}
	const domain::ItemInstance item = found->second;
	player.equipment.erase(found);
	player.bag.push_back(item);
	clamp_resources();
	return single(Event{"item_unequipped", {{"uid", item.uid}, {"itemId", item.item_id}, {"slot", slot}}});
}

std::vector<Event> Merchant::buy_stock(std::int64_t index) {
	RunState& state = *state_;
	if (index < 0 || std::cmp_greater_equal(index, state.merchant_stock.size())) {
		return single(error_event(error_code::invalid_item));
	}
	if (std::cmp_greater_equal(state.player.bag.size(), data_->balance.bag_capacity)) {
		return single(error_event(error_code::bag_full));
	}
	const auto position = state.merchant_stock.begin() + index;
	const domain::ItemInstance item = *position;
	const std::int64_t price = stock_price(item, *data_);
	if (state.player.gold < price) {
		return single(error_event(error_code::not_enough_gold));
	}
	state.player.gold -= price;
	state.merchant_stock.erase(position);
	state.player.bag.push_back(item);
	return single(Event{"item_bought", {{"uid", item.uid}, {"itemId", item.item_id}, {"gold", price}}});
}

void Merchant::clamp_resources() {
	domain::Player& player = state_->player;
	const domain::CharacterSheet sheet = domain::build_sheet(player, *data_);
	player.hp = std::min(player.hp, sheet.max_hp);
	player.mp = std::min(player.mp, sheet.max_mp);
}

} // namespace rpg::application
