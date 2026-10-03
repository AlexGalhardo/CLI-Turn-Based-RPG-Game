//! ratatui renderer of the UI controller (layout specified in docs/tui.md).
//!
//! ratatui is immediate-mode: every frame `render` rebuilds the widgets from the controller state, much like a
//! React/Ink component or a Bubble Tea `View`. The event loop lives in [`run_tui`]; tests drive [`App`] directly
//! with key events and a `TestBackend`.

use std::rc::Rc;
use std::time::{Duration, Instant};
use std::{env, io};

use crossterm::event::{self, KeyCode, KeyEvent, KeyEventKind, KeyModifiers};
use ratatui::Frame;
use ratatui::buffer::Buffer;
use ratatui::layout::{Constraint, Layout, Rect};
use ratatui::style::{Color, Modifier, Style};
use ratatui::text::{Line, Span, Text};
use ratatui::widgets::{Block, BorderType, Padding, Paragraph};

use crate::application::game_session::Repositories;
use crate::assets::SharedFs;
use crate::domain::definitions::GameData;
use crate::infrastructure::art::{ArtLibrary, frame_for};
use crate::infrastructure::paths::resolve_data_dir;
use crate::infrastructure::repositories::{
	FileHistoryRepository, FileProfileRepository, FileSaveRepository, SettingsRepository, SystemClock,
};
use crate::presentation::cli::CliOptions;
use crate::presentation::controller::{Controller, Services};
use crate::presentation::render::{BAR_WIDTH, MIN_COLUMNS, MIN_ROWS, bar, element_color, hp_color, rarity_color};
use crate::version::VERSION;

pub const ANIMATION_INTERVAL: Duration = Duration::from_millis(500);
pub const LOG_LINES: usize = 5;
pub const TWO_COLUMN_THRESHOLD: usize = 4;
pub const COLUMN_WIDTH: usize = 44;
const TOP_HEIGHT: u16 = 8;
const ART_WIDTH: u16 = 34;
const PLAYER_HEIGHT: u16 = 5;
const LOG_HEIGHT: u16 = 7;
const MENU_MIN_HEIGHT: u16 = 10;
const ACCENT: Color = Color::Rgb(0xff, 0xa6, 0x2b);
const TITLE_ART: (&str, &str) = ("families", "dragon");

/// Maps the reference's colour names (Rich names) to terminal colours.
pub fn color(name: &str) -> Color {
	match name {
		"red" => Color::Red,
		"green" => Color::Green,
		"yellow" => Color::Yellow,
		"blue" => Color::Blue,
		"magenta" => Color::Magenta,
		"cyan" => Color::Cyan,
		"bright_black" => Color::DarkGray,
		"dodger_blue1" => Color::Rgb(0x00, 0x87, 0xff),
		"medium_purple1" => Color::Rgb(0xaf, 0x87, 0xff),
		"orange1" => Color::Rgb(0xff, 0xaf, 0x00),
		_ => Color::White,
	}
}

/// Colour of a menu option: a rarity, an element id or a plain colour name.
pub fn option_style(name: Option<&str>) -> Style {
	let Some(name) = name else {
		return Style::default();
	};
	let resolved = rarity_color(name).unwrap_or_else(|| {
		crate::domain::enums::ELEMENTS
			.iter()
			.find(|element| element.as_str() == name)
			.map_or(name, |&element| element_color(element))
	});
	Style::default().fg(color(resolved))
}

/// Maps crossterm keys to the controller's key names (same names as the other implementations).
pub fn key_name(key: &KeyEvent) -> Option<String> {
	match key.code {
		KeyCode::Enter => Some("enter".to_owned()),
		KeyCode::Esc => Some("escape".to_owned()),
		KeyCode::Backspace | KeyCode::Delete => Some("backspace".to_owned()),
		KeyCode::Char(character) => Some(character.to_string()),
		_ => None,
	}
}

pub struct App {
	pub controller: Controller,
	art: ArtLibrary,
	animate: bool,
	tick: usize,
	cues: Vec<String>,
}

impl App {
	pub fn new(controller: Controller, art: ArtLibrary, animate: bool) -> App {
		App { controller, art, animate, tick: 0, cues: Vec::new() }
	}

	/// Advances the idle animation and consumes one queued cue (every [`ANIMATION_INTERVAL`]).
	pub fn on_tick(&mut self) {
		self.tick += 1;
		if !self.cues.is_empty() {
			self.cues.remove(0);
		}
	}

	/// Handles a key press; returns true when the app should exit.
	pub fn on_key(&mut self, key: &KeyEvent) -> bool {
		if key.kind != KeyEventKind::Press {
			return false;
		}
		if key.code == KeyCode::Char('c') && key.modifiers.contains(KeyModifiers::CONTROL) {
			return true;
		}
		let Some(name) = key_name(key) else {
			return false;
		};
		self.press(&name)
	}

	/// Forwards a key name to the controller; returns true when the app should exit.
	pub fn press(&mut self, name: &str) -> bool {
		self.controller.press(name);
		if self.controller.exit_requested {
			return true;
		}
		self.cues = if self.animate { std::mem::take(&mut self.controller.animation_cues) } else { Vec::new() };
		self.controller.animation_cues.clear();
		false
	}

