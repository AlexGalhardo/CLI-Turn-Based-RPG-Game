"""Typed helpers to read untrusted JSON (data files, saves) without `Any`."""

type JsonValue = int | str | bool | list[JsonValue] | dict[str, JsonValue] | None
type JsonObject = dict[str, JsonValue]


def json_obj(value: JsonValue) -> JsonObject:
	if not isinstance(value, dict):
		raise TypeError(f"expected object, got {type(value).__name__}")
	return value


def json_list(value: JsonValue) -> list[JsonValue]:
	if not isinstance(value, list):
		raise TypeError(f"expected list, got {type(value).__name__}")
	return value


def json_int(value: JsonValue) -> int:
	if isinstance(value, bool) or not isinstance(value, int):
		raise TypeError(f"expected int, got {type(value).__name__}")
	return value


def json_str(value: JsonValue) -> str:
	if not isinstance(value, str):
		raise TypeError(f"expected str, got {type(value).__name__}")
	return value


def json_bool(value: JsonValue) -> bool:
	if not isinstance(value, bool):
		raise TypeError(f"expected bool, got {type(value).__name__}")
	return value
