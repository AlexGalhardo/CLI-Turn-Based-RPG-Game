package infrastructure

import (
	"encoding/json"
	"fmt"
	"io/fs"
	"regexp"
	"slices"
)

// DefaultLocale is English; it is also the fallback for missing keys.
const DefaultLocale = "en"

// SupportedLocales lists the locales of shared/i18n.
var SupportedLocales = []string{"en", "pt-BR"}

var placeholder = regexp.MustCompile(`\{(\w+)\}`)

// Translator renders flat key → template translations.
type Translator struct {
	Locale   string
	fallback map[string]string
	messages map[string]string
}

func loadLocale(shared fs.FS, locale string) (map[string]string, error) {
	content, err := fs.ReadFile(shared, "i18n/"+locale+".json")
	if err != nil {
		return nil, fmt.Errorf("read locale %s: %w", locale, err)
	}

	messages := map[string]string{}
	if err := json.Unmarshal(content, &messages); err != nil {
		return nil, fmt.Errorf("parse locale %s: %w", locale, err)
	}

	return messages, nil
}

// NewTranslator loads a supported locale.
func NewTranslator(shared fs.FS, locale string) (*Translator, error) {
	if !slices.Contains(SupportedLocales, locale) {
		return nil, fmt.Errorf("unsupported locale: %s", locale)
	}

	fallback, err := loadLocale(shared, DefaultLocale)
	if err != nil {
		return nil, err
	}

	messages := fallback
	if locale != DefaultLocale {
		if messages, err = loadLocale(shared, locale); err != nil {
			return nil, err
		}
	}

	return &Translator{Locale: locale, fallback: fallback, messages: messages}, nil
}

// Has reports whether a key exists.
func (t *Translator) Has(key string) bool {
	_, inMessages := t.messages[key]
	_, inFallback := t.fallback[key]

	return inMessages || inFallback
}

// T renders a key. Missing keys render as the key itself; missing params keep their {placeholder}.
func (t *Translator) T(key string, params map[string]any) string {
	template := t.messages[key]
	if template == "" {
		template = t.fallback[key]
	}

	if template == "" {
		template = key
	}

	return placeholder.ReplaceAllStringFunc(template, func(match string) string {
		if value, ok := params[match[1:len(match)-1]]; ok {
			return fmt.Sprint(value)
		}

		return match
	})
}
