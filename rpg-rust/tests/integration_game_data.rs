//! Shared data content and loader errors (port of `tests/integration/test_game_data.py`).

mod common;

use std::collections::HashSet;
use std::ffi::OsString;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::path::PathBuf;

use rpg::assets::SharedFs;
use rpg::domain::enums::{EnemyClass, Stat, StatusKind};
use rpg::infrastructure::art::{ArtLibrary, frame_for, parse_art};
use rpg::infrastructure::data_loader::load_game_data;
use rpg::infrastructure::i18n::Translator;
use rpg::infrastructure::paths::{DEFAULT_DATA_DIR_NAME, resolve_data_dir, resolve_data_dir_from};
use serde_json::Value;

#[test]
fn content_requirements() {
	let data = common::data();
	assert!(data.monsters.len() >= 100);
	assert_eq!(data.tier_count(), 10);
	let vocations: HashSet<&str> = data.vocations.iter().map(|vocation| vocation.id.as_str()).collect();
	assert_eq!(vocations, HashSet::from(["warrior", "archer", "mage"]));
	for tier in 0..data.tier_count() {
		let monsters = data.monsters_in_tier(tier);
		assert!(monsters.len() >= 9);
		assert!(monsters.windows(2).all(|pair| pair[0].id < pair[1].id), "sorted by id");
		assert!(data.boss_of_tier(tier).is_boss);
	}
	assert!(data.monsters_in_tier(99).is_empty());
}

#[test]
fn cross_references_are_valid() {
	let data = common::data();
	for vocation in &data.vocations {
		assert!(data.find_item(&vocation.starter_weapon).is_some());
		for spell_id in &vocation.spells {
			assert!(data.find_spell(spell_id).is_some());
		}
	}
	for creature in data.monsters.iter().chain(&data.bosses) {
		assert!(data.families.contains(&creature.family), "{}", creature.id);
		for attack in &creature.attacks {
			assert!(attack.min <= attack.max);
			if let Some(status) = &attack.status {
				assert!(data.find_status(&status.status).is_some());
			}
		}
		if creature.is_boss {
			let charge = creature.charge_attack.as_deref().expect("bosses have a charge attack");
			assert!(creature.find_attack(charge).is_some());
		}
	}
	for spell in &data.spells {
		if let Some(status) = &spell.level3_bonus.status {
			assert!(matches!(data.status(status).kind, StatusKind::Dot | StatusKind::Stun));
		}
	}
	for potion in &data.balance.starting_potions {
		assert!(data.find_potion(&potion.potion_id).is_some());
	}
	let ids: Vec<&str> = data.monsters.iter().chain(&data.bosses).map(|creature| creature.id.as_str()).collect();
	assert_eq!(ids.len(), ids.iter().collect::<HashSet<_>>().len());
}

#[test]
fn balance_m8_tables() {
	let data = common::data();
	let balance = &data.balance;
	let rarities: Vec<&str> = balance.rarities.iter().map(|rarity| rarity.id.as_str()).collect();
	assert_eq!(rarities, ["common", "rare", "legendary", "mythic"]);
	assert_eq!(balance.rarities.iter().map(|rarity| rarity.stat_pct).collect::<Vec<_>>(), [100, 150, 200, 300]);
	assert_eq!(balance.spell_levels.iter().map(|level| level.effect_pct).collect::<Vec<_>>(), [100, 150, 200]);
	assert_eq!(balance.rarity_weights.keys().collect::<Vec<_>>(), ["merchant"]);
	for enemy_class in [EnemyClass::Normal, EnemyClass::Elite, EnemyClass::Boss] {
		for rarity in balance.enemy_class(enemy_class).rarity_weights.keys() {
			assert!(rarities.contains(&rarity.as_str()), "{rarity}");
		}
	}
	assert!(balance.enemy_class(EnemyClass::Elite).stat_pct > balance.enemy_class(EnemyClass::Normal).stat_pct);
	let weighted: Vec<Stat> = balance.item_score_weights.keys().copied().collect();
	let every_stat: Vec<Stat> = serde_json::from_value(serde_json::json!([
		"attack",
		"armor",
		"maxHp",
		"maxMp",
		"hpRegen",
		"mpRegen",
		"critChance",
		"critDamage",
		"spellPower",
		"physicalDamage",
		"dodge",
		"parry",
		"lifeLeech",
		"manaLeech",
		"protPhysical",
		"protFire",
		"protIce",
		"protEnergy",
		"protEarth",
		"protHoly",
		"protDeath"
	]))
	.unwrap();
	assert_eq!(weighted, every_stat);
	assert_eq!(data.boss_of_tier(data.tier_count() - 1).id, "ferumbras");
	assert_eq!(balance.final_round, balance.rounds_per_tier * data.tier_count());
	assert_eq!(balance.auto_battle.mode("balanced").support_every, 2);
}

#[test]
fn lookup_errors() {
	let data = common::data();
	assert!(data.find_spell("avada_kedavra").is_none());
	assert!(data.balance.find_difficulty("nightmare").is_none());
	assert!(data.balance.find_rarity("epic").is_none());
	assert!(data.creature("rat").find_attack("laser").is_none());
	let panics = |lookup: &dyn Fn()| catch_unwind(AssertUnwindSafe(lookup)).is_err();
	assert!(panics(&|| {
		data.spell("avada_kedavra");
	}));
	assert!(panics(&|| {
		data.balance.difficulty("nightmare");
	}));
	assert!(panics(&|| {
		data.balance.rarity("epic");
	}));
	assert!(panics(&|| {
		data.balance.auto_battle.mode("berserk");
	}));
	assert!(panics(&|| {
		data.creature("rat").attack("laser");
	}));
	assert!(panics(&|| {
		data.vocation("knight");
	}));
	assert!(panics(&|| {
		data.potion("elixir");
	}));
	assert!(panics(&|| {
		data.item("excalibur");
	}));
	assert!(panics(&|| {
		data.status("sleepy");
	}));
}

