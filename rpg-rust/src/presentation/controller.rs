//! Framework-independent UI state machine: which screen is shown, its options and what each key does.
//!
//! The ratatui app only renders this controller. The Python (Textual), TypeScript (Ink) and Go (Bubble Tea) ports
//! implement the same controller, which is what keeps the interfaces practically identical (docs/tui.md).
//!
//! The reference builds menus as `(option, closure)` pairs. Here a menu entry carries an [`Action`] value instead:
//! closures that capture `&mut self` would fight the borrow checker, while plain data is easy to test and match.

use std::collections::VecDeque;
use std::collections::hash_map::RandomState;
use std::fmt::Display;
use std::hash::{BuildHasher, Hasher};
use std::rc::Rc;
use std::time::{SystemTime, UNIX_EPOCH};

use crate::application::commands::Command;
use crate::application::events::Event;
use crate::application::game_session::{GameSession, Repositories, StepResult};
use crate::application::loot::can_use;
use crate::application::merchant::{available_potions, stock_price};
use crate::application::ports::Clock;
use crate::application::profile::{Profile, ProfileService};
use crate::application::run_state::RunConfig;
use crate::assets::SharedFs;
use crate::domain::character::{build_sheet, item_value};
use crate::domain::definitions::GameData;
use crate::domain::entities::{ActiveStatus, ItemInstance};
use crate::domain::enums::{ELEMENTS, Element, Phase, SLOTS, SpellKind};
use crate::domain::formulas::{mana_for_magic_level, pct, round_info, spell_level_for_uses, xp_for_level};
use crate::infrastructure::i18n::{DEFAULT_LOCALE, Params, SUPPORTED_LOCALES, Translator};
use crate::infrastructure::repositories::{Settings, SettingsRepository};
use crate::presentation::event_text::EventFormatter;
use crate::presentation::render::list_key;

pub const MAX_LOG_LINES: usize = 50;
pub const MAX_NAME_LENGTH: usize = 16;
pub const MAX_QUANTITY_DIGITS: usize = 2;
pub const PAGE_SIZE: usize = 10;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum View {
	Language,
	Title,
	Difficulty,
	Name,
	Vocation,
	Merchant,
	BuyPotions,
	Quantity,
	Sell,
	Equipment,
	Stock,
	Character,
	Battle,
	Spells,
	Potions,
	GameOver,
	HallOfFame,
	Bestiary,
	Achievements,
}

impl View {
	pub fn is_battle(self) -> bool {
		matches!(self, View::Battle | View::Spells | View::Potions)
	}

	pub fn is_text_input(self) -> bool {
		matches!(self, View::Name | View::Quantity)
	}

