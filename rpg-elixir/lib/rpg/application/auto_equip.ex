defmodule Rpg.Application.AutoEquip do
  @moduledoc "Auto-equip with auto-sell (docs/game-design.md §8.1). Consumes no randomness."

  alias Rpg.Application.{Events, Loot, RunState}
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.GameData
  alias Rpg.Domain.Entities.ItemInstance
  alias Rpg.Domain.Enums

  @doc "Highest-score bag item the player can wear in `slot` now; ties go to the lowest uid."
  @spec best_bag_item(RunState.t(), GameData.t(), Enums.slot()) :: ItemInstance.t() | nil
  def best_bag_item(%RunState{player: player}, %GameData{} = data, slot) do
    vocation = GameData.vocation(data, player.vocation_id)

    candidates =
      Enum.filter(player.bag, fn item ->
        definition = GameData.item(data, item.item_id)

        definition.slot == slot and Loot.can_use(definition, vocation) and
          Character.required_level(item, data) <= player.level
      end)

    case candidates do
      [] -> nil
      _ -> Enum.min_by(candidates, &{-Character.item_score(&1, data), &1.uid})
    end
  end

  @spec auto_equip(RunState.t(), GameData.t()) :: {[Events.t()], RunState.t()}
  def auto_equip(%RunState{} = state, %GameData{} = data) do
    {events, state} =
      Enum.reduce(Enums.equipment_slot_order(), {[], state}, fn slot, {events, state} ->
        case best_bag_item(state, data, slot) do
          nil -> {events, state}
          best -> equip_if_better(state, data, slot, best, events)
        end
      end)

    events = Enum.reverse(events)

    if events == [] do
      {events, state}
    else
      player = state.player
      sheet = Character.build_sheet(player, data)
      player = %{player | hp: min(player.hp, sheet.max_hp), mp: min(player.mp, sheet.max_mp)}
      {events, %{state | player: player}}
    end
  end

  # `events` is newest first.
  defp equip_if_better(state, data, slot, best, events) do
    player = state.player
    score = Character.item_score(best, data)
    current = Map.get(player.equipment, slot)

    if current != nil and score <= Character.item_score(current, data) do
      {events, state}
    else
      player = %{player | bag: List.delete(player.bag, best), equipment: Map.put(player.equipment, slot, best)}

      equipped =
        Events.event("item_auto_equipped", uid: best.uid, itemId: best.item_id, slot: slot, score: score)

      case current do
        nil ->
          {[equipped | events], %{state | player: player}}

        current ->
          gold = Character.item_value(current, data)
          sold = Events.event("item_auto_sold", uid: current.uid, itemId: current.item_id, gold: gold)
          {[sold, equipped | events], %{state | player: %{player | gold: player.gold + gold}}}
      end
    end
  end
end
