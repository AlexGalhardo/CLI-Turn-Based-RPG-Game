defmodule Rpg.Application.SaveGame do
  @moduledoc "Save file and finished-run record formats, shared by every implementation (docs/persistence.md)."

  alias Rpg.Application.{RunState, SessionInfo}
  alias Rpg.Domain.JsonTypes

  @schema_version 2
  @implementation "elixir"

  defmodule NewerSchemaError do
    @moduledoc "The file was written by a newer game version; it is never overwritten."
    defexception [:message]
  end

  @enforce_keys [:game_version, :implementation, :saved_at, :rng_state, :session, :run]
  defstruct @enforce_keys

  @type t :: %__MODULE__{
          game_version: String.t(),
          implementation: String.t(),
          saved_at: String.t(),
          rng_state: non_neg_integer(),
          session: SessionInfo.t(),
          run: RunState.t()
        }

  @spec schema_version() :: integer()
  def schema_version, do: @schema_version

  @spec implementation() :: String.t()
  def implementation, do: @implementation

  @spec format_timestamp(DateTime.t()) :: String.t()
  def format_timestamp(%DateTime{} = moment) do
    moment |> DateTime.shift_zone!("Etc/UTC") |> DateTime.truncate(:second) |> Calendar.strftime("%Y-%m-%dT%H:%M:%SZ")
  end

  @spec parse_timestamp(String.t()) :: DateTime.t()
  def parse_timestamp(text) do
    {:ok, moment, 0} = DateTime.from_iso8601(text)
    moment
  end

  @spec make_run_id(DateTime.t(), integer()) :: String.t()
  def make_run_id(%DateTime{} = started_at, seed) do
    utc = DateTime.shift_zone!(started_at, "Etc/UTC")
    "#{Calendar.strftime(utc, "%Y%m%dT%H%M%SZ")}-#{seed}"
  end

  @spec check_schema(map(), String.t()) :: :ok
  def check_schema(data, what) do
    version = JsonTypes.int(JsonTypes.field(data, "schemaVersion"))

    if version > @schema_version do
      raise NewerSchemaError,
        message: "#{what} uses schema #{version}; update the game (supports #{@schema_version})"
    end

    :ok
  end

  @spec to_map(t()) :: map()
  def to_map(%__MODULE__{} = save) do
    %{
      "schemaVersion" => @schema_version,
      "gameVersion" => save.game_version,
      "implementation" => save.implementation,
      "savedAt" => save.saved_at,
      "rngState" => save.rng_state,
      "session" => SessionInfo.to_map(save.session),
      "run" => RunState.to_map(save.run)
    }
  end

  @spec from_map(term()) :: t()
  def from_map(raw) do
    check_schema(raw, "save.json")
    f = &JsonTypes.field(raw, &1)

    %__MODULE__{
      game_version: JsonTypes.str(f.("gameVersion")),
      implementation: JsonTypes.str(f.("implementation")),
      saved_at: JsonTypes.str(f.("savedAt")),
      rng_state: JsonTypes.int(f.("rngState")),
      session: SessionInfo.from_map(f.("session")),
      run: RunState.from_map(f.("run"))
    }
  end
end

defmodule Rpg.Application.SessionInfo do
  @moduledoc false
  alias Rpg.Domain.JsonTypes

  @enforce_keys [:run_id, :started_at]
  defstruct [:run_id, :started_at, play_time_seconds: 0, sessions: 1]

  @type t :: %__MODULE__{run_id: String.t(), started_at: String.t(), play_time_seconds: integer(), sessions: integer()}

  @spec to_map(t()) :: map()
  def to_map(%__MODULE__{} = info) do
    %{
      "runId" => info.run_id,
      "startedAt" => info.started_at,
      "playTimeSeconds" => info.play_time_seconds,
      "sessions" => info.sessions
    }
  end

  @spec from_map(term()) :: t()
  def from_map(raw) do
    %__MODULE__{
      run_id: JsonTypes.str(JsonTypes.field(raw, "runId")),
      started_at: JsonTypes.str(JsonTypes.field(raw, "startedAt")),
      play_time_seconds: JsonTypes.int(JsonTypes.field(raw, "playTimeSeconds")),
      sessions: JsonTypes.int(JsonTypes.field(raw, "sessions"))
    }
  end
end

defmodule Rpg.Application.RunRecord do
  @moduledoc "A finished run, written to history/<runId>.json."

  alias Rpg.Application.{RunStatistics, SaveGame}
  alias Rpg.Domain.JsonTypes

  @fields [
    run_id: {"runId", :str},
    name: {"name", :str},
    vocation: {"vocation", :str},
    difficulty: {"difficulty", :str},
    seed: {"seed", :int},
    implementation: {"implementation", :str},
    game_version: {"gameVersion", :str},
    started_at: {"startedAt", :str},
    ended_at: {"endedAt", :str},
    play_time_seconds: {"playTimeSeconds", :int},
    sessions: {"sessions", :int},
    round: {"round", :int},
    level: {"level", :int},
    magic_level: {"magicLevel", :int},
    death_cause: {"deathCause", :str},
    won: {"won", :bool}
  ]

  @enforce_keys Keyword.keys(@fields)
  defstruct Keyword.keys(@fields) ++ [stats: %RunStatistics{}]

  @type t :: %__MODULE__{}

  @spec to_map(t()) :: map()
  def to_map(%__MODULE__{} = record) do
    @fields
    |> Map.new(fn {field, {key, _type}} -> {key, Map.fetch!(record, field)} end)
    |> Map.put("schemaVersion", SaveGame.schema_version())
    |> Map.put("stats", RunStatistics.to_map(record.stats))
  end

  @spec from_map(term()) :: t()
  def from_map(raw) do
    SaveGame.check_schema(raw, "history record")

    fields =
      Enum.map(@fields, fn {field, {key, type}} ->
        value = JsonTypes.field(raw, key)
        {field, field_value(type, value)}
      end)

    struct!(__MODULE__, fields ++ [stats: RunStatistics.from_map(JsonTypes.field(raw, "stats"))])
  end

  defp field_value(:int, value), do: JsonTypes.int(value)
  defp field_value(:bool, value), do: JsonTypes.bool(value)
  defp field_value(:str, value), do: JsonTypes.str(value)
end