	// ── rendering ─────────────────────────────────────────────────────────────

	pub fn render(&mut self, frame: &mut Frame<'_>) {
		let area = frame.area();
		let show_log = !self.controller.view.is_paged();
		let mut constraints = vec![Constraint::Length(TOP_HEIGHT), Constraint::Length(PLAYER_HEIGHT)];
		if show_log {
			constraints.push(Constraint::Length(LOG_HEIGHT));
		}
		constraints.push(Constraint::Min(MENU_MIN_HEIGHT));
		let rows = Layout::vertical(constraints).split(area);

		let top =
			panel().title(Line::from(format!("─ {} ", self.controller.header())).style(Style::default().fg(ACCENT)));
		let top_inner = top.inner(rows[0]);
		frame.render_widget(top, rows[0]);
		let [art_area, monster_area] =
			Layout::horizontal([Constraint::Length(ART_WIDTH - 2), Constraint::Min(0)]).areas(top_inner);
		frame.render_widget(Paragraph::new(self.art_text()), art_area);
		frame.render_widget(Paragraph::new(self.monster_text()), monster_area);

		frame.render_widget(Paragraph::new(self.player_text()).block(panel()), rows[1]);
		if show_log {
			let log: Vec<Line> = self
				.controller
				.log
				.iter()
				.skip(self.controller.log.len().saturating_sub(LOG_LINES))
				.map(|line| Line::from(line.clone()))
				.collect();
			frame.render_widget(Paragraph::new(log).block(panel()), rows[2]);
		}
		let menu = self.menu_text(area);
		frame.render_widget(Paragraph::new(menu).block(panel()), rows[rows.len() - 1]);
	}

	fn art_text(&self) -> Text<'static> {
		let Some(monster) = self.controller.monster_view() else {
			let animations = self.art.load_file(TITLE_ART.0, TITLE_ART.1);
			let style = Style::default().fg(Color::Green).add_modifier(Modifier::BOLD);
			return Text::from(
				frame_for(&animations, "idle", self.tick).into_iter().map(Line::from).collect::<Vec<_>>(),
			)
			.style(style);
		};
		let creature = self.controller.services().data.creature(&monster.creature_id);
		let animation = self.cues.first().map_or("idle", String::as_str);
		let lines: Vec<Line> =
			frame_for(&self.art.for_creature(creature), animation, self.tick).into_iter().map(Line::from).collect();
		let style = if animation == "hurt" {
			Style::default().fg(Color::Red).add_modifier(Modifier::BOLD)
		} else {
			Style::default().fg(color(element_color(monster.element)))
		};
		Text::from(lines).style(style)
	}

	fn monster_text(&self) -> Text<'static> {
		let controller = &self.controller;
		let bold = Style::default().add_modifier(Modifier::BOLD);
		let Some(monster) = controller.monster_view() else {
			return Text::from(vec![
				Line::styled(controller.t("app.title", &[]), bold),
				Line::styled(controller.t("app.subtitle", &[]), Style::default().add_modifier(Modifier::ITALIC)),
				Line::default(),
				Line::from(format!("v{VERSION} · Rust")),
			]);
		};
		let mut name = Vec::new();
		if monster.is_boss {
			name.push(Span::styled(
				format!("{} ", controller.t("hud.boss", &[])),
				Style::default().fg(Color::Magenta).add_modifier(Modifier::BOLD),
			));
		}
		name.push(Span::styled(monster.name.to_uppercase(), bold));
		Text::from(vec![
			Line::from(name),
			Line::from(vec![
				Span::styled("HP ", bold),
				Span::styled(
					bar(monster.hp, monster.max_hp, BAR_WIDTH),
					Style::default().fg(color(hp_color(monster.hp, monster.max_hp))),
				),
				Span::raw(format!("  {}/{}", monster.hp, monster.max_hp)),
			]),
			Line::styled(monster.details, Style::default().fg(color(element_color(monster.element)))),
		])
	}

	fn player_text(&self) -> Text<'static> {
		let Some(player) = self.controller.player_view() else {
			return Text::default();
		};
		let bold = Style::default().add_modifier(Modifier::BOLD);
		let mut summary = vec![
			Span::raw(player.summary),
			Span::styled(format!("   {}", player.gold), Style::default().fg(Color::Yellow)),
		];
		if !player.statuses.is_empty() {
			summary.push(Span::styled(format!("   {}", player.statuses), Style::default().fg(Color::Red)));
		}
		Text::from(vec![
			Line::from(summary),
			Line::from(vec![
				Span::styled("HP ", bold),
				Span::styled(
					bar(player.hp, player.max_hp, BAR_WIDTH),
					Style::default().fg(color(hp_color(player.hp, player.max_hp))),
				),
				Span::styled(format!("  {}/{}", player.hp, player.max_hp), bold),
			]),
			Line::from(vec![
				Span::styled("MP ", bold),
				Span::styled(bar(player.mp, player.max_mp, BAR_WIDTH), Style::default().fg(Color::Blue)),
				Span::raw(format!("  {}/{}   {}", player.mp, player.max_mp, player.xp)),
			]),
		])
	}

	fn menu_text(&mut self, area: Rect) -> Text<'static> {
		let controller = &mut self.controller;
		if area.width < MIN_COLUMNS || area.height < MIN_ROWS {
			let message = controller.t("app.resize", &[("columns", &MIN_COLUMNS), ("rows", &MIN_ROWS)]);
			return Text::from(Line::styled(message, Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD)));
		}
		let mut lines = vec![Line::styled(
			controller.title(),
			Style::default().add_modifier(Modifier::BOLD | Modifier::UNDERLINED),
		)];
		lines.extend(controller.body_lines().into_iter().map(Line::from));
		let options = controller.options();
		if !options.is_empty() {
			lines.push(Line::default());
		}
		let columns = if options.len() > TWO_COLUMN_THRESHOLD { 2 } else { 1 };
		let key_style = Style::default().fg(Color::Cyan).add_modifier(Modifier::BOLD);
		for row in options.chunks(columns) {
			let mut spans = Vec::new();
			for option in row {
				spans.push(Span::styled(format!("[{}] ", option.key.to_uppercase()), key_style));
				let label = if columns == 1 {
					option.label.clone()
				} else {
					let truncated: String = option.label.chars().take(COLUMN_WIDTH - 1).collect();
					format!("{truncated:<COLUMN_WIDTH$}")
				};
				spans.push(Span::styled(label, option_style(option.color.as_deref())));
			}
			lines.push(Line::from(spans));
		}
		if let Some(prompt) = controller.input_prompt() {
			lines.push(Line::default());
			lines.push(Line::styled(prompt, Style::default().add_modifier(Modifier::BOLD)));
		}
		if !controller.message.is_empty() {
			lines.push(Line::default());
			lines.push(Line::styled(
				controller.message.clone(),
				Style::default().fg(Color::Red).add_modifier(Modifier::BOLD),
			));
		}
		if let Some(error) = &controller.error {
			lines.push(Line::default());
			lines.push(Line::styled(error.clone(), Style::default().fg(Color::Red)));
		}
		Text::from(lines)
	}
}

