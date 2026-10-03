"""Textual renderer of the UI controller (layout specified in docs/tui.md)."""

import os

from rich.text import Text
from textual import events
from textual.app import App, ComposeResult
from textual.containers import Horizontal, VerticalScroll
from textual.timer import Timer
from textual.widgets import Static

from rpg import __version__
from rpg.application.game_session import Repositories
from rpg.infrastructure.art import ArtLibrary, frame_for
from rpg.infrastructure.data_loader import load_game_data
from rpg.infrastructure.paths import find_shared_dir, resolve_data_dir
from rpg.infrastructure.repositories import (
	FileHistoryRepository,
	FileProfileRepository,
	FileSaveRepository,
	SettingsRepository,
	SystemClock,
)
from rpg.presentation.cli import CliOptions
from rpg.presentation.controller import PAGED_VIEWS, Controller, Services
from rpg.presentation.render import (
	ELEMENT_COLORS,
	MIN_COLUMNS,
	MIN_ROWS,
	RARITY_COLORS,
	STYLE_COLORS,
	bar,
	hp_color,
)

ANIMATION_SECONDS = 0.5
LOG_LINES = 5
TWO_COLUMN_THRESHOLD = 4
COLUMN_WIDTH = 44
TITLE_ART = ("families", "dragon")

CSS = """
Screen { layout: vertical; background: $background; }
#top { height: 8; border: round $accent; }
#art { width: 34; padding: 0 1; }
#monster { width: 1fr; padding: 0 1; }
#player { height: 5; border: round $accent; padding: 0 1; }
#log { height: 7; border: round $accent; padding: 0 1; }
#menu { height: 1fr; min-height: 10; border: round $accent; padding: 0 1; }
"""


def option_style(color: str | None) -> str:
	if color is None:
		return ""
	return RARITY_COLORS.get(color) or STYLE_COLORS.get(color) or ELEMENT_COLORS.get(color) or color


