defmodule Rpg.Application.Battle do
  @moduledoc """
  Battle resolution, following docs/game-design.md §6. Every RNG call here is part of the contract.

  The battle is a value (`%Battle{}`) carrying the data, the PRNG, the run state and the events emitted so far
  (newest first). Each step returns a new battle, so the order of PRNG calls is visible in the code: whatever
  consumes randomness takes the battle and returns the updated one.
  """

  alias Rpg.Application.Commands.{Attack, Cast, Defend, UsePotion}
  alias Rpg.Application.{Events, Progression, RunState}
  alias Rpg.Domain.Character.CharacterSheet
  alias Rpg.Domain.Definitions.{GameData, MonsterAttack, MonsterDef, SpellDef}
  alias Rpg.Domain.Entities.{ActiveStatus, MonsterInstance, Player}
  alias Rpg.Domain.{Character, Formulas, Rng}

  @stun "stun"
  @stun_cooldown_turns 2

  @enforce_keys [:data, :rng, :state]
  defstruct [:data, :rng, :state, events: []]

  @type t :: %__MODULE__{data: GameData.t(), rng: Rng.t(), state: RunState.t(), events: [Events.t()]}
  @type outcome :: :ongoing | :victory | :defeat
  @type command :: %Attack{} | %Cast{} | %UsePotion{} | %Defend{}

  # ── validation ──────────────────────────────────────────────────────────────

  @doc "Returns an error event for an invalid command, or nil. Validation never consumes randomness."
  @spec validate(GameData.t(), RunState.t(), command()) :: Events.t() | nil
  def validate(%GameData{} = data, %RunState{player: player}, %Cast{spell_id: spell_id}) do
    vocation = GameData.vocation(data, player.vocation_id)

    cond do
      spell_id not in vocation.spells -> Events.error("unknown_spell")
      player.mp < spell_cost(data, player, GameData.spell(data, spell_id)) -> Events.error("not_enough_mana")
      true -> nil
    end
  end

  def validate(%GameData{} = data, %RunState{player: player}, %UsePotion{potion_id: potion_id}) do
    cond do
      not Enum.any?(data.potions, &(&1.id == potion_id)) -> Events.error("unknown_potion")
      Player.potion_count(player, potion_id) <= 0 -> Events.error("no_potion")
      true -> nil
    end
  end

  def validate(_data, _state, %Attack{}), do: nil
  def validate(_data, _state, %Defend{}), do: nil

  @spec spell_cost(GameData.t(), Player.t(), SpellDef.t()) :: integer()
  def spell_cost(%GameData{} = data, %Player{} = player, %SpellDef{} = spell) do
    level = Formulas.spell_level_for_uses(Map.get(player.spell_uses, spell.id, 0), data.balance.spell_levels)
    Formulas.pct(spell.mana, level.mana_pct)
  end

  # ── turn ────────────────────────────────────────────────────────────────────

  @doc "Plays one player command. Returns the events, the outcome, the new run state and the new PRNG."
  @spec play_turn(GameData.t(), Rng.t(), RunState.t(), command()) :: {[Events.t()], outcome(), RunState.t(), Rng.t()}
  def play_turn(%GameData{} = data, %Rng{} = rng, %RunState{} = state, command) do
    battle = player_action(%__MODULE__{data: data, rng: rng, state: state}, command)

    if monster(battle).hp <= 0 do
      finish(battle, :victory)
    else
      enemy_turns(battle)
    end
  end

  defp enemy_turns(battle) do
    {battle, monster_died} = monster_phase(battle)

    cond do
      monster_died ->
        finish(battle, :victory)

      player(battle).hp <= 0 ->
        finish(battle, :defeat)

      true ->
        case end_of_turn(battle) do
          {battle, true} ->
            finish(battle, :defeat)

          {battle, false} ->
            case consume_stun(player(battle).statuses) do
              {_statuses, false} ->
                finish(battle, :ongoing)

              {statuses, true} ->
                battle
                |> update_player(&%{&1 | statuses: statuses, stun_cooldown: @stun_cooldown_turns})
                |> emit(Events.event("player_stunned"))
                |> enemy_turns()
            end
        end
    end
  end

  defp finish(battle, outcome), do: {Enum.reverse(battle.events), outcome, battle.state, battle.rng}

  # ── helpers ─────────────────────────────────────────────────────────────────

  defp player(%__MODULE__{state: state}), do: state.player
  defp monster(%__MODULE__{state: %RunState{monster: %MonsterInstance{} = monster}}), do: monster
  defp monster(_battle), do: raise(RuntimeError, "battle without a monster")
  defp creature(battle), do: GameData.creature(battle.data, monster(battle).creature_id)
  defp sheet(battle), do: Character.build_sheet(player(battle), battle.data)

  defp update_player(battle, fun), do: put_in(battle.state.player, fun.(player(battle)))
  defp update_monster(battle, fun), do: put_in(battle.state.monster, fun.(monster(battle)))
  defp emit(battle, event), do: %{battle | events: [event | battle.events]}
  defp emit_all(battle, events), do: Enum.reduce(events, battle, &emit(&2, &1))

  defp roll(battle, minimum, maximum) do
    {value, rng} = Rng.roll(battle.rng, minimum, maximum)
    {value, %{battle | rng: rng}}
  end

  defp chance(battle, percent) do
    {value, rng} = Rng.chance(battle.rng, percent)
    {value, %{battle | rng: rng}}
  end

  # ── step 1: player action ───────────────────────────────────────────────────

  defp player_action(battle, %Attack{}), do: melee(battle)
  defp player_action(battle, %Cast{spell_id: id}), do: cast(battle, GameData.spell(battle.data, id))
  defp player_action(battle, %UsePotion{potion_id: id}), do: drink(battle, id)

  defp player_action(battle, %Defend{}) do
    battle |> update_player(&%{&1 | defending: true}) |> emit(Events.event("player_defended"))
  end

  defp melee(battle) do
    sheet = sheet(battle)
    {base, battle} = roll(battle, sheet.melee_min, sheet.melee_max)
    damage = Formulas.pct(base, 100 + sheet.physical_damage)
    {damage, crit, battle} = roll_crit(battle, damage, sheet)
    damage = resisted(battle, damage, sheet.weapon_element)

    battle
    |> hit_monster(damage)
    |> emit(Events.event("player_attacked", damage: damage, crit: crit, element: sheet.weapon_element))
    |> leech(damage, sheet)
  end

  defp cast(battle, %SpellDef{} = spell) do
    player = player(battle)
    sheet = sheet(battle)
    level = Formulas.spell_level_for_uses(Map.get(player.spell_uses, spell.id, 0), battle.data.balance.spell_levels)
    cost = Formulas.pct(spell.mana, level.mana_pct)
    battle = update_player(battle, &%{&1 | mp: &1.mp - cost})
    bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
    {raw, battle} = roll(battle, spell.min + bonus, spell.max + bonus)
    amount = Formulas.pct(Formulas.pct(raw, level.effect_pct), 100 + sheet.spell_power)

    battle =
      case spell.kind do
        :attack -> cast_attack(battle, spell, level.level, amount, cost, sheet)
        :heal -> cast_heal(battle, spell, level.level, amount, cost, sheet)
      end

    {player, events} = Progression.after_cast(battle.data, player(battle), spell, cost)
    battle |> update_player(fn _ -> player end) |> emit_all(events)
  end

  defp cast_attack(battle, spell, level, amount, cost, sheet) do
    {damage, crit, battle} = roll_crit(battle, amount, sheet)
    damage = resisted(battle, damage, spell.element)

    battle =
      battle
      |> hit_monster(damage)
      |> emit(
        Events.event("spell_cast", spellId: spell.id, damage: damage, crit: crit, element: spell.element, mana: cost)
      )
      |> leech(damage, sheet)

    bonus = spell.level3_bonus

    if level == 3 and bonus.status != nil do
      case chance(battle, bonus.chance) do
        {true, battle} ->
          per_turn = max(1, Formulas.pct(damage, battle.data.balance.spell_status_damage_pct))
          apply_status(battle, :monster, bonus.status, per_turn)

        {false, battle} ->
          battle
      end
    else
      battle
    end
  end

  defp cast_heal(battle, spell, level, amount, cost, sheet) do
    healed = min(amount, sheet.max_hp - player(battle).hp)

    battle =
      battle
      |> update_player(&%{&1 | hp: &1.hp + healed})
      |> emit(Events.event("spell_healed", spellId: spell.id, amount: healed, mana: cost))

    if level == 3 and spell.level3_bonus.cleanse do
      expired =
        Enum.map(player(battle).statuses, &Events.event("status_expired", target: "player", status: &1.status_id))

      battle |> update_player(&%{&1 | statuses: []}) |> emit_all(expired)
    else
      battle
    end
  end

  defp drink(battle, potion_id) do
    sheet = sheet(battle)
    potion = GameData.potion(battle.data, potion_id)
    battle = update_player(battle, &%{&1 | potions: Map.update!(&1.potions, potion_id, fn n -> n - 1 end)})
    {amount, battle} = roll(battle, potion.min, potion.max)
    player = player(battle)

    {restored, battle} =
      case potion.resource do
        :hp ->
          restored = min(amount, sheet.max_hp - player.hp)
          {restored, update_player(battle, &%{&1 | hp: &1.hp + restored})}

        :mp ->
          restored = min(amount, sheet.max_mp - player.mp)
          {restored, update_player(battle, &%{&1 | mp: &1.mp + restored})}
      end

    emit(
      battle,
      Events.event("potion_used", potionId: potion_id, amount: restored, resource: Atom.to_string(potion.resource))
    )
  end

  defp roll_crit(battle, damage, %CharacterSheet{} = sheet) do
    case chance(battle, sheet.crit_chance) do
      {true, battle} ->
        {Formulas.pct(damage, battle.data.balance.crit_multiplier_pct + sheet.crit_damage), true, battle}

      {false, battle} ->
        {damage, false, battle}
    end
  end

  defp resisted(battle, damage, element) do
    case MonsterDef.resistance(creature(battle), element) do
      0 -> 0
      resistance -> max(1, Formulas.pct(damage, resistance))
    end
  end

  defp hit_monster(battle, damage), do: update_monster(battle, &%{&1 | hp: max(0, &1.hp - damage)})

  defp leech(battle, damage, sheet) do
    player = player(battle)
    hp_gain = min(Formulas.pct(damage, sheet.life_leech), sheet.max_hp - player.hp)
    mp_gain = min(Formulas.pct(damage, sheet.mana_leech), sheet.max_mp - player.mp)

    if hp_gain <= 0 and mp_gain <= 0 do
      battle
    else
      hp_gain = max(0, hp_gain)
      mp_gain = max(0, mp_gain)

      battle
      |> update_player(&%{&1 | hp: &1.hp + hp_gain, mp: &1.mp + mp_gain})
      |> emit(Events.event("leeched", hp: hp_gain, mp: mp_gain))
    end
  end

  # ── step 3: monster phase ───────────────────────────────────────────────────

  # Returns {battle, true} when the monster died from its own status ticks.
  defp monster_phase(battle) do
    battle = tick(battle, :monster)

    if monster(battle).hp <= 0 do
      {battle, true}
    else
      case consume_stun(monster(battle).statuses) do
        {statuses, true} ->
          battle =
            battle
            |> update_monster(&%{&1 | statuses: statuses, stun_cooldown: @stun_cooldown_turns})
            |> emit(Events.event("monster_stunned"))

          {battle, false}

        {_statuses, false} ->
          {monster_action(battle), false}
      end
    end
  end

  defp monster_action(battle) do
    monster = monster(battle)
    every = battle.data.balance.boss_telegraph_every

    boss_move =
      if monster.is_boss do
        position = rem(monster.boss_actions, every + 1)
        charge_id = creature(battle).charge_attack

        cond do
          charge_id != nil and position == every - 1 -> {:telegraph, MonsterInstance.attack(monster, charge_id)}
          charge_id != nil and position == every -> {:charge, MonsterInstance.attack(monster, charge_id)}
          true -> :normal
        end
      else
        :normal
      end

    battle =
      if monster.is_boss, do: update_monster(battle, &%{&1 | boss_actions: &1.boss_actions + 1}), else: battle

    case boss_move do
      {:telegraph, charge} ->
        emit(battle, Events.event("boss_telegraph", attackId: charge.id, element: charge.element))

      {:charge, charge} ->
        resolve_monster_attack(battle, charge, true)

      :normal ->
        {index, rng} = Rng.weighted(battle.rng, Enum.map(monster.attacks, & &1.weight))
        resolve_monster_attack(%{battle | rng: rng}, Enum.at(monster.attacks, index), false)
    end
  end

  defp resolve_monster_attack(battle, %MonsterAttack{} = attack, charged) do
    sheet = sheet(battle)

    case chance(battle, sheet.dodge) do
      {true, battle} ->
        emit(battle, Events.event("attack_dodged", attackId: attack.id))

      {false, battle} ->
        {parried, battle} = if attack.element == "physical", do: chance(battle, sheet.parry), else: {false, battle}

        if parried do
          emit(battle, Events.event("attack_parried", attackId: attack.id))
        else
          land_monster_attack(battle, attack, charged, sheet)
        end
    end
  end

  defp land_monster_attack(battle, attack, charged, sheet) do
    balance = battle.data.balance
    {damage, battle} = roll(battle, attack.min, attack.max)
    damage = if charged, do: Formulas.pct(damage, balance.boss_charge_damage_pct), else: damage
    damage = if attack.element == "physical", do: Formulas.armor_mitigation(damage, sheet.armor), else: damage
    damage = Formulas.pct(damage, 100 - CharacterSheet.protection(sheet, attack.element))
    damage = if player(battle).defending, do: Formulas.pct(damage, balance.defend_damage_pct), else: damage
    damage = max(1, damage)

    battle =
      battle
      |> update_player(&%{&1 | hp: max(0, &1.hp - damage)})
      |> emit(
        Events.event("monster_attacked", attackId: attack.id, damage: damage, element: attack.element, charged: charged)
      )

    case attack.status do
      nil ->
        battle

      status ->
        case chance(battle, status.chance) do
          {true, battle} ->
            apply_status(battle, :player, status.status, max(1, Formulas.pct(damage, status.damage_pct)))

          {false, battle} ->
            battle
        end
    end
  end

  # ── step 5: end of turn ─────────────────────────────────────────────────────

  # Returns {battle, true} when the player died from status ticks.
  defp end_of_turn(battle) do
    battle = tick(battle, :player)

    if player(battle).hp <= 0 do
      {battle, true}
    else
      sheet = sheet(battle)
      player = player(battle)
      hp_gain = max(0, min(sheet.hp_regen, sheet.max_hp - player.hp))
      mp_gain = max(0, min(sheet.mp_regen, sheet.max_mp - player.mp))
      battle = update_player(battle, &%{&1 | hp: &1.hp + hp_gain, mp: &1.mp + mp_gain})

      battle =
        if hp_gain > 0 or mp_gain > 0,
          do: emit(battle, Events.event("regenerated", hp: hp_gain, mp: mp_gain)),
          else: battle

      battle =
        battle
        |> update_player(&%{&1 | defending: false, stun_cooldown: max(0, &1.stun_cooldown - 1)})
        |> update_monster(&%{&1 | stun_cooldown: max(0, &1.stun_cooldown - 1)})

      {put_in(battle.state.turn, battle.state.turn + 1), false}
    end
  end

  # ── statuses ────────────────────────────────────────────────────────────────

  defp apply_status(battle, target, status_id, per_turn) do
    definition = GameData.status(battle.data, status_id)

    {statuses, cooldown} =
      case target do
        :player -> {player(battle).statuses, player(battle).stun_cooldown}
        :monster -> {monster(battle).statuses, monster(battle).stun_cooldown}
      end

    immune = target == :monster and MonsterDef.resistance(creature(battle), definition.element) == 0

    cond do
      immune ->
        battle

      definition.kind == :stun ->
        if cooldown > 0 or Enum.any?(statuses, &(&1.status_id == @stun)) do
          battle
        else
          stun = %ActiveStatus{status_id: @stun, turns: definition.turns, per_turn: 0}

          battle
          |> set_statuses(target, statuses ++ [stun])
          |> emit(status_applied(target, @stun, definition.turns, 0))
        end

      true ->
        {statuses, entry} =
          case Enum.find_index(statuses, &(&1.status_id == status_id)) do
            nil ->
              entry = %ActiveStatus{status_id: status_id, turns: definition.turns, per_turn: per_turn}
              {statuses ++ [entry], entry}

            index ->
              existing = Enum.at(statuses, index)
              entry = %{existing | turns: definition.turns, per_turn: max(existing.per_turn, per_turn)}
              {List.replace_at(statuses, index, entry), entry}
          end

        battle
        |> set_statuses(target, statuses)
        |> emit(status_applied(target, status_id, entry.turns, entry.per_turn))
    end
  end

  defp status_applied(target, status, turns, per_turn) do
    Events.event("status_applied", target: Atom.to_string(target), status: status, turns: turns, perTurn: per_turn)
  end

  defp set_statuses(battle, :player, statuses), do: update_player(battle, &%{&1 | statuses: statuses})
  defp set_statuses(battle, :monster, statuses), do: update_monster(battle, &%{&1 | statuses: statuses})

  defp tick(battle, target) do
    statuses =
      case target do
        :player -> player(battle).statuses
        :monster -> monster(battle).statuses
      end

    {battle, remaining} =
      Enum.reduce(statuses, {battle, []}, fn status, {battle, kept} ->
        definition = GameData.status(battle.data, status.status_id)

        if definition.kind != :dot do
          {battle, [status | kept]}
        else
          tick_one(battle, target, status, definition, kept)
        end
      end)

    set_statuses(battle, target, Enum.reverse(remaining))
  end

  defp tick_one(battle, target, status, definition, kept) do
    damage = status_damage(battle, target, status.per_turn, definition.element)
    name = Atom.to_string(target)

    battle =
      case target do
        :player -> update_player(battle, &%{&1 | hp: max(0, &1.hp - damage)})
        :monster -> hit_monster(battle, damage)
      end

    battle = emit(battle, Events.event("status_ticked", target: name, status: status.status_id, damage: damage))
    status = %{status | turns: status.turns - 1}

    if status.turns <= 0 do
      {emit(battle, Events.event("status_expired", target: name, status: status.status_id)), kept}
    else
      {battle, [status | kept]}
    end
  end

  defp status_damage(battle, :player, per_turn, element) do
    max(1, Formulas.pct(per_turn, 100 - CharacterSheet.protection(sheet(battle), element)))
  end

  defp status_damage(battle, :monster, per_turn, element) do
    case MonsterDef.resistance(creature(battle), element) do
      0 -> 0
      resistance -> max(1, Formulas.pct(per_turn, resistance))
    end
  end

  # Removes the first stun, if any: {remaining statuses, consumed?}.
  defp consume_stun(statuses) do
    case Enum.find_index(statuses, &(&1.status_id == @stun)) do
      nil -> {statuses, false}
      index -> {List.delete_at(statuses, index), true}
    end
  end
end