	/// Informative screens: paged lines and no combat log.
	pub fn is_paged(self) -> bool {
		matches!(self, View::HallOfFame | View::Bestiary | View::Achievements | View::Character)
	}
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MenuOption {
	pub key: String,
	pub label: String,
	pub color: Option<String>,
}

impl MenuOption {
	fn new(key: &str, label: String) -> MenuOption {
		MenuOption { key: key.to_owned(), label, color: None }
	}
}

/// What a menu option does when its key is pressed.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Action {
	Step(Command),
	Go(View),
	ChooseLanguage(&'static str),
	OpenLanguage,
	Quit,
	NewRun,
	ChooseDifficulty(String),
	ChooseVocation(String),
	Continue,
	AskQuantity(String),
	SaveAndQuit,
}

type Menu = Vec<(MenuOption, Action)>;

/// The collaborators of the controller.
#[derive(Clone)]
pub struct Services {
	pub data: Rc<GameData>,
	pub shared: Rc<SharedFs>,
	pub settings: SettingsRepository,
	pub repositories: Repositories,
	pub clock: Rc<dyn Clock>,
	pub version: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MonsterView {
	pub name: String,
	pub creature_id: String,
	pub hp: i64,
	pub max_hp: i64,
	pub is_boss: bool,
	pub element: Element,
	pub details: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PlayerView {
	pub summary: String,
	pub gold: String,
	pub hp: i64,
	pub max_hp: i64,
	pub mp: i64,
	pub max_mp: i64,
	pub xp: String,
	pub statuses: String,
}

/// A seed from the OS-seeded hasher keys plus the clock (presentation-level randomness only; the engine never
/// sees anything but the resulting number).
pub fn random_seed() -> u64 {
	let mut hasher = RandomState::new().build_hasher();
	hasher.write_u128(SystemTime::now().duration_since(UNIX_EPOCH).map_or(0, |elapsed| elapsed.as_nanos()));
	hasher.finish() & 0xFFFF_FFFF
}

pub struct Controller {
	pub view: View,
	pub session: Option<GameSession>,
	pub log: VecDeque<String>,
	pub message: String,
	pub input_buffer: String,
	pub exit_requested: bool,
	pub animation_cues: Vec<String>,
	pub page: usize,
	pub locale: String,
	/// The last persistence error, shown by the renderer instead of crashing.
	pub error: Option<String>,
	services: Services,
	translator: Rc<Translator>,
	formatter: EventFormatter,
	language_return: View,
	difficulty: String,
	name: String,
	potion_id: String,
	seed: Option<u64>,
	seed_source: Box<dyn Fn() -> u64>,
}

impl Controller {
	pub fn new(
		services: Services,
		seed: Option<u64>,
		locale_override: Option<&str>,
		seed_source: Option<Box<dyn Fn() -> u64>>,
	) -> Result<Controller, String> {
		let settings = services.settings.load().map_err(|error| format!("load settings: {error}"))?;
		let chosen = locale_override.map(str::to_owned).or(settings.locale);
		let locale = chosen.clone().unwrap_or_else(|| DEFAULT_LOCALE.to_owned());
		let translator = Rc::new(Translator::new(&services.shared, &locale)?);
		let formatter = EventFormatter::new(Rc::clone(&services.data), Rc::clone(&translator));
		Ok(Controller {
			view: if chosen.is_some() { View::Title } else { View::Language },
			session: None,
			log: VecDeque::new(),
			message: String::new(),
			input_buffer: String::new(),
			exit_requested: false,
			animation_cues: Vec::new(),
			page: 0,
			locale,
			error: None,
			services,
			translator,
			formatter,
			language_return: View::Title,
			difficulty: "normal".to_owned(),
			name: String::new(),
			potion_id: String::new(),
			seed,
			seed_source: seed_source.unwrap_or_else(|| Box::new(random_seed)),
		})
	}

	pub fn services(&self) -> &Services {
		&self.services
	}

	fn data(&self) -> &GameData {
		&self.services.data
	}

	// ── i18n ──────────────────────────────────────────────────────────────────

	fn set_locale(&mut self, locale: &str) -> Result<(), String> {
		let translator = Rc::new(Translator::new(&self.services.shared, locale)?);
		self.formatter = EventFormatter::new(Rc::clone(&self.services.data), Rc::clone(&translator));
		self.translator = translator;
		locale.clone_into(&mut self.locale);
		Ok(())
	}

	pub fn t(&self, key: &str, params: &Params<'_>) -> String {
		self.translator.t(key, params)
	}

	/// Translation without parameters.
	fn tr(&self, key: &str) -> String {
		self.translator.t(key, &[])
	}

	// ── queries used by renderers ─────────────────────────────────────────────

	pub fn title(&self) -> String {
		match self.view {
			View::Language => self.tr("language.title"),
			View::Title => self.tr("app.title"),
			View::Difficulty => self.tr("new_run.difficulty"),
			View::Name => self.tr("new_run.name"),
			View::Vocation => self.tr("new_run.vocation"),
			View::Merchant => {
				let round = self.session.as_ref().map_or(0, |session| session.state().round);
				if round == 0 {
					return self.tr("merchant.title_start");
				}
				self.t("merchant.title", &[("round", &round)])
			}
			View::BuyPotions => self.tr("merchant.buy_potions"),
			View::Quantity => self.t("merchant.quantity", &[("name", &self.data().potion(&self.potion_id).name)]),
			View::Sell => self.tr("merchant.sell_items"),
			View::Equipment => self.tr("merchant.equipment"),
			View::Stock => self.tr("merchant.stock"),
			View::Character => self.tr("merchant.character"),
			View::Battle => self.tr("battle.title"),
			View::Spells => self.tr("battle.spells"),
			View::Potions => self.tr("battle.potions"),
			View::GameOver => self.tr("gameover.title"),
			View::HallOfFame => self.tr("menu.hall_of_fame"),
			View::Bestiary => self.tr("menu.bestiary"),
			View::Achievements => self.tr("menu.achievements"),
		}
	}

	pub fn options(&self) -> Vec<MenuOption> {
		self.menu().into_iter().map(|(option, _)| option).collect()
	}

	/// Informative lines shown above the options (paged for long lists).
	pub fn body_lines(&mut self) -> Vec<String> {
		let lines = self.body();
		if !self.view.is_paged() || lines.len() <= PAGE_SIZE {
			return lines;
		}
		let pages = lines.len().div_ceil(PAGE_SIZE);
		self.page = self.page.min(pages - 1);
		let start = self.page * PAGE_SIZE;
		let mut page: Vec<String> = lines[start..lines.len().min(start + PAGE_SIZE)].to_vec();
		page.push(String::new());
		page.push(self.t("menu.page", &[("page", &(self.page + 1)), ("pages", &pages)]));
		page
	}

	pub fn input_prompt(&self) -> Option<String> {
		self.view.is_text_input().then(|| format!("> {}_", self.input_buffer))
	}

	pub fn header(&self) -> String {
		let Some(session) = &self.session else {
			return self.tr("app.subtitle");
		};
		let state = session.state();
		let data = self.data();
		let tier = round_info(state.round.max(1), &data.balance, data.tier_count()).tier + 1;
		let difficulty = self.tr(&format!("difficulty.{}", state.config.difficulty_id));
		let round_text = self.t("hud.round", &[("round", &state.round), ("tier", &tier), ("difficulty", &difficulty)]);
		format!("{round_text} · {}", self.t("hud.seed", &[("seed", &state.seed)]))
	}

	fn status_text(&self, statuses: &[ActiveStatus]) -> Vec<String> {
		statuses
			.iter()
			.map(|status| format!("{}({})", self.tr(&format!("status.{}", status.status_id)), status.turns))
			.collect()
	}

	fn element_label(&self, element: Element) -> String {
		self.tr(&format!("element.{element}"))
	}

	pub fn monster_view(&self) -> Option<MonsterView> {
		let monster = self.session.as_ref()?.state().monster.as_ref()?;
		let creature = self.data().creature(&monster.creature_id);
		let main_element = monster
			.attacks
			.iter()
			.max_by(|a, b| (a.weight, &a.id).cmp(&(b.weight, &b.id)))
			.map_or(Element::Physical, |attack| attack.element);
		let weak: Vec<String> = ELEMENTS
			.iter()
			.filter(|&&element| creature.resistance(element) > 100)
			.map(|&element| self.element_label(element))
			.collect();
		let mut parts: Vec<String> = Vec::new();
		for attack in &monster.attacks {
			let label = self.element_label(attack.element);
			if !parts.contains(&label) {
				parts.push(label);
			}
		}
		let mut details = parts.join(" · ");
		if !weak.is_empty() {
			details.push_str(" · ");
			details.push_str(&self.t("hud.weak", &[("elements", &weak.join(", "))]));
		}
		let statuses = self.status_text(&monster.statuses);
		if !statuses.is_empty() {
			details.push_str(" · ");
			details.push_str(&statuses.join(" "));
		}
		Some(MonsterView {
			name: creature.name.clone(),
			creature_id: creature.id.clone(),
			hp: monster.hp,
			max_hp: monster.max_hp,
			is_boss: monster.is_boss,
			element: main_element,
			details,
		})
	}

	pub fn player_view(&self) -> Option<PlayerView> {
		let player = &self.session.as_ref()?.state().player;
		let sheet = build_sheet(player, self.data());
		let vocation = self.tr(&format!("vocation.{}", player.vocation_id));
		let summary = self.t(
			"hud.player",
			&[
				("name", &player.name),
				("vocation", &vocation),
				("level", &player.level),
				("magicLevel", &player.magic_level),
			],
		);
		Some(PlayerView {
			summary,
			gold: self.t("hud.gold", &[("gold", &player.gold)]),
			hp: player.hp,
			max_hp: sheet.max_hp,
			mp: player.mp,
			max_mp: sheet.max_mp,
			xp: self.t("hud.xp", &[("xp", &player.xp), ("next", &xp_for_level(player.level + 1))]),
			statuses: self.status_text(&player.statuses).join(" "),
		})
	}

	// ── input ─────────────────────────────────────────────────────────────────

	/// Key names: single characters, plus `enter`, `escape` and `backspace` (same names in every implementation).
	pub fn press(&mut self, key: &str) {
		self.message.clear();
		if self.view.is_text_input() {
			self.text_input(key);
			return;
		}
		if self.view.is_paged() && (key == "n" || key == "p") {
			self.page = if key == "n" { self.page + 1 } else { self.page.saturating_sub(1) };
			return;
		}
		let key = if key == "escape" { "0" } else { key };
		if let Some((_, action)) = self.menu().into_iter().find(|(option, _)| option.key == key) {
			self.run(action);
		}
	}

	fn text_input(&mut self, key: &str) {
		let mut characters = key.chars();
		let single = match (characters.next(), characters.next()) {
			(Some(character), None) => Some(character),
			_ => None,
		};
		match key {
			"escape" => {
				self.input_buffer.clear();
				self.view = if self.view == View::Name { View::Difficulty } else { View::BuyPotions };
			}
			"backspace" => {
				self.input_buffer.pop();
			}
			"enter" => self.submit_text(),
			_ => match single {
				// Python's `str.isprintable()`: no control characters and no separators other than the space.
				Some(character)
					if self.view == View::Name
						&& !character.is_control()
						&& (character == ' ' || !character.is_whitespace()) =>
				{
					if self.input_buffer.chars().count() < MAX_NAME_LENGTH {
						self.input_buffer.push(character);
					}
				}
				Some(character)
					if self.view == View::Quantity
						&& character.is_ascii_digit()
						&& self.input_buffer.len() < MAX_QUANTITY_DIGITS =>
				{
					self.input_buffer.push(character);
				}
				_ => {}
			},
		}
	}

	fn submit_text(&mut self) {
		let text = self.input_buffer.trim().to_owned();
		self.input_buffer.clear();
		if self.view == View::Name {
			if !(1..=MAX_NAME_LENGTH).contains(&text.chars().count()) {
				self.message = self.tr("new_run.name_invalid");
				return;
			}
			self.name = text;
			self.view = View::Vocation;
			return;
		}
		self.view = View::BuyPotions;
		if let Ok(quantity) = text.parse::<i64>()
			&& quantity > 0
		{
			self.step(&Command::buy_potion(&self.potion_id.clone(), quantity));
		}
	}

	// ── menus ─────────────────────────────────────────────────────────────────

	fn menu(&self) -> Menu {
		let back = |target: View| (MenuOption::new("0", self.tr("menu.back")), Action::Go(target));
		let with_back = |mut menu: Menu, target: View| {
			menu.push(back(target));
			menu
		};
		match self.view {
			View::Language => SUPPORTED_LOCALES
				.iter()
				.enumerate()
				.map(|(index, &locale)| {
					(
						MenuOption::new(&(index + 1).to_string(), self.tr(&format!("language.{locale}"))),
						Action::ChooseLanguage(locale),
					)
				})
				.collect(),
			View::Title => self.title_menu(),
			View::Difficulty => {
				let items = self
					.data()
					.balance
					.difficulties
					.iter()
					.enumerate()
					.map(|(index, difficulty)| {
						let label = format!(
							"{} — {}",
							self.tr(&format!("difficulty.{}", difficulty.id)),
							self.tr(&format!("difficulty.{}.description", difficulty.id))
						);
						(
							MenuOption::new(&(index + 1).to_string(), label),
							Action::ChooseDifficulty(difficulty.id.clone()),
						)
					})
					.collect();
				with_back(items, View::Title)
			}
			View::Vocation => {
				let items = self
					.data()
					.vocations
					.iter()
					.enumerate()
					.map(|(index, vocation)| {
						let label = format!(
							"{} — {}",
							self.tr(&format!("vocation.{}", vocation.id)),
							self.tr(&format!("vocation.{}.description", vocation.id))
						);
						(MenuOption::new(&(index + 1).to_string(), label), Action::ChooseVocation(vocation.id.clone()))
					})
					.collect();
				with_back(items, View::Difficulty)
			}
			View::Merchant => self.merchant_menu(),
			View::BuyPotions => with_back(self.potion_shop(), View::Merchant),
			View::Sell => with_back(self.sell_menu(), View::Merchant),
			View::Equipment => with_back(self.equipment_menu(), View::Merchant),
			View::Stock => with_back(self.stock_menu(), View::Merchant),
			View::Character => vec![back(View::Merchant)],
			View::Battle => vec![
				(MenuOption::new("1", self.tr("battle.attack")), Action::Step(Command::Attack)),
				(MenuOption::new("2", self.tr("battle.spells")), Action::Go(View::Spells)),
				(MenuOption::new("3", self.tr("battle.potions")), Action::Go(View::Potions)),
				(MenuOption::new("4", self.tr("battle.defend")), Action::Step(Command::Defend)),
				(MenuOption::new("q", self.tr("battle.save_quit")), Action::SaveAndQuit),
			],
			View::Spells => with_back(self.spell_menu(), View::Battle),
			View::Potions => with_back(self.battle_potions(), View::Battle),
			View::GameOver => vec![
				(MenuOption::new("1", self.tr("gameover.new_run")), Action::NewRun),
				(MenuOption::new("2", self.tr("gameover.title_screen")), Action::Go(View::Title)),
			],
			View::HallOfFame | View::Bestiary | View::Achievements => vec![back(View::Title)],
			View::Name | View::Quantity => Vec::new(),
		}
	}

	fn title_menu(&self) -> Menu {
		let mut menu = Vec::new();
		if matches!(self.services.repositories.saves.load(), Ok(Some(_))) {
			menu.push((MenuOption::new("1", self.tr("menu.continue")), Action::Continue));
		}
		menu.extend([
			(MenuOption::new("2", self.tr("menu.new_run")), Action::NewRun),
			(MenuOption::new("3", self.tr("menu.hall_of_fame")), Action::Go(View::HallOfFame)),
			(MenuOption::new("4", self.tr("menu.bestiary")), Action::Go(View::Bestiary)),
			(MenuOption::new("5", self.tr("menu.achievements")), Action::Go(View::Achievements)),
			(MenuOption::new("6", self.tr("menu.language")), Action::OpenLanguage),
			(MenuOption::new("0", self.tr("menu.quit")), Action::Quit),
		]);
		menu
	}

	fn merchant_menu(&self) -> Menu {
		vec![
			(MenuOption::new("1", self.tr("merchant.buy_potions")), Action::Go(View::BuyPotions)),
			(MenuOption::new("2", self.tr("merchant.sell_items")), Action::Go(View::Sell)),
			(MenuOption::new("3", self.tr("merchant.equipment")), Action::Go(View::Equipment)),
			(MenuOption::new("4", self.tr("merchant.stock")), Action::Go(View::Stock)),
			(MenuOption::new("5", self.tr("merchant.character")), Action::Go(View::Character)),
			(MenuOption::new("0", self.tr("merchant.next_fight")), Action::Step(Command::NextFight)),
			(MenuOption::new("q", self.tr("battle.save_quit")), Action::SaveAndQuit),
		]
	}

	fn item_label(&self, key: String, template: &str, item: &ItemInstance, gold: Option<i64>) -> MenuOption {
		let definition = self.data().item(&item.item_id);
		let rarity = self.tr(&format!("rarity.{}", item.rarity));
		let slot = self.tr(&format!("slot.{}", definition.slot));
		let mut params: Vec<(&str, &dyn Display)> =
			vec![("name", &definition.name), ("rarity", &rarity), ("slot", &slot)];
		if let Some(gold) = &gold {
			params.push(("gold", gold));
		}
		MenuOption { key, label: self.t(template, &params), color: Some(item.rarity.clone()) }
	}

	fn require_session(&self) -> &GameSession {
		self.session.as_ref().unwrap_or_else(|| panic!("view {:?} needs an active run", self.view))
	}

	fn potion_shop(&self) -> Menu {
		let state = self.require_session().state();
		available_potions(state, self.data())
			.into_iter()
			.enumerate()
			.map(|(index, potion_id)| {
				let potion = self.data().potion(&potion_id);
				let label = self.t(
					"merchant.potion_option",
					&[
						("name", &potion.name),
						("price", &potion.price),
						("count", &state.player.potion_count(&potion_id)),
					],
				);
				(MenuOption::new(&list_key(index), label), Action::AskQuantity(potion_id))
			})
			.collect()
	}

	fn sell_menu(&self) -> Menu {
		let player = &self.require_session().state().player;
		player
			.bag
			.iter()
			.enumerate()
			.map(|(index, item)| {
				let option =
					self.item_label(list_key(index), "merchant.sell_option", item, Some(item_value(item, self.data())));
				(option, Action::Step(Command::SellItem { uid: item.uid }))
			})
			.collect()
	}

	fn equipment_menu(&self) -> Menu {
		let player = &self.require_session().state().player;
		let vocation = self.data().vocation(&player.vocation_id);
		let mut entries: Vec<(&ItemInstance, &str, Command)> = player
			.bag
			.iter()
			.filter(|item| can_use(self.data().item(&item.item_id), vocation))
			.map(|item| (item, "merchant.equip_option", Command::Equip { uid: item.uid }))
			.collect();
		for slot in SLOTS {
			if let Some(equipped) = player.equipment.get(&slot) {
				entries.push((equipped, "merchant.unequip_option", Command::Unequip { slot }));
			}
		}
		entries
			.into_iter()
			.enumerate()
			.map(|(index, (item, template, command))| {
				(self.item_label(list_key(index), template, item, None), Action::Step(command))
			})
			.collect()
	}

	fn stock_menu(&self) -> Menu {
		let stock = &self.require_session().state().merchant_stock;
		stock
			.iter()
			.enumerate()
			.map(|(index, item)| {
				let option = self.item_label(
					list_key(index),
					"merchant.stock_option",
					item,
					Some(stock_price(item, self.data())),
				);
				(option, Action::Step(Command::BuyStockItem { index: index as i64 }))
			})
			.collect()
	}

	fn spell_menu(&self) -> Menu {
		let player = &self.require_session().state().player;
		let levels = &self.data().balance.spell_levels;
		self.data()
			.vocation(&player.vocation_id)
			.spells
			.iter()
			.enumerate()
			.map(|(index, spell_id)| {
				let spell = self.data().spell(spell_id);
				let uses = player.spell_use_count(spell_id);
				let level = spell_level_for_uses(uses, levels);
				let label = self.t(
					"battle.spell_option",
					&[
						("name", &spell.name),
						("words", &spell.words),
						("mana", &pct(spell.mana, level.mana_pct)),
						("level", &level.level),
						("uses", &uses),
					],
				);
				let color = if spell.kind == SpellKind::Heal { "green".to_owned() } else { spell.element.to_string() };
				let option = MenuOption { key: list_key(index), label, color: Some(color) };
				(option, Action::Step(Command::cast(spell_id)))
			})
			.collect()
	}

	fn battle_potions(&self) -> Menu {
		let player = &self.require_session().state().player;
		self.data()
			.potions
			.iter()
			.filter(|potion| player.potion_count(&potion.id) > 0)
			.enumerate()
			.map(|(index, potion)| {
				let label = self
					.t("battle.potion_option", &[("name", &potion.name), ("count", &player.potion_count(&potion.id))]);
				(MenuOption::new(&list_key(index), label), Action::Step(Command::use_potion(&potion.id)))
			})
			.collect()
	}

	// ── actions ───────────────────────────────────────────────────────────────

	fn run(&mut self, action: Action) {
		match action {
			Action::Step(command) => self.step(&command),
			Action::Go(target) => {
				self.view = target;
				self.page = 0;
			}
			Action::ChooseLanguage(locale) => self.choose_language(locale),
			Action::OpenLanguage => {
				self.language_return = View::Title;
				self.view = View::Language;
			}
			Action::Quit => self.exit_requested = true,
			Action::NewRun => {
				self.session = None;
				self.view = View::Difficulty;
			}
			Action::ChooseDifficulty(difficulty_id) => {
				self.difficulty = difficulty_id;
				self.input_buffer.clear();
				self.view = View::Name;
			}
			Action::ChooseVocation(vocation_id) => self.choose_vocation(&vocation_id),
			Action::Continue => self.continue_run(),
			Action::AskQuantity(potion_id) => {
				self.potion_id = potion_id;
				self.input_buffer.clear();
				self.view = View::Quantity;
			}
			Action::SaveAndQuit => self.save_and_quit(),
		}
	}

	fn choose_language(&mut self, locale: &str) {
		if let Err(error) = self.set_locale(locale) {
			self.error = Some(error);
			return;
		}
		if let Err(error) = self.services.settings.save(&Settings { locale: Some(locale.to_owned()) }) {
			self.error = Some(error.to_string());
		}
		self.view = self.language_return;
	}

	fn choose_vocation(&mut self, vocation_id: &str) {
		let seed = self.seed.unwrap_or_else(|| (self.seed_source)());
		let services = self.services.clone();
		let config = RunConfig::new(&self.name, vocation_id, &self.difficulty);
		match GameSession::start(services.data, config, seed, services.repositories, services.clock, &services.version)
		{
			Ok((session, events)) => {
				self.session = Some(session);
				self.log.clear();
				self.record(&StepResult { events, achievements: Vec::new() });
				self.view = View::Merchant;
			}
			Err(error) => self.error = Some(error.to_string()),
		}
	}

	fn continue_run(&mut self) {
		let services = self.services.clone();
		match GameSession::resume(services.data, services.repositories, services.clock, &services.version) {
			Ok(Some(session)) => {
				let welcome = self.t(
					"menu.welcome_back",
					&[("name", &session.state().player.name), ("round", &session.state().round)],
				);
				self.session = Some(session);
				self.log.clear();
				self.push_log(welcome);
				self.view = View::Merchant;
			}
			Ok(None) => {}
			Err(error) => self.error = Some(error.to_string()),
		}
	}

	fn save_and_quit(&mut self) {
		if let Some(session) = &mut self.session
			&& let Err(error) = session.save_and_quit()
		{
			self.error = Some(error.to_string());
		}
		self.session = None;
		self.view = View::Title;
	}

	fn step(&mut self, command: &Command) {
		let session = self.session.as_mut().unwrap_or_else(|| panic!("view {:?} needs an active run", self.view));
		let result = match session.step(command) {
			Ok(result) => result,
			Err(error) => {
				self.error = Some(error.to_string());
				return;
			}
		};
		self.record(&result);
		match self.require_session().state().phase {
			Phase::Battle => self.view = View::Battle,
			Phase::GameOver => self.view = View::GameOver,
			Phase::Merchant if self.view.is_battle() => self.view = View::Merchant,
			Phase::Merchant => {}
		}
	}

	fn push_log(&mut self, line: String) {
		if self.log.len() == MAX_LOG_LINES {
			self.log.pop_front();
		}
		self.log.push_back(line);
	}

	fn record(&mut self, result: &StepResult) {
		let state = self.require_session().state();
		let mut lines = Vec::new();
		let mut message = None;
		let mut cues = Vec::new();
		for event in &result.events {
			let text = self.formatter.format(event, state);
			if event.is_error() {
				message = Some(text);
				continue;
			}
			lines.push(text);
			match event {
				Event::PlayerAttacked { damage, .. } | Event::SpellCast { damage, .. } if *damage != 0 => {
					cues.push("hurt".to_owned());
				}
				Event::MonsterAttacked { .. } => cues.push("attack".to_owned()),
				_ => {}
			}
		}
		for achievement in &result.achievements {
			let name = self.tr(&format!("achievement.{}.name", achievement.id));
			lines.push(self.t("achievement.unlocked", &[("name", &name)]));
		}
		if let Some(message) = message {
			self.message = message;
		}
		for line in lines {
			self.push_log(line);
		}
		self.animation_cues = cues;
	}

	// ── informative bodies ────────────────────────────────────────────────────

	fn profile(&self) -> ProfileService {
		match &self.session {
			Some(session) => session.profile.clone(),
			None => {
				let profile = self.services.repositories.profile.load().unwrap_or_else(|_| Profile::default());
				ProfileService::new(Rc::clone(&self.services.data), profile)
			}
		}
	}

	fn body(&self) -> Vec<String> {
		match self.view {
			View::Character => self.character_sheet(),
			View::GameOver => self.game_over_summary(),
			View::HallOfFame => self.hall_of_fame(),
			View::Bestiary => self.bestiary(),
			View::Achievements => self.achievements(),
			View::Merchant => {
				let player = &self.require_session().state().player;
				vec![self.t("merchant.welcome", &[("name", &player.name), ("gold", &player.gold)])]
			}
			View::Sell if self.require_session().state().player.bag.is_empty() => vec![self.tr("merchant.empty_bag")],
			View::Stock if self.require_session().state().merchant_stock.is_empty() => {
				vec![self.tr("merchant.empty_stock")]
			}
			View::Potions if self.require_session().state().player.potions.values().all(|&count| count == 0) => {
				vec![self.tr("battle.no_potions")]
			}
			_ => Vec::new(),
		}
	}

	fn character_sheet(&self) -> Vec<String> {
		let player = &self.require_session().state().player;
		let sheet = build_sheet(player, self.data());
		let balance = &self.data().balance;
		let element = self.element_label(sheet.weapon_element);
		let mut lines = vec![
			self.t(
				"character.level",
				&[("level", &player.level), ("xp", &player.xp), ("next", &xp_for_level(player.level + 1))],
			),
			self.t(
				"character.magic_level",
				&[
					("magicLevel", &player.magic_level),
					("spent", &player.mana_spent),
					("next", &mana_for_magic_level(player.magic_level, balance)),
				],
			),
			self.t(
				"character.hp_mp",
				&[("hp", &player.hp), ("maxHp", &sheet.max_hp), ("mp", &player.mp), ("maxMp", &sheet.max_mp)],
			),
			self.t("character.melee", &[("min", &sheet.melee_min), ("max", &sheet.melee_max), ("element", &element)]),
		];
		let stats = [
			("stat.armor", sheet.armor),
			("stat.hpRegen", sheet.hp_regen),
			("stat.mpRegen", sheet.mp_regen),
			("stat.critChance", sheet.crit_chance),
			("stat.critDamage", sheet.crit_damage),
			("stat.spellPower", sheet.spell_power),
			("stat.physicalDamage", sheet.physical_damage),
			("stat.dodge", sheet.dodge),
			("stat.parry", sheet.parry),
			("stat.lifeLeech", sheet.life_leech),
			("stat.manaLeech", sheet.mana_leech),
		];
		for (key, value) in stats {
			if value != 0 {
				lines.push(self.t("character.stat_line", &[("stat", &self.tr(key)), ("value", &value)]));
			}
		}
		for (&element, &value) in &sheet.protections {
			if value != 0 {
				let label = self.element_label(element);
				lines.push(self.t("character.stat_line", &[("stat", &label), ("value", &format!("{value}%"))]));
			}
		}
		lines.push(String::new());
		lines.push(self.tr("character.equipment"));
		for slot in SLOTS {
			let slot_label = self.tr(&format!("slot.{slot}"));
			match player.equipment.get(&slot) {
				None => lines.push(self.t("character.empty_slot", &[("slot", &slot_label)])),
				Some(item) => {
					let rarity = self.tr(&format!("rarity.{}", item.rarity));
					lines.push(self.t(
						"character.slot",
						&[("slot", &slot_label), ("item", &self.data().item(&item.item_id).name), ("rarity", &rarity)],
					));
				}
			}
		}
		lines.push(self.t("character.bag", &[("count", &player.bag.len()), ("capacity", &balance.bag_capacity)]));
		lines
	}

	fn game_over_summary(&self) -> Vec<String> {
		let state = self.require_session().state();
		let monster = match state.death_cause.as_deref() {
			Some(cause) if !cause.is_empty() => self.data().creature(cause).name.clone(),
			_ => "?".to_owned(),
		};
		let vocation = self.tr(&format!("vocation.{}", state.player.vocation_id));
		vec![
			self.t(
				"gameover.summary",
				&[
					("name", &state.player.name),
					("vocation", &vocation),
					("round", &state.round),
					("monster", &monster),
				],
			),
			self.t(
				"gameover.stats",
				&[
					("level", &state.player.level),
					("damage", &state.stats.damage_dealt),
					("kills", &state.stats.total_kills()),
					("bosses", &state.stats.bosses_killed),
				],
			),
		]
	}

	fn hall_of_fame(&self) -> Vec<String> {
		let hall = self.profile().profile.hall_of_fame;
		if hall.is_empty() {
			return vec![self.tr("hall.empty")];
		}
		hall.iter()
			.enumerate()
			.map(|(index, entry)| {
				let vocation = self.tr(&format!("vocation.{}", entry.vocation));
				let difficulty = self.tr(&format!("difficulty.{}", entry.difficulty));
				let date: String = entry.ended_at.chars().take(10).collect();
				self.t(
					"hall.entry",
					&[
						("position", &(index + 1)),
						("name", &entry.name),
						("vocation", &vocation),
						("difficulty", &difficulty),
						("round", &entry.round),
						("level", &entry.level),
						("date", &date),
					],
				)
			})
			.collect()
	}

	fn bestiary(&self) -> Vec<String> {
		let profile = self.profile();
		let mut creatures: Vec<_> = self.data().monsters.iter().chain(&self.data().bosses).collect();
		creatures.sort_by(|a, b| (a.tier, a.is_boss, &a.name).cmp(&(b.tier, b.is_boss, &b.name)));
		creatures
			.into_iter()
			.map(|creature| {
				let tier = creature.tier + 1;
				let Some(entry) = profile.profile.bestiary.get(&creature.id) else {
					return self.t("bestiary.unknown", &[("tier", &tier)]);
				};
				if !profile.revealed(&creature.id) {
					return self
						.t("bestiary.entry", &[("name", &creature.name), ("tier", &tier), ("kills", &entry.kills)]);
				}
				let labels = |keep: fn(i64) -> bool| -> String {
					let labels: Vec<String> = ELEMENTS
						.iter()
						.filter(|&&element| keep(creature.resistance(element)))
						.map(|&element| self.element_label(element))
						.collect();
					if labels.is_empty() { "—".to_owned() } else { labels.join(", ") }
				};
				self.t(
					"bestiary.entry_revealed",
					&[
						("name", &creature.name),
						("tier", &tier),
						("kills", &entry.kills),
						("weak", &labels(|resistance| resistance > 100)),
						("strong", &labels(|resistance| resistance < 100)),
					],
				)
			})
			.collect()
	}

	fn achievements(&self) -> Vec<String> {
		let unlocked = self.profile().profile.achievements;
		self.data()
			.achievements
			.iter()
			.map(|achievement| {
				let name = self.tr(&format!("achievement.{}.name", achievement.id));
				let description =
					self.t(&format!("achievement.{}.description", achievement.id), &[("value", &achievement.value)]);
				match unlocked.get(&achievement.id) {
					None => self.t("achievements.locked", &[("name", &name), ("description", &description)]),
					Some(unlock) => {
						let date: String = unlock.unlocked_at.chars().take(10).collect();
						self.t(
							"achievements.unlocked",
							&[("name", &name), ("description", &description), ("date", &date)],
						)
					}
				}
			})
			.collect()
	}
}
