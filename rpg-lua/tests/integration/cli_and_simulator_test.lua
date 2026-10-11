local simulator = require("rpg.application.simulator")
local i18n = require("rpg.infrastructure.i18n")
local main_run = require("rpg.main_run")
local cli = require("rpg.presentation.cli")
local helpers = require("support.helpers")
local T = require("support.test")

local GROUP = "integration/cli_and_simulator"

--- A stream that collects what is written to it (the `capsys` of the reference tests).
local function capture()
	local stream = { parts = {} }
	function stream:write(...)
		for _, part in ipairs({ ... }) do
			self.parts[#self.parts + 1] = tostring(part)
		end
		return self
	end
	function stream:text()
		return table.concat(self.parts)
	end
	return stream
end

T.test(GROUP, "simulate summary", function()
	local data = helpers.data()
	local summary = simulator.simulate(data, "warrior", "normal", 3, 10)
	T.eq(summary.runs, 3)
	T.ok(0 <= summary.wins and summary.wins <= 3)
	T.eq(summary.win_rate_pct, summary.wins * 100 // 3)
	T.ok(1 <= summary.min_round)
	T.ok(summary.min_round <= summary.median_round)
	T.ok(summary.median_round <= summary.max_round)
	local deaths = 0
	for _, killer in ipairs(summary.top_killers) do
		deaths = deaths + killer[2]
	end
	T.ok(deaths <= 3)
	T.raises("ValueError", function()
		simulator.simulate(data, "warrior", "normal", 0)
	end, "positive")
end)

T.test(GROUP, "main simulate prints report", function()
	local out, err = capture(), capture()
	local arguments = { "--simulate", "1", "--vocation", "mage", "--difficulty", "hard", "--seed", "5" }
	T.eq(main_run.run(arguments, out, err), 0)
	local output = out:text()
	T.contains(output, "median")
	T.contains(output, "win %")
	T.contains(output, "mage")
	T.eq(err:text(), "")
end)

T.test(GROUP, "simulator counts won runs", function()
	local data = helpers.data()
	local summary = simulator.simulate(data, "archer", "easy", 2, 2002)
	T.ok(summary.wins >= 1)
	T.eq(summary.max_round, data.balance.final_round)
end)

T.test(GROUP, "main simulate rejects unknown vocation", function()
	local out, err = capture(), capture()
	T.eq(main_run.run({ "--simulate", "1", "--vocation", "knight" }, out, err), 2)
	T.contains(err:text(), "invalid run config")
end)

T.test(GROUP, "parse args defaults and validation", function()
	local options = cli.parse_args({})
	T.eq(options.seed, nil)
	T.eq(options.no_anim, false)
	options = cli.parse_args({ "--seed", "42", "--lang", "pt-BR", "--no-anim", "--data-dir", "x" })
	T.eq({ options.seed, options.lang, options.no_anim, options.data_dir }, { 42, "pt-BR", true, "x" })
	-- argparse exits where this port raises a UsageError (`main_run.run` turns it into exit code 2).
	for _, arguments in ipairs({ { "--seed", "-1" }, { "--simulate", "0" }, { "--lang", "fr" } }) do
		T.raises("UsageError", function()
			cli.parse_args(arguments)
		end)
		T.eq(main_run.run(arguments, capture(), capture()), 2)
	end
end)

T.test(GROUP, "translator", function()
	local shared_dir = helpers.shared_dir()
	local english = i18n.Translator.new(shared_dir)
	local portuguese = i18n.Translator.new(shared_dir, "pt-BR")
	T.eq(english:t("event.gold_looted", { amount = 5 }), "You looted 5 gold.")
	T.eq(portuguese:t("event.gold_looted", { amount = 5 }), "Você saqueou 5 de ouro.")
	T.eq(english:t("missing.key"), "missing.key")
	T.eq(english:t("event.gold_looted"), "You looted {amount} gold.")
	T.ok(portuguese:has("menu.quit"))
	T.raises("ValueError", function()
		i18n.Translator.new(shared_dir, "fr")
	end, "unsupported")
end)
