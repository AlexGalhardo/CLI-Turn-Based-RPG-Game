//! The shared game content (data, i18n, art) embedded at compile time by `build.rs`.

use std::collections::BTreeMap;
use std::path::Path;
use std::rc::Rc;
use std::{fs, io};

include!(concat!(env!("OUT_DIR"), "/shared_assets.rs"));

/// A read-only tree of shared files addressed by relative paths like `data/monsters.json`.
///
/// The binary uses the embedded copy; tests can build one from a directory or patch single files.
#[derive(Debug, Clone, Default)]
pub struct SharedFs {
	files: BTreeMap<String, String>,
}

impl SharedFs {
	/// The files embedded in the binary.
	pub fn embedded() -> Rc<SharedFs> {
		let files = FILES.iter().map(|(path, text)| ((*path).to_owned(), (*text).to_owned())).collect();
		Rc::new(SharedFs { files })
	}

	/// Reads every file under `dir` (used by tests that need modified data).
	pub fn from_dir(dir: &Path) -> io::Result<SharedFs> {
		let mut shared = SharedFs::default();
		shared.load_dir(dir, dir)?;
		Ok(shared)
	}

	fn load_dir(&mut self, root: &Path, dir: &Path) -> io::Result<()> {
		for entry in fs::read_dir(dir)? {
			let path = entry?.path();
			if path.is_dir() {
				self.load_dir(root, &path)?;
			} else if let Ok(text) = fs::read_to_string(&path) {
				let relative = path.strip_prefix(root).unwrap_or(&path).to_string_lossy().replace('\\', "/");
				self.files.insert(relative, text);
			}
		}
		Ok(())
	}

	/// Returns the contents of a file, or `None` when it does not exist.
	pub fn read(&self, path: &str) -> Option<&str> {
		self.files.get(path).map(String::as_str)
	}

	/// Replaces (or adds) a file; `None` removes it.
	pub fn set(&mut self, path: &str, contents: Option<String>) {
		match contents {
			Some(text) => self.files.insert(path.to_owned(), text),
			None => self.files.remove(path),
		};
	}

	/// Relative paths of every file, sorted.
	pub fn paths(&self) -> impl Iterator<Item = &str> {
		self.files.keys().map(String::as_str)
	}
}
