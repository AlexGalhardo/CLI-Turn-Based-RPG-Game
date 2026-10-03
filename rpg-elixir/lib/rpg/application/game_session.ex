defmodule Rpg.Application.GameSession do
  @moduledoc """
  Use case that wraps the pure engine with time, persistence and the profile.

  The engine stays deterministic; everything that depends on the clock or the filesystem happens here. The session is
  an immutable value too: `step/2` returns the new session plus a `StepResult`.
  """

  alias Rpg.Application.{GameEngine, ProfileService, RunConfig, RunRecord, RunState, SaveGame, SessionInfo}
  alias Rpg.Application.Ports.{Clock, HistoryRepository, ProfileRepository, SaveRepository}
  alias Rpg.Application.Profile.HallOfFameEntry
  alias Rpg.Domain.Definitions.{AchievementDef, GameData}

  defmodule StepResult do
    @moduledoc false
    @enforce_keys [:events, :achievements]
    defstruct @enforce_keys
    @type t :: %__MODULE__{events: [map()], achievements: [AchievementDef.t()]}
  end

  defmodule Repositories do
    @moduledoc "The persistence adapters (any struct implementing the matching port behaviour)."
    @enforce_keys [:saves, :history, :profile]
    defstruct @enforce_keys
    @type t :: %__MODULE__{saves: struct(), history: struct(), profile: struct()}
  end

  @enforce_keys [:data, :engine, :info, :repositories, :clock, :game_version, :profile, :segment_started]
  defstruct [
    :data,
    :engine,
    :info,
    :repositories,
    :clock,
    :game_version,
    :profile,
    :segment_started,
    merchant_snapshot: nil,
    finished_record: nil
  ]

  @type t :: %__MODULE__{
          data: GameData.t(),
          engine: GameEngine.t(),
          info: SessionInfo.t(),
          repositories: Repositories.t(),
          clock: struct(),
          game_version: String.t(),
          profile: ProfileService.t(),
          segment_started: DateTime.t(),
          merchant_snapshot: {RunState.t(), non_neg_integer()} | nil,
          finished_record: RunRecord.t() | nil
        }

  @spec state(t()) :: RunState.t()
  def state(%__MODULE__{engine: engine}), do: engine.state

  defp new(data, engine, info, repositories, clock, game_version) do
    %__MODULE__{
      data: data,
      engine: engine,
      info: info,
      repositories: repositories,
      clock: clock,
      game_version: game_version,
      profile: ProfileService.new(data, ProfileRepository.load(repositories.profile)),
      segment_started: Clock.now(clock)
    }
  end

  # ── creation ────────────────────────────────────────────────────────────────

  @spec start(GameData.t(), RunConfig.t(), integer(), keyword()) :: {t(), [map()]}
  def start(%GameData{} = data, %RunConfig{} = config, seed, opts) do
    {engine, events} = GameEngine.new_run(data, config, seed)
    clock = Keyword.fetch!(opts, :clock)
    now = Clock.now(clock)
    info = %SessionInfo{run_id: SaveGame.make_run_id(now, seed), started_at: SaveGame.format_timestamp(now)}
    session = new(data, engine, info, Keyword.fetch!(opts, :repositories), clock, Keyword.fetch!(opts, :game_version))
    {session, _unlocked} = after_step(session, events)
    {session, events}
  end

  @spec resume(GameData.t(), Repositories.t(), struct(), String.t()) :: t() | nil
  def resume(%GameData{} = data, %Repositories{} = repositories, clock, game_version) do
    case SaveRepository.load(repositories.saves) do
      nil ->
        nil

      save ->
        engine = GameEngine.restore(data, save.run, save.rng_state)
        info = %{save.session | sessions: save.session.sessions + 1}
        session = new(data, engine, info, repositories, clock, game_version)
        %{session | merchant_snapshot: {save.run, save.rng_state}}
    end
  end

  # ── play ────────────────────────────────────────────────────────────────────

  @spec step(t(), struct()) :: {t(), StepResult.t()}
  def step(%__MODULE__{} = session, command) do
    {engine, events} = GameEngine.step(session.engine, command)
    {session, unlocked} = after_step(%{session | engine: engine}, events)
    {session, %StepResult{events: events, achievements: unlocked}}
  end

  defp after_step(session, events) do
    now = SaveGame.format_timestamp(Clock.now(session.clock))
    {profile, unlocked} = ProfileService.observe(session.profile, events, state(session), now, session.info.run_id)
    session = %{session | profile: profile}
    profile_changed = unlocked != [] or Enum.any?(events, &(&1["type"] == "monster_killed"))

    {session, profile_changed} =
      case state(session).phase do
        phase when phase in [:merchant, :victory] ->
          session = %{session | merchant_snapshot: {state(session), GameEngine.rng_state(session.engine)}}
          {write_save(session), profile_changed}

        :game_over when session.finished_record == nil ->
          {finish(session), true}

        _ ->
          {session, profile_changed}
      end

    if profile_changed, do: ProfileRepository.save(session.repositories.profile, session.profile.profile)
    {session, unlocked}
  end

  @doc "Persists play time. Mid-battle quits resume from the last merchant (or victory) snapshot."
  @spec save_and_quit(t()) :: t()
  def save_and_quit(%__MODULE__{} = session) do
    if state(session).phase == :game_over, do: session, else: write_save(session)
  end

  # ── persistence ─────────────────────────────────────────────────────────────

  defp accumulate_play_time(session) do
    now = Clock.now(session.clock)
    elapsed = max(0, DateTime.diff(now, session.segment_started, :second))
    info = %{session.info | play_time_seconds: session.info.play_time_seconds + elapsed}
    {%{session | info: info, segment_started: now}, now}
  end

  defp write_save(%__MODULE__{merchant_snapshot: nil} = session), do: session

  defp write_save(session) do
    {session, now} = accumulate_play_time(session)
    {run, rng_state} = session.merchant_snapshot

    save = %SaveGame{
      game_version: session.game_version,
      implementation: SaveGame.implementation(),
      saved_at: SaveGame.format_timestamp(now),
      rng_state: rng_state,
      session: session.info,
      run: run
    }

    SaveRepository.save(session.repositories.saves, save)
    session
  end

  defp finish(session) do
    {session, now} = accumulate_play_time(session)
    state = state(session)
    info = session.info

    record = %RunRecord{
      run_id: info.run_id,
      name: state.config.name,
      vocation: state.config.vocation_id,
      difficulty: state.config.difficulty_id,
      seed: state.seed,
      implementation: SaveGame.implementation(),
      game_version: session.game_version,
      started_at: info.started_at,
      ended_at: SaveGame.format_timestamp(now),
      play_time_seconds: info.play_time_seconds,
      sessions: info.sessions,
      round: state.round,
      level: state.player.level,
      magic_level: state.player.magic_level,
      death_cause: state.death_cause || "",
      won: state.won,
      stats: state.stats
    }

    HistoryRepository.add(session.repositories.history, record)
    SaveRepository.delete(session.repositories.saves)

    entry = %HallOfFameEntry{
      run_id: record.run_id,
      name: record.name,
      vocation: record.vocation,
      difficulty: record.difficulty,
      round: record.round,
      level: record.level,
      ended_at: record.ended_at,
      won: record.won
    }

    %{session | profile: ProfileService.record_finished_run(session.profile, entry), finished_record: record}
  end
end
