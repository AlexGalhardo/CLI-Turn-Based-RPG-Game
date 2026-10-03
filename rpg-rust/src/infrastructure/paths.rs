//! The player's data directory (docs/persistence.md). Shared content is embedded, so there is no shared-dir lookup.

use std::env;
use std::ffi::OsString;
use std::path::PathBuf;

pub const DATA_DIR_ENV: &str = "RPG_DATA_DIR";
pub const DEFAULT_DATA_DIR_NAME: &str = ".cli-turn-based-rpg";

/// `--data-dir`, else `RPG_DATA_DIR`, else `~/.cli-turn-based-rpg`.
pub fn resolve_data_dir(cli_value: Option<&str>) -> PathBuf {
	resolve_data_dir_from(cli_value, env::var_os(DATA_DIR_ENV), env::home_dir())
}

/// The pure part of [`resolve_data_dir`]. Tests call it instead of mutating the process environment, which is
/// `unsafe` since Rust 2024 (other threads may read it concurrently).
pub fn resolve_data_dir_from(cli_value: Option<&str>, env_value: Option<OsString>, home: Option<PathBuf>) -> PathBuf {
	if let Some(value) = cli_value.filter(|value| !value.is_empty()) {
		return PathBuf::from(value);
	}
	if let Some(value) = env_value.filter(|value| !value.is_empty()) {
		return PathBuf::from(value);
	}
	home.unwrap_or_default().join(DEFAULT_DATA_DIR_NAME)
}
