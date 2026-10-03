defmodule Rpg.Unit.EventTextTest do
  use ExUnit.Case, async: true

  alias Rpg.Application.Commands.NextFight
  alias Rpg.Application.GameEngine
  alias Rpg.Domain.Definitions.GameData
  alias Rpg.Domain.Entities.{ItemInstance, MonsterInstance}
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
    {%{"type" => "error", "code" => "not_enough_mana"}, "Not enough mana."},
    {%{"type" => "error", "code" => "level_too_low"}, "Your level is too low for that item."},
    {%{
       "type" => "round_started",
       "round" => 3,
       "tier" => 0,
       "cycle" => 0,
       "monsterId" => "rat",
       "isBoss" => false,
       "enemyClass" => "elite",
       "hp" => 90
     }, "Round 3: an ELITE Rat appears! (90 HP)"},
    {%{
       "type" => "monster_attacked",
       "attackId" => "bite",
       "damage" => 9,
       "element" => "physical",
       "charged" => false,
       "crit" => true
     }, "CRITICAL! Rat hits you for 9 physical damage."},
    {%{"type" => "monster_dodged"}, "Rat dodges your attack!"},
    {%{"type" => "monster_parried", "reflected" => 4}, "Rat parries your attack: you take 4 damage!"},
    {%{"type" => "monster_healed", "amount" => 12}, "Rat heals 12 HP."},
    {%{"type" => "attack_parried", "attackId" => "bite", "reflected" => 3},
     "You parry the attack and reflect 3 damage!"},
    {%{"type" => "item_auto_equipped", "uid" => 5, "itemId" => "sword", "slot" => "weapon", "score" => 60},
     "Auto-equipped Sword (score 60)."},
    {%{"type" => "item_auto_sold", "uid" => 6, "itemId" => "bow", "gold" => 30}, "Sold Bow for 30 gold (auto-sell)."},
    {%{"type" => "potion_dropped", "potionId" => "mana_potion"}, "Loot: Mana Potion!"},
    {%{"type" => "run_won", "round" => 100}, "VICTORY! You defeated the final boss on round 100!"},
    {%{"type" => "run_ended", "won" => true}, "Your victory is recorded in the Hall of Fame."}
  ]

  defp rat do
    %MonsterInstance{
      creature_id: "rat",
      is_boss: false,
      enemy_class: "normal",
      hp: 10,
      max_hp: 10,
      xp: 1,
      gold_min: 1,
      gold_max: 1,
      attacks: []
    }
  end

  for {{event, expected}, index} <- Enum.with_index(@cases) do
    test "formats event #{index} (#{event["type"]})" do
      state = %{Helpers.new_engine().state | monster: rat()}
      assert format(unquote(Macro.escape(event)), state) == unquote(expected)
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
