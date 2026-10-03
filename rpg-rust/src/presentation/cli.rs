//! Command-line flags, identical in every implementation (docs/tui.md).
//!
//! Hand-written with the standard library: it accepts `--flag value` and `--flag=value`, and reports usage errors
//! with the same messages and exit code (2) as Python's argparse.

use std::fmt;

use crate::infrastructure::i18n::SUPPORTED_LOCALES;
use crate::version::VERSION;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CliOptions {
	pub seed: Option<u64>,
	pub lang: Option<String>,
	pub no_anim: bool,
	pub data_dir: Option<String>,
	pub simulate: Option<usize>,
	pub vocation: Option<String>,
	pub difficulty: Option<String>,
}

/// What the program should do after parsing.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CliResult {
	Run(CliOptions),
	/// Print this text to stdout and exit with 0 (`--help`, `--version`).
	Exit(String),
}

/// A usage error: the caller prints [`USAGE`] and `rpg: error: <message>` to stderr and exits with 2.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UsageError(pub String);

impl fmt::Display for UsageError {
	fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
		write!(formatter, "{USAGE}\nrpg: error: {}", self.0)
	}
}

impl std::error::Error for UsageError {}

pub const USAGE: &str = "usage: rpg [-h] [--version] [--seed SEED] [--lang {en,pt-BR}] [--no-anim]
           [--data-dir DATA_DIR] [--simulate N] [--vocation VOCATION]
           [--difficulty DIFFICULTY]";

/// Mirrors the argparse help of the Python reference.
pub fn help_text() -> String {
	format!(
		"{USAGE}

Endless turn-based RPG for the terminal.

options:
  -h, --help            show this help message and exit
  --version             show program's version number and exit
  --seed SEED           deterministic run
  --lang {{en,pt-BR}}     override the saved language
  --no-anim             disable animations
  --data-dir DATA_DIR   saves/profile location
  --simulate N          run N headless bot games and print a report
  --vocation VOCATION   (simulator) restrict to one vocation
  --difficulty DIFFICULTY
                        (simulator) restrict to one difficulty"
	)
}

pub fn version_text() -> String {
	format!("rpg {VERSION} (rust)")
}

/// argparse treats `-x` as an option but `-1` as a (negative number) value.
fn looks_like_flag(text: &str) -> bool {
	text.len() > 1 && text.starts_with('-') && text[1..].parse::<f64>().is_err()
}

const VALUE_FLAGS: [&str; 6] = ["--seed", "--lang", "--data-dir", "--simulate", "--vocation", "--difficulty"];

fn invalid(flag: &str, message: &str) -> UsageError {
	UsageError(format!("argument {flag}: {message}"))
}

fn parse_seed(text: &str) -> Result<u64, UsageError> {
	let number: i128 =
		text.parse().map_err(|_| invalid("--seed", &format!("invalid _non_negative value: '{text}'")))?;
	u64::try_from(number).map_err(|_| invalid("--seed", "must be >= 0"))
}

fn parse_simulate(text: &str) -> Result<usize, UsageError> {
	let number: i128 =
		text.parse().map_err(|_| invalid("--simulate", &format!("invalid _positive value: '{text}'")))?;
	match usize::try_from(number) {
		Ok(runs) if runs > 0 => Ok(runs),
		_ => Err(invalid("--simulate", "must be > 0")),
	}
}

