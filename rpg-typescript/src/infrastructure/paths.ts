/** Filesystem locations: the player's data directory (docs/persistence.md). */
import { homedir } from "node:os";
import { join } from "node:path";

export const DATA_DIR_ENV = "RPG_DATA_DIR";
export const DEFAULT_DATA_DIR_NAME = ".cli-turn-based-rpg";

export function resolveDataDir(cliValue: string | null = null): string {
	if (cliValue) return cliValue;
	const override = process.env[DATA_DIR_ENV];
	if (override) return override;
	return join(homedir(), DEFAULT_DATA_DIR_NAME);
}
