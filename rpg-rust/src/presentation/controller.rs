//! Framework-independent UI state machine: which screen is shown, its options and what each key does.
//!
//! The ratatui app only renders this controller. The other five ports implement the same controller, which is what
//! keeps the six interfaces practically identical (docs/tui.md).
//!
//! The reference builds menus as `(option, closure)` pairs. Here a menu entry carries an [`Action`] value instead:
//! closures that capture `&mut self` would fight the borrow checker, while plain data is easy to test and match.

use std::collections::VecDeque;
use std::collections::hash_map::RandomState;
use std::fmt::Display;
use std::hash::{BuildHasher, Hasher};
use std::rc::Rc;
use std::time::{SystemTime, UNIX_EPOCH};

use crate::application::auto_battle::{AUTO_BATTLE_MODES, AutoBattleMode, AutoBattlePolicy};
use crate::application::commands::Command;
use crate::application::events::Event;
use crate::application::game_session::{GameSession, Repositories, StepResult};
use crate::application::loot::can_use;
use crate::application::merchant::{available_potions, stock_price};
use crate::application::ports::Clock;
use crate::application::profile::{Profile, ProfileService};
use crate::application::run_state::RunConfig;
use crate::assets::SharedFs;
use crate::domain::character::{build_sheet, equipment_score, item_score, item_stats, item_value, required_level};
use crate::domain::definitions::GameData;
use crate::domain::entities::{ActiveStatus, ItemInstance};
use crate::domain::enums::{ELEMENTS, EQUIPMENT_SLOT_ORDER, Element, EnemyClass, Phase, SLOTS, Slot, SpellKind};
use crate::domain::formulas::{mana_for_magic_level, pct, round_info, spell_level_for_uses, xp_for_level};
use crate::infrastructure::i18n::{DEFAULT_LOCALE, Params, SUPPORTED_LOCALES, Translator};
use crate::infrastructure::repositories::{BATTLE_SPEEDS, Settings, SettingsRepository};
use crate::presentation::event_text::EventFormatter;
use crate::presentation::render::{
	STYLE_DIM, STYLE_GAIN, STYLE_LOSS, STYLE_WARNING, delta_style, format_delta, list_key,
};

pub const MAX_LOG_LINES: usize = 50;
pub const MAX_NAME_LENGTH: usize = 16;
pub const MAX_QUANTITY_DIGITS: usize = 2;
pub const PAGE_SIZE: usize = 10;
/// Auto-battle pace (docs/tui.md): one turn every 600 ms at 1x, 300 ms at 2x; instant with --no-anim.
pub const AUTO_BATTLE_BASE_MS: u64 = 600;
/// Safety net: a fight that somehow never ends hands control back to the player.
pub const MAX_AUTO_BATTLE_TURNS: usize = 10_000;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum View {
	Language,
	Title,
	Settings,
	Difficulty,
	Name,
	Vocation,
	AutoEquip,
	Merchant,
	BuyPotions,
	Quantity,
	Sell,
	Equipment,
	Compare,
	EquippedSlot,
	Stock,
	Character,
	Battle,
	Spells,
	Potions,
	AutoBattle,
	Victory,
	GameOver,
	HallOfFame,
	Bestiary,
	Achievements,
}

impl View {
	pub fn is_battle(self) -> bool {
		matches!(self, View::Battle | View::Spells | View::Potions | View::AutoBattle)
	}

	/// The equipment screens: body lines carry their own colours.
	pub fn is_styled(self) -> bool {
		matches!(self, View::Equipment | View::Compare | View::EquippedSlot)
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
	/// Shown after the label in its own colour (the score delta of the equipment screen).
	pub detail: String,
	pub detail_color: Option<String>,
}

impl MenuOption {
	fn new(key: &str, label: String) -> MenuOption {
		MenuOption::colored(key, label, None)
	}

	fn colored(key: &str, label: String, color: Option<String>) -> MenuOption {
		MenuOption { key: key.to_owned(), label, color, detail: String::new(), detail_color: None }
	}
}

/// A body line with an optional colour (a semantic style from render.rs or a rarity id).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BodyLine {
	pub text: String,
	pub color: Option<String>,
}

