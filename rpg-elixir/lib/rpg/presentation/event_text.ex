defmodule Rpg.Presentation.EventText do
  @moduledoc "Turns engine events into translated sentences. Shared by every presentation (text UI and TUI)."

  alias Rpg.Application.RunState
  alias Rpg.Domain.Definitions.{GameData, UnknownIdError}
  alias Rpg.Infrastructure.I18n
  alias Rpg.Infrastructure.I18n.Translator

  # Event fields that hold ids: they are replaced by display names before formatting.
  @name_fields [{"spellId", "spell"}, {"potionId", "potion"}, {"monsterId", "monster"}, {"itemId", "item"}]
  @label_fields ["element", "status", "rarity", "resource"]

  defmodule EventFormatter do
    @moduledoc false
    @enforce_keys [:data, :translator]
    defstruct @enforce_keys
    @type t :: %__MODULE__{data: GameData.t(), translator: Translator.t()}
  end

  @spec formatter(GameData.t(), Translator.t()) :: EventFormatter.t()
  def formatter(%GameData{} = data, %Translator{} = translator), do: %EventFormatter{data: data, translator: translator}

  @spec format(EventFormatter.t(), map(), RunState.t()) :: String.t()
  def format(%EventFormatter{data: data, translator: t} = formatter, event, %RunState{} = state) do
    params =
      Enum.reduce(@name_fields, event, fn {field, name}, params ->
        if Map.has_key?(event, field), do: Map.put(params, name, display_name(data, field, event[field])), else: params
      end)

    params =
      Enum.reduce(@label_fields, params, fn field, params ->
        if Map.has_key?(event, field), do: Map.put(params, field, I18n.t(t, "#{field}.#{event[field]}")), else: params
      end)

    params =
      if not Map.has_key?(params, "monster") and state.monster != nil,
        do: Map.put(params, "monster", GameData.creature(data, state.monster.creature_id).name),
        else: params

    params =
      if Map.has_key?(event, "uid") and not Map.has_key?(params, "item"),
        do: Map.put(params, "item", item_name_by_uid(formatter, event["uid"], state)),
        else: params

    I18n.t(t, key(event), params)
  end

  defp key(%{"type" => "error", "code" => code}), do: "error.#{code}"

  defp key(%{"type" => type} = event) do
    variant =
      cond do
        event["crit"] === true -> "_crit"
        event["charged"] === true -> "_charged"
        type == "round_started" and event["isBoss"] === true -> "_boss"
        type == "round_started" and event["enemyClass"] == "elite" -> "_elite"
        Map.has_key?(event, "target") -> "_#{event["target"]}"
        true -> ""
      end

    "event.#{type}#{variant}"
  end

  defp display_name(data, field, value) do
    identifier = to_string(value)

    try do
      case field do
        "spellId" -> GameData.spell(data, identifier).name
        "potionId" -> GameData.potion(data, identifier).name
        "monsterId" -> GameData.creature(data, identifier).name
        _ -> GameData.item(data, identifier).name
      end
    rescue
      UnknownIdError -> identifier
    end
  end

  defp item_name_by_uid(%EventFormatter{data: data}, uid, state) do
    player = state.player

    case Enum.find(player.bag ++ Map.values(player.equipment) ++ state.merchant_stock, &(&1.uid == uid)) do
      nil -> "##{uid}"
      item -> GameData.item(data, item.item_id).name
    end
  end
end
