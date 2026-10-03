//! Save file and finished-run record formats, shared by every implementation (docs/persistence.md).

use std::fmt;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde::{Deserialize, Serialize};
use serde_json::Value;

use crate::application::run_state::RunState;
use crate::application::statistics::RunStatistics;

pub const SCHEMA_VERSION: i64 = 1;
pub const IMPLEMENTATION: &str = "rust";

/// Days since 1970-01-01 → (year, month, day) in the proleptic Gregorian calendar (Howard Hinnant's algorithm).
/// The standard library has no calendar, and the stack is limited to serde/ratatui, so the conversion lives here.
fn civil_from_days(days: i64) -> (i64, i64, i64) {
	let z = days + 719_468;
	let era = z.div_euclid(146_097);
	let day_of_era = z.rem_euclid(146_097);
	let year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36_524 - day_of_era / 146_096) / 365;
	let day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
	let month_index = (5 * day_of_year + 2) / 153;
	let day = day_of_year - (153 * month_index + 2) / 5 + 1;
	let month = if month_index < 10 { month_index + 3 } else { month_index - 9 };
	let year = year_of_era + era * 400 + i64::from(month <= 2);
	(year, month, day)
}

fn days_from_civil(year: i64, month: i64, day: i64) -> i64 {
	let year = if month <= 2 { year - 1 } else { year };
	let era = year.div_euclid(400);
	let year_of_era = year.rem_euclid(400);
	let month_index = if month > 2 { month - 3 } else { month + 9 };
	let day_of_year = (153 * month_index + 2) / 5 + day - 1;
	let day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
	era * 146_097 + day_of_era - 719_468
}

/// UTC calendar fields of a moment: (year, month, day, hour, minute, second).
fn utc_fields(moment: SystemTime) -> (i64, i64, i64, i64, i64, i64) {
	let seconds = moment.duration_since(UNIX_EPOCH).map_or(0, |elapsed| elapsed.as_secs() as i64);
	let (year, month, day) = civil_from_days(seconds.div_euclid(86_400));
	let second_of_day = seconds.rem_euclid(86_400);
	(year, month, day, second_of_day / 3600, second_of_day % 3600 / 60, second_of_day % 60)
}

/// `2026-09-27T21:04:11Z`
pub fn format_timestamp(moment: SystemTime) -> String {
	let (year, month, day, hour, minute, second) = utc_fields(moment);
	format!("{year:04}-{month:02}-{day:02}T{hour:02}:{minute:02}:{second:02}Z")
}

pub fn parse_timestamp(text: &str) -> Option<SystemTime> {
	let bytes = text.as_bytes();
	let shape_ok = bytes.len() == 20
		&& bytes[4] == b'-'
		&& bytes[7] == b'-'
		&& bytes[10] == b'T'
		&& bytes[13] == b':'
		&& bytes[16] == b':'
		&& bytes[19] == b'Z';
	if !shape_ok {
		return None;
	}
	let field = |start: usize, end: usize| text.get(start..end)?.parse::<i64>().ok();
	let (year, month, day) = (field(0, 4)?, field(5, 7)?, field(8, 10)?);
	let (hour, minute, second) = (field(11, 13)?, field(14, 16)?, field(17, 19)?);
	let seconds = days_from_civil(year, month, day) * 86_400 + hour * 3600 + minute * 60 + second;
	Some(UNIX_EPOCH + Duration::from_secs(u64::try_from(seconds).ok()?))
}

/// `<startedAt as yyyyMMddTHHmmssZ>-<seed>`
pub fn make_run_id(started_at: SystemTime, seed: u64) -> String {
	let (year, month, day, hour, minute, second) = utc_fields(started_at);
	format!("{year:04}{month:02}{day:02}T{hour:02}{minute:02}{second:02}Z-{seed}")
}

/// Failure to read or write a persistence file.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum PersistenceError {
	/// The file was written by a newer game version; it is never overwritten.
	NewerSchema(String),
	Io(String),
	Invalid(String),
}

impl fmt::Display for PersistenceError {
	fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
		match self {
			PersistenceError::NewerSchema(message)
			| PersistenceError::Io(message)
			| PersistenceError::Invalid(message) => formatter.write_str(message),
		}
	}
}