impl BodyLine {
	fn new(text: String, color: Option<&str>) -> BodyLine {
		BodyLine { text, color: color.map(str::to_owned) }
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
	StartRun(bool),
	Continue,
	AskQuantity(String),
	SaveAndQuit,
	ToggleAutoEquip,
	CycleBattleSpeed,
	OpenCompare(i64),
	OpenSlot(Slot),
	EquipCompared,
	UnequipSlot,
	StartAutoBattle(AutoBattleMode),
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
	pub enemy_class: EnemyClass,
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
	pub settings: Settings,
	services: Services,
	translator: Rc<Translator>,
	formatter: EventFormatter,
	language_return: View,
	difficulty: String,
	name: String,
	vocation: String,
	potion_id: String,
	compare_uid: i64,
	slot: Slot,
	auto_battle: Option<AutoBattleMode>,
	auto_turns: usize,
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
		let chosen = locale_override.map(str::to_owned).or_else(|| settings.locale.clone());
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
			settings,
			services,
			translator,
			formatter,
			language_return: View::Title,
			difficulty: "normal".to_owned(),
			name: String::new(),
			vocation: String::new(),
			potion_id: String::new(),
			compare_uid: 0,
			slot: Slot::Weapon,
			auto_battle: None,
			auto_turns: 0,
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
			View::Settings => self.tr("settings.title"),
			View::Difficulty => self.tr("new_run.difficulty"),
			View::Name => self.tr("new_run.name"),
			View::Vocation => self.tr("new_run.vocation"),
			View::AutoEquip => self.tr("new_run.auto_equip"),
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
			View::Compare => self.compare_title(),
			View::EquippedSlot => self.tr(&format!("slot.{}", self.slot)),
			View::Stock => self.tr("merchant.stock"),
			View::Character => self.tr("merchant.character"),
			View::Battle => self.tr("battle.title"),
			View::Spells => self.tr("battle.spells"),
			View::Potions => self.tr("battle.potions"),
			View::AutoBattle => self.tr("auto_battle.title"),
			View::Victory => self.tr("victory.title"),
			View::GameOver => {
				let won = self.session.as_ref().is_some_and(|session| session.state().won);
				self.tr(if won { "gameover.title_won" } else { "gameover.title" })
			}
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
		let lines = if self.view.is_styled() {
			self.styled_body().into_iter().map(|line| line.text).collect()
		} else {
			self.body()
		};
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

	/// Colour of each line of `body_lines()` (semantic styles from render.rs or rarity ids).
	pub fn body_colors(&mut self) -> Vec<Option<String>> {
		if self.view.is_styled() {
			return self.styled_body().into_iter().map(|line| line.color).collect();
		}
		vec![None; self.body_lines().len()]
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
			enemy_class: monster.enemy_class,
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
		if self.auto_battle_active() {
			return;
		}
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

	// ── auto-battle (docs/game-design.md §13, docs/tui.md) ──────────────────────

	pub fn auto_battle_active(&self) -> bool {
		self.auto_battle.is_some()
	}

	pub fn auto_battle_interval_ms(&self) -> u64 {
		AUTO_BATTLE_BASE_MS / self.settings.battle_speed.max(1) as u64
	}

	/// Plays one auto-battle turn. Returns true while the fight goes on (the renderer's timer keeps ticking).
	pub fn auto_battle_step(&mut self) -> bool {
		let in_battle = self.session.as_ref().is_some_and(|session| session.state().phase == Phase::Battle);
		let Some(mode) = self.auto_battle.filter(|_| in_battle) else {
			self.auto_battle = None;
			return false;
		};
		self.auto_turns += 1;
		let command = AutoBattlePolicy::new(self.data(), mode).choose(self.require_session().state());
		self.step(&command);
		let in_battle = self.session.as_ref().is_some_and(|session| session.state().phase == Phase::Battle);
		if !in_battle || self.auto_turns >= MAX_AUTO_BATTLE_TURNS {
			self.auto_battle = None;
		}
		self.auto_battle_active()
	}

	/// Instant mode (--no-anim): plays the whole fight at once.
	pub fn run_auto_battle(&mut self) {
		while self.auto_battle_step() {}
	}

	fn start_auto_battle(&mut self, mode: AutoBattleMode) {
		self.auto_turns = 0;
		self.auto_battle = Some(mode);
		self.view = View::Battle;
		let label = self.tr(&format!("auto_battle.{}", mode.as_str()));
		let line = self.t("auto_battle.started", &[("mode", &label)]);
		self.push_log(line);
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
			View::Settings => self.settings_menu(),
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
			View::AutoEquip => with_back(self.auto_equip_menu(), View::Vocation),
			View::Merchant => self.merchant_menu(),
			View::BuyPotions => with_back(self.potion_shop(), View::Merchant),
			View::Sell => with_back(self.sell_menu(), View::Merchant),
			View::Equipment => with_back(self.equipment_menu(), View::Merchant),
			View::Compare => {
				vec![(MenuOption::new("1", self.tr("equipment.equip")), Action::EquipCompared), back(View::Equipment)]
			}
			View::EquippedSlot => {
				vec![(MenuOption::new("1", self.tr("equipment.unequip")), Action::UnequipSlot), back(View::Equipment)]
			}
			View::Stock => with_back(self.stock_menu(), View::Merchant),
			View::Character => vec![back(View::Merchant)],
			View::Battle => vec![
				(MenuOption::new("1", self.tr("battle.attack")), Action::Step(Command::Attack)),
				(MenuOption::new("2", self.tr("battle.spells")), Action::Go(View::Spells)),
				(MenuOption::new("3", self.tr("battle.potions")), Action::Go(View::Potions)),
				(MenuOption::new("4", self.tr("battle.defend")), Action::Step(Command::Defend)),
				(MenuOption::new("5", self.tr("battle.auto")), Action::Go(View::AutoBattle)),
				(MenuOption::new("q", self.tr("battle.save_quit")), Action::SaveAndQuit),
			],
			View::AutoBattle => {
				let modes = AUTO_BATTLE_MODES
					.iter()
					.enumerate()
					.map(|(index, &mode)| {
						(
							MenuOption::new(
								&(index + 1).to_string(),
								self.tr(&format!("auto_battle.{}", mode.as_str())),
							),
							Action::StartAutoBattle(mode),
						)
					})
					.collect();
				with_back(modes, View::Battle)
			}
			View::Victory => vec![
				(MenuOption::new("1", self.tr("victory.end_run")), Action::Step(Command::EndRun)),
				(MenuOption::new("2", self.tr("victory.continue")), Action::Step(Command::ContinueRun)),
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
			(MenuOption::new("6", self.tr("menu.settings")), Action::Go(View::Settings)),
			(MenuOption::new("0", self.tr("menu.quit")), Action::Quit),
		]);
		menu
	}

	fn on_off(&self, enabled: bool) -> String {
		self.tr(if enabled { "settings.on" } else { "settings.off" })
	}

	fn settings_menu(&self) -> Menu {
		let settings = &self.settings;
		let language = self.tr(&format!("language.{}", self.locale));
		vec![
			(MenuOption::new("1", self.t("settings.language", &[("language", &language)])), Action::OpenLanguage),
			(
				MenuOption::new("2", self.t("settings.auto_equip", &[("state", &self.on_off(settings.auto_equip))])),
				Action::ToggleAutoEquip,
			),
			(
				MenuOption::new("3", self.t("settings.battle_speed", &[("speed", &settings.battle_speed)])),
				Action::CycleBattleSpeed,
			),
			(MenuOption::new("0", self.tr("menu.back")), Action::Go(View::Title)),
		]
	}

	fn auto_equip_menu(&self) -> Menu {
		let default = self.tr("new_run.default");
		[("1", true), ("2", false)]
			.into_iter()
			.map(|(key, enabled)| {
				let mut label = self.tr(if enabled { "new_run.auto_equip_on" } else { "new_run.auto_equip_off" });
				if enabled == self.settings.auto_equip {
					label = format!("{label} {default}");
				}
				(MenuOption::new(key, label), Action::StartRun(enabled))
			})
			.collect()
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

	fn item_label(&self, key: &str, template: &str, item: &ItemInstance, gold: Option<i64>) -> MenuOption {
		let definition = self.data().item(&item.item_id);
		let rarity = self.tr(&format!("rarity.{}", item.rarity));
		let slot = self.tr(&format!("slot.{}", definition.slot));
		let mut params: Vec<(&str, &dyn Display)> =
			vec![("name", &definition.name), ("rarity", &rarity), ("slot", &slot)];
		if let Some(gold) = &gold {
			params.push(("gold", gold));
		}
		MenuOption::colored(key, self.t(template, &params), Some(item.rarity.clone()))
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
				let option = self.item_label(
					&list_key(index),
					"merchant.sell_option",
					item,
					Some(item_value(item, self.data())),
				);
				(option, Action::Step(Command::SellItem { uid: item.uid }))
			})
			.collect()
	}

