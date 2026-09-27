/**
 * Framework-independent UI state machine: which screen is shown, its options and what each key does.
 * Port of rpg-python/src/rpg/presentation/controller.py — the Ink app only renders this controller.
 */
import {
	Attack,
	BuyPotion,
	BuyStockItem,
	Cast,
	type Command,
	Defend,
	Equip,
	NextFight,
	SellItem,
	Unequip,
	UsePotion,
} from "../application/commands";
import { GameSession, type Repositories, type StepResult } from "../application/game-session";
import { canUse } from "../application/loot";
import { availablePotions, stockPrice } from "../application/merchant";
import type { Clock } from "../application/ports";
import { ProfileService } from "../application/profile";
import { RunConfig } from "../application/run-state";
import { buildSheet, itemValue } from "../domain/character";
import type { GameData } from "../domain/definitions";
import type { ItemInstance } from "../domain/entities";
import { ELEMENTS, type Element, SLOTS } from "../domain/enums";
import { manaForMagicLevel, pct, roundInfo, spellLevelForUses, xpForLevel } from "../domain/formulas";
import { DEFAULT_LOCALE, SUPPORTED_LOCALES, Translator } from "../infrastructure/i18n";
import type { SettingsRepository } from "../infrastructure/repositories";
import { EventFormatter } from "./event-text";
import { listKey } from "./render";

const MAX_LOG_LINES = 50;
const MAX_NAME_LENGTH = 16;
const MAX_QUANTITY_DIGITS = 2;
export const PAGE_SIZE = 10;

export type View =
	| "language"
	| "title"
	| "difficulty"
	| "name"
	| "vocation"
	| "merchant"
	| "buy_potions"
	| "quantity"
	| "sell"
	| "equipment"
	| "stock"
	| "character"
	| "battle"
	| "spells"
	| "potions"
	| "game_over"
	| "hall_of_fame"
	| "bestiary"
	| "achievements";

const BATTLE_VIEWS: ReadonlySet<View> = new Set<View>(["battle", "spells", "potions"]);
const TEXT_INPUT_VIEWS: ReadonlySet<View> = new Set<View>(["name", "quantity"]);
export const PAGED_VIEWS: ReadonlySet<View> = new Set<View>(["hall_of_fame", "bestiary", "achievements", "character"]);

export interface MenuOption {
	readonly key: string;
	readonly label: string;
	readonly color: string | null;
}

export interface Services {
	readonly data: GameData;
	readonly settings: SettingsRepository;
	readonly repositories: Repositories;
	readonly clock: Clock;
	readonly version: string;
}

export interface MonsterView {
	readonly name: string;
	readonly creatureId: string;
	readonly hp: number;
	readonly maxHp: number;
	readonly isBoss: boolean;
	readonly element: Element;
	readonly details: string;
}

export interface PlayerView {
	readonly summary: string;
	readonly gold: string;
	readonly hp: number;
	readonly maxHp: number;
	readonly mp: number;
	readonly maxMp: number;
	readonly xp: string;
	readonly statuses: string;
}

type Action = () => void;
type MenuEntry = readonly [MenuOption, Action];

const option = (key: string, label: string, color: string | null = null): MenuOption => ({ key, label, color });

export function randomSeed(): number {
	return crypto.getRandomValues(new Uint32Array(1))[0] ?? 0;
}

export interface ControllerOptions {
	readonly seed?: number | null;
	readonly localeOverride?: string | null;
	readonly seedSource?: () => number;
}

export class Controller {
	view: View;
	session: GameSession | null = null;
	readonly log: string[] = [];
	message = "";
	inputBuffer = "";
	exitRequested = false;
	animationCues: string[] = [];
	page = 0;
	locale: string;
	#translator: Translator;
	#formatter: EventFormatter;
	#languageReturn: View = "title";
	#difficulty = "normal";
	#name = "";
	#potionId = "";
	readonly #data: GameData;
	readonly #seed: number | null;
	readonly #seedSource: () => number;

