#include "presentation/simulator_report.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <vector>

#include "presentation/render.hpp"

namespace rpg::presentation {

namespace {

constexpr std::array<std::string_view, 12> report_header{
    "vocation", "difficulty", "runs", "wins", "win %", "min", "p10", "median", "p90", "max", "avg lvl", "top killers"};

std::string join(const std::vector<std::string>& parts, std::string_view separator) {
	std::string result;
	for (std::size_t i = 0; i < parts.size(); ++i) {
		if (i > 0) {
			result += separator;
		}
		result += parts[i];
	}
	return result;
}

std::string trim_right(std::string text) {
	text.erase(text.find_last_not_of(' ') + 1);
	return text;
}

} // namespace

std::string render_report(std::span<const application::SimulationSummary> summaries, const domain::GameData& data) {
	std::vector<std::vector<std::string>> rows{{report_header.begin(), report_header.end()}};
	for (const auto& summary : summaries) {
		std::vector<std::string> killers;
		for (const auto& [creature_id, count] : summary.top_killers) {
			killers.push_back(std::format("{} ({})", data.creature(creature_id).name, count));
		}
		rows.push_back({summary.vocation, summary.difficulty, std::to_string(summary.runs),
		    std::to_string(summary.wins), std::format("{}%", summary.win_rate_pct()), std::to_string(summary.min_round),
		    std::to_string(summary.p10_round), std::to_string(summary.median_round), std::to_string(summary.p90_round),
		    std::to_string(summary.max_round), std::to_string(summary.mean_level), join(killers, ", ")});
	}

	// Column widths in characters (creature names may contain non-ASCII letters).
	std::vector<std::size_t> widths(report_header.size(), 0);
	for (const auto& row : rows) {
		for (std::size_t i = 0; i < row.size(); ++i) {
			widths[i] = std::max(widths[i], utf8_length(row[i]));
		}
	}

	std::vector<std::string> lines;
	for (const auto& row : rows) {
		std::vector<std::string> cells;
		for (std::size_t i = 0; i < row.size(); ++i) {
			cells.push_back(row[i] + std::string(widths[i] - utf8_length(row[i]), ' '));
		}
		lines.push_back(trim_right(join(cells, "  ")));
	}
	std::vector<std::string> separator;
	for (const std::size_t width : widths) {
		separator.emplace_back(width, '-');
	}
	lines.insert(lines.begin() + 1, join(separator, "  "));
	return join(lines, "\n");
}

} // namespace rpg::presentation
