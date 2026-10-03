defmodule Rpg.Presentation.Render do
  @moduledoc "Framework-independent rendering helpers (bars, colours, list keys) — specified in docs/tui.md."

  @bar_width 25
  @min_columns 100
  @min_rows 30
  @list_keys "123456789abcdefghijklmnopqrstuvwxyz"

  # Hex colours shared with the Go renderer, so every terminal port looks the same.
  @element_colors %{
    "physical" => "#ffffff",
    "fire" => "#ff5f5f",
    "ice" => "#5fd7ff",
    "energy" => "#d75fff",
    "earth" => "#5fd75f",
    "holy" => "#ffd75f",
    "death" => "#8a8a8a"
  }

  @rarity_colors %{
    "common" => "#ffffff",
    "rare" => "#1e90ff",
    "epic" => "#af87ff",
    "legendary" => "#ffaf00"
  }

  @named_colors %{"green" => "#5fd75f", "yellow" => "#ffd75f", "red" => "#ff5f5f", "blue" => "#5f87ff"}

  @spec bar_width() :: pos_integer()
  def bar_width, do: @bar_width

  @spec min_columns() :: pos_integer()
  def min_columns, do: @min_columns

  @spec min_rows() :: pos_integer()
  def min_rows, do: @min_rows

  @spec element_colors() :: %{String.t() => String.t()}
  def element_colors, do: @element_colors

  @spec rarity_colors() :: %{String.t() => String.t()}
  def rarity_colors, do: @rarity_colors

  @doc "Hex colour of a named colour, an element or a rarity (nil when unknown)."
  @spec color_hex(String.t() | nil) :: String.t() | nil
  def color_hex(nil), do: nil
  def color_hex(name), do: @rarity_colors[name] || @element_colors[name] || @named_colors[name]

  @doc "`█` filled / `░` empty. A living creature always shows at least one filled cell."
  @spec bar(integer(), integer(), pos_integer()) :: String.t()
  def bar(current, maximum, width \\ @bar_width)
  def bar(_current, maximum, width) when maximum <= 0, do: String.duplicate("░", width)

  def bar(current, maximum, width) do
    filled = div(width * max(0, min(current, maximum)), maximum)
    filled = if current > 0, do: max(1, filled), else: filled
    String.duplicate("█", filled) <> String.duplicate("░", width - filled)
  end

  @spec hp_color(integer(), integer()) :: String.t()
  def hp_color(current, maximum) do
    cond do
      maximum > 0 and current * 100 > maximum * 50 -> "green"
      maximum > 0 and current * 100 > maximum * 25 -> "yellow"
      true -> "red"
    end
  end

  @spec list_key(non_neg_integer()) :: String.t()
  def list_key(index), do: String.at(@list_keys, index)

  @spec list_index(String.t()) :: non_neg_integer() | nil
  def list_index(key) when byte_size(key) == 1 do
    case :binary.match(@list_keys, key) do
      {position, 1} -> position
      :nomatch -> nil
    end
  end

  def list_index(_key), do: nil
end