pub fn parse_cli<S: AsRef<str>>(args: &[S]) -> Result<CliResult, UsageError> {
	let mut options = CliOptions::default();
	let mut unrecognized: Vec<String> = Vec::new();
	let mut index = 0;
	while index < args.len() {
		let arg = args[index].as_ref();
		index += 1;
		let (flag, inline_value) = match arg.split_once('=') {
			Some((flag, value)) if flag.starts_with("--") => (flag, Some(value.to_owned())),
			_ => (arg, None),
		};
		match flag {
			"-h" | "--help" => return Ok(CliResult::Exit(help_text())),
			"--version" => return Ok(CliResult::Exit(version_text())),
			"--no-anim" => options.no_anim = true,
			_ if VALUE_FLAGS.contains(&flag) => {
				let value = match inline_value {
					Some(value) => value,
					None if index < args.len() && !looks_like_flag(args[index].as_ref()) => {
						index += 1;
						args[index - 1].as_ref().to_owned()
					}
					None => return Err(invalid(flag, "expected one argument")),
				};
				match flag {
					"--seed" => options.seed = Some(parse_seed(&value)?),
					"--lang" => {
						if !SUPPORTED_LOCALES.contains(&value.as_str()) {
							return Err(invalid(
								"--lang",
								&format!("invalid choice: '{value}' (choose from 'en', 'pt-BR')"),
							));
						}
						options.lang = Some(value);
					}
					"--data-dir" => options.data_dir = Some(value),
					"--simulate" => options.simulate = Some(parse_simulate(&value)?),
					"--vocation" => options.vocation = Some(value),
					_ => options.difficulty = Some(value),
				}
			}
			_ => unrecognized.push(arg.to_owned()),
		}
	}
	if !unrecognized.is_empty() {
		return Err(UsageError(format!("unrecognized arguments: {}", unrecognized.join(" "))));
	}
	Ok(CliResult::Run(options))
}

#[cfg(test)]
mod tests {
	use super::*;

	fn options(args: &[&str]) -> CliOptions {
		match parse_cli(args).unwrap() {
			CliResult::Run(options) => options,
			CliResult::Exit(text) => panic!("unexpected exit: {text}"),
		}
	}

	fn error(args: &[&str]) -> String {
		parse_cli(args).unwrap_err().0
	}

	#[test]
	fn parse_args_defaults_and_validation() {
		let defaults = options(&[]);
		assert_eq!(defaults.seed, None);
		assert!(!defaults.no_anim);
		let parsed = options(&["--seed", "42", "--lang", "pt-BR", "--no-anim", "--data-dir", "x"]);
		assert_eq!(
			(parsed.seed, parsed.lang.as_deref(), parsed.no_anim, parsed.data_dir.as_deref()),
			(Some(42), Some("pt-BR"), true, Some("x"))
		);
		let simulator = options(&["--simulate=3", "--vocation", "mage", "--difficulty=hard"]);
		assert_eq!(simulator.simulate, Some(3));
		assert_eq!(simulator.vocation.as_deref(), Some("mage"));
		assert_eq!(simulator.difficulty.as_deref(), Some("hard"));
		assert_eq!(error(&["--seed", "-1"]), "argument --seed: must be >= 0");
		assert_eq!(error(&["--seed", "--no-anim"]), "argument --seed: expected one argument");
		assert_eq!(error(&["--seed=-1"]), "argument --seed: must be >= 0");
		assert_eq!(error(&["--seed", "abc"]), "argument --seed: invalid _non_negative value: 'abc'");
		assert_eq!(error(&["--simulate", "0"]), "argument --simulate: must be > 0");
		assert_eq!(error(&["--simulate", "x"]), "argument --simulate: invalid _positive value: 'x'");
		assert_eq!(error(&["--simulate"]), "argument --simulate: expected one argument");
		assert_eq!(error(&["--lang", "fr"]), "argument --lang: invalid choice: 'fr' (choose from 'en', 'pt-BR')");
		assert_eq!(error(&["--foo", "bar"]), "unrecognized arguments: --foo bar");
	}

	#[test]
	fn help_and_version_exit() {
		assert_eq!(parse_cli(&["--version"]).unwrap(), CliResult::Exit(format!("rpg {VERSION} (rust)")));
		match parse_cli(&["-h"]).unwrap() {
			CliResult::Exit(text) => assert!(text.contains("--lang {en,pt-BR}     override the saved language")),
			CliResult::Run(_) => panic!("help must exit"),
		}
		assert!(UsageError("x".into()).to_string().ends_with("rpg: error: x"));
	}
}