	fn usable_bag(&self) -> Vec<&ItemInstance> {
		let player = &self.require_session().state().player;
		let vocation = self.data().vocation(&player.vocation_id);
		player.bag.iter().filter(|item| can_use(self.data().item(&item.item_id), vocation)).collect()
	}

	/// Score of `item` minus the score of what is equipped in its slot (0 for an empty slot).
	fn score_delta(&self, item: &ItemInstance) -> i64 {
		let equipment = &self.require_session().state().player.equipment;
		let equipped = equipment.get(&self.data().item(&item.item_id).slot);
		item_score(item, self.data()) - equipped.map_or(0, |equipped| item_score(equipped, self.data()))
	}

	/// Usable bag items first (keys 1..n, as in docs/tui.md), then the equipped slots.
	fn equipment_menu(&self) -> Menu {
		let player = &self.require_session().state().player;
		let mut entries: Vec<(MenuOption, Action)> = Vec::new();
		for item in self.usable_bag() {
			let definition = self.data().item(&item.item_id);
			let level = required_level(item, self.data());
			let rarity = self.tr(&format!("rarity.{}", item.rarity));
			let slot = self.tr(&format!("slot.{}", definition.slot));
			let mut label = self.t(
				"equipment.bag_option",
				&[
					("name", &definition.name),
					("rarity", &rarity),
					("slot", &slot),
					("level", &level),
					("score", &item_score(item, self.data())),
				],
			);
			let too_high = level > player.level;
			if too_high {
				label = format!("{label} · {}", self.t("equipment.requires_level", &[("level", &level)]));
			}
			let delta = self.score_delta(item);
			let color = if too_high { STYLE_DIM.to_owned() } else { item.rarity.clone() };
			let option = MenuOption {
				detail: format_delta(delta),
				detail_color: delta_style(delta).map(str::to_owned),
				..MenuOption::colored("", label, Some(color))
			};
			entries.push((option, Action::OpenCompare(item.uid)));
		}
		for slot in EQUIPMENT_SLOT_ORDER {
			if let Some(equipped) = player.equipment.get(&slot) {
				let slot_label = self.tr(&format!("slot.{slot}"));
				let rarity = self.tr(&format!("rarity.{}", equipped.rarity));
				let label = self.t(
					"equipment.slot_option",
					&[("slot", &slot_label), ("name", &self.data().item(&equipped.item_id).name), ("rarity", &rarity)],
				);
				entries.push((MenuOption::colored("", label, Some(equipped.rarity.clone())), Action::OpenSlot(slot)));
			}
		}
		entries
			.into_iter()
			.enumerate()
			.map(|(index, (option, action))| (MenuOption { key: list_key(index), ..option }, action))
			.collect()
	}

