defmodule Rpg.Application.Simulator do
  @moduledoc "Headless balance simulator: the bot plays many runs and we aggregate how far it gets."

  alias Rpg.Application.{GameEngine, GreedyBot, RunConfig}
  alias Rpg.Domain.Definitions.GameData

  @max_steps_per_run 200_000

  defmodule RunResult do
    @moduledoc false
    @enforce_keys [:round, :level, :death_cause]
    defstruct @enforce_keys
    @type t :: %__MODULE__{round: integer(), level: integer(), death_cause: String.t()}
  end

  defmodule SimulationSummary do
    @moduledoc false
    @enforce_keys [
      :vocation,
      :difficulty,
      :runs,
      :min_round,
      :p10_round,
      :median_round,
      :p90_round,
      :max_round,
      :mean_level,
      :top_killers
    ]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            vocation: String.t(),
            difficulty: String.t(),
            runs: integer(),
            min_round: integer(),
            p10_round: integer(),
            median_round: integer(),
            p90_round: integer(),
            max_round: integer(),
            mean_level: integer(),
            top_killers: [{String.t(), integer()}]
          }
  end

  @spec play_one(GameData.t(), RunConfig.t(), integer()) :: RunResult.t()
  def play_one(%GameData{} = data, %RunConfig{} = config, seed) do
    {engine, _events} = GameEngine.new_run(data, config, seed)
    state = play(data, engine, @max_steps_per_run, seed).state
    %RunResult{round: state.round, level: state.player.level, death_cause: state.death_cause || ""}
  end

  defp play(_data, %GameEngine{state: %{phase: :game_over}} = engine, _steps_left, _seed), do: engine
  defp play(_data, _engine, 0, seed), do: raise(RuntimeError, "run did not finish (seed #{seed})")

  defp play(data, engine, steps_left, seed) do
    {engine, _events} = GameEngine.step(engine, GreedyBot.choose(data, engine.state))
    play(data, engine, steps_left - 1, seed)
  end

  defp percentile(sorted, percent), do: Enum.at(sorted, min(length(sorted) - 1, div(length(sorted) * percent, 100)))

  @spec simulate(GameData.t(), String.t(), String.t(), integer(), integer()) :: SimulationSummary.t()
  def simulate(data, vocation, difficulty, runs, base_seed \\ 1)

  def simulate(_data, _vocation, _difficulty, runs, _base_seed) when runs <= 0,
    do: raise(ArgumentError, "runs must be positive")

  def simulate(%GameData{} = data, vocation, difficulty, runs, base_seed) do
    config = %RunConfig{name: "Bot", vocation_id: vocation, difficulty_id: difficulty}
    results = Enum.map(0..(runs - 1), &play_one(data, config, base_seed + &1))
    rounds = results |> Enum.map(& &1.round) |> Enum.sort()

    top_killers =
      results
      |> Enum.frequencies_by(& &1.death_cause)
      |> Enum.sort_by(fn {cause, count} -> {-count, cause} end)
      |> Enum.take(3)

    %SimulationSummary{
      vocation: vocation,
      difficulty: difficulty,
      runs: runs,
      min_round: List.first(rounds),
      p10_round: percentile(rounds, 10),
      median_round: percentile(rounds, 50),
      p90_round: percentile(rounds, 90),
      max_round: List.last(rounds),
      mean_level: div(results |> Enum.map(& &1.level) |> Enum.sum(), runs),
      top_killers: top_killers
    }
  end
end
