/** Ink renderer of the UI controller (layout specified in docs/tui.md). */
import { Box, Text, useApp, useInput, useStdout } from "ink";
import { type ReactElement, useEffect, useState } from "react";
import type { Element } from "../../domain/enums";
import { type ArtLibrary, frameFor } from "../../infrastructure/art";
import { VERSION } from "../../version";
import { type Controller, type MenuOption, PAGED_VIEWS } from "../controller";
import { bar, ELEMENT_COLORS, hpColor, MIN_COLUMNS, MIN_ROWS, RARITY_COLORS, STYLE_COLORS } from "../render";

const ANIMATION_MS = 500;
const LOG_LINES = 5;
const TWO_COLUMN_THRESHOLD = 4;
const COLUMN_WIDTH = 44;
const ACCENT = "#ffa62b";
const TITLE_ART: readonly [string, string] = ["families", "dragon"];

export interface AppProps {
	readonly controller: Controller;
	readonly art: ArtLibrary;
	readonly animate: boolean;
	readonly columns?: number;
	readonly rows?: number;
}

function optionColor(color: string | null): string | undefined {
	if (color === null) return undefined;
	return RARITY_COLORS[color] ?? STYLE_COLORS[color] ?? ELEMENT_COLORS[color as Element] ?? color;
}

/** Maps Ink key events to the controller's key names (same names as the Python/Textual version). */
export function keyName(
	input: string,
	key: { escape: boolean; return: boolean; backspace: boolean; delete: boolean },
): string {
	if (key.escape) return "escape";
	if (key.return) return "enter";
	if (key.backspace || key.delete) return "backspace";
	return input;
}

function TopBorder({ title, width }: { readonly title: string; readonly width: number }): ReactElement {
	const fill = Math.max(0, width - title.length - 5);
	return (
		<Text color={ACCENT}>
			{"╭─ "}
			{title}
			{` ${"─".repeat(fill)}╮`}
		</Text>
	);
}

function Panel({ height, children }: { readonly height?: number; readonly children: React.ReactNode }): ReactElement {
	return (
		<Box
			borderStyle="round"
			borderColor={ACCENT}
			paddingX={1}
			flexDirection="column"
			{...(height === undefined ? { flexGrow: 1 } : { height })}
		>
			{children}
		</Box>
	);
}

function MenuOptions({ options }: { readonly options: readonly MenuOption[] }): ReactElement {
	const columns = options.length > TWO_COLUMN_THRESHOLD ? 2 : 1;
	const rows: MenuOption[][] = [];
	for (let i = 0; i < options.length; i += columns) rows.push(options.slice(i, i + columns));
	return (
		<Box flexDirection="column">
			{rows.map((row) => (
				<Box key={row.map((o) => o.key).join()}>
					{row.map((entry) => {
						const detail = entry.detail ? `  ${entry.detail}` : "";
						const label =
							columns === 1 ? entry.label : entry.label.slice(0, COLUMN_WIDTH - 1 - detail.length);
						const padding = columns === 1 ? "" : " ".repeat(COLUMN_WIDTH - label.length - detail.length);
						const color = optionColor(entry.color);
						const detailColor = optionColor(entry.detailColor);
						return (
							<Text key={entry.key}>
								<Text bold color="cyan">{`[${entry.key.toUpperCase()}] `}</Text>
								<Text {...(color === undefined ? {} : { color })}>{label}</Text>
								{detail ? (
									<Text {...(detailColor === undefined ? {} : { color: detailColor })}>{detail}</Text>
								) : null}
								{padding}
							</Text>
						);
					})}
				</Box>
			))}
		</Box>
	);
}

