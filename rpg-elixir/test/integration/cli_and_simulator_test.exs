defmodule Rpg.Integration.CliAndSimulatorTest do
  use ExUnit.Case, async: true

  import ExUnit.CaptureIO

  alias Rpg.Application.Simulator
  alias Rpg.Application.Simulator.SimulationSummary
  alias Rpg.Infrastructure.I18n
  alias Rpg.Presentation.Cli
  alias Rpg.Presentation.Cli.CliOptions
  alias Rpg.Test.Helpers

  test "simulate summary" do
    summary = Simulator.simulate(Helpers.data(), "warrior", "normal", 3, 10)
    assert summary.runs == 3
    assert summary.wins in 0..3
    assert SimulationSummary.win_rate_pct(summary) == div(summary.wins * 100, 3)
    assert 1 <= summary.min_round and summary.min_round <= summary.median_round
    assert summary.median_round <= summary.max_round
    assert summary.top_killers |> Enum.map(&elem(&1, 1)) |> Enum.sum() <= 3
    assert_raise ArgumentError, ~r/positive/, fn -> Simulator.simulate(Helpers.data(), "warrior", "normal", 0) end
  end

  test "main --simulate prints a report" do
    output =
      capture_io(fn ->
        assert Rpg.Main.run(~w(--simulate 1 --vocation mage --difficulty hard --seed 5)) == 0
      end)

    assert output =~ "median"
    assert output =~ "win %"
    assert output =~ "mage"
  end

  test "the simulator counts won runs" do
    data = Helpers.data()
    summary = Simulator.simulate(data, "archer", "easy", 2, 2002)
    assert summary.wins >= 1
    assert summary.max_round == data.balance.final_round
  end

  test "main --simulate rejects an unknown vocation" do
    error = capture_io(:stderr, fn -> assert Rpg.Main.run(~w(--simulate 1 --vocation knight)) == 2 end)
    assert error =~ "invalid run config"
  end

  test "--version and --help" do
    assert capture_io(fn -> assert Rpg.Main.run(["--version"]) == 0 end) == "rpg #{Rpg.Version.version()} (elixir)\n"
    assert capture_io(fn -> assert Rpg.Main.run(["-h"]) == 0 end) =~ "usage: rpg"
    assert capture_io(fn -> assert Rpg.Main.run(["--help"]) == 0 end) =~ "--simulate N"
  end

  test "usage errors exit with code 2" do
    for argv <- [~w(--seed -1), ~w(--simulate 0), ~w(--lang fr), ~w(--bogus), ~w(extra), ~w(--seed abc)] do
      error = capture_io(:stderr, fn -> assert Rpg.Main.run(argv) == 2 end)
      assert error =~ "rpg: error:"
    end
  end

  test "parse args defaults and validation" do
    assert {:ok, %CliOptions{seed: nil, no_anim: false, simulate: nil}} = Cli.parse_args([])

    assert {:ok, %CliOptions{seed: 42, lang: "pt-BR", no_anim: true, data_dir: "x"}} =
             Cli.parse_args(~w(--seed 42 --lang pt-BR --no-anim --data-dir x))

    assert {:ok, %CliOptions{simulate: 3, vocation: "mage", difficulty: "easy"}} =
             Cli.parse_args(~w(--simulate=3 --vocation mage --difficulty easy))

    assert {:error, "argument --seed: must be >= 0"} = Cli.parse_args(~w(--seed -1))
    assert {:error, "argument --simulate: must be > 0"} = Cli.parse_args(~w(--simulate 0))
    assert {:error, "argument --lang: invalid choice: 'fr' (choose from 'en', 'pt-BR')"} = Cli.parse_args(~w(--lang fr))
    assert {:error, _} = Cli.parse_args(["--seed"])
  end

  test "translator" do
    english = I18n.new(:embedded)
    portuguese = I18n.new(:embedded, "pt-BR")
    assert I18n.t(english, "event.gold_looted", amount: 5) == "You looted 5 gold."
    assert I18n.t(portuguese, "event.gold_looted", amount: 5) == "Você saqueou 5 de ouro."
    assert I18n.t(english, "missing.key") == "missing.key"
    assert I18n.t(english, "event.gold_looted") == "You looted {amount} gold."
    assert I18n.has?(portuguese, "menu.quit")
    refute I18n.has?(portuguese, "missing.key")
    assert_raise ArgumentError, ~r/unsupported/, fn -> I18n.new(:embedded, "fr") end
  end

  test "both locales have the same keys" do
    {:ok, en} = Rpg.Infrastructure.Assets.read(:embedded, "i18n/en.json")
    {:ok, pt} = Rpg.Infrastructure.Assets.read(:embedded, "i18n/pt-BR.json")
    assert en |> JSON.decode!() |> Map.keys() |> Enum.sort() == pt |> JSON.decode!() |> Map.keys() |> Enum.sort()
  end

  test "the simulator report matches the Python layout" do
    data = Helpers.data()
    summaries = [Simulator.simulate(data, "archer", "easy", 2, 3)]
    [header, separator, row] = String.split(Rpg.Presentation.SimulatorReport.render_report(summaries, data), "\n")
    assert String.starts_with?(header, "vocation  difficulty  runs")
    assert String.starts_with?(separator, "--------  ----------  ----")
    assert String.starts_with?(row, "archer    easy        2")
  end
end
