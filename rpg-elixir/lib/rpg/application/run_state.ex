defmodule Rpg.Application.RunConfig do
  @moduledoc false
  alias Rpg.Domain.JsonTypes

  @enforce_keys [:name, :vocation_id, :difficulty_id]
  defstruct @enforce_keys
  @type t :: %__MODULE__{name: String.t(), vocation_id: String.t(), difficulty_id: String.t()}

  @spec to_map(t()) :: map()
  def to_map(%__MODULE__{} = c), do: %{"name" => c.name, "vocation" => c.vocation_id, "difficulty" => c.difficulty_id}

  @spec from_map(term()) :: t()
  def from_map(raw) do
    %__MODULE__{
      name: JsonTypes.str(JsonTypes.field(raw, "name")),
      vocation_id: JsonTypes.str(JsonTypes.field(raw, "vocation")),
      difficulty_id: JsonTypes.str(JsonTypes.field(raw, "difficulty"))
    }
  end
end

defmodule Rpg.Application.RunState do
  @moduledoc "Everything needed to continue a run, except the PRNG state (kept by the engine)."

  alias Rpg.Application.{RunConfig, RunStatistics}
  alias Rpg.Domain.Entities.{ItemInstance, MonsterInstance, Player}
  alias Rpg.Domain.{Enums, JsonTypes}

  @enforce_keys [:seed, :config, :player]
  defstruct [
    :seed,
    :config,
    :player,
    phase: :merchant,
    round: 0,
    turn: 0,
    monster: nil,
    merchant_stock: [],
    next_item_uid: 1,
    death_cause: nil,
    stats: %RunStatistics{}
  ]

  @type t :: %__MODULE__{
          seed: integer(),
          config: RunConfig.t(),
          player: Player.t(),
          phase: Enums.phase(),
          round: integer(),
          turn: integer(),
          monster: MonsterInstance.t() | nil,
          merchant_stock: [ItemInstance.t()],
          next_item_uid: integer(),
          death_cause: String.t() | nil,
          stats: RunStatistics.t()
        }

  @doc "Returns the next item uid and the state with the counter advanced."
  @spec take_item_uid(t()) :: {integer(), t()}
  def take_item_uid(%__MODULE__{next_item_uid: uid} = state), do: {uid, %{state | next_item_uid: uid + 1}}

  @spec to_map(t()) :: map()
  def to_map(%__MODULE__{} = s) do
    %{
      "seed" => s.seed,
      "config" => RunConfig.to_map(s.config),
      "player" => Player.to_map(s.player),
      "phase" => Atom.to_string(s.phase),
      "round" => s.round,
      "turn" => s.turn,
      "monster" => if(s.monster, do: MonsterInstance.to_map(s.monster)),
      "merchantStock" => Enum.map(s.merchant_stock, &ItemInstance.to_map/1),
      "nextItemUid" => s.next_item_uid,
      "deathCause" => s.death_cause,
      "stats" => RunStatistics.to_map(s.stats)
    }
  end

  @spec from_map(term()) :: t()
  def from_map(raw) do
    f = &JsonTypes.field(raw, &1)
    monster = f.("monster")
    death_cause = f.("deathCause")

    %__MODULE__{
      seed: JsonTypes.int(f.("seed")),
      config: RunConfig.from_map(f.("config")),
      player: Player.from_map(f.("player")),
      phase: Enums.phase!(JsonTypes.str(f.("phase"))),
      round: JsonTypes.int(f.("round")),
      turn: JsonTypes.int(f.("turn")),
      monster: if(monster != nil, do: MonsterInstance.from_map(monster)),
      merchant_stock: Enum.map(JsonTypes.list(f.("merchantStock")), &ItemInstance.from_map/1),
      next_item_uid: JsonTypes.int(f.("nextItemUid")),
      death_cause: if(death_cause != nil, do: JsonTypes.str(death_cause)),
      stats: RunStatistics.from_map(f.("stats"))
    }
  end
end
