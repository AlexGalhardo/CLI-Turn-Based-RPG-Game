//! Turns engine events into translated sentences. Shared by every presentation (text UI and TUI).
//!
//! It works on the flat JSON object of the event (like the reference works on its dict), so the same generic
//! rules apply to every event type: id fields become display names, enum fields become translated labels.

use std::fmt::Display;
use std::rc::Rc;

use serde_json::{Map, Value};

use crate::application::events::Event;
use crate::application::run_state::RunState;
use crate::domain::definitions::GameData;
use crate::infrastructure::i18n::Translator;

/// Event fields that hold ids: they are replaced by display names before formatting.
const NAME_FIELDS: [(&str, &str); 4] =
	[("spellId", "spell"), ("potionId", "potion"), ("monsterId", "monster"), ("itemId", "item")];
const LABEL_FIELDS: [&str; 4] = ["element", "status", "rarity", "resource"];

/// Python's `str()` of an event value (`True`/`False` for booleans).
fn value_text(value: &Value) -> String {
	match value {
		Value::String(text) => text.clone(),
		Value::Bool(true) => "True".to_owned(),
		Value::Bool(false) => "False".to_owned(),
		other => other.to_string(),
	}
}

pub struct EventFormatter {
	data: Rc<GameData>,
	translator: Rc<Translator>,
}

impl EventFormatter {
	pub fn new(data: Rc<GameData>, translator: Rc<Translator>) -> EventFormatter {
		EventFormatter { data, translator }
	}

	pub fn format(&self, event: &Event, state: &RunState) -> String {
		self.format_fields(&event.to_json(), state)
	}

	/// Formats a raw event object (tests use partial objects, as the reference tests do with dicts).
	pub fn format_fields(&self, event: &Map<String, Value>, state: &RunState) -> String {
		let mut params: Vec<(String, String)> =
			event.iter().map(|(key, value)| (key.clone(), value_text(value))).collect();
		let mut set = |name: &str, text: String| match params.iter_mut().find(|(key, _)| key == name) {
			Some(entry) => entry.1 = text,
			None => params.push((name.to_owned(), text)),
		};
		for (field, name) in NAME_FIELDS {
			if let Some(value) = event.get(field) {
				set(name, self.display_name(field, &value_text(value)));
			}
		}
		for field in LABEL_FIELDS {
			if let Some(value) = event.get(field) {
				set(field, self.translator.t(&format!("{field}.{}", value_text(value)), &[]));
			}
		}
		let has = |params: &Vec<(String, String)>, name: &str| params.iter().any(|(key, _)| key == name);
		if !has(&params, "monster")
			&& let Some(monster) = &state.monster
		{
			params.push(("monster".to_owned(), self.data.creature(&monster.creature_id).name.clone()));
		}
		if let Some(uid) = event.get("uid")
			&& !has(&params, "item")
		{
			params.push(("item".to_owned(), self.item_name_by_uid(uid, state)));
		}
		let pairs: Vec<(&str, &dyn Display)> =
			params.iter().map(|(key, value)| (key.as_str(), value as &dyn Display)).collect();
		self.translator.t(&key(event), &pairs)
	}

	fn display_name(&self, field: &str, identifier: &str) -> String {
		let data = &self.data;
		let name = match field {
			"spellId" => data.find_spell(identifier).map(|spell| &spell.name),
			"potionId" => data.find_potion(identifier).map(|potion| &potion.name),
			"monsterId" => data.find_creature(identifier).map(|creature| &creature.name),
			_ => data.find_item(identifier).map(|item| &item.name),
		};
		name.cloned().unwrap_or_else(|| identifier.to_owned())
	}

	fn item_name_by_uid(&self, uid: &Value, state: &RunState) -> String {
		let player = &state.player;
		let found = player
			.bag
			.iter()
			.chain(player.equipment.values())
			.chain(state.merchant_stock.iter())
			.find(|item| uid.as_i64() == Some(item.uid));
		match found {
			Some(item) => self.data.item(&item.item_id).name.clone(),
			None => format!("#{}", value_text(uid)),
		}
	}
}

/// `event.<type>` plus the variant suffix chosen by the presentation (docs/data-format.md).
fn key(event: &Map<String, Value>) -> String {
	let event_type = event.get("type").map(value_text).unwrap_or_default();
	if event_type == "error" {
		return format!("error.{}", event.get("code").map(value_text).unwrap_or_default());
	}
	let is_true = |field: &str| event.get(field) == Some(&Value::Bool(true));
	let variant = if is_true("crit") {
		"_crit".to_owned()
	} else if is_true("charged") {
		"_charged".to_owned()
	} else if event_type == "round_started" && is_true("isBoss") {
		"_boss".to_owned()
	} else if let Some(target) = event.get("target") {
		format!("_{}", value_text(target))
	} else {
		String::new()
	};
	format!("event.{event_type}{variant}")
}
