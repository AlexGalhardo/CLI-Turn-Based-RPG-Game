"""Flat key → template translations from shared/i18n (English is the default and the fallback)."""

import json
import re
from pathlib import Path

from rpg.domain.json_types import json_obj, json_str

DEFAULT_LOCALE = "en"
SUPPORTED_LOCALES = ("en", "pt-BR")
_PLACEHOLDER = re.compile(r"\{(\w+)\}")


def _load(path: Path) -> dict[str, str]:
	document = json_obj(json.loads(path.read_text(encoding="utf-8")))
	return {key: json_str(value) for key, value in document.items()}


class Translator:
	def __init__(self, shared_dir: Path, locale: str = DEFAULT_LOCALE) -> None:
		if locale not in SUPPORTED_LOCALES:
			raise ValueError(f"unsupported locale: {locale}")
		self.locale = locale
		self._fallback = _load(shared_dir / "i18n" / f"{DEFAULT_LOCALE}.json")
		self._messages = self._fallback if locale == DEFAULT_LOCALE else _load(shared_dir / "i18n" / f"{locale}.json")

	def has(self, key: str) -> bool:
		return key in self._messages or key in self._fallback

	def t(self, key: str, **params: object) -> str:
		"""Missing keys render as the key itself; missing params keep their `{placeholder}`."""
		template = self._messages.get(key) or self._fallback.get(key) or key
		return _PLACEHOLDER.sub(lambda m: str(params[m.group(1)]) if m.group(1) in params else m.group(0), template)
