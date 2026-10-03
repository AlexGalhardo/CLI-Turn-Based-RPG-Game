//! Auto-equip with auto-sell (docs/game-design.md §8.1). Consumes no randomness.

use crate::application::events::Event;
use crate::application::loot::can_use;
use crate::application::run_state::RunState;
use crate::domain::character::{build_sheet, item_score, item_value, required_level};
use crate::domain::definitions::GameData;
use crate::domain::entities::ItemInstance;
use crate::domain::enums::{EQUIPMENT_SLOT_ORDER, Slot};

/// Highest-score bag item the player can wear in `slot` now; ties go to the lowest uid.
pub fn best_bag_item<'a>(state: &'a RunState, data: &GameData, slot: Slot) -> Option<&'a ItemInstance> {
	let player = &state.player;
	let vocation = data.vocation(&player.vocation_id);
	player
		.bag
		.iter()
		.filter(|item| {
			let definition = data.item(&item.item_id);
			definition.slot == slot && can_use(definition, vocation) && required_level(item, data) <= player.level
		})
		.min_by_key(|item| (-item_score(item, data), item.uid))
}

pub fn auto_equip(state: &mut RunState, data: &GameData) -> Vec<Event> {
	let mut events = Vec::new();
	for slot in EQUIPMENT_SLOT_ORDER {
		let Some(best) = best_bag_item(state, data, slot) else {
			continue;
		};
		let uid = best.uid;
		let score = item_score(best, data);
		let player = &mut state.player;
		if let Some(current) = player.equipment.get(&slot)
			&& score <= item_score(current, data)
		{
			continue;
		}
		let index = player.bag.iter().position(|item| item.uid == uid).expect("the best item is in the bag");
		let best = player.bag.remove(index);
		events.push(Event::ItemAutoEquipped { uid, item_id: best.item_id.clone(), slot, score });
		if let Some(current) = player.equipment.insert(slot, best) {
			let gold = item_value(&current, data);
			player.gold += gold;
			events.push(Event::ItemAutoSold { uid: current.uid, item_id: current.item_id, gold });
		}
	}
	if !events.is_empty() {
		let sheet = build_sheet(&state.player, data);
		let player = &mut state.player;
		player.hp = player.hp.min(sheet.max_hp);
		player.mp = player.mp.min(sheet.max_mp);
	}
	events
}
