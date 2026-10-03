defmodule Rpg.Infrastructure.Migrations do
  @moduledoc """
  Upgrades older save/settings/history/profile documents to the current schema (docs/persistence.md).

  Each migration takes the raw JSON of version N and returns version N + 1, so the application layer only ever reads
  the current format. Version 1 → 2 is the 1.4.0 "ARPG update".
  """

  alias Rpg.Domain.JsonTypes

  @removed_rarity "epic"
  @replacement_rarity "legendary"

  @spec migrate_save(map()) :: map()
  def migrate_save(document) do
    if version(document) < 2 do
      document |> Map.update!("run", &run_v1_to_v2/1) |> Map.put("schemaVersion", 2)
    else
      document
    end
  end

  @spec migrate_history(map()) :: map()
  def migrate_history(document) do
    if version(document) < 2 do
      document
      |> Map.put_new("won", false)
      |> Map.update!("stats", &stats_v1_to_v2/1)
      |> Map.put("schemaVersion", 2)
    else
      document
    end
  end

  @spec migrate_profile(map()) :: map()
  def migrate_profile(document) do
    if version(document) < 2 do
      document
      |> update_existing("hallOfFame", fn
        hall when is_list(hall) -> Enum.map(hall, &Map.put_new(JsonTypes.obj(&1), "won", false))
        other -> other
      end)
      |> Map.put("schemaVersion", 2)
    else
      document
    end
  end

  @spec migrate_settings(map()) :: map()
  def migrate_settings(document) do
    if version(document) < 2 do
      document
      |> Map.put_new("autoEquip", false)
      |> Map.put_new("battleSpeed", 1)
      |> Map.put("schemaVersion", 2)
    else
      document
    end
  end

  defp version(document), do: JsonTypes.int(Map.get(document, "schemaVersion", 1))

  defp update_existing(map, key, fun) do
    if Map.has_key?(map, key), do: Map.update!(map, key, fun), else: map
  end

  defp rename_rarities(items) when is_list(items) do
    Enum.map(items, fn raw ->
      item = JsonTypes.obj(raw)
      if Map.get(item, "rarity") == @removed_rarity, do: Map.put(item, "rarity", @replacement_rarity), else: item
    end)
  end

  defp rename_rarities(other), do: other

  defp stats_v1_to_v2(raw) do
    stats =
      raw
      |> JsonTypes.obj()
      |> Map.put_new("itemsAutoEquipped", 0)
      |> Map.put_new("elitesKilled", 0)
      |> Map.put_new("potionsDropped", %{})

    dropped = JsonTypes.obj(Map.fetch!(stats, "itemsDropped"))

    dropped =
      case Map.pop(dropped, @removed_rarity) do
        {nil, dropped} ->
          dropped

        {epic, dropped} ->
          legendary = JsonTypes.int(Map.get(dropped, @replacement_rarity, 0))
          Map.put(dropped, @replacement_rarity, legendary + JsonTypes.int(epic))
      end

    stats
    |> Map.put("itemsDropped", dropped)
    |> update_existing("droppedItems", &rename_rarities/1)
  end

  defp run_v1_to_v2(raw) do
    run = JsonTypes.obj(raw)

    run
    |> Map.update!("config", &Map.put_new(JsonTypes.obj(&1), "autoEquip", false))
    |> Map.put_new("won", false)
    |> update_existing("monster", fn
      monster when is_map(monster) ->
        Map.put_new(monster, "enemyClass", if(Map.get(monster, "isBoss") === true, do: "boss", else: "normal"))

      other ->
        other
    end)
    |> Map.update!("player", fn player ->
      player
      |> JsonTypes.obj()
      |> update_existing("bag", &rename_rarities/1)
      |> update_existing("equipment", fn
        equipment when is_map(equipment) -> Map.new(equipment, fn {slot, item} -> {slot, rename_item(item)} end)
        other -> other
      end)
    end)
    |> update_existing("merchantStock", &rename_rarities/1)
    |> Map.update!("stats", &stats_v1_to_v2/1)
  end

  defp rename_item(item), do: item |> List.wrap() |> rename_rarities() |> hd()
end
