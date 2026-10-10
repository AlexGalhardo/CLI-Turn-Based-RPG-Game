// Engine events (docs/cross-language-parity.md §3): a type plus a few named fields, compared across implementations
// as flat JSON objects. An Event is a plain value, so lists of them need no per-event cleanup.
#ifndef RPG_APPLICATION_EVENTS_H
#define RPG_APPLICATION_EVENTS_H

#include "domain/json_types.h"

#define EVENT_MAX_FIELDS 7

typedef enum { EVENT_INT, EVENT_STR, EVENT_BOOL } EventValueKind;

typedef struct {
	const char *name; // a string literal
	EventValueKind kind;
	int64_t integer; // also holds booleans (0/1)
	Id text;
} EventField;

typedef struct {
	const char *type; // a string literal
	int field_count;
	EventField fields[EVENT_MAX_FIELDS];
} Event;

typedef struct {
	Event *items;
	size_t count;
	size_t capacity;
} EventList;

// Builders: `Event e = event_new("xp_gained"); event_int(&e, "amount", 5);`
Event event_new(const char *type);
void event_int(Event *event, const char *name, int64_t value);
void event_str(Event *event, const char *name, const char *value);
void event_bool(Event *event, const char *name, bool value);
Event event_error(const char *code);

void events_push(EventList *list, Event event);
void events_extend(EventList *list, const EventList *other);
void events_clear(EventList *list);
void events_free(EventList *list);

bool event_is(const Event *event, const char *type);
// NULL when the event has no such field.
const EventField *event_field(const Event *event, const char *name);
// These treat a missing or mistyped field as a bug.
int64_t event_get_int(const Event *event, const char *name);
const char *event_get_str(const Event *event, const char *name);
bool event_get_bool(const Event *event, const char *name);

JsonValue *event_to_json(const Event *event);
JsonValue *events_to_json(const EventList *list);

#define ERROR_NOT_ENOUGH_MANA "not_enough_mana"
#define ERROR_NOT_ENOUGH_GOLD "not_enough_gold"
#define ERROR_NO_POTION "no_potion"
#define ERROR_UNKNOWN_SPELL "unknown_spell"
#define ERROR_UNKNOWN_POTION "unknown_potion"
#define ERROR_POTION_LOCKED "potion_locked"
#define ERROR_INVALID_PHASE "invalid_phase"
#define ERROR_INVALID_QUANTITY "invalid_quantity"
#define ERROR_BAG_FULL "bag_full"
#define ERROR_CANNOT_EQUIP "cannot_equip"
#define ERROR_INVALID_ITEM "invalid_item"
#define ERROR_LEVEL_TOO_LOW "level_too_low"
#define ERROR_UNKNOWN_COMMAND "unknown_command"

#endif
