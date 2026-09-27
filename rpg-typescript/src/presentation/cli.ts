/** Command-line flags, identical in the three implementations (docs/tui.md). */
import { parseArgs } from "node:util";
import { SUPPORTED_LOCALES } from "../infrastructure/i18n";
import { VERSION } from "../version";

export interface CliOptions {
	readonly seed: number | null;
	readonly lang: string | null;
	readonly noAnim: boolean;
	readonly dataDir: string | null;
	readonly simulate: number | null;
	readonly vocation: string | null;
	readonly difficulty: string | null;
}

export class CliError extends Error {}

export const HELP = `usage: rpg [-h] [--version] [--seed SEED] [--lang {en,pt-BR}] [--no-anim]
           [--data-dir DATA_DIR] [--simulate N] [--vocation VOCATION]
           [--difficulty DIFFICULTY]

Endless turn-based RPG for the terminal.

options:
  -h, --help            show this help message and exit
  --version             show program's version number and exit
  --seed SEED           deterministic run
  --lang {en,pt-BR}     override the saved language
  --no-anim             disable animations
  --data-dir DATA_DIR   saves/profile location
  --simulate N          run N headless bot games and print a report
  --vocation VOCATION   (simulator) restrict to one vocation
  --difficulty DIFFICULTY
                        (simulator) restrict to one difficulty`;

export type CliResult =
	| { readonly kind: "run"; readonly options: CliOptions }
	| { readonly kind: "exit"; readonly output: string };

function integer(value: string | undefined, name: string, minimum: number): number | null {
	if (value === undefined) return null;
	if (!/^\d+$/.test(value) || Number(value) < minimum) {
		throw new CliError(`argument --${name}: must be ${minimum === 0 ? ">= 0" : "> 0"}`);
	}
	return Number(value);
}

function parseFlags(argv: readonly string[]) {
	try {
		return parseRaw(argv);
	} catch (exc) {
		// node:util reports usage errors as TypeError; surface them as CLI errors like argparse does.
		throw new CliError((exc as Error).message);
	}
}

function parseRaw(argv: readonly string[]) {
	return parseArgs({
		args: [...argv],
		options: {
			help: { type: "boolean", short: "h" },
			version: { type: "boolean" },
			seed: { type: "string" },
			lang: { type: "string" },
			"no-anim": { type: "boolean" },
			"data-dir": { type: "string" },
			simulate: { type: "string" },
			vocation: { type: "string" },
			difficulty: { type: "string" },
		},
		strict: true,
	});
}

export function parseCli(argv: readonly string[]): CliResult {
	const { values } = parseFlags(argv);
	if (values.help) return { kind: "exit", output: HELP };
	if (values.version) return { kind: "exit", output: `rpg ${VERSION} (typescript)` };
	if (values.lang !== undefined && !SUPPORTED_LOCALES.includes(values.lang)) {
		throw new CliError(`argument --lang: invalid choice: '${values.lang}' (choose from 'en', 'pt-BR')`);
	}
	return {
		kind: "run",
		options: {
			seed: integer(values.seed, "seed", 0),
			lang: values.lang ?? null,
			noAnim: values["no-anim"] ?? false,
			dataDir: values["data-dir"] ?? null,
			simulate: integer(values.simulate, "simulate", 1),
			vocation: values.vocation ?? null,
			difficulty: values.difficulty ?? null,
		},
	};
}
