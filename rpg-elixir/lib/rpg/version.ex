defmodule Rpg.Version do
  @moduledoc "Game version (one SemVer for the monorepo; also in mix.exs)."

  @version "1.6.1"

  @spec version() :: String.t()
  def version, do: @version
end
