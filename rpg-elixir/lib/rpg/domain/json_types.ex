defmodule Rpg.Domain.JsonTypes do
  @moduledoc """
  Typed helpers to read untrusted JSON (data files, saves). `JSON.decode/1` returns plain maps and lists, so each
  reader checks the shape and raises `Rpg.Domain.JsonTypes.Error` (the Python port raises `TypeError`).
  """

  defmodule Error do
    defexception [:message]
  end

  @spec obj(term()) :: map()
  def obj(value) when is_map(value), do: value
  def obj(value), do: fail("object", value)

  @spec list(term()) :: list()
  def list(value) when is_list(value), do: value
  def list(value), do: fail("list", value)

  @spec int(term()) :: integer()
  def int(value) when is_integer(value), do: value
  def int(value), do: fail("int", value)

  @spec str(term()) :: String.t()
  def str(value) when is_binary(value), do: value
  def str(value), do: fail("str", value)

  @spec bool(term()) :: boolean()
  def bool(value) when is_boolean(value), do: value
  def bool(value), do: fail("bool", value)

  @doc "Reads a required key, raising `Error` (naming the key) when it is missing."
  @spec field(map(), String.t()) :: term()
  def field(map, key) do
    case Map.fetch(obj(map), key) do
      {:ok, value} -> value
      :error -> raise Error, message: "missing key #{inspect(key)}"
    end
  end

  defp fail(expected, value) do
    raise Error, message: "expected #{expected}, got #{type_name(value)}"
  end

  defp type_name(value) when is_map(value), do: "object"
  defp type_name(value) when is_list(value), do: "list"
  defp type_name(value) when is_boolean(value), do: "bool"
  defp type_name(value) when is_integer(value), do: "int"
  defp type_name(value) when is_binary(value), do: "str"
  defp type_name(nil), do: "null"
  defp type_name(_value), do: "other"
end
