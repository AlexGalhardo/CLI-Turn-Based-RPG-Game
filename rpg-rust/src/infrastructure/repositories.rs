//! JSON file repositories under the player's data directory (docs/persistence.md).

use std::fs;
use std::io::ErrorKind;
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use serde::Serialize;
use serde_json::Value;
use serde_json::ser::{PrettyFormatter, Serializer};

use crate::application::ports::{Clock, HistoryRepository, ProfileRepository, SaveRepository};
use crate::application::profile::Profile;
use crate::application::save_game::{PersistenceError, RunRecord, SCHEMA_VERSION, SaveGame, check_schema};
use crate::infrastructure::i18n::SUPPORTED_LOCALES;
use crate::infrastructure::migrations::{migrate_history, migrate_profile, migrate_save, migrate_settings};

/// Auto-battle paces (docs/tui.md); the first one is the default.
pub const BATTLE_SPEEDS: [i64; 2] = [1, 2];

fn io_error(path: &Path, error: &std::io::Error) -> PersistenceError {
	PersistenceError::Io(format!("{}: {error}", path.display()))
}

/// Tab-indented JSON with a trailing newline, like the reference's `json.dumps(indent="\t")`.
pub fn to_pretty_json<T: Serialize>(document: &T) -> String {
	let mut buffer = Vec::new();
	let mut serializer = Serializer::with_formatter(&mut buffer, PrettyFormatter::with_indent(b"\t"));
	document.serialize(&mut serializer).expect("serialising to memory cannot fail");
	let mut text = String::from_utf8(buffer).expect("serde_json writes UTF-8");
	text.push('\n');
	text
}

/// Write to a temp file then rename, so a crash never leaves a half-written save.
pub fn write_json_atomic<T: Serialize>(path: &Path, document: &T) -> Result<(), PersistenceError> {
	if let Some(parent) = path.parent() {
		fs::create_dir_all(parent).map_err(|error| io_error(parent, &error))?;
	}
	let mut temporary = path.as_os_str().to_owned();
	temporary.push(".tmp");
	let temporary = PathBuf::from(temporary);
	fs::write(&temporary, to_pretty_json(document)).map_err(|error| io_error(&temporary, &error))?;
	fs::rename(&temporary, path).map_err(|error| io_error(path, &error))
}

/// `Ok(None)` when the file does not exist.
pub fn read_json(path: &Path) -> Result<Option<Value>, PersistenceError> {
	let text = match fs::read_to_string(path) {
		Ok(text) => text,
		Err(error) if error.kind() == ErrorKind::NotFound => return Ok(None),
		Err(error) => return Err(io_error(path, &error)),
	};
	serde_json::from_str(&text)
		.map(Some)
		.map_err(|error| PersistenceError::Invalid(format!("{}: {error}", path.display())))
}

#[derive(Debug, Clone, Copy, Default)]
pub struct SystemClock;

impl Clock for SystemClock {
	fn now(&self) -> SystemTime {
		SystemTime::now()
	}
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Settings {
	pub locale: Option<String>,
	pub auto_equip: bool,
	pub battle_speed: i64,
}

impl Default for Settings {
	fn default() -> Settings {
		Settings { locale: None, auto_equip: false, battle_speed: BATTLE_SPEEDS[0] }
	}
}

#[derive(Debug, Clone)]
pub struct SettingsRepository {
	path: PathBuf,
}

impl SettingsRepository {
	pub fn new(data_dir: &Path) -> SettingsRepository {
		SettingsRepository { path: data_dir.join("settings.json") }
	}

	pub fn load(&self) -> Result<Settings, PersistenceError> {
		let Some(data) = read_json(&self.path)? else {
			return Ok(Settings::default());
		};
		check_schema(&data, "settings.json")?;
		let data = migrate_settings(data);
		let invalid = |key: &str| PersistenceError::Invalid(format!("settings.json: invalid {key}"));
		let locale = data.get("locale").and_then(Value::as_str).filter(|locale| SUPPORTED_LOCALES.contains(locale));
		let auto_equip = data.get("autoEquip").and_then(Value::as_bool).ok_or_else(|| invalid("autoEquip"))?;
		let speed = data.get("battleSpeed").and_then(Value::as_i64).ok_or_else(|| invalid("battleSpeed"))?;
		Ok(Settings {
			locale: locale.map(str::to_owned),
			auto_equip,
			battle_speed: if BATTLE_SPEEDS.contains(&speed) { speed } else { BATTLE_SPEEDS[0] },
		})
	}

