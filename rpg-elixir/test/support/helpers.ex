defmodule Rpg.Test.Helpers do
  @moduledoc "Shared fixtures for the test suites (the counterpart of the Python conftest.py)."

  alias Rpg.Application.{GameEngine, RunConfig}
  alias Rpg.Domain.Definitions.{GameData, ItemDef}
  alias Rpg.Infrastructure.{DataLoader, Paths}

  @spec shared_dir() :: Path.t()
  def shared_dir, do: Paths.find_shared_dir(Path.expand("..", __DIR__))

  @spec golden_dir() :: Path.t()
  def golden_dir, do: Path.join(shared_dir(), "golden")

  @doc "Game data loaded once per test run (from the embedded copy, like the escript)."
  @spec data() :: GameData.t()
  def data do
    case :persistent_term.get({__MODULE__, :data}, nil) do
      nil ->
        data = DataLoader.load_game_data(:embedded)
        :persistent_term.put({__MODULE__, :data}, data)
        data

      data ->
        data
    end
  end

  @spec new_engine(keyword()) :: GameEngine.t()
  def new_engine(opts \\ []) do
    config = %RunConfig{
      name: "Tester",
      vocation_id: Keyword.get(opts, :vocation, "warrior"),
      difficulty_id: Keyword.get(opts, :difficulty, "normal")
    }

    {engine, _events} = GameEngine.new_run(Keyword.get(opts, :data, data()), config, Keyword.get(opts, :seed, 42))
    engine
  end

  @doc "Applies `fun` to the run state of an engine (tests set up situations the way Python mutates its state)."
  @spec update_state(GameEngine.t(), (term() -> term())) :: GameEngine.t()
  def update_state(%GameEngine{} = engine, fun), do: %{engine | state: fun.(engine.state)}

  @spec update_player(GameEngine.t(), (term() -> term())) :: GameEngine.t()
  def update_player(engine, fun), do: update_state(engine, &%{&1 | player: fun.(&1.player)})

  @spec update_monster(GameEngine.t(), (term() -> term())) :: GameEngine.t()
  def update_monster(engine, fun), do: update_state(engine, &%{&1 | monster: fun.(&1.monster)})

  @doc "Steps and returns only the events, plus the new engine."
  @spec step(GameEngine.t(), struct()) :: {GameEngine.t(), [map()]}
  def step(engine, command), do: GameEngine.step(engine, command)

  @doc "Adds deterministic test items (one per slot with every stat) without touching the shared files."
  @spec with_test_items(GameData.t()) :: GameData.t()
  def with_test_items(%GameData{} = data) do
    extra = [
      item("test_helmet", "Test Helmet", "helmet", "helmet", %{"armor" => 10, "maxHp" => 50}),
      item("test_ring", "Test Ring", "ring", "ring", %{"critChance" => 80, "dodge" => 90}),
      item("test_axe", "Test Axe", "weapon", "axe", %{"attack" => 20}),
      item("test_rod", "Test Rod", "weapon", "rod", %{"attack" => 1})
    ]

    GameData.replace(data, items: data.items ++ extra)
  end

  defp item(id, name, slot, type, stats) do
    %ItemDef{id: id, name: name, slot: slot, type: type, tier: 0, element: nil, stats: stats, value: 100}
  end

  @spec types([map()]) :: [String.t()]
  def types(events), do: Enum.map(events, & &1["type"])

  @spec read_json(Path.t()) :: term()
  def read_json(path), do: path |> File.read!() |> JSON.decode!()
end

defmodule Rpg.Test.FakeClock do
  @moduledoc "A clock that advances 10 seconds on every call (state kept in an Agent, since values are immutable)."
  @behaviour Rpg.Application.Ports.Clock

  @enforce_keys [:agent]
  defstruct [:agent]

  def new do
    {:ok, agent} = Agent.start_link(fn -> ~U[2026-09-27 12:00:00Z] end)
    %__MODULE__{agent: agent}
  end

  @impl true
  def now(%__MODULE__{agent: agent}) do
    Agent.get_and_update(agent, fn current ->
      next = DateTime.add(current, 10, :second)
      {next, next}
    end)
  end
end
