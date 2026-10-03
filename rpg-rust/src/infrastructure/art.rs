//! Parses shared/art files: `@animation` headers, frames separated by `%%` (docs/data-format.md).

use std::cell::RefCell;
use std::collections::{BTreeMap, HashMap};
use std::rc::Rc;

use crate::assets::SharedFs;
use crate::domain::definitions::MonsterDef;

pub type Frame = Vec<String>;
pub type Animations = BTreeMap<String, Vec<Frame>>;

pub fn parse_art(text: &str) -> Result<Animations, String> {
	let mut animations = Animations::new();
	let mut current: Option<String> = None;
	let normalised = text.replace("\r\n", "\n");
	for line in normalised.trim_end_matches('\n').split('\n') {
		if let Some(name) = line.strip_prefix('@') {
			let name = name.trim().to_owned();
			animations.insert(name.clone(), vec![Vec::new()]);
			current = Some(name);
		} else if line == "%%" {
			let name = current.as_ref().ok_or("frame separator before any @animation")?;
			animations.get_mut(name).expect("current animation").push(Vec::new());
		} else {
			let name = current.as_ref().ok_or("art content before the first @animation")?;
			let frames = animations.get_mut(name).expect("current animation");
			frames.last_mut().expect("at least one frame").push(line.to_owned());
		}
	}
	Ok(animations)
}

/// Loads art files on demand and caches them. `RefCell` gives the cache interior mutability behind `&self`, the
/// same "a getter that fills a cache" pattern as the Python `ArtLibrary`.
#[derive(Debug)]
pub struct ArtLibrary {
	shared: Rc<SharedFs>,
	cache: RefCell<HashMap<String, Rc<Animations>>>,
}

impl ArtLibrary {
	pub fn new(shared: Rc<SharedFs>) -> ArtLibrary {
		ArtLibrary { shared, cache: RefCell::new(HashMap::new()) }
	}

	pub fn for_creature(&self, creature: &MonsterDef) -> Rc<Animations> {
		if creature.is_boss {
			self.load_file("bosses", &creature.id)
		} else {
			self.load_file("families", &creature.family)
		}
	}

	/// Missing or malformed art renders as no art (the data tests guarantee every creature has a valid file).
	pub fn load_file(&self, folder: &str, name: &str) -> Rc<Animations> {
		let path = format!("art/{folder}/{name}.txt");
		let mut cache = self.cache.borrow_mut();
		let animations = cache.entry(path).or_insert_with_key(|path| {
			let parsed = self.shared.read(path).map(parse_art).and_then(Result::ok);
			Rc::new(parsed.unwrap_or_default())
		});
		Rc::clone(animations)
	}
}

/// Frame of `animation` at animation tick `tick`, falling back to idle; empty when there is no art.
pub fn frame_for(animations: &Animations, animation: &str, tick: usize) -> Frame {
	let frames = animations
		.get(animation)
		.filter(|frames| !frames.is_empty())
		.or_else(|| animations.get("idle").filter(|frames| !frames.is_empty()));
	match frames {
		Some(frames) => frames[tick % frames.len()].clone(),
		None => Frame::new(),
	}
}
