#include "application/events.h"

#include <stdlib.h>
#include <string.h>

Event event_new(const char *type) {
	Event event = {.type = type};
	return event;
}

static EventField *add_field(Event *event, const char *name, EventValueKind kind) {
	if (event->field_count == EVENT_MAX_FIELDS) {
		fatal("too many fields in event %s", event->type);
	}
	EventField *field = &event->fields[event->field_count++];
	memset(field, 0, sizeof(*field));
	field->name = name;
	field->kind = kind;
	return field;
}

void event_int(Event *event, const char *name, int64_t value) { add_field(event, name, EVENT_INT)->integer = value; }

void event_str(Event *event, const char *name, const char *value) {
	id_set(add_field(event, name, EVENT_STR)->text, value);
}

void event_bool(Event *event, const char *name, bool value) {
	add_field(event, name, EVENT_BOOL)->integer = value ? 1 : 0;
}

Event event_error(const char *code) {
	Event event = event_new("error");
	event_str(&event, "code", code);
	return event;
}

void events_push(EventList *list, Event event) { VEC_PUSH(list->items, list->count, list->capacity, event); }

void events_extend(EventList *list, const EventList *other) {
	for (size_t i = 0; i < other->count; i++) {
		events_push(list, other->items[i]);
	}
}

void events_clear(EventList *list) { list->count = 0; }

void events_free(EventList *list) {
	free(list->items);
	list->items = NULL;
	list->count = 0;
	list->capacity = 0;
}

bool event_is(const Event *event, const char *type) { return strcmp(event->type, type) == 0; }

const EventField *event_field(const Event *event, const char *name) {
	for (int i = 0; i < event->field_count; i++) {
		if (strcmp(event->fields[i].name, name) == 0) {
			return &event->fields[i];
		}
	}
	return NULL;
}

static const EventField *typed_field(const Event *event, const char *name, EventValueKind kind) {
	const EventField *field = event_field(event, name);
	if (field == NULL || field->kind != kind) {
		fatal("event %s has no field %s of the expected type", event->type, name);
	}
	return field;
}

int64_t event_get_int(const Event *event, const char *name) { return typed_field(event, name, EVENT_INT)->integer; }

const char *event_get_str(const Event *event, const char *name) { return typed_field(event, name, EVENT_STR)->text; }

bool event_get_bool(const Event *event, const char *name) { return typed_field(event, name, EVENT_BOOL)->integer != 0; }

JsonValue *event_to_json(const Event *event) {
	JsonValue *object = json_object();
	json_set(object, "type", json_string(event->type));
	for (int i = 0; i < event->field_count; i++) {
		const EventField *field = &event->fields[i];
		switch (field->kind) {
		case EVENT_INT:
			json_set(object, field->name, json_int(field->integer));
			break;
		case EVENT_STR:
			json_set(object, field->name, json_string(field->text));
			break;
		case EVENT_BOOL:
			json_set(object, field->name, json_bool(field->integer != 0));
			break;
		}
	}
	return object;
}

JsonValue *events_to_json(const EventList *list) {
	JsonValue *array = json_array();
	for (size_t i = 0; i < list->count; i++) {
		json_push(array, event_to_json(&list->items[i]));
	}
	return array;
}
