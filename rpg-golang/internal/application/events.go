// Package application implements the use cases: the deterministic engine, merchant, loot, progression,
// statistics, the bot, the simulator and the persistence-aware game session.
package application

import "maps"

// Event is a flat engine event (docs/cross-language-parity.md §3). Values are int, string or bool.
type Event map[string]any

// Type returns the event type.
func (e Event) Type() string {
	value, _ := e["type"].(string)

	return value
}

// Int returns an integer field (0 when missing).
func (e Event) Int(field string) int {
	value, _ := e[field].(int)

	return value
}

// Str returns a string field ("" when missing).
func (e Event) Str(field string) string {
	value, _ := e[field].(string)

	return value
}

// Bool returns a boolean field (false when missing).
func (e Event) Bool(field string) bool {
	value, _ := e[field].(bool)

	return value
}

// NewEvent builds an event with a type and fields.
func NewEvent(eventType string, fields map[string]any) Event {
	evt := Event{"type": eventType}
	maps.Copy(evt, fields)

	return evt
}

// ErrorEvent builds an `error` event.
func ErrorEvent(code string) Event {
	return Event{"type": "error", "code": code}
}

// Error codes of `error` events.
const (
	ErrNotEnoughMana   = "not_enough_mana"
	ErrNotEnoughGold   = "not_enough_gold"
	ErrNoPotion        = "no_potion"
	ErrUnknownSpell    = "unknown_spell"
	ErrUnknownPotion   = "unknown_potion"
	ErrPotionLocked    = "potion_locked"
	ErrInvalidPhase    = "invalid_phase"
	ErrInvalidQuantity = "invalid_quantity"
	ErrBagFull         = "bag_full"
	ErrCannotEquip     = "cannot_equip"
	ErrInvalidItem     = "invalid_item"
	ErrLevelTooLow     = "level_too_low"
)
