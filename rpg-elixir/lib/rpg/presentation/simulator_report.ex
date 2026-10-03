defmodule Rpg.Presentation.SimulatorReport do
  @moduledoc "Plain-text table of simulator summaries (byte-identical to the Python report)."

  alias Rpg.Application.Simulator.SimulationSummary
  alias Rpg.Domain.Definitions.GameData

  @header ["vocation", "difficulty", "runs", "min", "p10", "median", "p90", "max", "avg lvl", "top killers"]

  @spec render_report([SimulationSummary.t()], GameData.t()) :: String.t()
  def render_report(summaries, %GameData{} = data) do
    rows = [@header | Enum.map(summaries, &row(&1, data))]

    widths =
      Enum.map(0..(length(@header) - 1), fn i -> rows |> Enum.map(&String.length(Enum.at(&1, i))) |> Enum.max() end)

    lines =
      Enum.map(rows, fn row ->
        row
        |> Enum.zip(widths)
        |> Enum.map_join("  ", fn {cell, width} -> String.pad_trailing(cell, width) end)
        |> String.trim_trailing()
      end)

    separator = Enum.map_join(widths, "  ", &String.duplicate("-", &1))
    [header | body] = lines
    Enum.join([header, separator | body], "\n")
  end

  defp row(%SimulationSummary{} = s, data) do
    killers = Enum.map_join(s.top_killers, ", ", fn {id, n} -> "#{GameData.creature(data, id).name} (#{n})" end)

    [
      s.vocation,
      s.difficulty
      | Enum.map(
          [s.runs, s.min_round, s.p10_round, s.median_round, s.p90_round, s.max_round, s.mean_level],
          &Integer.to_string/1
        )
    ] ++ [killers]
  end
end
