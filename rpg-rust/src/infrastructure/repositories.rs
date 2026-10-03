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
use crate::application::save_game::{PersistenceError, RunRecord, SaveGame, check_schema};
use crate::infrastructure::i18n::SUPPORTED_LOCALES;

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

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Settings {
	pub locale: Option<String>,
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
		let locale = data.get("locale").and_then(Value::as_str).filter(|locale| SUPPORTED_LOCALES.contains(locale));
		Ok(Settings { locale: locale.map(str::to_owned) })
	}

	pub fn save(&self, settings: &Settings) -> Result<(), PersistenceError> {
		#[derive(Serialize)]
		#[serde(rename_all = "camelCase")]
		struct SettingsDocument<'a> {
			schema_version: i64,
			#[serde(skip_serializing_if = "Option::is_none")]
			locale: Option<&'a str>,
		}
		write_json_atomic(&self.path, &SettingsDocument { schema_version: 1, locale: settings.locale.as_deref() })
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
		read_json(&self.path)?.map(|raw| SaveGame::from_json(&raw)).transpose()
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
		paths.iter().filter_map(|path| read_json(path).transpose()).map(|raw| RunRecord::from_json(&raw?)).collect()
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
			Some(raw) => Profile::from_json(&raw),
		}
	}

	fn save(&self, profile: &Profile) -> Result<(), PersistenceError> {
		write_json_atomic(&self.path, profile)
	}
}
