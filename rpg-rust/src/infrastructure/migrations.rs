//! Upgrades older save/settings/history/profile documents to the current schema (docs/persistence.md).
//!
//! Each migration takes the raw JSON of version N and returns version N + 1, so the application layer only ever reads
//! the current format. Version 1 → 2 is the 1.4.0 "ARPG update". Malformed documents are left alone: the typed
//! loader that runs next reports them.

use serde_json::{Map, Value, json};

use crate::domain::enums::EnemyClass;

pub const REMOVED_RARITY: &str = "epic";
pub const REPLACEMENT_RARITY: &str = "legendary";

fn version(document: &Map<String, Value>) -> i64 {
	document.get("schemaVersion").and_then(Value::as_i64).unwrap_or(1)
}

fn set_default(object: &mut Map<String, Value>, key: &str, value: Value) {
	object.entry(key.to_owned()).or_insert(value);
}

fn rename_rarity(item: &mut Value) {
	if let Some(item) = item.as_object_mut()
		&& item.get("rarity").and_then(Value::as_str) == Some(REMOVED_RARITY)
	{
		item.insert("rarity".to_owned(), json!(REPLACEMENT_RARITY));
	}
}

fn rename_rarities(items: Option<&mut Value>) {
	if let Some(items) = items.and_then(Value::as_array_mut) {
		items.iter_mut().for_each(rename_rarity);
	}
}

fn stats_v1_to_v2(raw: Option<&mut Value>) {
	let Some(stats) = raw.and_then(Value::as_object_mut) else {
		return;
	};
	set_default(stats, "itemsAutoEquipped", json!(0));
	set_default(stats, "elitesKilled", json!(0));
	set_default(stats, "potionsDropped", json!({}));
	if let Some(dropped) = stats.get_mut("itemsDropped").and_then(Value::as_object_mut)
		&& let Some(epic) = dropped.remove(REMOVED_RARITY)
	{
		let legendary = dropped.get(REPLACEMENT_RARITY).and_then(Value::as_i64).unwrap_or(0);
		dropped.insert(REPLACEMENT_RARITY.to_owned(), json!(legendary + epic.as_i64().unwrap_or(0)));
	}
	rename_rarities(stats.get_mut("droppedItems"));
}

fn run_v1_to_v2(raw: Option<&mut Value>) {
	let Some(run) = raw.and_then(Value::as_object_mut) else {
		return;
	};
	if let Some(config) = run.get_mut("config").and_then(Value::as_object_mut) {
		set_default(config, "autoEquip", json!(false));
	}
	set_default(run, "won", json!(false));
	if let Some(monster) = run.get_mut("monster").and_then(Value::as_object_mut) {
		let enemy_class =
			if monster.get("isBoss") == Some(&Value::Bool(true)) { EnemyClass::Boss } else { EnemyClass::Normal };
		set_default(monster, "enemyClass", json!(enemy_class.as_str()));
	}
	if let Some(player) = run.get_mut("player").and_then(Value::as_object_mut) {
		rename_rarities(player.get_mut("bag"));
		if let Some(equipment) = player.get_mut("equipment").and_then(Value::as_object_mut) {
			equipment.values_mut().for_each(rename_rarity);
		}
	}
	rename_rarities(run.get_mut("merchantStock"));
	stats_v1_to_v2(run.get_mut("stats"));
}

/// Applies `upgrade` to an object document older than version 2 and stamps it as version 2.
fn migrate(mut document: Value, upgrade: impl FnOnce(&mut Map<String, Value>)) -> Value {
	if let Some(object) = document.as_object_mut()
		&& version(object) < 2
	{
		upgrade(object);
		object.insert("schemaVersion".to_owned(), json!(2));
	}
	document
}

pub fn migrate_save(document: Value) -> Value {
	migrate(document, |save| run_v1_to_v2(save.get_mut("run")))
}

pub fn migrate_history(document: Value) -> Value {
	migrate(document, |record| {
		set_default(record, "won", json!(false));
		stats_v1_to_v2(record.get_mut("stats"));
	})
}

pub fn migrate_profile(document: Value) -> Value {
	migrate(document, |profile| {
		if let Some(Value::Array(hall)) = profile.get_mut("hallOfFame") {
			for entry in hall.iter_mut().filter_map(Value::as_object_mut) {
				set_default(entry, "won", json!(false));
			}
		}
	})
}

pub fn migrate_settings(document: Value) -> Value {
	migrate(document, |settings| {
		set_default(settings, "autoEquip", json!(false));
		set_default(settings, "battleSpeed", json!(1));
	})
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn current_documents_are_untouched() {
		let document = json!({"schemaVersion": 2, "locale": "en"});
		assert_eq!(migrate_settings(document.clone()), document);
		assert_eq!(migrate_profile(json!([1])), json!([1]));
	}

	#[test]
	fn malformed_parts_are_skipped() {
		let save = migrate_save(json!({"schemaVersion": 1, "run": {"monster": null, "stats": 3}}));
		assert_eq!(save, json!({"schemaVersion": 2, "run": {"monster": null, "stats": 3, "won": false}}));
		let history = migrate_history(json!({"stats": {"itemsDropped": {"epic": 2}, "droppedItems": [1]}}));
		assert_eq!(history["stats"]["itemsDropped"], json!({"legendary": 2}));
		assert_eq!(history["schemaVersion"], json!(2));
	}
}
