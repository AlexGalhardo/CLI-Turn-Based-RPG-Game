defmodule Rpg.Application.Ports do
  @moduledoc """
  Ports implemented by the infrastructure layer (Dependency Inversion: the application owns the interfaces).

  Elixir has no interfaces on values; a *behaviour* is a contract for a module. An adapter is a struct whose module
  implements the behaviour (`@behaviour` + `@impl true`), and the dispatch functions below call
  `adapter.__struct__.callback(adapter, ...)`. The compiler warns when an adapter misses a callback, much like a Go
  type that does not satisfy an interface.
  """

  defmodule Clock do
    @moduledoc false
    @callback now(clock :: struct()) :: DateTime.t()

    @spec now(struct()) :: DateTime.t()
    def now(%module{} = clock), do: module.now(clock)
  end

  defmodule SaveRepository do
    @moduledoc false
    alias Rpg.Application.SaveGame

    @callback load(repo :: struct()) :: SaveGame.t() | nil
    @callback save(repo :: struct(), SaveGame.t()) :: :ok
    @callback delete(repo :: struct()) :: :ok

    @spec load(struct()) :: SaveGame.t() | nil
    def load(%module{} = repo), do: module.load(repo)

    @spec save(struct(), SaveGame.t()) :: :ok
    def save(%module{} = repo, save), do: module.save(repo, save)

    @spec delete(struct()) :: :ok
    def delete(%module{} = repo), do: module.delete(repo)
  end

  defmodule HistoryRepository do
    @moduledoc false
    alias Rpg.Application.RunRecord

    @callback add(repo :: struct(), RunRecord.t()) :: :ok
    @callback list(repo :: struct()) :: [RunRecord.t()]

    @spec add(struct(), RunRecord.t()) :: :ok
    def add(%module{} = repo, record), do: module.add(repo, record)

    @spec list(struct()) :: [RunRecord.t()]
    def list(%module{} = repo), do: module.list(repo)
  end

  defmodule ProfileRepository do
    @moduledoc false
    alias Rpg.Application.Profile

    @callback load(repo :: struct()) :: Profile.t()
    @callback save(repo :: struct(), Profile.t()) :: :ok

    @spec load(struct()) :: Profile.t()
    def load(%module{} = repo), do: module.load(repo)

    @spec save(struct(), Profile.t()) :: :ok
    def save(%module{} = repo, profile), do: module.save(repo, profile)
  end
end