class RpgApp(App[int]):
	CSS = CSS
	TITLE = "CLI Turn-Based RPG"

	def __init__(self, controller: Controller, art: ArtLibrary, *, animate: bool) -> None:
		super().__init__()
		self.controller = controller
		self._art = art
		self._animations_enabled = animate
		self._tick = 0
		self._cues: list[str] = []
		self._auto_timer: Timer | None = None

	def compose(self) -> ComposeResult:
		with Horizontal(id="top"):
			yield Static(id="art")
			yield Static(id="monster")
		yield Static(id="player")
		yield Static(id="log")
		with VerticalScroll(id="menu"):
			yield Static(id="menu-body")

	def on_mount(self) -> None:
		if self._animations_enabled:
			self.set_interval(ANIMATION_SECONDS, self._advance_animation)
		self.refresh_view()

	def _advance_animation(self) -> None:
		self._tick += 1
		if self._cues:
			self._cues.pop(0)
		self.refresh_view()

	def on_key(self, event: events.Key) -> None:
		key = event.key
		if key not in {"escape", "enter", "backspace"}:
			key = event.character or key
		self.controller.press(key)
		event.stop()
		if self.controller.exit_requested:
			self.exit(0)
			return
		if self.controller.auto_battle_active and self._auto_timer is None:
			self._start_auto_battle()
		self._take_cues()
		self.refresh_view()

	def _take_cues(self) -> None:
		self._cues = list(self.controller.animation_cues) if self._animations_enabled else []
		self.controller.animation_cues = []

	def _start_auto_battle(self) -> None:
		"""Paced by the battle speed setting; instant without animation (--no-anim, tests)."""
		if not self._animations_enabled:
			self.controller.run_auto_battle()
			return
		seconds = self.controller.auto_battle_interval_ms() / 1000
		self._auto_timer = self.set_interval(seconds, self._auto_battle_tick)

	def _auto_battle_tick(self) -> None:
		running = self.controller.auto_battle_step()
		if not running and self._auto_timer is not None:
			self._auto_timer.stop()
			self._auto_timer = None
		self._take_cues()
		self.refresh_view()

	def on_resize(self, _: events.Resize) -> None:
		self.refresh_view()

	# ── rendering ─────────────────────────────────────────────────────────────

	def refresh_view(self) -> None:
		controller = self.controller
		self.query_one("#art", Static).update(self._art_text())
		self.query_one("#monster", Static).update(self._monster_text())
		self.query_one("#player", Static).update(self._player_text())
		log = self.query_one("#log", Static)
		log.display = controller.view not in PAGED_VIEWS
		log.update(Text("\n".join(list(controller.log)[-LOG_LINES:])))
		self.query_one("#menu-body", Static).update(self._menu_text())
		self.query_one("#top", Horizontal).border_title = controller.header()

	def _art_text(self) -> Text:
		monster = self.controller.monster_view()
		if monster is None:
			animations = self._art.load_file(*TITLE_ART)
			return Text("\n".join(frame_for(animations, "idle", self._tick)), style="bold green")
		creature = self.controller.services.data.creature(monster.creature_id)
		animation = self._cues[0] if self._cues else "idle"
		frame = frame_for(self._art.for_creature(creature), animation, self._tick)
		style = "bold red" if animation == "hurt" else ELEMENT_COLORS.get(monster.element, "white")
		return Text("\n".join(frame), style=style)

	def _monster_text(self) -> Text:
		controller = self.controller
		monster = controller.monster_view()
		if monster is None:
			text = Text()
			text.append(controller.t("app.title") + "\n", style="bold")
			text.append(controller.t("app.subtitle") + "\n\n", style="italic")
			text.append(f"v{__version__} · Python")
			return text
		text = Text()
		if monster.is_boss:
			text.append(controller.t("hud.boss") + " ", style="bold magenta")
		elif monster.enemy_class == "elite":
			text.append(controller.t("hud.elite") + " ", style="bold yellow")
		text.append(monster.name.upper() + "\n", style="bold")
		text.append("HP ", style="bold")
		text.append(bar(monster.hp, monster.max_hp), style=hp_color(monster.hp, monster.max_hp))
		text.append(f"  {monster.hp}/{monster.max_hp}\n")
		text.append(monster.details, style=ELEMENT_COLORS.get(monster.element, "white"))
		return text

	def _player_text(self) -> Text:
		player = self.controller.player_view()
		if player is None:
			return Text("")
		text = Text(player.summary)
		text.append("   " + player.gold, style="yellow")
		if player.statuses:
			text.append("   " + player.statuses, style="red")
		text.append("\nHP ", style="bold")
		text.append(bar(player.hp, player.max_hp), style=hp_color(player.hp, player.max_hp))
		text.append(f"  {player.hp}/{player.max_hp}\nMP ", style="bold")
		text.append(bar(player.mp, player.max_mp), style="blue")
		text.append(f"  {player.mp}/{player.max_mp}   {player.xp}")
		return text

	def _menu_text(self) -> Text:
		controller = self.controller
		if self.size.width and (self.size.width < MIN_COLUMNS or self.size.height < MIN_ROWS):
			return Text(controller.t("app.resize", columns=MIN_COLUMNS, rows=MIN_ROWS), style="bold yellow")
		text = Text()
		text.append(controller.title(), style="bold underline")
		text.append("\n")
		for line, color in zip(controller.body_lines(), controller.body_colors(), strict=True):
			text.append(line + "\n", style=option_style(color))
		options = controller.options()
		if options:
			text.append("\n")
		columns = 2 if len(options) > TWO_COLUMN_THRESHOLD else 1
		for index, option in enumerate(options):
			text.append(f"[{option.key.upper()}] ", style="bold cyan")
			last_in_row = columns == 1 or index % columns == columns - 1 or index == len(options) - 1
			detail = f"  {option.detail}" if option.detail else ""
			label = option.label if columns == 1 else option.label[: COLUMN_WIDTH - 1 - len(detail)]
			text.append(label, style=option_style(option.color))
			text.append(detail, style=option_style(option.detail_color))
			if columns > 1:
				text.append(" " * (COLUMN_WIDTH - len(label) - len(detail)))
			text.append("\n" if last_in_row else "")
		prompt = controller.input_prompt()
		if prompt is not None:
			text.append("\n" + prompt, style="bold")
		if controller.message:
			text.append("\n" + controller.message, style="bold red")
		return text


def build_services(options: CliOptions) -> Services:
	shared_dir = find_shared_dir()
	data_dir = resolve_data_dir(options.data_dir)
	return Services(
		data=load_game_data(shared_dir),
		shared_dir=shared_dir,
		settings=SettingsRepository(data_dir),
		repositories=Repositories(
			FileSaveRepository(data_dir), FileHistoryRepository(data_dir), FileProfileRepository(data_dir)
		),
		clock=SystemClock(),
		version=__version__,
	)


def run_tui(options: CliOptions) -> int:
	services = build_services(options)
	controller = Controller(services, seed=options.seed, locale_override=options.lang)
	animate = not (options.no_anim or os.environ.get("RPG_NO_ANIM"))
	app = RpgApp(controller, ArtLibrary(services.shared_dir), animate=animate)
	app.run()
	if controller.session is not None:
		controller.session.save_and_quit()
	return 0
