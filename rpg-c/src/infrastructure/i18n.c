#include "infrastructure/i18n.h"

#include "assets/shared_files.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

const char *const SUPPORTED_LOCALES[] = {"en", "pt-BR"};
const size_t SUPPORTED_LOCALE_COUNT = ARRAY_LEN(SUPPORTED_LOCALES);

bool locale_supported(const char *locale) {
	for (size_t i = 0; i < SUPPORTED_LOCALE_COUNT; i++) {
		if (strcmp(SUPPORTED_LOCALES[i], locale) == 0) {
			return true;
		}
	}
	return false;
}

static JsonValue *load_messages(const char *locale) {
	char path[64];
	snprintf(path, sizeof(path), "i18n/%s.json", locale);
	const char *text = shared_file(path);
	if (text == NULL) {
		fatal("missing embedded file %s", path);
	}
	JsonError error = {0};
	JsonValue *document = json_parse(text, &error);
	if (document == NULL || document->type != JSON_OBJECT) {
		fatal("%s: %s", path, error.failed ? error.message : "expected object");
	}
	return document;
}

bool translator_init(Translator *translator, const char *locale) {
	if (!locale_supported(locale)) {
		return false;
	}
	str_copy(translator->locale, LOCALE_SIZE, locale);
	translator->fallback = load_messages(DEFAULT_LOCALE);
	translator->messages = strcmp(locale, DEFAULT_LOCALE) == 0 ? translator->fallback : load_messages(locale);
	return true;
}

void translator_free(Translator *translator) {
	if (translator->messages != translator->fallback) {
		json_free(translator->messages);
	}
	json_free(translator->fallback);
	translator->messages = NULL;
	translator->fallback = NULL;
}

static const char *lookup(const JsonValue *messages, const char *key) {
	const JsonValue *value = json_get(messages, key);
	return value != NULL && value->type == JSON_STRING && value->string[0] != '\0' ? value->string : NULL;
}

bool translator_has(const Translator *translator, const char *key) {
	return json_has(translator->messages, key) || json_has(translator->fallback, key);
}

static bool is_word_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

void translate_into(StrBuf *out, const Translator *translator, const char *key, const Param *params, size_t count) {
	const char *template = lookup(translator->messages, key);
	if (template == NULL) {
		template = lookup(translator->fallback, key);
	}
	if (template == NULL) {
		template = key;
	}
	const char *at = template;
	while (*at != '\0') {
		if (*at != '{') {
			sb_append_char(out, *at++);
			continue;
		}
		// A placeholder is `{` + word characters + `}`; anything else is literal text.
		const char *end = at + 1;
		while (is_word_char(*end)) {
			end++;
		}
		size_t name_length = (size_t)(end - at - 1);
		const Param *found = NULL;
		if (*end == '}' && name_length > 0) {
			for (size_t i = 0; i < count; i++) {
				if (strlen(params[i].name) == name_length && strncmp(params[i].name, at + 1, name_length) == 0) {
					found = &params[i];
				}
			}
		}
		if (found == NULL) {
			sb_append_char(out, *at++);
			continue;
		}
		if (found->text != NULL) {
			sb_append(out, found->text);
		} else {
			sb_appendf(out, "%lld", (long long)found->number);
		}
		at = end + 1;
	}
	if (out->data == NULL) {
		sb_append(out, "");
	}
}

char *translate(const Translator *translator, const char *key, const Param *params, size_t count) {
	StrBuf out = {0};
	translate_into(&out, translator, key, params, count);
	return sb_take(&out);
}
