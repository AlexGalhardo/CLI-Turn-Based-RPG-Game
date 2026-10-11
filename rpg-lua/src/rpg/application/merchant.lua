--- Merchant phase: potions, bag, equipment and rotating stock (docs/game-design.md §10).

local auto_equip = require("rpg.application.auto_equip")
local events_module = require("rpg.application.events")
local loot = require("rpg.application.loot")
local base = require("rpg.domain.base")
local character = require("rpg.domain.character")
local formulas = require("rpg.domain.formulas")

local event, error_event, ErrorCode = events_module.event, events_module.error, events_module.ErrorCode

local MAX_POTIONS_PER_PURCHASE = 99

local Merchant = base.class("Merchant")
Merchant.MAX_POTIONS_PER_PURCHASE = MAX_POTIONS_PER_PURCHASE

function Merchant.stock_price(item, data)
	return formulas.pct(character.item_value(item, data), data.balance.merchant_markup_pct)
end

--- Ids of the potions on sale, in file order.
function Merchant.available_potions(state, data)
	local next_round = state.round + 1
	local result = {}
	for _, potion in ipairs(data.potions) do
		if potion.unlock_round <= next_round then
			result[#result + 1] = potion.id
		end
	end
	return result
end

function Merchant.new(data, rng, state)
	return setmetatable({ _data = data, _rng = rng, _state = state }, Merchant)
end

--- Generates the rotating stock for the tier of the next round.
function Merchant:enter()
	local state = self._state
	local data = self._data
	local vocation = data:vocation(state.player.vocation_id)
	local tier = formulas.round_info(state.round + 1, data.balance, data.tier_count).tier
	state.merchant_stock = {}
	for _ = 1, data.balance.merchant_stock_size do
		local item = loot.generate_item(data, self._rng, {
			vocation = vocation,
			tier = tier,
			weights = data.balance.rarity_weights.merchant,
			uid = state.next_item_uid,
		})
		if item ~= nil then
			state:take_item_uid()
			state.merchant_stock[#state.merchant_stock + 1] = item
		end
	end
	return { event("merchant_entered", { round = state.round }) }
end

function Merchant:handle(command)
	if command.type == "buy_potion" then
		return self:_buy_potion(command.potion_id, command.quantity)
	elseif command.type == "sell_item" then
		return self:_sell(command.uid)
	elseif command.type == "equip" then
		return self:_equip(command.uid)
	elseif command.type == "unequip" then
		return self:_unequip(command.slot)
	end
	return self:_buy_stock(command.index)
end

function Merchant:_buy_potion(potion_id, quantity)
	local player = self._state.player
	if not self._data:has_potion(potion_id) then
		return { error_event(ErrorCode.UNKNOWN_POTION) }
	end
	if not base.contains(Merchant.available_potions(self._state, self._data), potion_id) then
		return { error_event(ErrorCode.POTION_LOCKED) }
	end
	if quantity < 1 or quantity > MAX_POTIONS_PER_PURCHASE then
		return { error_event(ErrorCode.INVALID_QUANTITY) }
	end
	local cost = self._data:potion(potion_id).price * quantity
	if player.gold < cost then
		return { error_event(ErrorCode.NOT_ENOUGH_GOLD) }
	end
	player.gold = player.gold - cost
	player.potions[potion_id] = player:potion_count(potion_id) + quantity
	return { event("potion_bought", { potionId = potion_id, quantity = quantity, gold = cost }) }
end

function Merchant:_find_in_bag(uid)
	for _, item in ipairs(self._state.player.bag) do
		if item.uid == uid then
			return item
		end
	end
	return nil
end

function Merchant:_sell(uid)
	local player = self._state.player
	local item = self:_find_in_bag(uid)
	if item == nil then
		return { error_event(ErrorCode.INVALID_ITEM) }
	end
	local value = character.item_value(item, self._data)
	base.remove_value(player.bag, item)
	player.gold = player.gold + value
	return { event("item_sold", { uid = uid, itemId = item.item_id, gold = value }) }
end

function Merchant:_equip(uid)
	local player = self._state.player
	local item = self:_find_in_bag(uid)
	if item == nil then
		return { error_event(ErrorCode.INVALID_ITEM) }
	end
	local definition = self._data:item(item.item_id)
	if not loot.can_use(definition, self._data:vocation(player.vocation_id)) then
		return { error_event(ErrorCode.CANNOT_EQUIP) }
	end
	if character.required_level(item, self._data) > player.level then
		return { error_event(ErrorCode.LEVEL_TOO_LOW) }
	end
	local events = {}
	base.remove_value(player.bag, item)
	local previous = player.equipment[definition.slot]
	if previous ~= nil then
		player.bag[#player.bag + 1] = previous
		events[#events + 1] =
			event("item_unequipped", { uid = previous.uid, itemId = previous.item_id, slot = definition.slot })
	end
	player.equipment[definition.slot] = item
	events[#events + 1] = event("item_equipped", { uid = item.uid, itemId = item.item_id, slot = definition.slot })
	self:_clamp_resources()
	return events
end

function Merchant:_unequip(slot)
	local player = self._state.player
	local item = player.equipment[slot]
	if item == nil then
		return { error_event(ErrorCode.INVALID_ITEM) }
	end
	if #player.bag >= self._data.balance.bag_capacity then
		return { error_event(ErrorCode.BAG_FULL) }
	end
	player.equipment[slot] = nil
	player.bag[#player.bag + 1] = item
	self:_clamp_resources()
	return { event("item_unequipped", { uid = item.uid, itemId = item.item_id, slot = slot }) }
end

function Merchant:_buy_stock(index)
	local state = self._state
	local player = state.player
	if index < 0 or index >= #state.merchant_stock then
		return { error_event(ErrorCode.INVALID_ITEM) }
	end
	if #player.bag >= self._data.balance.bag_capacity then
		return { error_event(ErrorCode.BAG_FULL) }
	end
	local item = state.merchant_stock[index + 1]
	local price = Merchant.stock_price(item, self._data)
	if player.gold < price then
		return { error_event(ErrorCode.NOT_ENOUGH_GOLD) }
	end
	player.gold = player.gold - price
	table.remove(state.merchant_stock, index + 1)
	player.bag[#player.bag + 1] = item
	local events = { event("item_bought", { uid = item.uid, itemId = item.item_id, gold = price }) }
	if state.config.auto_equip then
		events_module.extend(events, auto_equip.auto_equip(state, self._data))
	end
	return events
end

function Merchant:_clamp_resources()
	local player = self._state.player
	local sheet = character.build_sheet(player, self._data)
	player.hp = math.min(player.hp, sheet.max_hp)
	player.mp = math.min(player.mp, sheet.max_mp)
end

return Merchant
