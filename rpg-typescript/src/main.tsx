/** Entry point: `bun run dev [flags]` or the compiled `rpg-typescript` executable. */
import { render } from "ink";
import { simulate } from "./application/simulator";
import { ArtLibrary } from "./infrastructure/art";
import { loadGameData } from "./infrastructure/data-loader";
import { resolveDataDir } from "./infrastructure/paths";
import {
	FileHistoryRepository,
	FileProfileRepository,
	FileSaveRepository,
	SettingsRepository,
	SystemClock,
} from "./infrastructure/repositories";
import { CliError, type CliOptions, parseCli } from "./presentation/cli";
import { Controller, type Services } from "./presentation/controller";
import { renderReport } from "./presentation/simulator-report";
import { App } from "./presentation/tui/app";
import { VERSION } from "./version";

export function buildServices(options: CliOptions): Services {
	const dataDir = resolveDataDir(options.dataDir);
	return {
		data: loadGameData(),
		settings: new SettingsRepository(dataDir),
		repositories: {
			saves: new FileSaveRepository(dataDir),
			history: new FileHistoryRepository(dataDir),
			profile: new FileProfileRepository(dataDir),
		},
		clock: new SystemClock(),
		version: VERSION,
	};
}

export function runSimulator(options: CliOptions, write: (text: string) => void): number {
	const data = loadGameData();
	const vocations = options.vocation ? [options.vocation] : data.vocations.map((v) => v.id);
	const difficulties = options.difficulty ? [options.difficulty] : data.balance.difficulties.map((d) => d.id);
	try {
		// Like the reference (`options.seed or 1`), seed 0 means "no seed" for the simulator.
		const summaries = vocations.flatMap((v) =>
			difficulties.map((d) => simulate(data, v, d, options.simulate ?? 1, options.seed || 1)),
		);
		write(renderReport(summaries, data));
		return 0;
	} catch (exc) {
		write(`error: ${(exc as Error).message}`);
		return 2;
	}
}

export async function main(argv: readonly string[]): Promise<number> {
	let parsed: ReturnType<typeof parseCli>;
	try {
		parsed = parseCli(argv);
	} catch (exc) {
		if (exc instanceof CliError) {
			console.error(`rpg: error: ${exc.message}`);
			return 2;
		}
		throw exc;
	}
	if (parsed.kind === "exit") {
		console.log(parsed.output);
		return 0;
	}
	const options = parsed.options;
	if (options.simulate !== null) return runSimulator(options, (text) => console.log(text));

	const controller = new Controller(buildServices(options), { seed: options.seed, localeOverride: options.lang });
	const animate = !(options.noAnim || process.env.RPG_NO_ANIM);
	const instance = render(<App controller={controller} art={new ArtLibrary()} animate={animate} />, {
		exitOnCtrlC: true,
	});
	await instance.waitUntilExit();
	controller.session?.saveAndQuit();
	return 0;
}

if (import.meta.main) {
	process.exitCode = await main(process.argv.slice(2));
}
