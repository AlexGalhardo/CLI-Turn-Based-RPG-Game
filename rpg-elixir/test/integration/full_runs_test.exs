defmodule Rpg.Integration.FullRunsTest do
  @moduledoc "Whole runs driven by the bot: the engine must always terminate, be deterministic and survive save/restore."
  use ExUnit.Case, async: true

  alias Rpg.Application.{GameEngine, GreedyBot, RunConfig, RunState, RunStatistics}
  alias Rpg.Test.Helpers

  @max_steps 50_000

  defp config(vocation, difficulty), do: %RunConfig{name: "Bot", vocation_id: vocation, difficulty_id: difficulty}

  defp play_to_death(engine), do: play_to_death(engine, [], @max_steps)

  defp play_to_death(_engine, _log, 0), do: flunk("run did not finish")
  defp play_to_death(%GameEngine{state: %{phase: :game_over}} = engine, log, _left), do: {engine, Enum.reverse(log)}

  defp play_to_death(engine, log, left) do
    {engine, events} = GameEngine.step(engine, GreedyBot.choose(engine.data, engine.state))
    assert Enum.all?(events, &(&1["type"] != "error")), inspect(events)
    play_to_death(engine, [events | log], left - 1)
  end

  for vocation <- ["warrior", "archer", "mage"], difficulty <- ["easy", "normal", "hard"] do
    test "the bot plays #{vocation} #{difficulty} until death" do
      {engine, _} = GameEngine.new_run(Helpers.data(), config(unquote(vocation), unquote(difficulty)), 1234)
      {engine, log} = play_to_death(engine)
      state = engine.state
      assert state.phase == :game_over
      assert state.round >= 1
      assert state.death_cause not in [nil, ""]
      assert List.last(List.last(log))["type"] == "player_died"
      assert state.stats.kills |> Map.values() |> Enum.sum() == state.round - 1
      assert state.stats.damage_dealt > 0
    end
  end

  test "same seed, same events" do
    logs =
      for _ <- 1..2 do
        {engine, first} = GameEngine.new_run(Helpers.data(), config("archer", "normal"), 777)
        {_engine, log} = play_to_death(engine)
        [first | log]
      end

    assert Enum.at(logs, 0) == Enum.at(logs, 1)
  end

  test "different seeds diverge" do
    finals =
      for seed <- [1, 2, 3], into: MapSet.new() do
        {engine, _} = GameEngine.new_run(Helpers.data(), config("warrior", "normal"), seed)
        {engine, _log} = play_to_death(engine)
        RunStatistics.to_map(engine.state.stats)
      end

    assert MapSet.size(finals) > 1
  end

  test "restoring mid-run continues identically" do
    data = Helpers.data()
    {reference, _} = GameEngine.new_run(data, config("mage", "hard"), 99)
    {_reference, reference_log} = play_to_death(reference)
    {engine, _} = GameEngine.new_run(data, config("mage", "hard"), 99)
    assert restore_loop(data, engine, []) == reference_log
  end

  defp restore_loop(_data, %GameEngine{state: %{phase: :game_over}}, log), do: Enum.reverse(log)

  defp restore_loop(data, engine, log) do
    engine =
      if engine.state.phase == :merchant and rem(engine.state.round, 3) == 0 do
        snapshot = engine.state |> RunState.to_map() |> JSON.encode!() |> JSON.decode!()
        GameEngine.restore(data, RunState.from_map(snapshot), GameEngine.rng_state(engine))
      else
        engine
      end

    {engine, events} = GameEngine.step(engine, GreedyBot.choose(data, engine.state))
    restore_loop(data, engine, [events | log])
  end

  test "new run rejects an invalid config" do
    assert_raise ArgumentError, ~r/invalid run config/, fn ->
      GameEngine.new_run(Helpers.data(), %RunConfig{name: "X", vocation_id: "knight", difficulty_id: "normal"}, 1)
    end

    assert_raise ArgumentError, ~r/invalid run config/, fn ->
      GameEngine.new_run(Helpers.data(), %RunConfig{name: "X", vocation_id: "mage", difficulty_id: "nightmare"}, 1)
    end
  end
end
