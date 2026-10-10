// Flat key → template translations from shared/i18n (English is the default and the fallback).
#ifndef RPG_INFRASTRUCTURE_I18N_H
#define RPG_INFRASTRUCTURE_I18N_H

#include "domain/json_types.h"

#define DEFAULT_LOCALE "en"
#define LOCALE_SIZE 16

extern const char *const SUPPORTED_LOCALES[];
extern const size_t SUPPORTED_LOCALE_COUNT;

bool locale_supported(const char *locale);

typedef struct {
	char locale[LOCALE_SIZE];
	JsonValue *messages; // the chosen locale; the same object as `fallback` for English
	JsonValue *fallback;
} Translator;

// A value for a `{name}` placeholder: a text or an integer.
typedef struct {
	const char *name;
	const char *text;
	int64_t number;
} Param;

#define P_STR(placeholder, value) ((Param){.name = (placeholder), .text = (value)})
#define P_INT(placeholder, value) ((Param){.name = (placeholder), .number = (int64_t)(value)})

// Returns false for an unsupported locale.
bool translator_init(Translator *translator, const char *locale);
void translator_free(Translator *translator);
bool translator_has(const Translator *translator, const char *key);
// Missing keys render as the key itself; missing params keep their `{placeholder}`. The caller frees the result.
char *translate(const Translator *translator, const char *key, const Param *params, size_t count);
// Appends the translation to `out` instead of allocating a new string.
void translate_into(StrBuf *out, const Translator *translator, const char *key, const Param *params, size_t count);

// `TR(t, "key", P_INT("n", 3), P_STR("name", "Orc"))` — at least one param; use translate(t, key, NULL, 0) for none.
#define TR(translator, key, ...)                                                                                       \
	translate((translator), (key), (const Param[]){__VA_ARGS__}, sizeof((const Param[]){__VA_ARGS__}) / sizeof(Param))

#endif
