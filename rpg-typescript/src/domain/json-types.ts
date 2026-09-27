/** Typed helpers to read untrusted JSON (data files, saves) without `any`. */
export type JsonValue = number | string | boolean | null | JsonValue[] | { [key: string]: JsonValue };
export type JsonObject = { [key: string]: JsonValue };

function typeName(value: unknown): string {
	if (value === null) return "null";
	if (Array.isArray(value)) return "list";
	return typeof value;
}

export function jsonObj(value: unknown): JsonObject {
	if (typeof value !== "object" || value === null || Array.isArray(value)) {
		throw new TypeError(`expected object, got ${typeName(value)}`);
	}
	return value as JsonObject;
}

export function jsonList(value: unknown): JsonValue[] {
	if (!Array.isArray(value)) {
		throw new TypeError(`expected list, got ${typeName(value)}`);
	}
	return value as JsonValue[];
}

export function jsonInt(value: unknown): number {
	if (typeof value !== "number" || !Number.isInteger(value)) {
		throw new TypeError(`expected int, got ${typeName(value)}`);
	}
	return value;
}

export function jsonStr(value: unknown): string {
	if (typeof value !== "string") {
		throw new TypeError(`expected str, got ${typeName(value)}`);
	}
	return value;
}

export function jsonBool(value: unknown): boolean {
	if (typeof value !== "boolean") {
		throw new TypeError(`expected bool, got ${typeName(value)}`);
	}
	return value;
}

/** Reads a required key, failing with the key name (mirrors a Python KeyError). */
export function field(object: JsonObject, key: string): JsonValue {
	const value = object[key];
	if (value === undefined) {
		throw new TypeError(`missing key: ${key}`);
	}
	return value;
}
