defmodule Rpg.Application.GreedyBot do
  @moduledoc """
  A deterministic heuristic player used by the simulator and by the end-to-end parity tests.

  The bot only reads the run state and returns one command at a time, so it can drive any engine implementation.
  Its decisions are part of the golden "bot full run" files: changing them requires regenerating those files.
  Tie-breakers mirror Python's `max(key=(value, id))`: the highest value wins, then the highest id.
  """

  alias Rpg.Application.Commands.{Attack, BuyPotion, Cast, Defend, EndRun, Equip, NextFight, SellItem, UsePotion}
  alias Rpg.Application.{Battle, Commands, Loot, Merchant, RunState}
  alias Rpg.Domain.Definitions.{GameData, MonsterDef}
  alias Rpg.Domain.Entities.{MonsterInstance, Player}
  alias Rpg.Domain.Character

  @heal_threshold_pct 45
  @mana_potion_threshold_pct 25
  @max_potion_stock 20

  @spec choose(GameData.t(), RunState.t()) :: Commands.t()
  def choose(%GameData{} = data, %RunState{phase: :battle} = state), do: battle(data, state)
  def choose(%GameData{}, %RunState{phase: :victory}), do: %EndRun{}
  def choose(%GameData{} = data, %RunState{} = state), do: merchant(data, state)

  # ── battle ──────────────────────────────────────────────────────────────────

  defp battle(_data, %RunState{monster: nil}), do: raise(RuntimeError, "battle without a monster")

  defp battle(data, %RunState{player: player, monster: monster} = state) do
    sheet = Character.build_sheet(player, data)

    cond do
      charge_incoming?(data, monster) ->
        %Defend{}

      player.hp * 100 < sheet.max_hp * @heal_threshold_pct and heal(data, state) != nil ->
        heal(data, state)

      player.mp * 100 < sheet.max_mp * @mana_potion_threshold_pct and best_owned_potion(data, player, :mp) != nil ->
        %UsePotion{potion_id: best_owned_potion(data, player, :mp).id}

      (spell = best_attack_spell(data, state, monster)) != nil ->
        %Cast{spell_id: spell.id}

      true ->
        %Attack{}
    end
  end

  defp charge_incoming?(_data, %MonsterInstance{is_boss: false}), do: false

  defp charge_incoming?(data, %MonsterInstance{boss_actions: actions}) do
    every = data.balance.boss_telegraph_every
    rem(actions, every + 1) == every
  end

  defp spells(data, %RunState{player: player}, kind) do
    data
    |> GameData.vocation(player.vocation_id)
    |> Map.fetch!(:spells)
    |> Enum.map(&GameData.spell(data, &1))
    |> Enum.filter(&(&1.kind == kind and Battle.spell_cost(data, player, &1) <= player.mp))
  end

  defp heal(data, state) do
    case spells(data, state, :heal) do
      [] ->
        case best_owned_potion(data, state.player, :hp) do
          nil -> nil
          potion -> %UsePotion{potion_id: potion.id}
        end

      spells ->
        %Cast{spell_id: Enum.max_by(spells, &{&1.max, &1.id}).id}
    end
  end

  defp best_owned_potion(data, player, resource) do
    case Enum.filter(data.potions, &(&1.resource == resource and Player.potion_count(player, &1.id) > 0)) do
      [] -> nil
      owned -> Enum.max_by(owned, &{&1.max, &1.id})
    end
  end

  defp best_attack_spell(data, state, monster) do
    creature = GameData.creature(data, monster.creature_id)

    case Enum.filter(spells(data, state, :attack), &(MonsterDef.resistance(creature, &1.element) > 0)) do
      [] -> nil
      candidates -> Enum.max_by(candidates, &{(&1.min + &1.max) * MonsterDef.resistance(creature, &1.element), &1.id})
    end
  end

  # ── merchant ────────────────────────────────────────────────────────────────

  defp merchant(data, %RunState{player: player} = state) do
    vocation = GameData.vocation(data, player.vocation_id)

    upgrade =
      player.bag
      |> Enum.sort_by(& &1.uid)
      |> Enum.find(fn item ->
        definition = GameData.item(data, item.item_id)

        Loot.can_use(definition, vocation) and Character.required_level(item, data) <= player.level and
          case Map.get(player.equipment, definition.slot) do
            nil -> true
            current -> Character.item_score(item, data) > Character.item_score(current, data)
          end
      end)

    cond do
      upgrade != nil -> %Equip{uid: upgrade.uid}
      player.bag != [] -> %SellItem{uid: Enum.min_by(player.bag, & &1.uid).uid}
      true -> potion_purchase(data, state, :hp) || potion_purchase(data, state, :mp) || %NextFight{}
    end
  end

  defp potion_purchase(data, %RunState{player: player} = state, resource) do
    unlocked = Merchant.available_potions(state, data)

    case Enum.filter(data.potions, &(&1.resource == resource and &1.id in unlocked)) do
      [] ->
        nil

      options ->
        best = Enum.max_by(options, &{&1.max, &1.id})
        owned = options |> Enum.map(&Player.potion_count(player, &1.id)) |> Enum.sum()
        target = min(@max_potion_stock, 5 + div(state.round, 5))
        budget = if resource == :mp, do: div(player.gold, 2), else: player.gold
        quantity = min(target - owned, div(budget, best.price))
        if quantity <= 0, do: nil, else: %BuyPotion{potion_id: best.id, quantity: quantity}
    end
  end
end
