defmodule Rpg.Unit.EventTextTest do
  use ExUnit.Case, async: true

  alias Rpg.Application.Commands.NextFight
  alias Rpg.Application.GameEngine
  alias Rpg.Domain.Definitions.GameData
  alias Rpg.Domain.Entities.ItemInstance
  alias Rpg.Infrastructure.I18n
  alias Rpg.Presentation.EventText
  alias Rpg.Test.Helpers

  defp formatter, do: EventText.formatter(Helpers.data(), I18n.new(:embedded))
  defp format(event, state), do: EventText.format(formatter(), event, state)

  @cases [
    {%{"type" => "player_attacked", "damage" => 12, "crit" => false, "element" => "fire"},
     "You hit for 12 fire damage."},
    {%{"type" => "player_attacked", "damage" => 30, "crit" => true, "element" => "physical"},
     "CRITICAL! You hit for 30 physical damage."},
    {%{
       "type" => "spell_cast",
       "spellId" => "flame_strike",
       "damage" => 9,
       "crit" => false,
       "element" => "fire",
       "mana" => 20
     }, "Flame Strike deals 9 fire damage."},
    {%{"type" => "potion_used", "potionId" => "mana_potion", "amount" => 80, "resource" => "mp"},
     "Mana Potion restores 80 MP."},
    {%{"type" => "status_applied", "target" => "player", "status" => "burn", "turns" => 3, "perTurn" => 2},
     "You are burning (3 turns)."},
    {%{"type" => "monster_killed", "monsterId" => "dragon", "isBoss" => false}, "You defeated Dragon!"},
    {%{
       "type" => "round_started",
       "round" => 10,
       "tier" => 0,
       "cycle" => 0,
       "monsterId" => "munster",
       "isBoss" => true,
       "hp" => 5
     }, "Round 10: the boss Munster challenges you! (5 HP)"},
    {%{"type" => "item_sold", "uid" => 3, "itemId" => "sword", "gold" => 25}, "You sold Sword for 25 gold."},
    {%{"type" => "error", "code" => "not_enough_mana"}, "Not enough mana."}
  ]

  for {{event, expected}, index} <- Enum.with_index(@cases) do
    test "formats event #{index} (#{event["type"]})" do
      assert format(unquote(Macro.escape(event)), Helpers.new_engine().state) == unquote(expected)
    end
  end

  test "unknown ids fall back to the id" do
    event = %{
      "type" => "spell_cast",
      "spellId" => "ghost_spell",
      "damage" => 1,
      "crit" => false,
      "element" => "fire",
      "mana" => 1
    }

    assert format(event, Helpers.new_engine().state) =~ "ghost_spell"
  end

  test "the monster name comes from the state" do
    {engine, _} = GameEngine.step(Helpers.new_engine(), %NextFight{})
    name = GameData.creature(Helpers.data(), engine.state.monster.creature_id).name
    assert String.starts_with?(format(%{"type" => "monster_stunned"}, engine.state), name)
  end

  test "item name by uid" do
    engine =
      Helpers.update_player(
        Helpers.new_engine(),
        &%{&1 | bag: [%ItemInstance{uid: 77, item_id: "bow", rarity: "rare", tier: 0}]}
      )

    assert format(%{"type" => "item_dropped", "uid" => 77, "itemId" => "bow", "rarity" => "rare"}, engine.state) ==
             "Loot: Bow [Rare]!"

    assert format(%{"type" => "item_equipped", "uid" => 999, "slot" => "ring"}, engine.state) =~ "#999"
  end

  test "every event type and error code has an English text" do
    translator = I18n.new(:embedded)

    for code <- Rpg.Application.Events.error_codes(), code != "unknown_command" do
      assert I18n.has?(translator, "error.#{code}"), code
    end
  end
end
