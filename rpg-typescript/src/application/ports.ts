/** Ports implemented by the infrastructure layer (Dependency Inversion: the application owns the interfaces). */
import type { Profile } from "./profile";
import type { RunRecord, SaveGame } from "./save-game";

export interface Clock {
	now(): Date;
}

export interface SaveRepository {
	load(): SaveGame | null;
	save(save: SaveGame): void;
	delete(): void;
}

export interface HistoryRepository {
	add(record: RunRecord): void;
	list(): RunRecord[];
}

export interface ProfileRepository {
	load(): Profile;
	save(profile: Profile): void;
}
