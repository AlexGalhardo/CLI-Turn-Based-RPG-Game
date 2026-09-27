/** Parses shared/art files: `@animation` headers, frames separated by `%%` (docs/data-format.md). */
import type { MonsterDef } from "../domain/definitions";
import { EMBEDDED_ART } from "./embedded-shared";

export type Frame = readonly string[];
export type Animations = ReadonlyMap<string, readonly Frame[]>;

export function parseArt(text: string): Animations {
	const animations = new Map<string, string[][]>();
	let current: string[][] | null = null;
	for (const line of text.replace(/\n+$/, "").split("\n")) {
		if (line.startsWith("@")) {
			current = [[]];
			animations.set(line.slice(1).trim(), current);
		} else if (line === "%%") {
			if (current === null) throw new SyntaxError("frame separator before any @animation");
			current.push([]);
		} else if (current === null) {
			throw new SyntaxError("art content before the first @animation");
		} else {
			current[current.length - 1]?.push(line);
		}
	}
	return animations;
}

export class ArtLibrary {
	readonly #cache = new Map<string, Animations>();

	constructor(private readonly files: Readonly<Record<string, string>> = EMBEDDED_ART) {}

	forCreature(creature: MonsterDef): Animations {
		return creature.isBoss ? this.loadFile("bosses", creature.id) : this.loadFile("families", creature.family);
	}

	loadFile(folder: string, name: string): Animations {
		const key = `${folder}/${name}`;
		let animations = this.#cache.get(key);
		if (animations === undefined) {
			const text = this.files[key];
			animations = text === undefined ? new Map() : parseArt(text);
			this.#cache.set(key, animations);
		}
		return animations;
	}
}

/** Frame of `animation` at animation tick `tick`, falling back to idle; empty when there is no art. */
export function frameFor(animations: Animations, animation: string, tick: number): Frame {
	const frames = animations.get(animation) ?? animations.get("idle") ?? [];
	if (frames.length === 0) return [];
	return frames[tick % frames.length] ?? [];
}
