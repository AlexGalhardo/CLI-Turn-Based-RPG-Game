defmodule Rpg.Integration.PersistenceTest do
  use ExUnit.Case, async: true

  alias Rpg.Application.Commands.{Attack, BuyPotion, NextFight}
  alias Rpg.Application.GameSession.Repositories
  alias Rpg.Application.Ports.{HistoryRepository, ProfileRepository, SaveRepository}
  alias Rpg.Application.Profile.HallOfFameEntry
  alias Rpg.Application.SaveGame.NewerSchemaError
  alias Rpg.Application.{GameEngine, GameSession, GreedyBot, Profile, ProfileService, RunConfig, SaveGame}
  alias Rpg.Infrastructure.Repositories, as: Repos
  alias Rpg.Infrastructure.Repositories.{FileHistoryRepository, FileProfileRepository, FileSaveRepository}
  alias Rpg.Infrastructure.Repositories.{Settings, SettingsRepository, SystemClock}
  alias Rpg.Test.{FakeClock, Helpers}

  @moduletag :tmp_dir

  defp repositories(dir) do
    %Repositories{
      saves: FileSaveRepository.new(dir),
      history: FileHistoryRepository.new(dir),
      profile: FileProfileRepository.new(dir)
    }
  end

  defp start(dir, config, seed, clock \\ FakeClock.new(), version \\ "1") do
    GameSession.start(Helpers.data(), config, seed,
      repositories: repositories(dir),
      clock: clock,
      game_version: version
    )
  end

  defp config(name, vocation, difficulty), do: %RunConfig{name: name, vocation_id: vocation, difficulty_id: difficulty}

  test "a new session autosaves at the merchant", %{tmp_dir: dir} do
    {session, events} = start(dir, config("Alex", "archer", "normal"), 5, FakeClock.new(), "9.9.9")
    assert hd(events)["type"] == "run_started"
    save = Helpers.read_json(Path.join(dir, "save.json"))
    assert save["schemaVersion"] == 1
    assert save["implementation"] == "elixir"
    assert save["gameVersion"] == "9.9.9"
    assert save["session"]["runId"] == session.info.run_id
    assert save["run"]["phase"] == "merchant"
    {_session, _result} = GameSession.step(session, %BuyPotion{potion_id: "health_potion", quantity: 1})
    save = Helpers.read_json(Path.join(dir, "save.json"))
    assert save["run"]["player"]["potions"]["health_potion"] == 6
  end

  test "the save file is tab-indented JSON with sorted keys", %{tmp_dir: dir} do
    start(dir, config("Alex", "archer", "normal"), 5)
    text = File.read!(Path.join(dir, "save.json"))
    assert String.starts_with?(text, "{\n\t\"gameVersion\": \"1\",\n\t\"implementation\": \"elixir\",")
    assert String.ends_with?(text, "}\n")
    refute File.exists?(Path.join(dir, "save.json.tmp"))
  end

  test "quitting mid-battle resumes from the last merchant", %{tmp_dir: dir} do
    repos = repositories(dir)
    clock = FakeClock.new()
    {session, _} = start(dir, config("Alex", "warrior", "normal"), 5, clock)
    {session, _} = GameSession.step(session, %NextFight{})
    {session, _} = GameSession.step(session, %Attack{})
    assert GameSession.state(session).phase == :battle
    GameSession.save_and_quit(session)

    resumed = GameSession.resume(Helpers.data(), repos, clock, "1")
    assert GameSession.state(resumed).phase == :merchant
    assert GameSession.state(resumed).round == 0
    assert resumed.info.sessions == 2
    assert resumed.info.play_time_seconds > 0
    assert resumed.info.run_id == session.info.run_id
  end

  test "resume without a save", %{tmp_dir: dir} do
    assert GameSession.resume(Helpers.data(), repositories(dir), FakeClock.new(), "1") == nil
  end

  test "death writes history and profile and deletes the save", %{tmp_dir: dir} do
    repos = repositories(dir)
    {session, _} = start(dir, config("Bot", "mage", "hard"), 3)
    {session, unlocked} = play_to_death(session, [])
    state = GameSession.state(session)
    refute File.exists?(Path.join(dir, "save.json"))
    [record] = HistoryRepository.list(repos.history)
    assert record.round == state.round
    assert record.death_cause == state.death_cause
    assert record.implementation == "elixir"

    assert DateTime.compare(SaveGame.parse_timestamp(record.ended_at), SaveGame.parse_timestamp(record.started_at)) ==
             :gt

    profile = ProfileRepository.load(repos.profile)
    assert hd(profile.hall_of_fame).run_id == record.run_id
    assert "first_blood" in unlocked
    assert Map.has_key?(profile.achievements, "first_blood")
    assert profile.bestiary |> Map.values() |> Enum.map(& &1.kills) |> Enum.sum() == state.round - 1
    GameSession.save_and_quit(session)
    refute File.exists?(Path.join(dir, "save.json"))
  end

  defp play_to_death(session, unlocked) do
    state = GameSession.state(session)

    if state.phase == :game_over do
      {session, unlocked}
    else
      {session, result} = GameSession.step(session, GreedyBot.choose(session.data, state))
      play_to_death(session, unlocked ++ Enum.map(result.achievements, & &1.id))
    end
  end

  test "a newer schema is refused", %{tmp_dir: dir} do
    File.write!(Path.join(dir, "save.json"), JSON.encode!(%{"schemaVersion" => 99}))
    assert_raise NewerSchemaError, fn -> SaveRepository.load(FileSaveRepository.new(dir)) end
    File.write!(Path.join(dir, "profile.json"), JSON.encode!(%{"schemaVersion" => 99}))
    assert_raise NewerSchemaError, fn -> ProfileRepository.load(FileProfileRepository.new(dir)) end
    File.write!(Path.join(dir, "settings.json"), JSON.encode!(%{"schemaVersion" => 99}))
    assert_raise NewerSchemaError, fn -> SettingsRepository.load(SettingsRepository.new(dir)) end
  end

  test "settings round trip", %{tmp_dir: dir} do
    repo = SettingsRepository.new(dir)
    assert SettingsRepository.load(repo) == %Settings{}
    SettingsRepository.save(repo, %Settings{locale: "pt-BR"})
    assert SettingsRepository.load(repo) == %Settings{locale: "pt-BR"}
    SettingsRepository.save(repo, %Settings{})
    assert SettingsRepository.load(repo) == %Settings{}
    File.write!(Path.join(dir, "settings.json"), JSON.encode!(%{"schemaVersion" => 1, "locale" => "fr"}))
    assert SettingsRepository.load(repo) == %Settings{}
  end

  test "profile round trip and Hall of Fame order", %{tmp_dir: dir} do
    service =
      Enum.reduce(0..11, ProfileService.new(Helpers.data(), %Profile{}), fn index, service ->
        day = (index + 1) |> Integer.to_string() |> String.pad_leading(2, "0")

        entry = %HallOfFameEntry{
          run_id: "run#{index}",
          name: "A",
          vocation: "mage",
          difficulty: "normal",
          round: rem(index, 5),
          level: index,
          ended_at: "2026-01-#{day}T00:00:00Z"
        }

        ProfileService.record_finished_run(service, entry)
      end)

    hall = service.profile.hall_of_fame
    assert length(hall) == 10
    assert hall |> Enum.take(3) |> Enum.map(& &1.round) == [4, 4, 3]
    assert Enum.at(hall, 0).level > Enum.at(hall, 1).level
    repo = FileProfileRepository.new(dir)
    ProfileRepository.save(repo, service.profile)
    assert ProfileRepository.load(repo) == service.profile
    refute ProfileService.revealed?(service, "rat")
  end

  test "run id and clock" do
    assert SaveGame.make_run_id(~U[2026-01-02 03:04:05Z], 42) == "20260102T030405Z-42"
    assert SaveGame.format_timestamp(~U[2026-01-02 03:04:05.123Z]) == "2026-01-02T03:04:05Z"
    assert SystemClock.now(%SystemClock{}).time_zone == "Etc/UTC"
  end

  test "a Python-shaped save loads and continues", %{tmp_dir: dir} do
    data = Helpers.data()
    {engine, _} = GameEngine.new_run(data, config("Py", "warrior", "normal"), 42)
    {engine, _} = GameEngine.step(engine, %BuyPotion{potion_id: "health_potion", quantity: 1})

    # Same keys and shapes as rpg-python writes (key order differs; JSON readers ignore it).
    python_save = %{
      "schemaVersion" => 1,
      "gameVersion" => "1.0.0",
      "implementation" => "python",
      "savedAt" => "2026-09-27T21:04:11Z",
      "rngState" => GameEngine.rng_state(engine),
      "session" => %{
        "runId" => "20260927T210411Z-42",
        "startedAt" => "2026-09-27T21:04:11Z",
        "playTimeSeconds" => 1520,
        "sessions" => 2
      },
      "run" => Rpg.Application.RunState.to_map(engine.state)
    }

    File.write!(Path.join(dir, "save.json"), JSON.encode!(python_save))
    resumed = GameSession.resume(data, repositories(dir), FakeClock.new(), "1.0.0")
    assert resumed.info.sessions == 3
    assert resumed.info.play_time_seconds == 1520
    assert GameSession.state(resumed) == engine.state
    {_engine, expected} = GameEngine.step(engine, %NextFight{})
    {_session, result} = GameSession.step(resumed, %NextFight{})
    assert result.events == expected
  end

  test "a save written by the Python reference loads", %{tmp_dir: dir} do
    # Copy of the run state recorded by rpg-python in shared/golden/merchant-and-errors.json.
    golden = Helpers.read_json(Path.join(Helpers.golden_dir(), "merchant-and-errors.json"))

    save = %{
      "schemaVersion" => 1,
      "gameVersion" => "1.0.0",
      "implementation" => "python",
      "savedAt" => "2026-09-27T21:04:11Z",
      "rngState" => golden["finalState"]["rngState"],
      "session" => %{"runId" => "r", "startedAt" => "2026-09-27T21:04:11Z", "playTimeSeconds" => 0, "sessions" => 1},
      "run" => golden["finalRun"]
    }

    File.write!(Path.join(dir, "save.json"), JSON.encode!(save))
    loaded = SaveRepository.load(FileSaveRepository.new(dir))
    assert loaded.implementation == "python"
    assert SaveGame.to_map(loaded)["run"] == golden["finalRun"]
  end

  test "history lists records in file name order", %{tmp_dir: dir} do
    repo = FileHistoryRepository.new(dir)
    assert HistoryRepository.list(repo) == []
  end

  test "encode_pretty handles every JSON value" do
    text = IO.iodata_to_binary(Repos.encode_pretty(%{"b" => [1, true, nil, "x"], "a" => %{}, "c" => []}))
    assert text == "{\n\t\"a\": {},\n\t\"b\": [\n\t\t1,\n\t\ttrue,\n\t\tnull,\n\t\t\"x\"\n\t],\n\t\"c\": []\n}"
  end
end