	pub fn save(&self, settings: &Settings) -> Result<(), PersistenceError> {
		#[derive(Serialize)]
		#[serde(rename_all = "camelCase")]
		struct SettingsDocument<'a> {
			schema_version: i64,
			#[serde(skip_serializing_if = "Option::is_none")]
			locale: Option<&'a str>,
			auto_equip: bool,
			battle_speed: i64,
		}
		write_json_atomic(
			&self.path,
			&SettingsDocument {
				schema_version: SCHEMA_VERSION,
				locale: settings.locale.as_deref(),
				auto_equip: settings.auto_equip,
				battle_speed: settings.battle_speed,
			},
		)
	}
}

#[derive(Debug, Clone)]
pub struct FileSaveRepository {
	path: PathBuf,
}

impl FileSaveRepository {
	pub fn new(data_dir: &Path) -> FileSaveRepository {
		FileSaveRepository { path: data_dir.join("save.json") }
	}
}

impl SaveRepository for FileSaveRepository {
	fn load(&self) -> Result<Option<SaveGame>, PersistenceError> {
		let Some(document) = read_json(&self.path)? else {
			return Ok(None);
		};
		check_schema(&document, "save.json")?;
		SaveGame::from_json(&migrate_save(document)).map(Some)
	}

	fn save(&self, save: &SaveGame) -> Result<(), PersistenceError> {
		write_json_atomic(&self.path, save)
	}

	fn delete(&self) -> Result<(), PersistenceError> {
		match fs::remove_file(&self.path) {
			Err(error) if error.kind() != ErrorKind::NotFound => Err(io_error(&self.path, &error)),
			_ => Ok(()),
		}
	}
}

#[derive(Debug, Clone)]
pub struct FileHistoryRepository {
	dir: PathBuf,
}

impl FileHistoryRepository {
	pub fn new(data_dir: &Path) -> FileHistoryRepository {
		FileHistoryRepository { dir: data_dir.join("history") }
	}
}

impl HistoryRepository for FileHistoryRepository {
	fn add(&self, record: &RunRecord) -> Result<(), PersistenceError> {
		write_json_atomic(&self.dir.join(format!("{}.json", record.run_id)), record)
	}

	fn list(&self) -> Result<Vec<RunRecord>, PersistenceError> {
		let entries = match fs::read_dir(&self.dir) {
			Ok(entries) => entries,
			Err(error) if error.kind() == ErrorKind::NotFound => return Ok(Vec::new()),
			Err(error) => return Err(io_error(&self.dir, &error)),
		};
		let mut paths: Vec<PathBuf> = entries
			.filter_map(Result::ok)
			.map(|entry| entry.path())
			.filter(|path| path.extension().is_some_and(|extension| extension == "json"))
			.collect();
		paths.sort();
		let mut records = Vec::new();
		for path in &paths {
			let Some(document) = read_json(path)? else {
				continue;
			};
			check_schema(&document, "history record")?;
			records.push(RunRecord::from_json(&migrate_history(document))?);
		}
		Ok(records)
	}
}

#[derive(Debug, Clone)]
pub struct FileProfileRepository {
	path: PathBuf,
}

impl FileProfileRepository {
	pub fn new(data_dir: &Path) -> FileProfileRepository {
		FileProfileRepository { path: data_dir.join("profile.json") }
	}
}

impl ProfileRepository for FileProfileRepository {
	fn load(&self) -> Result<Profile, PersistenceError> {
		match read_json(&self.path)? {
			None => Ok(Profile::default()),
			Some(raw) => {
				check_schema(&raw, "profile.json")?;
				Profile::from_json(&migrate_profile(raw))
			}
		}
	}

	fn save(&self, profile: &Profile) -> Result<(), PersistenceError> {
		write_json_atomic(&self.path, profile)
	}
}