impl std::error::Error for PersistenceError {}

pub fn check_schema(data: &Value, what: &str) -> Result<(), PersistenceError> {
	let version = data
		.get("schemaVersion")
		.and_then(Value::as_i64)
		.ok_or_else(|| PersistenceError::Invalid(format!("{what}: missing schemaVersion")))?;
	if version > SCHEMA_VERSION {
		return Err(PersistenceError::NewerSchema(format!(
			"{what} uses schema {version}; update the game (supports {SCHEMA_VERSION})"
		)));
	}
	Ok(())
}

fn from_document<T: for<'de> Deserialize<'de>>(raw: &Value, what: &str) -> Result<T, PersistenceError> {
	check_schema(raw, what)?;
	T::deserialize(raw).map_err(|error| PersistenceError::Invalid(format!("{what}: {error}")))
}

fn schema_version() -> i64 {
	SCHEMA_VERSION
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionInfo {
	pub run_id: String,
	pub started_at: String,
	pub play_time_seconds: i64,
	pub sessions: i64,
}

impl SessionInfo {
	pub fn new(run_id: String, started_at: String) -> SessionInfo {
		SessionInfo { run_id, started_at, play_time_seconds: 0, sessions: 1 }
	}
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SaveGame {
	/// Always written as [`SCHEMA_VERSION`]; checked before the rest is parsed.
	#[serde(default = "schema_version")]
	pub schema_version: i64,
	pub game_version: String,
	pub implementation: String,
	pub saved_at: String,
	pub rng_state: u32,
	pub session: SessionInfo,
	pub run: RunState,
}

impl SaveGame {
	pub fn new(game_version: &str, saved_at: String, rng_state: u32, session: SessionInfo, run: RunState) -> SaveGame {
		SaveGame {
			schema_version: SCHEMA_VERSION,
			game_version: game_version.to_owned(),
			implementation: IMPLEMENTATION.to_owned(),
			saved_at,
			rng_state,
			session,
			run,
		}
	}

	pub fn from_json(raw: &Value) -> Result<SaveGame, PersistenceError> {
		from_document(raw, "save.json")
	}
}

/// A finished run, written to `history/<runId>.json`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RunRecord {
	#[serde(default = "schema_version")]
	pub schema_version: i64,
	pub run_id: String,
	pub name: String,
	pub vocation: String,
	pub difficulty: String,
	pub seed: u64,
	pub implementation: String,
	pub game_version: String,
	pub started_at: String,
	pub ended_at: String,
	pub play_time_seconds: i64,
	pub sessions: i64,
	pub round: i64,
	pub level: i64,
	pub magic_level: i64,
	pub death_cause: String,
	pub stats: RunStatistics,
}

impl RunRecord {
	pub fn from_json(raw: &Value) -> Result<RunRecord, PersistenceError> {
		from_document(raw, "history record")
	}
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn timestamps_round_trip() {
		for text in ["1970-01-01T00:00:00Z", "2000-02-29T23:59:59Z", "2026-09-27T21:04:11Z", "2100-03-01T00:00:00Z"] {
			let moment = parse_timestamp(text).expect(text);
			assert_eq!(format_timestamp(moment), text);
		}
		assert_eq!(parse_timestamp("2026-09-27 21:04:11"), None);
		assert_eq!(parse_timestamp("2026-0X-27T21:04:11Z"), None);
		assert_eq!(format_timestamp(UNIX_EPOCH - Duration::from_secs(5)), "1970-01-01T00:00:00Z");
	}

	#[test]
	fn run_id_uses_compact_utc_time_and_seed() {
		let moment = parse_timestamp("2026-01-02T03:04:05Z").unwrap();
		assert_eq!(make_run_id(moment, 42), "20260102T030405Z-42");
	}

	#[test]
	fn newer_schema_is_refused() {
		let error = check_schema(&serde_json::json!({"schemaVersion": 99}), "save.json").unwrap_err();
		assert!(matches!(error, PersistenceError::NewerSchema(_)));
		assert!(error.to_string().contains("update the game"));
		assert!(matches!(check_schema(&serde_json::json!({}), "x"), Err(PersistenceError::Invalid(_))));
	}
}