	constructor(
		readonly services: Services,
		options: ControllerOptions = {},
	) {
		this.#data = services.data;
		this.#seed = options.seed ?? null;
		this.#seedSource = options.seedSource ?? randomSeed;
		const settings = services.settings.load();
		const override = options.localeOverride ?? null;
		this.locale = override ?? settings.locale ?? DEFAULT_LOCALE;
		this.#translator = new Translator(this.locale);
		this.#formatter = new EventFormatter(this.#data, this.#translator);
		this.view = override !== null || settings.locale !== null ? "title" : "language";
	}

	// ── i18n ──────────────────────────────────────────────────────────────────

	private setLocale(locale: string): void {
		this.locale = locale;
		this.#translator = new Translator(locale);
		this.#formatter = new EventFormatter(this.#data, this.#translator);
	}

	t(key: string, params: Readonly<Record<string, unknown>> = {}): string {
		return this.#translator.t(key, params);
	}

	// ── queries used by renderers ─────────────────────────────────────────────

	title(): string {
		switch (this.view) {
			case "language":
				return this.t("language.title");
			case "title":
				return this.t("app.title");
			case "difficulty":
				return this.t("new_run.difficulty");
			case "name":
				return this.t("new_run.name");
			case "vocation":
				return this.t("new_run.vocation");
			case "merchant": {
				const round = this.session?.state.round ?? 0;
				return round === 0 ? this.t("merchant.title_start") : this.t("merchant.title", { round });
			}
			case "buy_potions":
				return this.t("merchant.buy_potions");
			case "quantity":
				return this.t("merchant.quantity", { name: this.#data.potion(this.#potionId).name });
			case "sell":
				return this.t("merchant.sell_items");
			case "equipment":
				return this.t("merchant.equipment");
			case "stock":
				return this.t("merchant.stock");
			case "character":
				return this.t("merchant.character");
			case "battle":
				return this.t("battle.title");
			case "spells":
				return this.t("battle.spells");
			case "potions":
				return this.t("battle.potions");
			case "game_over":
				return this.t("gameover.title");
			case "hall_of_fame":
				return this.t("menu.hall_of_fame");
			case "bestiary":
				return this.t("menu.bestiary");
			case "achievements":
				return this.t("menu.achievements");
		}
	}

	options(): MenuOption[] {
		return this.menu().map(([entry]) => entry);
	}

	/** Informative lines shown above the options (paged for long lists). */
	bodyLines(): string[] {
		const lines = this.body();
		if (!PAGED_VIEWS.has(this.view) || lines.length <= PAGE_SIZE) return lines;
		const pages = Math.ceil(lines.length / PAGE_SIZE);
		this.page = Math.min(this.page, pages - 1);
		const start = this.page * PAGE_SIZE;
		return [...lines.slice(start, start + PAGE_SIZE), "", this.t("menu.page", { page: this.page + 1, pages })];
	}

	inputPrompt(): string | null {
		return TEXT_INPUT_VIEWS.has(this.view) ? `> ${this.inputBuffer}_` : null;
	}

	header(): string {
		if (this.session === null) return this.t("app.subtitle");
		const state = this.session.state;
		const tier = roundInfo(Math.max(1, state.round), this.#data.balance, this.#data.tierCount).tier + 1;
		const difficulty = this.t(`difficulty.${state.config.difficultyId}`);
		const roundText = this.t("hud.round", { round: state.round, tier, difficulty });
		return `${roundText} · ${this.t("hud.seed", { seed: state.seed })}`;
	}

	monsterView(): MonsterView | null {
		const monster = this.session?.state.monster ?? null;
		if (monster === null) return null;
		const creature = this.#data.creature(monster.creatureId);
		let main = monster.attacks[0];
		for (const attack of monster.attacks) {
			if (
				main === undefined ||
				attack.weight > main.weight ||
				(attack.weight === main.weight && attack.id > main.id)
			) {
				main = attack;
			}
		}
		const weak = ELEMENTS.filter((e) => creature.resistance(e) > 100).map((e) => this.t(`element.${e}`));
		const parts = [...new Set(monster.attacks.map((a) => this.t(`element.${a.element}`)))];
		let details = parts.join(" · ");
		if (weak.length > 0) details += ` · ${this.t("hud.weak", { elements: weak.join(", ") })}`;
		const statuses = monster.statuses.map((s) => `${this.t(`status.${s.statusId}`)}(${s.turns})`);
		if (statuses.length > 0) details += ` · ${statuses.join(" ")}`;
		return {
			name: creature.name,
			creatureId: creature.id,
			hp: monster.hp,
			maxHp: monster.maxHp,
			isBoss: monster.isBoss,
			element: main?.element ?? "physical",
			details,
		};
	}

	playerView(): PlayerView | null {
		if (this.session === null) return null;
		const player = this.session.state.player;
		const sheet = buildSheet(player, this.#data);
		return {
			summary: this.t("hud.player", {
				name: player.name,
				vocation: this.t(`vocation.${player.vocationId}`),
				level: player.level,
				magicLevel: player.magicLevel,
			}),
			gold: this.t("hud.gold", { gold: player.gold }),
			hp: player.hp,
			maxHp: sheet.maxHp,
			mp: player.mp,
			maxMp: sheet.maxMp,
			xp: this.t("hud.xp", { xp: player.xp, next: xpForLevel(player.level + 1) }),
			statuses: player.statuses.map((s) => `${this.t(`status.${s.statusId}`)}(${s.turns})`).join(" "),
		};
	}

	// ── input ─────────────────────────────────────────────────────────────────

	press(rawKey: string): void {
		this.message = "";
		if (TEXT_INPUT_VIEWS.has(this.view)) {
			this.textInput(rawKey);
			return;
		}
		if (PAGED_VIEWS.has(this.view) && (rawKey === "n" || rawKey === "p")) {
			this.page = Math.max(0, this.page + (rawKey === "n" ? 1 : -1));
			return;
		}
		const key = rawKey === "escape" ? "0" : rawKey;
		for (const [entry, action] of this.menu()) {
			if (entry.key === key) {
				action();
				return;
			}
		}
	}

	private textInput(key: string): void {
		if (key === "escape") {
			this.inputBuffer = "";
			this.view = this.view === "name" ? "difficulty" : "buy_potions";
		} else if (key === "backspace") {
			this.inputBuffer = this.inputBuffer.slice(0, -1);
		} else if (key === "enter") {
			this.submitText();
		} else if (this.view === "name" && [...key].length === 1 && key >= " ") {
			if ([...this.inputBuffer].length < MAX_NAME_LENGTH) this.inputBuffer += key;
		} else if (this.view === "quantity" && /^\d$/.test(key) && this.inputBuffer.length < MAX_QUANTITY_DIGITS) {
			this.inputBuffer += key;
		}
	}

	private submitText(): void {
		const text = this.inputBuffer.trim();
		this.inputBuffer = "";
		if (this.view === "name") {
			const length = [...text].length;
			if (length < 1 || length > MAX_NAME_LENGTH) {
				this.message = this.t("new_run.name_invalid");
				return;
			}
			this.#name = text;
			this.view = "vocation";
			return;
		}
		this.view = "buy_potions";
		if (text !== "" && Number(text) > 0) this.step(BuyPotion(this.#potionId, Number(text)));
	}

	// ── menus ─────────────────────────────────────────────────────────────────

	private menu(): MenuEntry[] {
		switch (this.view) {
			case "language":
				return SUPPORTED_LOCALES.map((locale, i) => [
					option(String(i + 1), this.t(`language.${locale}`)),
					() => this.chooseLanguage(locale),
				]);
			case "title":
				return this.titleMenu();
			case "difficulty":
				return [
					...this.#data.balance.difficulties.map(
						(d, i): MenuEntry => [
							option(
								String(i + 1),
								`${this.t(`difficulty.${d.id}`)} — ${this.t(`difficulty.${d.id}.description`)}`,
							),
							() => this.chooseDifficulty(d.id),
						],
					),
					this.back("title"),
				];
			case "vocation":
				return [
					...this.#data.vocations.map(
						(v, i): MenuEntry => [
							option(
								String(i + 1),
								`${this.t(`vocation.${v.id}`)} — ${this.t(`vocation.${v.id}.description`)}`,
							),
							() => this.chooseVocation(v.id),
						],
					),
					this.back("difficulty"),
				];
			case "merchant":
				return this.merchantMenu();
			case "buy_potions":
				return [...this.potionShop(), this.back("merchant")];
			case "sell":
				return [...this.sellMenu(), this.back("merchant")];
			case "equipment":
				return [...this.equipmentMenu(), this.back("merchant")];
			case "stock":
				return [...this.stockMenu(), this.back("merchant")];
			case "character":
				return [this.back("merchant")];
			case "battle":
				return [
					[option("1", this.t("battle.attack")), () => this.step(Attack())],
					[option("2", this.t("battle.spells")), this.go("spells")],
					[option("3", this.t("battle.potions")), this.go("potions")],
					[option("4", this.t("battle.defend")), () => this.step(Defend())],
					[option("q", this.t("battle.save_quit")), () => this.saveAndQuit()],
				];
			case "spells":
				return [...this.spellMenu(), this.back("battle")];
			case "potions":
				return [...this.battlePotions(), this.back("battle")];
			case "game_over":
				return [
					[option("1", this.t("gameover.new_run")), () => this.newRun()],
					[option("2", this.t("gameover.title_screen")), this.go("title")],
				];
			case "hall_of_fame":
			case "bestiary":
			case "achievements":
				return [this.back("title")];
			case "name":
			case "quantity":
				return [];
		}
	}

	private back(target: View): MenuEntry {
		return [option("0", this.t("menu.back")), this.go(target)];
	}

	private go(target: View): Action {
		return () => {
			this.view = target;
			this.page = 0;
		};
	}

	private titleMenu(): MenuEntry[] {
		const menu: MenuEntry[] = [];
		if (this.services.repositories.saves.load() !== null) {
			menu.push([option("1", this.t("menu.continue")), () => this.continueRun()]);
		}
		menu.push(
			[option("2", this.t("menu.new_run")), () => this.newRun()],
			[option("3", this.t("menu.hall_of_fame")), this.go("hall_of_fame")],
			[option("4", this.t("menu.bestiary")), this.go("bestiary")],
			[option("5", this.t("menu.achievements")), this.go("achievements")],
			[option("6", this.t("menu.language")), () => this.openLanguage()],
			[option("0", this.t("menu.quit")), () => this.quit()],
		);
		return menu;
	}

	private merchantMenu(): MenuEntry[] {
		return [
			[option("1", this.t("merchant.buy_potions")), this.go("buy_potions")],
			[option("2", this.t("merchant.sell_items")), this.go("sell")],
			[option("3", this.t("merchant.equipment")), this.go("equipment")],
			[option("4", this.t("merchant.stock")), this.go("stock")],
			[option("5", this.t("merchant.character")), this.go("character")],
			[option("0", this.t("merchant.next_fight")), () => this.step(NextFight())],
			[option("q", this.t("battle.save_quit")), () => this.saveAndQuit()],
		];
	}

	private itemLabel(template: string, item: ItemInstance, params: Readonly<Record<string, unknown>> = {}): string {
		const definition = this.#data.item(item.itemId);
		return this.t(template, {
			name: definition.name,
			rarity: this.t(`rarity.${item.rarity}`),
			slot: this.t(`slot.${definition.slot}`),
			...params,
		});
	}

	private potionShop(): MenuEntry[] {
		const session = this.requireSession();
		return availablePotions(session.state, this.#data).map((potionId, index): MenuEntry => {
			const potion = this.#data.potion(potionId);
			const label = this.t("merchant.potion_option", {
				name: potion.name,
				price: potion.price,
				count: session.state.player.potionCount(potionId),
			});
			return [option(listKey(index), label), () => this.askQuantity(potionId)];
		});
	}

	private sellMenu(): MenuEntry[] {
		return this.requireSession().state.player.bag.map(
			(item, index): MenuEntry => [
				option(
					listKey(index),
					this.itemLabel("merchant.sell_option", item, { gold: itemValue(item, this.#data) }),
					item.rarity,
				),
				this.command(SellItem(item.uid)),
			],
		);
	}

	private equipmentMenu(): MenuEntry[] {
		const player = this.requireSession().state.player;
		const vocation = this.#data.vocation(player.vocationId);
		const entries: Array<readonly [string, string, Command]> = [];
		for (const item of player.bag) {
			if (canUse(this.#data.item(item.itemId), vocation)) {
				entries.push([this.itemLabel("merchant.equip_option", item), item.rarity, Equip(item.uid)]);
			}
		}
		for (const slot of SLOTS) {
			const equipped = player.equipment.get(slot);
			if (equipped !== undefined) {
				entries.push([this.itemLabel("merchant.unequip_option", equipped), equipped.rarity, Unequip(slot)]);
			}
		}
		return entries.map(
			([label, color, command], i): MenuEntry => [option(listKey(i), label, color), this.command(command)],
		);
	}

	private stockMenu(): MenuEntry[] {
		return this.requireSession().state.merchantStock.map(
			(item, index): MenuEntry => [
				option(
					listKey(index),
					this.itemLabel("merchant.stock_option", item, { gold: stockPrice(item, this.#data) }),
					item.rarity,
				),
				this.command(BuyStockItem(index)),
			],
		);
	}

	private spellMenu(): MenuEntry[] {
		const player = this.requireSession().state.player;
		const levels = this.#data.balance.spellLevels;
		return this.#data.vocation(player.vocationId).spells.map((spellId, index): MenuEntry => {
			const spell = this.#data.spell(spellId);
			const uses = player.spellUses.get(spellId) ?? 0;
			const level = spellLevelForUses(uses, levels);
			const label = this.t("battle.spell_option", {
				name: spell.name,
				words: spell.words,
				mana: pct(spell.mana, level.manaPct),
				level: level.level,
				uses,
			});
			const color = spell.kind === "heal" ? "green" : spell.element;
			return [option(listKey(index), label, color), this.command(Cast(spellId))];
		});
	}

	private battlePotions(): MenuEntry[] {
		const player = this.requireSession().state.player;
		return this.#data.potions
			.filter((p) => player.potionCount(p.id) > 0)
			.map(
				(p, i): MenuEntry => [
					option(
						listKey(i),
						this.t("battle.potion_option", { name: p.name, count: player.potionCount(p.id) }),
					),
					this.command(UsePotion(p.id)),
				],
			);
	}

	// ── actions ───────────────────────────────────────────────────────────────

	private chooseLanguage(locale: string): void {
		this.setLocale(locale);
		this.services.settings.save({ locale });
		this.view = this.#languageReturn;
	}

	private openLanguage(): void {
		this.#languageReturn = "title";
		this.view = "language";
	}

	private quit(): void {
		this.exitRequested = true;
	}

	private newRun(): void {
		this.session = null;
		this.view = "difficulty";
	}

	private chooseDifficulty(difficultyId: string): void {
		this.#difficulty = difficultyId;
		this.inputBuffer = "";
		this.view = "name";
	}

	private chooseVocation(vocationId: string): void {
		const seed = this.#seed ?? this.#seedSource();
		const [session, events] = GameSession.start(
			this.#data,
			new RunConfig(this.#name, vocationId, this.#difficulty),
			seed,
			{
				repositories: this.services.repositories,
				clock: this.services.clock,
				gameVersion: this.services.version,
			},
		);
		this.session = session;
		this.log.length = 0;
		this.record({ events, achievements: [] });
		this.view = "merchant";
	}

	private continueRun(): void {
		const session = GameSession.resume(this.#data, {
			repositories: this.services.repositories,
			clock: this.services.clock,
			gameVersion: this.services.version,
		});
		if (session === null) return;
		this.session = session;
		this.log.length = 0;
		this.pushLog(this.t("menu.welcome_back", { name: session.state.player.name, round: session.state.round }));
		this.view = "merchant";
	}

	private askQuantity(potionId: string): void {
		this.#potionId = potionId;
		this.inputBuffer = "";
		this.view = "quantity";
	}

	private command(command: Command): Action {
		return () => this.step(command);
	}

	private saveAndQuit(): void {
		this.session?.saveAndQuit();
		this.session = null;
		this.view = "title";
	}

	private requireSession(): GameSession {
		if (this.session === null) throw new Error(`view ${this.view} needs an active run`);
		return this.session;
	}

	private step(command: Command): void {
		const session = this.requireSession();
		this.record(session.step(command));
		const phase = session.state.phase;
		if (phase === "battle") this.view = "battle";
		else if (phase === "game_over") this.view = "game_over";
		else if (BATTLE_VIEWS.has(this.view)) this.view = "merchant";
	}

	private pushLog(line: string): void {
		this.log.push(line);
		if (this.log.length > MAX_LOG_LINES) this.log.splice(0, this.log.length - MAX_LOG_LINES);
	}

	private record(result: StepResult): void {
		const session = this.requireSession();
		const cues: string[] = [];
		for (const evt of result.events) {
			const text = this.#formatter.format(evt, session.state);
			if (evt.type === "error") {
				this.message = text;
				continue;
			}
			this.pushLog(text);
			if ((evt.type === "player_attacked" || evt.type === "spell_cast") && (evt.damage ?? 0) !== 0)
				cues.push("hurt");
			else if (evt.type === "monster_attacked") cues.push("attack");
		}
		for (const achievement of result.achievements) {
			this.pushLog(this.t("achievement.unlocked", { name: this.t(`achievement.${achievement.id}.name`) }));
		}
		this.animationCues = cues;
	}

	// ── informative bodies ────────────────────────────────────────────────────

	private profile(): ProfileService {
		return this.session?.profile ?? new ProfileService(this.#data, this.services.repositories.profile.load());
	}

	private body(): string[] {
		switch (this.view) {
			case "character":
				return this.characterSheet();
			case "game_over":
				return this.gameOverSummary();
			case "hall_of_fame":
				return this.hallOfFame();
			case "bestiary":
				return this.bestiary();
			case "achievements":
				return this.achievements();
			case "merchant": {
				const player = this.requireSession().state.player;
				return [this.t("merchant.welcome", { name: player.name, gold: player.gold })];
			}
			case "sell":
				return this.requireSession().state.player.bag.length === 0 ? [this.t("merchant.empty_bag")] : [];
			case "stock":
				return this.requireSession().state.merchantStock.length === 0 ? [this.t("merchant.empty_stock")] : [];
			case "potions": {
				const potions = this.requireSession().state.player.potions;
				return [...potions.values()].some((count) => count > 0) ? [] : [this.t("battle.no_potions")];
			}
			default:
				return [];
		}
	}

	private characterSheet(): string[] {
		const player = this.requireSession().state.player;
		const sheet = buildSheet(player, this.#data);
		const balance = this.#data.balance;
		const lines = [
			this.t("character.level", { level: player.level, xp: player.xp, next: xpForLevel(player.level + 1) }),
			this.t("character.magic_level", {
				magicLevel: player.magicLevel,
				spent: player.manaSpent,
				next: manaForMagicLevel(player.magicLevel, balance),
			}),
			this.t("character.hp_mp", { hp: player.hp, maxHp: sheet.maxHp, mp: player.mp, maxMp: sheet.maxMp }),
			this.t("character.melee", {
				min: sheet.meleeMin,
				max: sheet.meleeMax,
				element: this.t(`element.${sheet.weaponElement}`),
			}),
		];
		const stats: Array<readonly [string, number]> = [
			["stat.armor", sheet.armor],
			["stat.hpRegen", sheet.hpRegen],
			["stat.mpRegen", sheet.mpRegen],
			["stat.critChance", sheet.critChance],
			["stat.critDamage", sheet.critDamage],
			["stat.spellPower", sheet.spellPower],
			["stat.physicalDamage", sheet.physicalDamage],
			["stat.dodge", sheet.dodge],
			["stat.parry", sheet.parry],
			["stat.lifeLeech", sheet.lifeLeech],
			["stat.manaLeech", sheet.manaLeech],
		];
		for (const [key, value] of stats) {
			if (value) lines.push(this.t("character.stat_line", { stat: this.t(key), value }));
		}
		for (const element of ELEMENTS) {
			const value = sheet.protections[element];
			if (value)
				lines.push(this.t("character.stat_line", { stat: this.t(`element.${element}`), value: `${value}%` }));
		}
		lines.push("", this.t("character.equipment"));
		for (const slot of SLOTS) {
			const item = player.equipment.get(slot);
			if (item === undefined) {
				lines.push(this.t("character.empty_slot", { slot: this.t(`slot.${slot}`) }));
			} else {
				lines.push(
					this.t("character.slot", {
						slot: this.t(`slot.${slot}`),
						item: this.#data.item(item.itemId).name,
						rarity: this.t(`rarity.${item.rarity}`),
					}),
				);
			}
		}
		lines.push(this.t("character.bag", { count: player.bag.length, capacity: balance.bagCapacity }));
		return lines;
	}

	private gameOverSummary(): string[] {
		const state = this.requireSession().state;
		const monster = state.deathCause ? this.#data.creature(state.deathCause).name : "?";
		return [
			this.t("gameover.summary", {
				name: state.player.name,
				vocation: this.t(`vocation.${state.player.vocationId}`),
				round: state.round,
				monster,
			}),
			this.t("gameover.stats", {
				level: state.player.level,
				damage: state.stats.damageDealt,
				kills: state.stats.kills.total(),
				bosses: state.stats.bossesKilled,
			}),
		];
	}

	private hallOfFame(): string[] {
		const hall = this.profile().profile.hallOfFame;
		if (hall.length === 0) return [this.t("hall.empty")];
		return hall.map((entry, index) =>
			this.t("hall.entry", {
				position: index + 1,
				name: entry.name,
				vocation: this.t(`vocation.${entry.vocation}`),
				difficulty: this.t(`difficulty.${entry.difficulty}`),
				round: entry.round,
				level: entry.level,
				date: entry.endedAt.slice(0, 10),
			}),
		);
	}

	private bestiary(): string[] {
		const profile = this.profile();
		const creatures = [...this.#data.monsters, ...this.#data.bosses].sort(
			(a, b) =>
				a.tier - b.tier ||
				Number(a.isBoss) - Number(b.isBoss) ||
				(a.name < b.name ? -1 : a.name > b.name ? 1 : 0),
		);
		return creatures.map((creature) => {
			const entry = profile.profile.bestiary.get(creature.id);
			if (entry === undefined) return this.t("bestiary.unknown", { tier: creature.tier + 1 });
			if (profile.revealed(creature.id)) {
				const weak = ELEMENTS.filter((e) => creature.resistance(e) > 100).map((e) => this.t(`element.${e}`));
				const strong = ELEMENTS.filter((e) => creature.resistance(e) < 100).map((e) => this.t(`element.${e}`));
				return this.t("bestiary.entry_revealed", {
					name: creature.name,
					tier: creature.tier + 1,
					kills: entry.kills,
					weak: weak.join(", ") || "—",
					strong: strong.join(", ") || "—",
				});
			}
			return this.t("bestiary.entry", { name: creature.name, tier: creature.tier + 1, kills: entry.kills });
		});
	}

	private achievements(): string[] {
		const unlocked = this.profile().profile.achievements;
		return this.#data.achievements.map((achievement) => {
			const name = this.t(`achievement.${achievement.id}.name`);
			const description = this.t(`achievement.${achievement.id}.description`, { value: achievement.value });
			const unlock = unlocked.get(achievement.id);
			return unlock === undefined
				? this.t("achievements.locked", { name, description })
				: this.t("achievements.unlocked", { name, description, date: unlock.unlockedAt.slice(0, 10) });
		});
	}
}
