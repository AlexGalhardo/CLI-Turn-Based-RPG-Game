defmodule Rpg.Application.Events do
  @moduledoc """
  Engine events (docs/cross-language-parity.md §3): flat maps with string keys, so they compare directly with the
  decoded golden files.
  """

  @type value :: integer() | String.t() | boolean()
  @type t :: %{String.t() => value()}

  @error_codes ~w(not_enough_mana not_enough_gold no_potion unknown_spell unknown_potion potion_locked invalid_phase
                  invalid_quantity bag_full cannot_equip invalid_item level_too_low unknown_command)

  @spec event(String.t(), keyword() | map()) :: t()
  def event(type, fields \\ []) do
    Map.new(fields, fn {key, value} -> {to_string(key), value} end) |> Map.put("type", type)
  end

  @spec error(String.t()) :: t()
  def error(code) when code in @error_codes, do: event("error", code: code)

  @spec error_codes() :: [String.t()]
  def error_codes, do: @error_codes
end