export function App({ controller, art, animate, columns, rows }: AppProps): ReactElement {
	const { exit } = useApp();
	const { stdout } = useStdout();
	const [, setVersion] = useState(0);
	const [tick, setTick] = useState(0);
	const [cues, setCues] = useState<string[]>([]);
	const [autoBattle, setAutoBattle] = useState(false);
	const width = columns ?? stdout.columns ?? MIN_COLUMNS;
	const height = rows ?? stdout.rows ?? MIN_ROWS;

	const takeCues = (): void => {
		setCues(animate ? [...controller.animationCues] : []);
		controller.animationCues = [];
	};

	useEffect(() => {
		if (!animate) return;
		const timer = setInterval(() => {
			setTick((value) => value + 1);
			setCues((current) => current.slice(1));
		}, ANIMATION_MS);
		return () => clearInterval(timer);
	}, [animate]);

	// Auto-battle is paced by the battle speed setting (docs/tui.md); the controller ignores keys meanwhile.
	useEffect(() => {
		if (!autoBattle) return;
		const timer = setInterval(() => {
			const running = controller.autoBattleStep();
			setCues(animate ? [...controller.animationCues] : []);
			controller.animationCues = [];
			setVersion((value) => value + 1);
			if (!running) setAutoBattle(false);
		}, controller.autoBattleIntervalMs());
		return () => clearInterval(timer);
	}, [autoBattle, animate, controller]);

	useInput((input, key) => {
		controller.press(keyName(input, key));
		if (controller.exitRequested) {
			exit();
			return;
		}
		if (controller.autoBattleActive && !autoBattle) {
			// Without animation (--no-anim, tests) the whole fight is played at once.
			if (animate) setAutoBattle(true);
			else controller.runAutoBattle();
		}
		takeCues();
		setVersion((value) => value + 1);
	});

	if (width < MIN_COLUMNS || height < MIN_ROWS) {
		return (
			<Text bold color="yellow">
				{controller.t("app.resize", { columns: MIN_COLUMNS, rows: MIN_ROWS })}
			</Text>
		);
	}

	const monster = controller.monsterView();
	const player = controller.playerView();
	const animation = cues[0] ?? "idle";
	const frame =
		monster === null
			? frameFor(art.loadFile(...TITLE_ART), "idle", tick)
			: frameFor(art.forCreature(controller.services.data.creature(monster.creatureId)), animation, tick);
	const artColor = monster === null ? "green" : animation === "hurt" ? "red" : ELEMENT_COLORS[monster.element];
	const body = controller.bodyLines();
	const bodyColors = controller.bodyColors();
	const prompt = controller.inputPrompt();

	return (
		<Box flexDirection="column" width={width} height={height}>
			<Box flexDirection="column" height={8}>
				<TopBorder title={controller.header()} width={width} />
				<Box borderStyle="round" borderColor={ACCENT} borderTop={false} paddingX={1} height={7}>
					<Box width={32} flexDirection="column">
						<Text color={artColor} bold={monster === null || animation === "hurt"}>
							{frame.join("\n")}
						</Text>
					</Box>
					<Box flexDirection="column" flexGrow={1}>
						{monster === null ? (
							<>
								<Text bold>{controller.t("app.title")}</Text>
								<Text italic>{controller.t("app.subtitle")}</Text>
								<Text> </Text>
								<Text>{`v${VERSION} · TypeScript`}</Text>
							</>
						) : (
							<>
								<Text>
									{monster.isBoss ? (
										<Text bold color="magenta">{`${controller.t("hud.boss")} `}</Text>
									) : monster.enemyClass === "elite" ? (
										<Text bold color="yellow">{`${controller.t("hud.elite")} `}</Text>
									) : null}
									<Text bold>{monster.name.toUpperCase()}</Text>
								</Text>
								<Text>
									<Text bold>{"HP "}</Text>
									<Text color={hpColor(monster.hp, monster.maxHp)}>
										{bar(monster.hp, monster.maxHp)}
									</Text>
									{`  ${monster.hp}/${monster.maxHp}`}
								</Text>
								<Text color={ELEMENT_COLORS[monster.element]}>{monster.details}</Text>
							</>
						)}
					</Box>
				</Box>
			</Box>
			<Panel height={5}>
				{player === null ? (
					<Text> </Text>
				) : (
					<>
						<Text>
							{player.summary}
							<Text color="yellow">{`   ${player.gold}`}</Text>
							{player.statuses ? <Text color="red">{`   ${player.statuses}`}</Text> : null}
						</Text>
						<Text>
							<Text bold>{"HP "}</Text>
							<Text color={hpColor(player.hp, player.maxHp)}>{bar(player.hp, player.maxHp)}</Text>
							<Text bold>{`  ${player.hp}/${player.maxHp}`}</Text>
						</Text>
						<Text>
							<Text bold>{"MP "}</Text>
							<Text color="blue">{bar(player.mp, player.maxMp)}</Text>
							{`  ${player.mp}/${player.maxMp}   ${player.xp}`}
						</Text>
					</>
				)}
			</Panel>
			{PAGED_VIEWS.has(controller.view) ? null : (
				<Panel height={7}>
					<Text>{controller.log.slice(-LOG_LINES).join("\n")}</Text>
				</Panel>
			)}
			<Panel>
				<Text bold underline>
					{controller.title()}
				</Text>
				{body.map((line, index) => {
					const color = optionColor(bodyColors[index] ?? null);
					return (
						// biome-ignore lint/suspicious/noArrayIndexKey: body lines are static text re-rendered as a whole.
						<Text key={index} {...(color === undefined ? {} : { color })}>
							{line === "" ? " " : line}
						</Text>
					);
				})}
				{controller.options().length > 0 ? <Text> </Text> : null}
				<MenuOptions options={controller.options()} />
				{prompt === null ? null : <Text bold>{`\n${prompt}`}</Text>}
				{controller.message ? <Text bold color="red">{`\n${controller.message}`}</Text> : null}
			</Panel>
		</Box>
	);
}
