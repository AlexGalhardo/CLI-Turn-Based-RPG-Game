//! The plain-text table printed by `--simulate` (byte-identical to the reference report).

use crate::application::simulator::SimulationSummary;
use crate::domain::definitions::GameData;

pub const HEADER: [&str; 12] =
	["vocation", "difficulty", "runs", "wins", "win %", "min", "p10", "median", "p90", "max", "avg lvl", "top killers"];

fn width(text: &str) -> usize {
	text.chars().count()
}

pub fn render_report(summaries: &[SimulationSummary], data: &GameData) -> String {
	let mut rows: Vec<Vec<String>> = vec![HEADER.iter().map(|cell| (*cell).to_owned()).collect()];
	for summary in summaries {
		let killers: Vec<String> = summary
			.top_killers
			.iter()
			.map(|(creature_id, count)| format!("{} ({count})", data.creature(creature_id).name))
			.collect();
		rows.push(vec![
			summary.vocation.clone(),
			summary.difficulty.clone(),
			summary.runs.to_string(),
			summary.wins.to_string(),
			format!("{}%", summary.win_rate_pct()),
			summary.min_round.to_string(),
			summary.p10_round.to_string(),
			summary.median_round.to_string(),
			summary.p90_round.to_string(),
			summary.max_round.to_string(),
			summary.mean_level.to_string(),
			killers.join(", "),
		]);
	}
	let widths: Vec<usize> =
		(0..HEADER.len()).map(|column| rows.iter().map(|row| width(&row[column])).max().unwrap_or(0)).collect();
	let mut lines: Vec<String> = rows
		.iter()
		.map(|row| {
			let cells: Vec<String> = row
				.iter()
				.enumerate()
				.map(|(column, cell)| format!("{cell}{}", " ".repeat(widths[column] - width(cell))))
				.collect();
			cells.join("  ").trim_end().to_owned()
		})
		.collect();
	let separator: Vec<String> = widths.iter().map(|&column_width| "-".repeat(column_width)).collect();
	lines.insert(1, separator.join("  "));
	lines.join("\n")
}
