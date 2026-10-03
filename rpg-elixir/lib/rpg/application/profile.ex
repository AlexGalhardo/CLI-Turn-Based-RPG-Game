defmodule Rpg.Application.Profile do
  @moduledoc "Cross-run profile: bestiary, achievements and Hall of Fame (docs/game-design.md §11)."

  alias Rpg.Domain.JsonTypes

  @schema_version 1

  defmodule BestiaryEntry do
    @moduledoc false
    @enforce_keys [:kills, :first_killed_at]
    defstruct @enforce_keys
    @type t :: %__MODULE__{kills: integer(), first_killed_at: String.t()}
  end

  defmodule Unlock do
    @moduledoc false
    @enforce_keys [:unlocked_at, :run_id]
    defstruct @enforce_keys
    @type t :: %__MODULE__{unlocked_at: String.t(), run_id: String.t()}
  end

  defmodule HallOfFameEntry do
    @moduledoc false
    @enforce_keys [:run_id, :name, :vocation, :difficulty, :round, :level, :ended_at]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            run_id: String.t(),
            name: String.t(),
            vocation: String.t(),
            difficulty: String.t(),
            round: integer(),
            level: integer(),
            ended_at: String.t()
          }

    @spec to_map(t()) :: map()
    def to_map(%__MODULE__{} = e) do
      %{
        "runId" => e.run_id,
        "name" => e.name,
        "vocation" => e.vocation,
        "difficulty" => e.difficulty,
        "round" => e.round,
        "level" => e.level,
        "endedAt" => e.ended_at
      }
    end

    @spec from_map(term()) :: t()
    def from_map(raw) do
      f = &JsonTypes.field(raw, &1)

      %__MODULE__{
        run_id: JsonTypes.str(f.("runId")),
        name: JsonTypes.str(f.("name")),
        vocation: JsonTypes.str(f.("vocation")),
        difficulty: JsonTypes.str(f.("difficulty")),
        round: JsonTypes.int(f.("round")),
        level: JsonTypes.int(f.("level")),
        ended_at: JsonTypes.str(f.("endedAt"))
      }
    end
  end

  defstruct bestiary: %{}, achievements: %{}, hall_of_fame: []

  @type t :: %__MODULE__{
          bestiary: %{String.t() => BestiaryEntry.t()},
          achievements: %{String.t() => Unlock.t()},
          hall_of_fame: [HallOfFameEntry.t()]
        }

  @spec schema_version() :: integer()
  def schema_version, do: @schema_version

  @spec to_map(t()) :: map()
  def to_map(%__MODULE__{} = profile) do
    %{
      "schemaVersion" => @schema_version,
      "bestiary" =>
        Map.new(profile.bestiary, fn {id, e} -> {id, %{"kills" => e.kills, "firstKilledAt" => e.first_killed_at}} end),
      "achievements" =>
        Map.new(profile.achievements, fn {id, u} -> {id, %{"unlockedAt" => u.unlocked_at, "runId" => u.run_id}} end),
      "hallOfFame" => Enum.map(profile.hall_of_fame, &HallOfFameEntry.to_map/1)
    }
  end

  @spec from_map(term()) :: t()
  def from_map(raw) do
    bestiary =
      Map.new(JsonTypes.obj(JsonTypes.field(raw, "bestiary")), fn {id, v} ->
        {id,
         %BestiaryEntry{
           kills: JsonTypes.int(JsonTypes.field(v, "kills")),
           first_killed_at: JsonTypes.str(JsonTypes.field(v, "firstKilledAt"))
         }}
      end)

    achievements =
      Map.new(JsonTypes.obj(JsonTypes.field(raw, "achievements")), fn {id, v} ->
        {id,
         %Unlock{
           unlocked_at: JsonTypes.str(JsonTypes.field(v, "unlockedAt")),
           run_id: JsonTypes.str(JsonTypes.field(v, "runId"))
         }}
      end)

    hall = Enum.map(JsonTypes.list(JsonTypes.field(raw, "hallOfFame")), &HallOfFameEntry.from_map/1)
    %__MODULE__{bestiary: bestiary, achievements: achievements, hall_of_fame: hall}
  end
