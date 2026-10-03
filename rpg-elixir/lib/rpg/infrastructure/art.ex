defmodule Rpg.Infrastructure.Art do
  @moduledoc """
  Parses shared/art files: `@animation` headers, frames separated by `%%` (docs/data-format.md).

  An animation set is a map of name → list of frames; a frame is a list of lines.
  """

  alias Rpg.Domain.Definitions.MonsterDef
  alias Rpg.Infrastructure.Assets

  @type frame :: [String.t()]
  @type animations :: %{String.t() => [frame()]}

  defmodule ArtLibrary do
    @moduledoc false
    @enforce_keys [:source]
    defstruct [:source]
    @type t :: %__MODULE__{source: Assets.source()}
  end

  @spec parse_art(String.t()) :: animations()
  def parse_art(text) do
    lines = text |> String.replace("\r\n", "\n") |> String.trim_trailing("\n") |> String.split("\n")

    {animations, _current} =
      Enum.reduce(lines, {%{}, nil}, fn line, {animations, current} ->
        cond do
          String.starts_with?(line, "@") ->
            name = line |> String.slice(1..-1//1) |> String.trim()
            {Map.put(animations, name, [[]]), name}

          line == "%%" ->
            if current == nil, do: raise(ArgumentError, "frame separator before any @animation")
            {Map.update!(animations, current, &[[] | &1]), current}

          current == nil ->
            raise ArgumentError, "art content before the first @animation"

          true ->
            {Map.update!(animations, current, fn [frame | rest] -> [[line | frame] | rest] end), current}
        end
      end)

    Map.new(animations, fn {name, frames} -> {name, frames |> Enum.reverse() |> Enum.map(&Enum.reverse/1)} end)
  end

  @spec library(Assets.source()) :: ArtLibrary.t()
  def library(source \\ :embedded), do: %ArtLibrary{source: source}

  @spec for_creature(ArtLibrary.t(), MonsterDef.t()) :: animations()
  def for_creature(%ArtLibrary{} = library, %MonsterDef{} = creature) do
    if creature.is_boss,
      do: load_file(library, "bosses", creature.id),
      else: load_file(library, "families", creature.family)
  end

  # Parsed files are memoised per process: the renderer asks for the same art on every frame.
  @spec load_file(ArtLibrary.t(), String.t(), String.t()) :: animations()
  def load_file(%ArtLibrary{source: source}, folder, name) do
    key = {__MODULE__, source, folder, name}

    case Process.get(key) do
      nil ->
        {:ok, text} = Assets.read(source, "art/#{folder}/#{name}.txt")
        animations = parse_art(text)
        Process.put(key, animations)
        animations

      animations ->
        animations
    end
  end

  @doc "Frame of `animation` at animation tick `tick`, falling back to idle; empty when there is no art."
  @spec frame_for(animations(), String.t(), non_neg_integer()) :: frame()
  def frame_for(animations, animation, tick) do
    frames = non_empty(Map.get(animations, animation)) || non_empty(Map.get(animations, "idle")) || []

    case frames do
      [] -> []
      frames -> Enum.at(frames, rem(tick, length(frames)))
    end
  end

  defp non_empty([]), do: nil
  defp non_empty(value), do: value
end
