defmodule Rpg.Golden.GoldenFilesTest do
  @moduledoc "Replays every shared/golden scenario; the Python, TypeScript and Go suites run the same files."
  use ExUnit.Case, async: true

  alias Rpg.Application.{Commands, GameEngine, GreedyBot, RunConfig, RunState}
  alias Rpg.Test.Helpers

  @scenario_files Helpers.golden_dir()
                  |> Path.join("*.json")
                  |> Path.wildcard()
                  |> Enum.reject(&(Path.basename(&1) == "prng.json"))
                  |> Enum.sort()

  defp final_state(%GameEngine{state: state} = engine) do
    player = state.player

    %{
      "phase" => Atom.to_string(state.phase),
      "round" => state.round,
      "turn" => state.turn,
      "level" => player.level,
      "xp" => player.xp,
      "magicLevel" => player.magic_level,
      "hp" => player.hp,
      "mp" => player.mp,
      "gold" => player.gold,
      "rngState" => GameEngine.rng_state(engine),
      "nextItemUid" => state.next_item_uid,
      "stats" => Rpg.Application.RunStatistics.to_map(state.stats)
    }
  end

  test "golden files exist" do
    assert length(@scenario_files) >= 11
  end

  for path <- @scenario_files do
    @path path
    test "replay matches #{Path.basename(path, ".json")}" do
      golden = Helpers.read_json(@path)
      data = Helpers.data()
      {engine, first} = GameEngine.new_run(data, RunConfig.from_map(golden["config"]), golden["seed"])
      [expected_first | expected_events] = golden["events"]
      assert first == expected_first

      engine =
        golden["commands"]
        |> Enum.zip(expected_events)
        |> Enum.with_index(1)
        |> Enum.reduce(engine, fn {{raw_command, expected}, index}, engine ->
          {engine, events} = GameEngine.step(engine, Commands.from_map(raw_command))
          assert events == expected, "command ##{index}"
          engine
        end)

      assert final_state(engine) == golden["finalState"]
      assert RunState.to_map(engine.state) == golden["finalRun"]
      assert golden["finalRun"] |> RunState.from_map() |> RunState.to_map() == golden["finalRun"]
    end
  end

  for path <- Enum.filter(@scenario_files, &String.contains?(&1, "bot-full-run")) do
    @path path
    test "the bot issues the recorded commands in #{Path.basename(path, ".json")}" do
      golden = Helpers.read_json(@path)
      data = Helpers.data()
      {engine, _first} = GameEngine.new_run(data, RunConfig.from_map(golden["config"]), golden["seed"])

      engine =
        Enum.reduce(golden["commands"], engine, fn raw_command, engine ->
          command = GreedyBot.choose(data, engine.state)
          assert Commands.to_map(command) == raw_command
          {engine, _events} = GameEngine.step(engine, command)
          engine
        end)

      assert engine.state.phase == :game_over
    end
  end
end
