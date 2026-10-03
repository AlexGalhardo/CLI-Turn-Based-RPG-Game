//! Entry point: `rpg-rust [flags]` → simulator report or the full-screen TUI.

use std::io::{self, Write};
use std::process::ExitCode;
use std::rc::Rc;

use rpg::application::simulator::simulate;
use rpg::assets::SharedFs;
use rpg::domain::definitions::GameData;
use rpg::infrastructure::data_loader::load_game_data;
use rpg::presentation::cli::{CliOptions, CliResult, parse_cli};
use rpg::presentation::simulator_report::render_report;
use rpg::presentation::tui::app::run_tui;

fn main() -> ExitCode {
	let args: Vec<String> = std::env::args().skip(1).collect();
	let code = run(&args, &mut io::stdout(), &mut io::stderr());
	ExitCode::from(code)
}

/// `main` without process exit, so it can be tested. A failed write to stdout/stderr has no better place to be
/// reported, hence the ignored results.
fn run(args: &[String], stdout: &mut dyn Write, stderr: &mut dyn Write) -> u8 {
	let options = match parse_cli(args) {
		Ok(CliResult::Exit(text)) => {
			let _ = writeln!(stdout, "{text}");
			return 0;
		}
		Ok(CliResult::Run(options)) => options,
		Err(error) => {
			let _ = writeln!(stderr, "{error}");
			return 2;
		}
	};
	let shared = SharedFs::embedded();
	let data = match load_game_data(&shared) {
		Ok(data) => Rc::new(data),
		Err(error) => {
			let _ = writeln!(stderr, "rpg: {error}");
			return 1;
		}
	};
	if options.simulate.is_some() {
		return run_simulator(&data, &options, stdout, stderr);
	}
	run_game(data, shared, &options, stderr)
}

fn run_simulator(data: &Rc<GameData>, options: &CliOptions, stdout: &mut dyn Write, stderr: &mut dyn Write) -> u8 {
	let vocations: Vec<String> = match &options.vocation {
		Some(vocation) => vec![vocation.clone()],
		None => data.vocations.iter().map(|vocation| vocation.id.clone()).collect(),
	};
	let difficulties: Vec<String> = match &options.difficulty {
		Some(difficulty) => vec![difficulty.clone()],
		None => data.balance.difficulties.iter().map(|difficulty| difficulty.id.clone()).collect(),
	};
	let runs = options.simulate.unwrap_or(1);
	// Python's `options.seed or 1`: a seed of 0 also falls back to 1.
	let base_seed = options.seed.filter(|&seed| seed != 0).unwrap_or(1);
	let mut summaries = Vec::new();
	for vocation in &vocations {
		for difficulty in &difficulties {
			match simulate(data, vocation, difficulty, runs, base_seed) {
				Ok(summary) => summaries.push(summary),
				Err(error) => {
					let _ = writeln!(stderr, "error: {error}");
					return 2;
				}
			}
		}
	}
	let _ = writeln!(stdout, "{}", render_report(&summaries, data));
	0
}

fn run_game(data: Rc<GameData>, shared: Rc<SharedFs>, options: &CliOptions, stderr: &mut dyn Write) -> u8 {
	match run_tui(data, shared, options) {
		Ok(()) => 0,
		Err(error) => {
			let _ = writeln!(stderr, "rpg: {error}");
			1
		}
	}
}

#[cfg(test)]
mod tests {
	use super::run;

	fn run_text(args: &[&str]) -> (u8, String, String) {
		let args: Vec<String> = args.iter().map(|arg| (*arg).to_owned()).collect();
		let (mut stdout, mut stderr) = (Vec::new(), Vec::new());
		let code = run(&args, &mut stdout, &mut stderr);
		(code, String::from_utf8(stdout).unwrap(), String::from_utf8(stderr).unwrap())
	}

	#[test]
	fn main_simulate_prints_report() {
		let (code, stdout, _) =
			run_text(&["--simulate", "1", "--vocation", "mage", "--difficulty", "hard", "--seed", "5"]);
		assert_eq!(code, 0);
		assert!(stdout.contains("median"));
		assert!(stdout.contains("mage"));
	}

	#[test]
	fn main_simulate_rejects_unknown_vocation() {
		let (code, _, stderr) = run_text(&["--simulate", "1", "--vocation", "knight"]);
		assert_eq!(code, 2);
		assert!(stderr.contains("invalid run config"), "{stderr}");
	}

	#[test]
	fn usage_errors_exit_with_2_and_help_exits_with_0() {
		let (code, _, stderr) = run_text(&["--lang", "fr"]);
		assert_eq!(code, 2);
		assert!(stderr.starts_with("usage: rpg"));
		assert!(stderr.contains("rpg: error: argument --lang: invalid choice: 'fr'"));
		let (code, stdout, _) = run_text(&["--version"]);
		assert_eq!(code, 0);
		assert!(stdout.contains("(rust)"));
		let (code, stdout, _) = run_text(&["--help"]);
		assert_eq!(code, 0);
		assert!(stdout.contains("Endless turn-based RPG for the terminal."));
	}
}
