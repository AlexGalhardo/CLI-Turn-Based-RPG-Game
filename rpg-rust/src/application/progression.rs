//! Experience, levels, magic levels and spell levels (docs/game-design.md §4 and §9).

use crate::application::events::Event;
use crate::domain::character::build_sheet;
use crate::domain::definitions::{GameData, SpellDef};
use crate::domain::entities::Player;
use crate::domain::formulas::{mana_for_magic_level, spell_level_for_uses, xp_for_level};

pub struct Progression<'a> {
	data: &'a GameData,
}

impl<'a> Progression<'a> {
	pub fn new(data: &'a GameData) -> Progression<'a> {
		Progression { data }
	}

	pub fn gain_experience(&self, player: &mut Player, amount: i64) -> Vec<Event> {
		player.xp += amount;
		let mut events = vec![Event::XpGained { amount, total: player.xp }];
		let vocation = self.data.vocation(&player.vocation_id);
		while player.xp >= xp_for_level(player.level + 1) {
			player.level += 1;
			let sheet = build_sheet(player, self.data);
			player.hp = sheet.max_hp.min(player.hp + vocation.hp_per_level);
			player.mp = sheet.max_mp.min(player.mp + vocation.mp_per_level);
			events.push(Event::LevelUp { level: player.level, max_hp: sheet.max_hp, max_mp: sheet.max_mp });
		}
		events
	}

	pub fn after_cast(&self, player: &mut Player, spell: &SpellDef, mana_cost: i64) -> Vec<Event> {
		let levels = &self.data.balance.spell_levels;
		let mut events = Vec::new();
		let uses_before = player.spell_use_count(&spell.id);
		player.spell_uses.insert(spell.id.clone(), uses_before + 1);
		let before = spell_level_for_uses(uses_before, levels);
		let after = spell_level_for_uses(uses_before + 1, levels);
		if after.level != before.level {
			events.push(Event::SpellLevelUp { spell_id: spell.id.clone(), level: after.level });
		}

		player.mana_spent += mana_cost;
		while player.mana_spent >= mana_for_magic_level(player.magic_level, &self.data.balance) {
			player.magic_level += 1;
			events.push(Event::MagicLevelUp { magic_level: player.magic_level });
		}
		events
	}
}