	fn compared_item(&self) -> Option<&ItemInstance> {
		self.require_session().state().player.bag.iter().find(|item| item.uid == self.compare_uid)
	}

	fn stock_menu(&self) -> Menu {
		let stock = &self.require_session().state().merchant_stock;
		stock
			.iter()
			.enumerate()
			.map(|(index, item)| {
				let option = self.item_label(
					&list_key(index),
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
				let option = MenuOption::colored(&list_key(index), label, Some(color));
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
				self.language_return = View::Settings;
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
			Action::ChooseVocation(vocation_id) => {
				self.vocation = vocation_id;
				self.view = View::AutoEquip;
			}
			Action::StartRun(auto_equip) => self.start_run(auto_equip),
			Action::Continue => self.continue_run(),
			Action::AskQuantity(potion_id) => {
				self.potion_id = potion_id;
				self.input_buffer.clear();
				self.view = View::Quantity;
			}
			Action::SaveAndQuit => self.save_and_quit(),
			Action::ToggleAutoEquip => {
				let settings = Settings { auto_equip: !self.settings.auto_equip, ..self.settings.clone() };
				self.save_settings(settings);
			}
			Action::CycleBattleSpeed => {
				let index = BATTLE_SPEEDS.iter().position(|&speed| speed == self.settings.battle_speed).unwrap_or(0);
				let speed = BATTLE_SPEEDS[(index + 1) % BATTLE_SPEEDS.len()];
				self.save_settings(Settings { battle_speed: speed, ..self.settings.clone() });
			}
			Action::OpenCompare(uid) => {
				self.compare_uid = uid;
				self.view = View::Compare;
			}
			Action::OpenSlot(slot) => {
				self.slot = slot;
				self.view = View::EquippedSlot;
			}
			Action::EquipCompared => {
				self.step(&Command::Equip { uid: self.compare_uid });
				self.view = View::Equipment;
			}
			Action::UnequipSlot => {
				self.step(&Command::Unequip { slot: self.slot });
				self.view = View::Equipment;
			}
			Action::StartAutoBattle(mode) => self.start_auto_battle(mode),
		}
	}

	fn save_settings(&mut self, settings: Settings) {
		if let Err(error) = self.services.settings.save(&settings) {
			self.error = Some(error.to_string());
		}
		self.settings = settings;
	}

	fn choose_language(&mut self, locale: &str) {
		if let Err(error) = self.set_locale(locale) {
			self.error = Some(error);
			return;
		}
		self.save_settings(Settings { locale: Some(locale.to_owned()), ..self.settings.clone() });
		self.view = self.language_return;
	}

	fn start_run(&mut self, auto_equip: bool) {
		let seed = self.seed.unwrap_or_else(|| (self.seed_source)());
		let services = self.services.clone();
		let config = RunConfig::new(&self.name, &self.vocation, &self.difficulty).with_auto_equip(auto_equip);
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
				let victory = self.require_session().state().phase == Phase::Victory;
				self.view = if victory { View::Victory } else { View::Merchant };
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
			Phase::Victory => self.view = View::Victory,
			Phase::Merchant if self.view.is_battle() || self.view == View::Victory => self.view = View::Merchant,
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
			View::Victory => self.victory_summary(),
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

	fn run_stats_line(&self) -> String {
		let state = self.require_session().state();
		self.t(
			"gameover.stats",
			&[
				("level", &state.player.level),
				("damage", &state.stats.damage_dealt),
				("kills", &state.stats.total_kills()),
				("elites", &state.stats.elites_killed),
				("bosses", &state.stats.bosses_killed),
			],
		)
	}

	fn game_over_summary(&self) -> Vec<String> {
		let state = self.require_session().state();
		let vocation = self.tr(&format!("vocation.{}", state.player.vocation_id));
		let params: [(&str, &dyn Display); 3] =
			[("name", &state.player.name), ("vocation", &vocation), ("round", &state.round)];
		let summary = match state.death_cause.as_deref() {
			Some(cause) if !cause.is_empty() => {
				let monster = &self.data().creature(cause).name;
				self.t("gameover.summary", &[&params[..], &[("monster", monster as &dyn Display)]].concat())
			}
			_ if state.won => self.t("gameover.won_summary", &params),
			_ => self.t("gameover.summary", &[&params[..], &[("monster", &"?" as &dyn Display)]].concat()),
		};
		vec![summary, self.run_stats_line()]
	}

	fn victory_summary(&self) -> Vec<String> {
		let state = self.require_session().state();
		let data = self.data();
		let boss = data.boss_of_tier(round_info(state.round, &data.balance, data.tier_count()).tier);
		let vocation = self.tr(&format!("vocation.{}", state.player.vocation_id));
		vec![
			self.t(
				"victory.summary",
				&[
					("name", &state.player.name),
					("vocation", &vocation),
					("monster", &boss.name),
					("round", &state.round),
				],
			),
			self.run_stats_line(),
			String::new(),
			self.tr("victory.choice"),
		]
	}

	// ── equipment screens (docs/tui.md "Equipment screen") ────────────────────

	fn styled_body(&self) -> Vec<BodyLine> {
		match self.view {
			View::Equipment => self.equipment_body(),
			View::Compare => self.compare_body(),
			_ => self.slot_body(),
		}
	}

	fn equipment_body(&self) -> Vec<BodyLine> {
		let player = &self.require_session().state().player;
		let header = self.t("equipment.equipped_header", &[("score", &equipment_score(player, self.data()))]);
		let mut lines = vec![BodyLine::new(header, None)];
		for slot in EQUIPMENT_SLOT_ORDER {
			let slot_name = self.tr(&format!("slot.{slot}"));
			let Some(item) = player.equipment.get(&slot) else {
				lines.push(BodyLine::new(self.t("equipment.slot_empty", &[("slot", &slot_name)]), Some(STYLE_WARNING)));
				continue;
			};
			let rarity = self.tr(&format!("rarity.{}", item.rarity));
			let text = self.t(
				"equipment.slot_line",
				&[
					("slot", &slot_name),
					("name", &self.data().item(&item.item_id).name),
					("rarity", &rarity),
					("level", &required_level(item, self.data())),
					("score", &item_score(item, self.data())),
				],
			);
			lines.push(BodyLine::new(text, Some(&item.rarity)));
		}
		lines.push(BodyLine::new(String::new(), None));
		lines.push(BodyLine::new(self.tr("equipment.bag_header"), None));
		if self.usable_bag().is_empty() {
			lines.push(BodyLine::new(self.tr("equipment.bag_empty"), None));
		}
		lines
	}

	fn compare_title(&self) -> String {
		let Some(item) = self.compared_item() else {
			return self.tr("merchant.equipment");
		};
		let player = &self.require_session().state().player;
		let slot = self.data().item(&item.item_id).slot;
		let current = match player.equipment.get(&slot) {
			None => self.tr("equipment.empty"),
			Some(current) => self.data().item(&current.item_id).name.clone(),
		};
		let slot_label = self.tr(&format!("slot.{slot}"));
		self.t(
			"equipment.compare_title",
			&[("slot", &slot_label), ("current", &current), ("new", &self.data().item(&item.item_id).name)],
		)
	}

	fn affix_list(&self, item: Option<&ItemInstance>) -> String {
		let Some(item) = item else {
			return String::new();
		};
		item.affixes
			.iter()
			.map(|affix| {
				let stat = self.tr(&format!("stat.{}", affix.stat));
				self.t("equipment.affix", &[("value", &affix.value), ("stat", &stat)])
			})
			.collect::<Vec<_>>()
			.join(", ")
	}

	fn compare_body(&self) -> Vec<BodyLine> {
		let Some(item) = self.compared_item() else {
			return Vec::new();
		};
		let player = &self.require_session().state().player;
		let current = player.equipment.get(&self.data().item(&item.item_id).slot);
		let new_stats = item_stats(item, self.data());
		let old_stats = current.map(|current| item_stats(current, self.data())).unwrap_or_default();
		let mut lines = Vec::new();
		// BTreeMap keys iterate in `Stat` declaration order, like Python's `for stat in Stat`.
		let mut stats: Vec<_> = new_stats.keys().chain(old_stats.keys()).copied().collect();
		stats.sort();
		stats.dedup();
		for stat in stats {
			let old = old_stats.get(&stat).copied().unwrap_or(0);
			let new = new_stats.get(&stat).copied().unwrap_or(0);
			let text = self.t(
				"equipment.stat_delta",
				&[
					("stat", &self.tr(&format!("stat.{stat}"))),
					("current", &old),
					("new", &new),
					("delta", &format_delta(new - old)),
				],
			);
			lines.push(BodyLine::new(text, delta_style(new - old)));
		}
		let gained = self.affix_list(Some(item));
		let lost = self.affix_list(current);
		if !gained.is_empty() {
			lines.push(BodyLine::new(self.t("equipment.affixes_gained", &[("affixes", &gained)]), Some(STYLE_GAIN)));
		}
		if !lost.is_empty() {
			lines.push(BodyLine::new(self.t("equipment.affixes_lost", &[("affixes", &lost)]), Some(STYLE_LOSS)));
		}
		let old_score = current.map_or(0, |current| item_score(current, self.data()));
		let new_score = item_score(item, self.data());
		let score = self.t(
			"equipment.score_delta",
			&[("current", &old_score), ("new", &new_score), ("delta", &format_delta(new_score - old_score))],
		);
		lines.push(BodyLine::new(score, delta_style(new_score - old_score)));
		let level = required_level(item, self.data());
		if level > player.level {
			let text = self.t("equipment.level_needed", &[("level", &level), ("current", &player.level)]);
			lines.push(BodyLine::new(text, Some(STYLE_LOSS)));
		}
		lines
	}

	fn slot_body(&self) -> Vec<BodyLine> {
		let Some(item) = self.require_session().state().player.equipment.get(&self.slot) else {
			return vec![BodyLine::new(self.tr("equipment.empty"), Some(STYLE_WARNING))];
		};
		let rarity = self.tr(&format!("rarity.{}", item.rarity));
		let title = self.t(
			"equipment.item_title",
			&[
				("name", &self.data().item(&item.item_id).name),
				("rarity", &rarity),
				("level", &required_level(item, self.data())),
				("score", &item_score(item, self.data())),
			],
		);
		let mut lines = vec![BodyLine::new(title, Some(&item.rarity))];
		for (stat, value) in item_stats(item, self.data()) {
			let label = self.tr(&format!("stat.{stat}"));
			lines.push(BodyLine::new(self.t("character.stat_line", &[("stat", &label), ("value", &value)]), None));
		}
		lines
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
					if entry.won { "hall.entry_won" } else { "hall.entry" },
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
