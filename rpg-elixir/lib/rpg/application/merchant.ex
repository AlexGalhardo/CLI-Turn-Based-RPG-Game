defmodule Rpg.Application.Merchant do
  @moduledoc "Merchant phase: potions, bag, equipment and rotating stock (docs/game-design.md §10)."

  alias Rpg.Application.Commands.{BuyPotion, BuyStockItem, Equip, SellItem, Unequip}
  alias Rpg.Application.{AutoEquip, Events, Loot, RunState}
  alias Rpg.Domain.Definitions.GameData
  alias Rpg.Domain.Entities.{ItemInstance, Player}
  alias Rpg.Domain.{Character, Formulas, Rng}

  @max_potions_per_purchase 99

  @type command :: %BuyPotion{} | %SellItem{} | %Equip{} | %Unequip{} | %BuyStockItem{}

  @spec stock_price(ItemInstance.t(), GameData.t()) :: integer()
  def stock_price(%ItemInstance{} = item, %GameData{} = data) do
    Formulas.pct(Character.item_value(item, data), data.balance.merchant_markup_pct)
  end

  @spec available_potions(RunState.t(), GameData.t()) :: [String.t()]
  def available_potions(%RunState{round: round}, %GameData{} = data) do
    for potion <- data.potions, potion.unlock_round <= round + 1, do: potion.id
  end

  @doc "Generates the rotating stock for the tier of the next round."
  @spec enter(GameData.t(), Rng.t(), RunState.t()) :: {[Events.t()], RunState.t(), Rng.t()}
  def enter(%GameData{} = data, %Rng{} = rng, %RunState{} = state) do
    vocation = GameData.vocation(data, state.player.vocation_id)
    tier = Formulas.round_info(state.round + 1, data.balance, GameData.tier_count(data)).tier

    {state, rng} =
      Enum.reduce(1..data.balance.merchant_stock_size//1, {%{state | merchant_stock: []}, rng}, fn _, {state, rng} ->
        weights = Map.fetch!(data.balance.rarity_weights, "merchant")
        opts = [vocation: vocation, tier: tier, weights: weights, uid: state.next_item_uid]

        case Loot.generate_item(data, rng, opts) do
          {nil, rng} ->
            {state, rng}

          {item, rng} ->
            {_uid, state} = RunState.take_item_uid(state)
            {%{state | merchant_stock: state.merchant_stock ++ [item]}, rng}
        end
      end)

    {[Events.event("merchant_entered", round: state.round)], state, rng}
  end

  @doc "Merchant actions consume no randomness, so they only return the events and the new state."
  @spec handle(GameData.t(), RunState.t(), command()) :: {[Events.t()], RunState.t()}
  def handle(data, state, %BuyPotion{potion_id: id, quantity: quantity}), do: buy_potion(data, state, id, quantity)
  def handle(data, state, %SellItem{uid: uid}), do: sell(data, state, uid)
  def handle(data, state, %Equip{uid: uid}), do: equip(data, state, uid)
  def handle(data, state, %Unequip{slot: slot}), do: unequip(data, state, slot)
  def handle(data, state, %BuyStockItem{index: index}), do: buy_stock(data, state, index)

  defp buy_potion(data, state, potion_id, quantity) do
    player = state.player

    cond do
      not Enum.any?(data.potions, &(&1.id == potion_id)) ->
        {[Events.error("unknown_potion")], state}

      potion_id not in available_potions(state, data) ->
        {[Events.error("potion_locked")], state}

      quantity < 1 or quantity > @max_potions_per_purchase ->
        {[Events.error("invalid_quantity")], state}

      player.gold < GameData.potion(data, potion_id).price * quantity ->
        {[Events.error("not_enough_gold")], state}

      true ->
        cost = GameData.potion(data, potion_id).price * quantity

        player = %{
          player
          | gold: player.gold - cost,
            potions: Map.put(player.potions, potion_id, Player.potion_count(player, potion_id) + quantity)
        }

        {[Events.event("potion_bought", potionId: potion_id, quantity: quantity, gold: cost)],
         %{state | player: player}}
    end
  end

  defp find_in_bag(%Player{bag: bag}, uid), do: Enum.find(bag, &(&1.uid == uid))

  defp sell(data, state, uid) do
    case find_in_bag(state.player, uid) do
      nil ->
        {[Events.error("invalid_item")], state}

      item ->
        value = Character.item_value(item, data)
        player = %{state.player | bag: List.delete(state.player.bag, item), gold: state.player.gold + value}
        {[Events.event("item_sold", uid: uid, itemId: item.item_id, gold: value)], %{state | player: player}}
    end
  end

  defp equip(data, state, uid) do
    player = state.player

    with %ItemInstance{} = item <- find_in_bag(player, uid),
         definition = GameData.item(data, item.item_id),
         true <- Loot.can_use(definition, GameData.vocation(data, player.vocation_id)),
         true <- Character.required_level(item, data) <= player.level || :level_too_low do
      slot = definition.slot
      {previous, equipment} = Map.pop(player.equipment, slot)
      bag = List.delete(player.bag, item)

      {bag, events} =
        case previous do
          nil ->
            {bag, []}

          previous ->
            {bag ++ [previous],
             [Events.event("item_unequipped", uid: previous.uid, itemId: previous.item_id, slot: slot)]}
        end

      player = %{player | bag: bag, equipment: Map.put(equipment, slot, item)}
      events = events ++ [Events.event("item_equipped", uid: item.uid, itemId: item.item_id, slot: slot)]
      {events, %{state | player: clamp_resources(data, player)}}
    else
      nil -> {[Events.error("invalid_item")], state}
      false -> {[Events.error("cannot_equip")], state}
      :level_too_low -> {[Events.error("level_too_low")], state}
    end
  end

  defp unequip(data, state, slot) do
    player = state.player

    case Map.fetch(player.equipment, slot) do
      :error ->
        {[Events.error("invalid_item")], state}

      {:ok, _item} when length(player.bag) >= data.balance.bag_capacity ->
        {[Events.error("bag_full")], state}

      {:ok, item} ->
        player = %{player | equipment: Map.delete(player.equipment, slot), bag: player.bag ++ [item]}
        event = Events.event("item_unequipped", uid: item.uid, itemId: item.item_id, slot: slot)
        {[event], %{state | player: clamp_resources(data, player)}}
    end
  end

  defp buy_stock(data, state, index) do
    cond do
      index < 0 or index >= length(state.merchant_stock) ->
        {[Events.error("invalid_item")], state}

      length(state.player.bag) >= data.balance.bag_capacity ->
        {[Events.error("bag_full")], state}

      true ->
        item = Enum.at(state.merchant_stock, index)
        price = stock_price(item, data)

        if state.player.gold < price do
          {[Events.error("not_enough_gold")], state}
        else
          player = %{state.player | gold: state.player.gold - price, bag: state.player.bag ++ [item]}
          state = %{state | player: player, merchant_stock: List.delete_at(state.merchant_stock, index)}
          bought = Events.event("item_bought", uid: item.uid, itemId: item.item_id, gold: price)

          if state.config.auto_equip do
            {more, state} = AutoEquip.auto_equip(state, data)
            {[bought | more], state}
          else
            {[bought], state}
          end
        end
    end
  end

  defp clamp_resources(data, player) do
    sheet = Character.build_sheet(player, data)
    %{player | hp: min(player.hp, sheet.max_hp), mp: min(player.mp, sheet.max_mp)}
  end
end