#[test]
fn invalid_data_raises_data_error() {
	let mut shared = (*SharedFs::embedded()).clone();
	let mut document: Value = serde_json::from_str(shared.read("data/vocations.json").unwrap()).unwrap();
	document["vocations"][0].as_object_mut().unwrap().remove("startHp");
	shared.set("data/vocations.json", Some(document.to_string()));
	let error = load_game_data(&shared).unwrap_err();
	assert!(error.to_string().contains("startHp"), "{error}");
	shared.set("data/vocations.json", Some("{ not json".to_owned()));
	assert!(load_game_data(&shared).unwrap_err().to_string().contains("vocations.json"));
	shared.set("data/vocations.json", None);
	assert!(load_game_data(&shared).unwrap_err().to_string().contains("file not found"));
}

#[test]
fn optional_files_default_to_empty_lists() {
	let mut shared = (*SharedFs::embedded()).clone();
	shared.set("data/affixes.json", None);
	shared.set("data/achievements.json", None);
	let data = load_game_data(&shared).unwrap();
	assert!(data.affixes.is_empty());
	assert!(data.achievements.is_empty());
}

#[test]
fn shared_fs_reads_a_directory() {
	let shared = SharedFs::from_dir(&common::shared_dir()).unwrap();
	assert!(shared.paths().any(|path| path == "data/monsters.json"));
	assert_eq!(load_game_data(&shared).unwrap().monsters.len(), common::data().monsters.len());
	assert!(SharedFs::from_dir(&common::shared_dir().join("missing")).is_err());
}

#[test]
fn embedded_assets_cover_data_i18n_and_art() {
	let shared = SharedFs::embedded();
	let paths: Vec<&str> = shared.paths().collect();
	assert!(paths.iter().any(|path| path.starts_with("data/")));
	assert!(paths.iter().any(|path| path.starts_with("i18n/")));
	assert!(paths.iter().any(|path| path.starts_with("art/bosses/")));
	assert!(!paths.iter().any(|path| path.starts_with("golden/")));
}

#[test]
fn paths_resolution_order() {
	let home = Some(PathBuf::from("home"));
	assert_eq!(
		resolve_data_dir_from(Some("custom"), Some(OsString::from("env")), home.clone()),
		PathBuf::from("custom")
	);
	assert_eq!(resolve_data_dir_from(None, Some(OsString::from("env")), home.clone()), PathBuf::from("env"));
	assert_eq!(
		resolve_data_dir_from(Some(""), Some(OsString::new()), home.clone()),
		PathBuf::from("home").join(DEFAULT_DATA_DIR_NAME)
	);
	assert_eq!(resolve_data_dir(Some("x")), PathBuf::from("x"));
}

#[test]
fn translator_lookups() {
	let shared = SharedFs::embedded();
	let english = Translator::new(&shared, "en").unwrap();
	let portuguese = Translator::new(&shared, "pt-BR").unwrap();
	assert_eq!(english.t("event.gold_looted", &[("amount", &5)]), "You looted 5 gold.");
	assert_eq!(portuguese.t("event.gold_looted", &[("amount", &5)]), "Você saqueou 5 de ouro.");
	assert_eq!(english.t("missing.key", &[]), "missing.key");
	assert_eq!(english.t("event.gold_looted", &[]), "You looted {amount} gold.");
	assert!(portuguese.has("menu.quit"));
	assert!(!portuguese.has("missing.key"));
	assert!(Translator::new(&shared, "fr").unwrap_err().contains("unsupported"));
	let mut broken = (*shared).clone();
	broken.set("i18n/pt-BR.json", None);
	assert!(Translator::new(&broken, "pt-BR").is_err());
}

#[test]
fn i18n_files_have_the_same_keys() {
	let shared = SharedFs::embedded();
	let keys = |locale: &str| -> HashSet<String> {
		let document: Value = serde_json::from_str(shared.read(&format!("i18n/{locale}.json")).unwrap()).unwrap();
		document.as_object().unwrap().keys().cloned().collect()
	};
	assert_eq!(keys("en"), keys("pt-BR"));
}

#[test]
fn art_parsing() {
	let animations = parse_art("@idle\n a\n%%\n b\n@hurt\n x\n").unwrap();
	assert_eq!(animations["idle"], vec![vec![" a".to_owned()], vec![" b".to_owned()]]);
	assert_eq!(animations["hurt"], vec![vec![" x".to_owned()]]);
	assert_eq!(frame_for(&animations, "idle", 3), vec![" b".to_owned()]);
	assert_eq!(frame_for(&animations, "attack", 0), vec![" a".to_owned()]);
	assert!(frame_for(&std::collections::BTreeMap::default(), "idle", 0).is_empty());
	assert!(parse_art("oops").unwrap_err().contains("before"));
	assert!(parse_art("%%").unwrap_err().contains("separator"));
	assert_eq!(parse_art("@idle\r\n a\r\n").unwrap()["idle"], vec![vec![" a".to_owned()]]);
	let data = common::data();
	let library = ArtLibrary::new(SharedFs::embedded());
	for creature in data.monsters.iter().chain(&data.bosses) {
		assert!(!frame_for(&library.for_creature(creature), "idle", 0).is_empty(), "{}", creature.id);
	}
	assert!(library.load_file("families", "missing").is_empty());
}