fn panel() -> Block<'static> {
	Block::bordered()
		.border_type(BorderType::Rounded)
		.border_style(Style::default().fg(ACCENT))
		.padding(Padding::horizontal(1))
}

/// Plain text of a rendered buffer, one line per row (used by the e2e tests).
pub fn screen_text(buffer: &Buffer) -> String {
	let area = buffer.area;
	(area.top()..area.bottom())
		.map(|y| (area.left()..area.right()).map(|x| buffer[(x, y)].symbol()).collect::<String>().trim_end().to_owned())
		.collect::<Vec<_>>()
		.join("\n")
}

/// Wires the real adapters for a data directory.
pub fn build_services(data: Rc<GameData>, shared: Rc<SharedFs>, options: &CliOptions) -> Services {
	let data_dir = resolve_data_dir(options.data_dir.as_deref());
	Services {
		data,
		shared,
		settings: SettingsRepository::new(&data_dir),
		repositories: Repositories {
			saves: Rc::new(FileSaveRepository::new(&data_dir)),
			history: Rc::new(FileHistoryRepository::new(&data_dir)),
			profile: Rc::new(FileProfileRepository::new(&data_dir)),
		},
		clock: Rc::new(SystemClock),
		version: VERSION.to_owned(),
	}
}

/// Runs the full-screen game until the player quits, then saves the active run (mid-battle quits resume from the
/// last merchant visit).
pub fn run_tui(data: Rc<GameData>, shared: Rc<SharedFs>, options: &CliOptions) -> Result<(), String> {
	let services = build_services(data, Rc::clone(&shared), options);
	let controller = Controller::new(services, options.seed, options.lang.as_deref(), None)?;
	let animate = !options.no_anim && env::var_os("RPG_NO_ANIM").is_none_or(|value| value.is_empty());
	let mut app = App::new(controller, ArtLibrary::new(shared), animate);
	let mut terminal = ratatui::try_init().map_err(|error| format!("cannot start the terminal UI: {error}"))?;
	let result = event_loop(&mut terminal, &mut app);
	ratatui::restore();
	result.map_err(|error| error.to_string())?;
	if let Some(session) = &mut app.controller.session {
		session.save_and_quit().map_err(|error| error.to_string())?;
	}
	Ok(())
}

fn event_loop(terminal: &mut ratatui::DefaultTerminal, app: &mut App) -> io::Result<()> {
	let mut last_tick = Instant::now();
	loop {
		terminal.draw(|frame| app.render(frame))?;
		let timeout =
			if app.animate { ANIMATION_INTERVAL.saturating_sub(last_tick.elapsed()) } else { Duration::from_secs(60) };
		if event::poll(timeout)?
			&& let event::Event::Key(key) = event::read()?
			&& app.on_key(&key)
		{
			return Ok(());
		}
		if app.animate && last_tick.elapsed() >= ANIMATION_INTERVAL {
			app.on_tick();
			last_tick = Instant::now();
		}
	}
}
