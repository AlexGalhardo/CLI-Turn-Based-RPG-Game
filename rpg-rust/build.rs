//! Embeds `../shared/{data,i18n,art}` into the binary (the Rust counterpart of Go's `go:embed`).
//!
//! `include_str!` only takes literal paths, so this script walks the shared folders and generates a table of
//! `("data/monsters.json", include_str!("<absolute path>"))` entries that `src/assets.rs` includes.

use std::env;
use std::fmt::Write as _;
use std::fs;
use std::path::{Path, PathBuf};

const EMBEDDED_FOLDERS: [&str; 3] = ["data", "i18n", "art"];

fn collect(dir: &Path, files: &mut Vec<PathBuf>) {
	let mut entries: Vec<PathBuf> = fs::read_dir(dir)
		.unwrap_or_else(|error| panic!("cannot read {}: {error}", dir.display()))
		.map(|entry| entry.expect("directory entry").path())
		.collect();
	entries.sort();
	for path in entries {
		if path.is_dir() {
			collect(&path, files);
		} else {
			files.push(path);
		}
	}
}

fn main() {
	let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
	let shared = manifest_dir.join("..").join("shared");
	let mut files = Vec::new();
	for folder in EMBEDDED_FOLDERS {
		let dir = shared.join(folder);
		println!("cargo:rerun-if-changed={}", dir.display());
		collect(&dir, &mut files);
	}

	let mut table = String::from("pub(crate) static FILES: &[(&str, &str)] = &[\n");
	for path in &files {
		println!("cargo:rerun-if-changed={}", path.display());
		let relative = path.strip_prefix(&shared).expect("file under shared/");
		let key = relative.to_string_lossy().replace('\\', "/");
		let absolute = path.canonicalize().expect("canonical path");
		writeln!(table, "\t({key:?}, include_str!({:?})),", absolute.to_string_lossy()).expect("write to string");
	}
	table.push_str("];\n");

	let out = PathBuf::from(env::var("OUT_DIR").expect("OUT_DIR")).join("shared_assets.rs");
	fs::write(out, table).expect("write shared_assets.rs");
}
