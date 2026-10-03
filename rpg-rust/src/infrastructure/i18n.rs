//! Flat key → template translations from shared/i18n (English is the default and the fallback).

use std::collections::HashMap;
use std::fmt::Display;

use crate::assets::SharedFs;

pub const DEFAULT_LOCALE: &str = "en";
pub const SUPPORTED_LOCALES: [&str; 2] = ["en", "pt-BR"];

/// Template parameters: `&[("gold", &120)]`. `&dyn Display` accepts numbers and strings alike, like Python's
/// `**params: object`.
pub type Params<'a> = [(&'a str, &'a dyn Display)];

fn load(shared: &SharedFs, locale: &str) -> Result<HashMap<String, String>, String> {
	let path = format!("i18n/{locale}.json");
	let text = shared.read(&path).ok_or_else(|| format!("{path}: file not found"))?;
	serde_json::from_str(text).map_err(|error| format!("{path}: {error}"))
}

#[derive(Debug, Clone)]
pub struct Translator {
	pub locale: String,
	messages: HashMap<String, String>,
	fallback: HashMap<String, String>,
}

impl Translator {
	pub fn new(shared: &SharedFs, locale: &str) -> Result<Translator, String> {
		if !SUPPORTED_LOCALES.contains(&locale) {
			return Err(format!("unsupported locale: {locale}"));
		}
		let fallback = load(shared, DEFAULT_LOCALE)?;
		let messages = if locale == DEFAULT_LOCALE { fallback.clone() } else { load(shared, locale)? };
		Ok(Translator { locale: locale.to_owned(), messages, fallback })
	}

	pub fn has(&self, key: &str) -> bool {
		self.messages.contains_key(key) || self.fallback.contains_key(key)
	}

	/// Missing keys render as the key itself; missing params keep their `{placeholder}`.
	pub fn t(&self, key: &str, params: &Params<'_>) -> String {
		let non_empty = |map: &HashMap<String, String>| map.get(key).filter(|text| !text.is_empty()).cloned();
		let template =
			non_empty(&self.messages).or_else(|| non_empty(&self.fallback)).unwrap_or_else(|| key.to_owned());
		substitute(&template, params)
	}
}

/// Replaces `{name}` placeholders (`name` = letters, digits, `_`) with the matching parameter.
fn substitute(template: &str, params: &Params<'_>) -> String {
	let mut result = String::with_capacity(template.len());
	let mut rest = template;
	while let Some(open) = rest.find('{') {
		result.push_str(&rest[..open]);
		let after = &rest[open + 1..];
		let name_length = after.find(|c: char| !(c.is_alphanumeric() || c == '_')).unwrap_or(after.len());
		let name = &after[..name_length];
		if name_length > 0 && after[name_length..].starts_with('}') {
			match params.iter().find(|(param, _)| *param == name) {
				Some((_, value)) => result.push_str(&value.to_string()),
				None => {
					result.push('{');
					result.push_str(name);
					result.push('}');
				}
			}
			rest = &after[name_length + 1..];
		} else {
			result.push('{');
			rest = after;
		}
	}
	result.push_str(rest);
	result
}

#[cfg(test)]
mod tests {
	use super::*;

	#[test]
	fn substitute_handles_params_and_stray_braces() {
		let gold = 5;
		assert_eq!(substitute("You have {gold} gold.", &[("gold", &gold)]), "You have 5 gold.");
		assert_eq!(substitute("{missing} and {gold}", &[("gold", &"x")]), "{missing} and x");
		assert_eq!(substitute("set {} and {a-b} {", &[]), "set {} and {a-b} {");
	}
}