end

defmodule Rpg.Application.ProfileService do
  @moduledoc "Feeds the profile from engine events. Returns the achievements unlocked by each step."

  alias Rpg.Application.{Profile, RunState}
  alias Rpg.Application.Profile.{BestiaryEntry, HallOfFameEntry, Unlock}
  alias Rpg.Domain.Definitions.{AchievementDef, GameData}

  @hall_of_fame_size 10
  @bestiary_reveal_kills 5

  @enforce_keys [:data, :profile]
  defstruct @enforce_keys

  @type t :: %__MODULE__{data: GameData.t(), profile: Profile.t()}

  @spec new(GameData.t(), Profile.t()) :: t()
  def new(%GameData{} = data, %Profile{} = profile), do: %__MODULE__{data: data, profile: profile}

  @spec observe(t(), [map()], RunState.t(), String.t(), String.t()) :: {t(), [AchievementDef.t()]}
  def observe(%__MODULE__{} = service, events, %RunState{} = state, now, run_id) do
    bestiary =
      Enum.reduce(events, service.profile.bestiary, fn
        %{"type" => "monster_killed", "monsterId" => id}, bestiary ->
          Map.update(bestiary, id, %BestiaryEntry{kills: 1, first_killed_at: now}, &%{&1 | kills: &1.kills + 1})

        _event, bestiary ->
          bestiary
      end)

    service = put_in(service.profile.bestiary, bestiary)

    Enum.reduce(service.data.achievements, {service, []}, fn achievement, {service, unlocked} ->
      if Map.has_key?(service.profile.achievements, achievement.id) or
           progress(service, achievement, state) < achievement.value do
        {service, unlocked}
      else
        unlock = %Unlock{unlocked_at: now, run_id: run_id}
        service = put_in(service.profile.achievements[achievement.id], unlock)
        {service, unlocked ++ [achievement]}
      end
    end)
  end

  defp progress(%__MODULE__{data: data, profile: profile}, %AchievementDef{type: type}, state) do
    player = state.player

    case type do
      "kills_total" ->
        profile.bestiary |> Map.values() |> Enum.map(& &1.kills) |> Enum.sum()

      "bosses_total" ->
        boss_ids = MapSet.new(data.bosses, & &1.id)
        for({id, entry} <- profile.bestiary, MapSet.member?(boss_ids, id), do: entry.kills) |> Enum.sum()

      "round_reached" ->
        state.round

      "level_reached" ->
        player.level

      "legendary_found" ->
        Map.get(state.stats.items_dropped, "legendary", 0)

      "spell_level_3" ->
        threshold = List.last(data.balance.spell_levels).uses
        Enum.count(player.spell_uses, fn {_spell, uses} -> uses >= threshold end)

      "gold_held" ->
        player.gold

      "distinct_monsters" ->
        map_size(profile.bestiary)

      "hard_round_reached" ->
        if state.config.difficulty_id == "hard", do: state.round, else: 0

      _ ->
        0
    end
  end

  @doc "Top 10 by round, then level, then earliest end (stable: ties keep the existing entry first)."
  @spec record_finished_run(t(), HallOfFameEntry.t()) :: t()
  def record_finished_run(%__MODULE__{} = service, %HallOfFameEntry{} = entry) do
    ranking =
      (service.profile.hall_of_fame ++ [entry])
      |> Enum.sort_by(&{-&1.round, -&1.level, &1.ended_at})
      |> Enum.take(@hall_of_fame_size)

    put_in(service.profile.hall_of_fame, ranking)
  end

  @spec revealed?(t(), String.t()) :: boolean()
  def revealed?(%__MODULE__{profile: profile}, monster_id) do
    case Map.get(profile.bestiary, monster_id) do
      nil -> false
      entry -> entry.kills >= @bestiary_reveal_kills
    end
  end
end
