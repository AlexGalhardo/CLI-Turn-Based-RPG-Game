defmodule Rpg.Integration.VictoryAndMigrationsTest do
  @moduledoc "Victory phase through the session (save, resume, history, Hall of Fame) and schema 1 → 2 migrations."
  use ExUnit.Case, async: true

  alias Rpg.Application.Commands.{Attack, ContinueRun, EndRun, NextFight}
  alias Rpg.Application.GameSession.Repositories
  alias Rpg.Application.Ports.{HistoryRepository, ProfileRepository, SaveRepository}
  alias Rpg.Application.{GameSession, RunConfig, RunState}
  alias Rpg.Infrastructure.Repositories.{FileHistoryRepository, FileProfileRepository, FileSaveRepository}
  alias Rpg.Test.{FakeClock, Helpers}

  @moduletag :tmp_dir
  @max_swings 200

  defp repositories(dir) do
    %Repositories{
      saves: FileSaveRepository.new(dir),
      history: FileHistoryRepository.new(dir),
      profile: FileProfileRepository.new(dir)
    }
  end

  defp start(dir, config, seed, clock \\ FakeClock.new()) do
    GameSession.start(Helpers.data(), config, seed, repositories: repositories(dir), clock: clock, game_version: "1")
  end

  defp update_state(session, fun), do: %{session | engine: Helpers.update_state(session.engine, fun)}

  defp win_final_fight(dir) do
    clock = FakeClock.new()
    config = %RunConfig{name: "Vic", vocation_id: "warrior", difficulty_id: "easy", auto_equip: true}
    {session, _} = start(dir, config, 8, clock)
    final_round = Helpers.data().balance.final_round
    {session, _} = session |> update_state(&%{&1 | round: final_round - 1}) |> GameSession.step(%NextFight{})
    monster = GameSession.state(session).monster
    assert monster.creature_id == "ferumbras"
    assert monster.enemy_class == "boss"
    session = swing(session, @max_swings)
    assert GameSession.state(session).phase == :victory
    {session, repositories(dir), clock}
  end

  defp swing(session, 0), do: session

  defp swing(session, swings) do
    if GameSession.state(session).phase != :battle do
      session
    else
      session =
        update_state(session, fn state ->
          %{state | player: %{state.player | hp: 1_000_000}, monster: %{state.monster | hp: 1}}
        end)

      {session, _} = GameSession.step(session, %Attack{})
      swing(session, swings - 1)
    end
  end

  test "a victory is saved, resumed and ended as won", %{tmp_dir: dir} do
    {_session, repos, clock} = win_final_fight(dir)
    save = Helpers.read_json(Path.join(dir, "save.json"))
    assert save["run"]["phase"] == "victory"
    assert save["run"]["won"] == true

    resumed = GameSession.resume(Helpers.data(), repos, clock, "1")
    assert GameSession.state(resumed).phase == :victory
    {_session, result} = GameSession.step(resumed, %EndRun{})
    assert result.events == [%{"type" => "run_ended", "won" => true}]
    assert Map.has_key?(ProfileRepository.load(repos.profile).achievements, "conqueror")
    refute File.exists?(Path.join(dir, "save.json"))
    [record] = HistoryRepository.list(repos.history)
    assert record.won == true
    assert record.death_cause == ""
    assert hd(ProfileRepository.load(repos.profile).hall_of_fame).won == true
  end

  test "continuing after a victory keeps the run won", %{tmp_dir: dir} do
    {session, _, _} = win_final_fight(dir)
    final_round = Helpers.data().balance.final_round
    {session, result} = GameSession.step(session, %ContinueRun{})
    assert result.events == [%{"type" => "merchant_entered", "round" => final_round}]
    assert GameSession.state(session).phase == :merchant
    assert GameSession.state(session).won == true
    {session, _} = GameSession.step(session, %NextFight{})
    assert GameSession.state(session).round == final_round + 1
  end

  # Turns a current save into what version 1 wrote: no M8 fields and the old `epic` rarity.
  defp v1(document) do
    run = put_in(document["run"]["player"]["equipment"]["weapon"]["rarity"], "epic")["run"]

    stats =
      run["stats"]
      |> Map.drop(["itemsAutoEquipped", "elitesKilled", "potionsDropped"])
      |> Map.put("itemsDropped", %{"epic" => 2, "legendary" => 1})
      |> Map.put("droppedItems", [%{"itemId" => "sword", "rarity" => "epic", "round" => 3}])

    run =
      run
      |> Map.update!("config", &Map.delete(&1, "autoEquip"))
      |> Map.delete("won")
      |> Map.put("stats", stats)

    %{document | "schemaVersion" => 1, "run" => run}
  end

  test "a v1 save is migrated", %{tmp_dir: dir} do
    start(dir, %RunConfig{name: "Old", vocation_id: "warrior", difficulty_id: "normal"}, 4)
    path = Path.join(dir, "save.json")
    File.write!(path, JSON.encode!(v1(Helpers.read_json(path))))

    run = SaveRepository.load(FileSaveRepository.new(dir)).run
    assert run.config.auto_equip == false
    assert run.won == false
    assert run.player.equipment["weapon"].rarity == "legendary"
    assert run.stats.items_dropped == %{"legendary" => 3}
    assert hd(run.stats.dropped_items).rarity == "legendary"
    assert run.stats.elites_killed == 0
  end

  test "a v1 monster gets its class from isBoss", %{tmp_dir: dir} do
    {session, _} = start(dir, %RunConfig{name: "Old", vocation_id: "mage", difficulty_id: "normal"}, 4)
    {session, _} = session |> update_state(&%{&1 | round: 9}) |> GameSession.step(%NextFight{})
    path = Path.join(dir, "save.json")
    document = Helpers.read_json(path)
    monster = RunState.to_map(GameSession.state(session))["monster"]
    old = v1(put_in(document["run"]["monster"], monster))
    old = update_in(old["run"]["monster"], &Map.delete(&1, "enemyClass"))
    File.write!(path, JSON.encode!(old))
    assert SaveRepository.load(FileSaveRepository.new(dir)).run.monster.enemy_class == "boss"
  end

  test "v1 history and profile are migrated", %{tmp_dir: dir} do
    won_dir = Path.join(dir, "won")
    old_dir = Path.join(dir, "old")
    {session, _, _} = win_final_fight(won_dir)
    GameSession.step(session, %EndRun{})
    [record_path] = Path.wildcard(Path.join([won_dir, "history", "*.json"]))
    record = Helpers.read_json(record_path)

    record =
      record
      |> Map.put("schemaVersion", 1)
      |> Map.delete("won")
      |> Map.update!("stats", &Map.drop(&1, ["itemsAutoEquipped", "elitesKilled", "potionsDropped"]))

    history_dir = Path.join(old_dir, "history")
    File.mkdir_p!(history_dir)
    File.write!(Path.join(history_dir, Path.basename(record_path)), JSON.encode!(record))
    assert hd(HistoryRepository.list(FileHistoryRepository.new(old_dir))).won == false

    profile = Helpers.read_json(Path.join(won_dir, "profile.json"))

    profile =
      profile
      |> Map.put("schemaVersion", 1)
      |> Map.update!("hallOfFame", fn hall -> Enum.map(hall, &Map.delete(&1, "won")) end)

    File.write!(Path.join(old_dir, "profile.json"), JSON.encode!(profile))
    assert hd(ProfileRepository.load(FileProfileRepository.new(old_dir)).hall_of_fame).won == false
  end
end
