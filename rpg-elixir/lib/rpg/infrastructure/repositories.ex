defmodule Rpg.Infrastructure.Repositories do
  @moduledoc """
  JSON file repositories under the player's data directory (docs/persistence.md), plus the system clock.

  Files are pretty-printed with tabs and sorted keys, and written atomically (temp file + rename).
  """

  alias Rpg.Application.{Profile, RunRecord, SaveGame}
  alias Rpg.Domain.JsonTypes
  alias Rpg.Infrastructure.I18n

  @doc "Write to a temp file then rename, so a crash never leaves a half-written save."
  @spec write_json_atomic(Path.t(), map()) :: :ok
  def write_json_atomic(path, document) do
    File.mkdir_p!(Path.dirname(path))
    temporary = path <> ".tmp"
    File.write!(temporary, [encode_pretty(document), "\n"])
    File.rename!(temporary, path)
  end

  @spec read_json(Path.t()) :: term()
  def read_json(path), do: path |> File.read!() |> JSON.decode!()

  @doc "JSON with tab indentation and keys sorted by code point (the Go port writes the same layout)."
  @spec encode_pretty(term(), non_neg_integer()) :: iodata()
  def encode_pretty(value, depth \\ 0)

  def encode_pretty(map, _depth) when map == %{}, do: "{}"

  def encode_pretty(map, depth) when is_map(map) do
    inner = String.duplicate("\t", depth + 1)

    entries =
      map
      |> Enum.sort_by(fn {key, _} -> key end)
      |> Enum.map(fn {key, value} -> [inner, JSON.encode!(key), ": ", encode_pretty(value, depth + 1)] end)

    ["{\n", Enum.intersperse(entries, ",\n"), "\n", String.duplicate("\t", depth), "}"]
  end

  def encode_pretty([], _depth), do: "[]"

  def encode_pretty(list, depth) when is_list(list) do
    inner = String.duplicate("\t", depth + 1)
    entries = Enum.map(list, &[inner, encode_pretty(&1, depth + 1)])
    ["[\n", Enum.intersperse(entries, ",\n"), "\n", String.duplicate("\t", depth), "]"]
  end

  def encode_pretty(value, _depth), do: JSON.encode!(value)

  defmodule SystemClock do
    @moduledoc false
    @behaviour Rpg.Application.Ports.Clock
    defstruct []

    @impl true
    def now(%__MODULE__{}), do: DateTime.utc_now() |> DateTime.truncate(:second)
  end

  defmodule Settings do
    @moduledoc false
    defstruct locale: nil
    @type t :: %__MODULE__{locale: String.t() | nil}
  end

  defmodule SettingsRepository do
    @moduledoc false
    alias Rpg.Infrastructure.Repositories

    @enforce_keys [:path]
    defstruct [:path]
    @type t :: %__MODULE__{path: Path.t()}

    @spec new(Path.t()) :: t()
    def new(data_dir), do: %__MODULE__{path: Path.join(data_dir, "settings.json")}

    @spec load(%__MODULE__{}) :: Settings.t()
    def load(%__MODULE__{path: path}) do
      if File.exists?(path) do
        data = JsonTypes.obj(Repositories.read_json(path))
        SaveGame.check_schema(data, "settings.json")
        locale = if Map.has_key?(data, "locale"), do: JsonTypes.str(data["locale"])
        %Settings{locale: if(locale in I18n.supported_locales(), do: locale)}
      else
        %Settings{}
      end
    end

    @spec save(%__MODULE__{}, Settings.t()) :: :ok
    def save(%__MODULE__{path: path}, %Settings{locale: locale}) do
      document = if locale, do: %{"schemaVersion" => 1, "locale" => locale}, else: %{"schemaVersion" => 1}
      Repositories.write_json_atomic(path, document)
    end
  end

  defmodule FileSaveRepository do
    @moduledoc false
    @behaviour Rpg.Application.Ports.SaveRepository
    alias Rpg.Infrastructure.Repositories

    @enforce_keys [:path]
    defstruct [:path]

    @spec new(Path.t()) :: %__MODULE__{}
    def new(data_dir), do: %__MODULE__{path: Path.join(data_dir, "save.json")}

    @impl true
    def load(%__MODULE__{path: path}) do
      if File.exists?(path), do: SaveGame.from_map(Repositories.read_json(path))
    end

    @impl true
    def save(%__MODULE__{path: path}, %SaveGame{} = save),
      do: Repositories.write_json_atomic(path, SaveGame.to_map(save))

    @impl true
    def delete(%__MODULE__{path: path}) do
      _ = File.rm(path)
      :ok
    end
  end

  defmodule FileHistoryRepository do
    @moduledoc false
    @behaviour Rpg.Application.Ports.HistoryRepository
    alias Rpg.Infrastructure.Repositories

    @enforce_keys [:dir]
    defstruct [:dir]

    @spec new(Path.t()) :: %__MODULE__{}
    def new(data_dir), do: %__MODULE__{dir: Path.join(data_dir, "history")}

    @impl true
    def add(%__MODULE__{dir: dir}, %RunRecord{} = record) do
      Repositories.write_json_atomic(Path.join(dir, "#{record.run_id}.json"), RunRecord.to_map(record))
    end

    @impl true
    def list(%__MODULE__{dir: dir}) do
      dir
      |> Path.join("*.json")
      |> Path.wildcard()
      |> Enum.sort()
      |> Enum.map(&RunRecord.from_map(Repositories.read_json(&1)))
    end
  end

  defmodule FileProfileRepository do
    @moduledoc false
    @behaviour Rpg.Application.Ports.ProfileRepository
    alias Rpg.Infrastructure.Repositories

    @enforce_keys [:path]
    defstruct [:path]

    @spec new(Path.t()) :: %__MODULE__{}
    def new(data_dir), do: %__MODULE__{path: Path.join(data_dir, "profile.json")}

    @impl true
    def load(%__MODULE__{path: path}) do
      if File.exists?(path) do
        data = JsonTypes.obj(Repositories.read_json(path))
        SaveGame.check_schema(data, "profile.json")
        Profile.from_map(data)
      else
        %Profile{}
      end
    end

    @impl true
    def save(%__MODULE__{path: path}, %Profile{} = profile) do
      Repositories.write_json_atomic(path, Profile.to_map(profile))
    end
  end
end
