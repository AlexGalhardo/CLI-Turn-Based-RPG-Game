//! Simulator, report and the real binary (port of `tests/integration/test_cli_and_simulator.py`).

mod common;

use std::process::Command;

use common::TempDir;
use rpg::application::run_state::RunConfig;
use rpg::application::simulator::{play_one, simulate};
use rpg::presentation::simulator_report::render_report;

#[test]
fn simulate_summary() {
	let data = common::data();
	let summary = simulate(&data, "warrior", "normal", 3, 10).unwrap();
	assert_eq!(summary.runs, 3);
	assert!(1 <= summary.min_round && summary.min_round <= summary.median_round);
	assert!(summary.median_round <= summary.max_round);
	assert!(summary.top_killers.iter().map(|(_, count)| count).sum::<i64>() <= 3);
	assert!(simulate(&data, "warrior", "normal", 0, 1).unwrap_err().contains("positive"));
	assert!(simulate(&data, "knight", "normal", 1, 1).unwrap_err().contains("invalid run config"));
}

#[test]
fn play_one_reports_the_death() {
	let data = common::data();
	let result = play_one(&data, RunConfig::new("Bot", "archer", "hard"), 4).unwrap();
	assert!(result.round >= 1);
	assert!(data.find_creature(&result.death_cause).is_some());
}

#[test]
fn report_aligns_columns_like_the_reference() {
	let data = common::data();
	let summaries =
		vec![simulate(&data, "mage", "easy", 2, 3).unwrap(), simulate(&data, "warrior", "hard", 2, 3).unwrap()];
	let report = render_report(&summaries, &data);
	let lines: Vec<&str> = report.lines().collect();
	assert_eq!(lines.len(), 4);
	assert!(lines[0].starts_with("vocation  difficulty  runs  min"));
	assert!(lines[1].starts_with("--------  ----------  ----  ---"));
	assert!(lines[2].starts_with("mage      easy        2"));
	assert!(lines.iter().all(|line| !line.ends_with(' ')));
}

fn binary() -> Command {
	let mut command = Command::new(env!("CARGO_BIN_EXE_rpg-rust"));
	command.env("RPG_NO_ANIM", "1");
	command
}

#[test]
fn binary_prints_version_help_and_simulator_report() {
	let dir = TempDir::new();
	let output = binary().arg("--version").env("RPG_DATA_DIR", dir.path()).output().unwrap();
	assert!(output.status.success());
	assert_eq!(String::from_utf8_lossy(&output.stdout).trim(), format!("rpg {} (rust)", rpg::version::VERSION));

	let output = binary().args(["--simulate", "2", "--seed", "42", "--vocation", "mage"]).output().unwrap();
	assert!(output.status.success());
	let report = String::from_utf8_lossy(&output.stdout).into_owned();
	assert_eq!(report.lines().count(), 5, "{report}");

	let output = binary().args(["--simulate", "0"]).output().unwrap();
	assert_eq!(output.status.code(), Some(2));
	assert!(String::from_utf8_lossy(&output.stderr).contains("argument --simulate: must be > 0"));

	let output = binary().args(["--simulate", "1", "--difficulty", "nightmare"]).output().unwrap();
	assert_eq!(output.status.code(), Some(2));
	assert!(String::from_utf8_lossy(&output.stderr).contains("error: invalid run config: 'nightmare'"));
	assert!(std::fs::read_dir(dir.path()).unwrap().next().is_none(), "the simulator writes no files");
}

#[test]
fn seed_zero_falls_back_to_one_like_the_reference() {
	let run = |seed: &str| {
		let output = binary()
			.args(["--simulate", "1", "--vocation", "archer", "--difficulty", "easy", "--seed", seed])
			.output()
			.unwrap();
		String::from_utf8_lossy(&output.stdout).into_owned()
	};
	assert_eq!(run("0"), run("1"));
}
