local simulator_report = {}

local HEADER = {
	"vocation",
	"difficulty",
	"runs",
	"wins",
	"win %",
	"min",
	"p10",
	"median",
	"p90",
	"max",
	"avg lvl",
	"top killers",
}

local function width(text)
	return utf8.len(text) or #text
end

function simulator_report.render_report(summaries, data)
	local rows = { HEADER }
	for _, s in ipairs(summaries) do
		local killers = {}
		for index, killer in ipairs(s.top_killers) do
			killers[index] = string.format("%s (%d)", data:creature(killer[1]).name, killer[2])
		end
		rows[#rows + 1] = {
			s.vocation,
			s.difficulty,
			tostring(s.runs),
			tostring(s.wins),
			s.win_rate_pct .. "%",
			tostring(s.min_round),
			tostring(s.p10_round),
			tostring(s.median_round),
			tostring(s.p90_round),
			tostring(s.max_round),
			tostring(s.mean_level),
			table.concat(killers, ", "),
		}
	end
	local widths = {}
	for column = 1, #HEADER do
		widths[column] = 0
		for _, row in ipairs(rows) do
			widths[column] = math.max(widths[column], width(row[column]))
		end
	end
	local lines = {}
	for _, row in ipairs(rows) do
		local cells = {}
		for column, cell in ipairs(row) do
			cells[column] = cell .. string.rep(" ", widths[column] - width(cell))
		end
		lines[#lines + 1] = (table.concat(cells, "  "):gsub(" +$", ""))
	end
	local rule = {}
	for column, size in ipairs(widths) do
		rule[column] = string.rep("-", size)
	end
	table.insert(lines, 2, table.concat(rule, "  "))
	return table.concat(lines, "\n")
end

return simulator_report
