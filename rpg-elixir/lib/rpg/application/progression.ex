defmodule Rpg.Application.Progression do
  @moduledoc "Experience, levels, magic levels and spell levels (docs/game-design.md §4 and §9)."

  alias Rpg.Application.Events
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.{GameData, SpellDef}
  alias Rpg.Domain.Entities.Player
  alias Rpg.Domain.Formulas

  @spec gain_experience(GameData.t(), Player.t(), integer()) :: {Player.t(), [Events.t()]}
  def gain_experience(%GameData{} = data, %Player{} = player, amount) do
    player = %{player | xp: player.xp + amount}
    vocation = GameData.vocation(data, player.vocation_id)
    {player, level_ups} = level_up(data, vocation, player, [])
    {player, [Events.event("xp_gained", amount: amount, total: player.xp) | level_ups]}
  end

  defp level_up(data, vocation, player, events) do
    if player.xp >= Formulas.xp_for_level(player.level + 1) do
      player = %{player | level: player.level + 1}
      sheet = Character.build_sheet(player, data)

      player = %{
        player
        | hp: min(sheet.max_hp, player.hp + vocation.hp_per_level),
          mp: min(sheet.max_mp, player.mp + vocation.mp_per_level)
      }

      event = Events.event("level_up", level: player.level, maxHp: sheet.max_hp, maxMp: sheet.max_mp)
      level_up(data, vocation, player, [event | events])
    else
      {player, Enum.reverse(events)}
    end
  end

  @spec after_cast(GameData.t(), Player.t(), SpellDef.t(), integer()) :: {Player.t(), [Events.t()]}
  def after_cast(%GameData{} = data, %Player{} = player, %SpellDef{} = spell, mana_cost) do
    levels = data.balance.spell_levels
    uses_before = Map.get(player.spell_uses, spell.id, 0)
    player = %{player | spell_uses: Map.put(player.spell_uses, spell.id, uses_before + 1)}
    before = Formulas.spell_level_for_uses(uses_before, levels)
    after_level = Formulas.spell_level_for_uses(uses_before + 1, levels)

    spell_events =
      if after_level.level != before.level,
        do: [Events.event("spell_level_up", spellId: spell.id, level: after_level.level)],
        else: []

    player = %{player | mana_spent: player.mana_spent + mana_cost}
    {player, magic_events} = magic_level_up(data, player, [])
    {player, spell_events ++ magic_events}
  end

  defp magic_level_up(data, player, events) do
    if player.mana_spent >= Formulas.mana_for_magic_level(player.magic_level, data.balance) do
      player = %{player | magic_level: player.magic_level + 1}
      magic_level_up(data, player, [Events.event("magic_level_up", magicLevel: player.magic_level) | events])
    else
      {player, Enum.reverse(events)}
    end
  end
end
