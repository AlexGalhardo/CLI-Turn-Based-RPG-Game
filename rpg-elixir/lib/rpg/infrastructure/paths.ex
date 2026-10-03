defmodule Rpg.Infrastructure.Paths do
  @moduledoc "Filesystem locations: the shared/ folder and the player's data directory (docs/persistence.md)."

  alias Rpg.Infrastructure.Assets

  @shared_dir_env "RPG_SHARED_DIR"
  @data_dir_env "RPG_DATA_DIR"
  @default_data_dir_name ".cli-turn-based-rpg"

  @doc """
  Looks for a `shared/data` folder from `start` upwards (`RPG_SHARED_DIR` wins). Raises `File.Error` when missing.
  """
  @spec find_shared_dir(Path.t()) :: Path.t()
  def find_shared_dir(start \\ File.cwd!()) do
    case System.get_env(@shared_dir_env) do
      override when override not in [nil, ""] -> override
      _ -> search_upwards(Path.expand(start))
    end
  end

  defp search_upwards(directory) do
    cond do
      File.dir?(Path.join([directory, "shared", "data"])) ->
        Path.join(directory, "shared")

      Path.dirname(directory) == directory ->
        raise File.Error, reason: :enoent, action: "find shared/", path: start_hint()

      true ->
        search_upwards(Path.dirname(directory))
    end
  end

  defp start_hint, do: "shared/ (set #{@shared_dir_env})"

  @doc "The shared source used by the game: `RPG_SHARED_DIR` when set, else the files embedded at build time."
  @spec shared_source() :: Assets.source()
  def shared_source do
    case System.get_env(@shared_dir_env) do
      override when override not in [nil, ""] -> override
      _ -> :embedded
    end
  end

  @spec resolve_data_dir(String.t() | nil) :: Path.t()
  def resolve_data_dir(cli_value \\ nil)
  def resolve_data_dir(cli_value) when cli_value not in [nil, ""], do: cli_value

  def resolve_data_dir(_cli_value) do
    case System.get_env(@data_dir_env) do
      override when override not in [nil, ""] -> override
      _ -> Path.join(System.user_home!(), @default_data_dir_name)
    end
  end
end
