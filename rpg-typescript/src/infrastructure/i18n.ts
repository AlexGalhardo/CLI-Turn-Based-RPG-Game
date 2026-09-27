/** Flat key → template translations from shared/i18n (English is the default and the fallback). */
import { jsonObj, jsonStr } from "../domain/json-types";
import { EMBEDDED, type SharedFiles } from "./embedded-shared";

export const DEFAULT_LOCALE = "en";
export const SUPPORTED_LOCALES: readonly string[] = ["en", "pt-BR"];
const PLACEHOLDER = /\{(\w+)\}/g;

function load(files: SharedFiles, locale: string): Map<string, string> {
	const document = jsonObj(files.i18n[locale]);
	return new Map(Object.entries(document).map(([key, value]) => [key, jsonStr(value)]));
}

export class Translator {
	readonly #fallback: Map<string, string>;
	readonly #messages: Map<string, string>;

	constructor(
		readonly locale: string = DEFAULT_LOCALE,
		files: SharedFiles = EMBEDDED,
	) {
		if (!SUPPORTED_LOCALES.includes(locale)) throw new RangeError(`unsupported locale: ${locale}`);
		this.#fallback = load(files, DEFAULT_LOCALE);
		this.#messages = locale === DEFAULT_LOCALE ? this.#fallback : load(files, locale);
	}

	has(key: string): boolean {
		return this.#messages.has(key) || this.#fallback.has(key);
	}

	/** Missing keys render as the key itself; missing params keep their `{placeholder}`. */
	t(key: string, params: Readonly<Record<string, unknown>> = {}): string {
		const template = this.#messages.get(key) || this.#fallback.get(key) || key;
		return template.replace(PLACEHOLDER, (match, name: string) => (name in params ? String(params[name]) : match));
	}
}
