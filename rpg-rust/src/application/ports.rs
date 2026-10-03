//! Ports implemented by the infrastructure layer (Dependency Inversion: the application owns the interfaces).

use std::time::SystemTime;

use crate::application::profile::Profile;
use crate::application::save_game::{PersistenceError, RunRecord, SaveGame};

pub trait Clock {
	fn now(&self) -> SystemTime;
}

pub trait SaveRepository {
	fn load(&self) -> Result<Option<SaveGame>, PersistenceError>;

	fn save(&self, save: &SaveGame) -> Result<(), PersistenceError>;

	fn delete(&self) -> Result<(), PersistenceError>;
}

pub trait HistoryRepository {
	fn add(&self, record: &RunRecord) -> Result<(), PersistenceError>;

	fn list(&self) -> Result<Vec<RunRecord>, PersistenceError>;
}

pub trait ProfileRepository {
	fn load(&self) -> Result<Profile, PersistenceError>;

	fn save(&self, profile: &Profile) -> Result<(), PersistenceError>;
}
