//! Headless balance simulator: the bot plays many runs and we aggregate how often it wins and how far it gets.

use std::collections::BTreeMap;
use std::rc::Rc;

use crate::application::bot::GreedyBot;
use crate::application::engine::GameEngine;
use crate::application::run_state::RunConfig;
use crate::domain::definitions::GameData;
use crate::domain::enums::Phase;

pub const MAX_STEPS_PER_RUN: usize = 200_000;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RunResult {
	pub round: i64,
	pub level: i64,
	pub death_cause: String,
	pub won: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SimulationSummary {
	pub vocation: String,
	pub difficulty: String,
	pub runs: usize,
	pub wins: usize,
	pub min_round: i64,
	pub p10_round: i64,
	pub median_round: i64,
	pub p90_round: i64,
	pub max_round: i64,
	pub mean_level: i64,
	pub top_killers: Vec<(String, i64)>,
}

impl SimulationSummary {
	pub fn win_rate_pct(&self) -> usize {
		self.wins * 100 / self.runs
	}
}

pub fn play_one(data: &Rc<GameData>, config: RunConfig, seed: u64) -> Result<RunResult, String> {
	let (mut engine, _) = GameEngine::new_run(Rc::clone(data), config, seed).map_err(|error| error.to_string())?;
	let bot = GreedyBot::new(data);
	let mut finished = false;
	for _ in 0..MAX_STEPS_PER_RUN {
		if engine.state().phase == Phase::GameOver {
			finished = true;
			break;
		}
		let command = bot.choose(engine.state());
		engine.step(&command);
	}
	if !finished {
		return Err(format!("run did not finish (seed {seed})"));
	}
	let state = engine.state();
	Ok(RunResult {
		round: state.round,
		level: state.player.level,
		death_cause: state.death_cause.clone().unwrap_or_default(),
		won: state.won,
	})
}

fn percentile(sorted_values: &[i64], percent: usize) -> i64 {
	sorted_values[(sorted_values.len() - 1).min(sorted_values.len() * percent / 100)]
}

pub fn simulate(
	data: &Rc<GameData>,
	vocation: &str,
	difficulty: &str,
	runs: usize,
	base_seed: u64,
) -> Result<SimulationSummary, String> {
	if runs == 0 {
		return Err("runs must be positive".to_owned());
	}
	let results = (0..runs)
		.map(|offset| play_one(data, RunConfig::new("Bot", vocation, difficulty), base_seed + offset as u64))
		.collect::<Result<Vec<_>, _>>()?;
	let mut rounds: Vec<i64> = results.iter().map(|result| result.round).collect();
	rounds.sort_unstable();
	let mut killers: BTreeMap<&str, i64> = BTreeMap::new();
	for result in results.iter().filter(|result| !result.death_cause.is_empty()) {
		*killers.entry(&result.death_cause).or_insert(0) += 1;
	}
	let mut top_killers: Vec<(String, i64)> = killers.into_iter().map(|(id, count)| (id.to_owned(), count)).collect();
	top_killers.sort_by(|a, b| b.1.cmp(&a.1).then_with(|| a.0.cmp(&b.0)));
	top_killers.truncate(3);
	Ok(SimulationSummary {
		vocation: vocation.to_owned(),
		difficulty: difficulty.to_owned(),
		runs,
		wins: results.iter().filter(|result| result.won).count(),
		min_round: rounds[0],
		p10_round: percentile(&rounds, 10),
		median_round: percentile(&rounds, 50),
		p90_round: percentile(&rounds, 90),
		max_round: rounds[rounds.len() - 1],
		mean_level: results.iter().map(|result| result.level).sum::<i64>() / runs as i64,
		top_killers,
	})
}
