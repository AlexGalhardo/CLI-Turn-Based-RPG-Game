//! Merchant phase: potions, bag, equipment and rotating stock (docs/game-design.md §10).

use crate::application::commands::Command;
use crate::application::events::{ErrorCode, Event};
use crate::application::loot::{can_use, generate_item};
use crate::application::run_state::RunState;
use crate::domain::character::{build_sheet, item_value};
use crate::domain::definitions::GameData;
use crate::domain::entities::ItemInstance;
use crate::domain::enums::Slot;
use crate::domain::formulas::{pct, round_info};
use crate::domain::rng::Rng;

pub const MAX_POTIONS_PER_PURCHASE: i64 = 99;

pub fn stock_price(item: &ItemInstance, data: &GameData) -> i64 {
	pct(item_value(item, data), data.balance.merchant_markup_pct)
}

/// Potion ids for sale before the next round, in file order.
pub fn available_potions(state: &RunState, data: &GameData) -> Vec<String> {
	let next_round = state.round + 1;
	data.potions.iter().filter(|potion| potion.unlock_round <= next_round).map(|potion| potion.id.clone()).collect()
}

pub struct Merchant<'a> {
	data: &'a GameData,
	rng: &'a mut Rng,
	state: &'a mut RunState,
}

impl<'a> Merchant<'a> {
	pub fn new(data: &'a GameData, rng: &'a mut Rng, state: &'a mut RunState) -> Merchant<'a> {
		Merchant { data, rng, state }
	}

	/// Generates the rotating stock for the tier of the next round.
	pub fn enter(&mut self) -> Vec<Event> {
		let data = self.data;
		let state = &mut *self.state;
		let difficulty = data.balance.difficulty(&state.config.difficulty_id);
		let vocation = data.vocation(&state.player.vocation_id);
		let tier = round_info(state.round + 1, &data.balance, data.tier_count()).tier;
		state.merchant_stock = Vec::new();
		for _ in 0..data.balance.merchant_stock_size {
			let item = generate_item(data, self.rng, vocation, tier, "merchant", difficulty, state.next_item_uid);
			if let Some(item) = item {
				state.take_item_uid();
				state.merchant_stock.push(item);
			}
		}
		vec![Event::MerchantEntered { round: state.round }]
	}

	pub fn handle(&mut self, command: &Command) -> Vec<Event> {
		match command {
			Command::BuyPotion { potion_id, quantity } => self.buy_potion(potion_id, *quantity),
			Command::SellItem { uid } => self.sell(*uid),
			Command::Equip { uid } => self.equip(*uid),
			Command::Unequip { slot } => self.unequip(*slot),
			Command::BuyStockItem { index } => self.buy_stock(*index),
			_ => unreachable!("not a merchant command: {command:?}"),
		}
	}

	fn buy_potion(&mut self, potion_id: &str, quantity: i64) -> Vec<Event> {
		let Some(potion) = self.data.find_potion(potion_id) else {
			return vec![Event::error(ErrorCode::UnknownPotion)];
		};
		if !available_potions(self.state, self.data).iter().any(|id| id == potion_id) {
			return vec![Event::error(ErrorCode::PotionLocked)];
		}
		if !(1..=MAX_POTIONS_PER_PURCHASE).contains(&quantity) {
			return vec![Event::error(ErrorCode::InvalidQuantity)];
		}
		let cost = potion.price * quantity;
		let player = &mut self.state.player;
		if player.gold < cost {
			return vec![Event::error(ErrorCode::NotEnoughGold)];
		}
		player.gold -= cost;
		player.potions.insert(potion_id.to_owned(), player.potion_count(potion_id) + quantity);
		vec![Event::PotionBought { potion_id: potion_id.to_owned(), quantity, gold: cost }]
	}

	fn bag_index(&self, uid: i64) -> Option<usize> {
		self.state.player.bag.iter().position(|item| item.uid == uid)
	}

	fn sell(&mut self, uid: i64) -> Vec<Event> {
		let Some(index) = self.bag_index(uid) else {
			return vec![Event::error(ErrorCode::InvalidItem)];
		};
		let player = &mut self.state.player;
		let item = player.bag.remove(index);
		let value = item_value(&item, self.data);
		player.gold += value;
		vec![Event::ItemSold { uid, item_id: item.item_id, gold: value }]
	}

	fn equip(&mut self, uid: i64) -> Vec<Event> {
		let Some(index) = self.bag_index(uid) else {
			return vec![Event::error(ErrorCode::InvalidItem)];
		};
		let player = &mut self.state.player;
		let definition = self.data.item(&player.bag[index].item_id);
		if !can_use(definition, self.data.vocation(&player.vocation_id)) {
			return vec![Event::error(ErrorCode::CannotEquip)];
		}
		let mut events = Vec::new();
		let item = player.bag.remove(index);
		if let Some(previous) = player.equipment.remove(&definition.slot) {
			events.push(Event::ItemUnequipped {
				uid: previous.uid,
				item_id: previous.item_id.clone(),
				slot: definition.slot,
			});
			player.bag.push(previous);
		}
		events.push(Event::ItemEquipped { uid: item.uid, item_id: item.item_id.clone(), slot: definition.slot });
		player.equipment.insert(definition.slot, item);
		self.clamp_resources();
		events
	}

	fn unequip(&mut self, slot: Slot) -> Vec<Event> {
		let player = &mut self.state.player;
		if !player.equipment.contains_key(&slot) {
			return vec![Event::error(ErrorCode::InvalidItem)];
		}
		if player.bag.len() as i64 >= self.data.balance.bag_capacity {
			return vec![Event::error(ErrorCode::BagFull)];
		}
		let item = player.equipment.remove(&slot).expect("checked above");
		let event = Event::ItemUnequipped { uid: item.uid, item_id: item.item_id.clone(), slot };
		player.bag.push(item);
		self.clamp_resources();
		vec![event]
	}

	fn buy_stock(&mut self, index: i64) -> Vec<Event> {
		let state = &mut *self.state;
		if index < 0 || index as usize >= state.merchant_stock.len() {
			return vec![Event::error(ErrorCode::InvalidItem)];
		}
		if state.player.bag.len() as i64 >= self.data.balance.bag_capacity {
			return vec![Event::error(ErrorCode::BagFull)];
		}
		let index = index as usize;
		let price = stock_price(&state.merchant_stock[index], self.data);
		if state.player.gold < price {
			return vec![Event::error(ErrorCode::NotEnoughGold)];
		}
		state.player.gold -= price;
		let item = state.merchant_stock.remove(index);
		let event = Event::ItemBought { uid: item.uid, item_id: item.item_id.clone(), gold: price };
		state.player.bag.push(item);
		vec![event]
	}

	fn clamp_resources(&mut self) {
		let sheet = build_sheet(&self.state.player, self.data);
		let player = &mut self.state.player;
		player.hp = player.hp.min(sheet.max_hp);
		player.mp = player.mp.min(sheet.max_mp);
	}
}
