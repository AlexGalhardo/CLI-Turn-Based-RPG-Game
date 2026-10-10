defmodule Rpg.Unit.BattleTest do
  use ExUnit.Case, async: true

  import Rpg.Test.Helpers, only: [new_engine: 0, new_engine: 1, update_player: 2, update_monster: 2, types: 1]

  alias Rpg.Application.Commands.{Attack, BuyPotion, Cast, Defend, NextFight, UsePotion}
  alias Rpg.Application.GameEngine
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.{GameData, MonsterAttack, MonsterDef, StatusOnHit}
  alias Rpg.Domain.Entities.{ActiveStatus, ItemInstance, Player}
  alias Rpg.Test.Helpers

  defp fight(engine) do
    {engine, _} = GameEngine.step(engine, %NextFight{})
    assert engine.state.monster != nil
    engine
  end

  defp fixed_attack(engine, damage, element \\ "physical", status \\ nil) do
    attack = %MonsterAttack{id: "test_hit", element: element, min: damage, max: damage, weight: 1, status: status}
    update_monster(engine, &%{&1 | attacks: [attack], hp: 10_000, max_hp: 10_000})
  end

  defp step(engine, command), do: GameEngine.step(engine, command)
  defp find(events, type), do: Enum.find(events, &(&1["type"] == type))

  test "melee damage is within the sheet range" do
    data = Helpers.data()
    engine = fight(new_engine())
    sheet = Character.build_sheet(engine.state.player, data)
    resistance = MonsterDef.resistance(GameData.creature(data, engine.state.monster.creature_id), "physical")
    {_engine, events} = step(engine, %Attack{})
    hit = find(events, "player_attacked")
    low = max(1, div(sheet.melee_min * resistance, 100))
    high = div(sheet.melee_max * resistance, 100)
    assert hit["crit"] == false
    assert hit["damage"] in low..max(high, low)
  end

  test "invalid battle commands do not consume rng" do
    engine = new_engine()
    assert {_, [%{"type" => "error", "code" => "invalid_phase"}]} = step(engine, %Attack{})
    engine = fight(engine)
    rng_state = GameEngine.rng_state(engine)
    {engine, events} = step(engine, %Cast{spell_id: "flame_strike"})
    assert events == [%{"type" => "error", "code" => "unknown_spell"}]
    engine = update_player(engine, &%{&1 | mp: 0})
    {engine, events} = step(engine, %Cast{spell_id: "brutal_strike"})
    assert events == [%{"type" => "error", "code" => "not_enough_mana"}]
    engine = update_player(engine, &%{&1 | potions: %{}})
    {engine, events} = step(engine, %UsePotion{potion_id: "health_potion"})
    assert events == [%{"type" => "error", "code" => "no_potion"}]
    {engine, events} = step(engine, %UsePotion{potion_id: "elixir"})
    assert events == [%{"type" => "error", "code" => "unknown_potion"}]
    {engine, events} = step(engine, %NextFight{})
    assert events == [%{"type" => "error", "code" => "invalid_phase"}]
    {engine, events} = step(engine, %BuyPotion{potion_id: "health_potion", quantity: 1})
    assert events == [%{"type" => "error", "code" => "invalid_phase"}]
    assert GameEngine.rng_state(engine) == rng_state
  end

  test "defend halves incoming damage" do
    engine = new_engine() |> fight() |> fixed_attack(100) |> update_player(&%{&1 | hp: 150})
    {engine, events} = step(engine, %Defend{})
    assert find(events, "monster_attacked")["damage"] == 50
    assert engine.state.player.defending == false
  end

  test "undefended damage and death" do
    engine = new_engine() |> fight() |> fixed_attack(1000)
    monster_id = engine.state.monster.creature_id
    {engine, events} = step(engine, %Attack{})
    assert List.last(events) == %{"type" => "player_died", "monsterId" => monster_id, "round" => 1}
    assert engine.state.phase == :game_over
    assert engine.state.death_cause == monster_id
    assert {_, [%{"code" => "invalid_phase"}]} = step(engine, %Attack{})
    assert {_, [%{"code" => "invalid_phase"}]} = step(engine, %NextFight{})
  end

  test "monster status applies and ticks" do
    status = %StatusOnHit{status: "burn", chance: 100, damage_pct: 50}
    engine = new_engine() |> fight() |> fixed_attack(10, "fire", status)
    {engine, events} = step(engine, %Defend{})

    assert find(events, "status_applied") == %{
             "type" => "status_applied",
             "target" => "player",
             "status" => "burn",
             "turns" => 3,
             "perTurn" => 2
           }

    assert find(events, "status_ticked") == %{
             "type" => "status_ticked",
             "target" => "player",
             "status" => "burn",
             "damage" => 2
           }

    assert engine.state.player.statuses == [%ActiveStatus{status_id: "burn", turns: 2, per_turn: 2}]
  end

  test "re-applying a status refreshes turns and keeps the higher damage" do
    status = %StatusOnHit{status: "burn", chance: 100, damage_pct: 50}

    engine =
      new_engine()
      |> fight()
      |> fixed_attack(10, "fire", status)
      |> update_player(&%{&1 | statuses: [%ActiveStatus{status_id: "burn", turns: 1, per_turn: 9}]})

    {engine, _} = step(engine, %Defend{})
    assert engine.state.player.statuses == [%ActiveStatus{status_id: "burn", turns: 2, per_turn: 9}]
  end

  test "status expires" do
    engine =
      new_engine()
      |> fight()
      |> fixed_attack(1)
      |> update_player(&%{&1 | statuses: [%ActiveStatus{status_id: "bleed", turns: 1, per_turn: 1}]})

    {engine, events} = step(engine, %Defend{})
    assert "status_expired" in types(events)
    assert engine.state.player.statuses == []
  end

  test "player stun skips a turn and has a cooldown" do
    status = %StatusOnHit{status: "stun", chance: 100, damage_pct: 0}
    engine = new_engine() |> fight() |> fixed_attack(1, "physical", status)
    {engine, events} = step(engine, %Defend{})
    types = types(events)
    assert Enum.count(types, &(&1 == "monster_attacked")) == 2
    assert "player_stunned" in types
    assert Enum.count(types, &(&1 == "status_applied")) == 1
    assert engine.state.player.stun_cooldown == 1
    {_engine, events} = step(engine, %Defend{})
    refute "status_applied" in types(events)
  end

  test "monster stun skips its attack" do
    engine =
      new_engine()
      |> fight()
      |> fixed_attack(5)
      |> update_monster(&%{&1 | statuses: [%ActiveStatus{status_id: "stun", turns: 1, per_turn: 0}]})

    {engine, events} = step(engine, %Defend{})
    assert "monster_stunned" in types(events)
    refute "monster_attacked" in types(events)
    assert engine.state.monster.stun_cooldown == 1
  end

  test "monster dies from a status tick" do
    engine =
      new_engine()
      |> fight()
      |> update_monster(&%{&1 | hp: 1, statuses: [%ActiveStatus{status_id: "bleed", turns: 3, per_turn: 5}]})

    {engine, events} = step(engine, %Defend{})
    assert "monster_killed" in types(events)
    assert engine.state.phase == :merchant
  end

  test "boss telegraphs then charges" do
    engine =
      new_engine()
      |> Helpers.update_state(&%{&1 | round: 9})
      |> fight()

    assert engine.state.monster.is_boss

    engine =
      engine
      |> update_monster(
        &%{&1 | hp: 1_000_000, max_hp: 1_000_000, attacks: Enum.map(&1.attacks, fn a -> %{a | status: nil} end)}
      )
      |> update_player(&%{&1 | hp: 1_000_000})

    {sequence, _engine} =
      Enum.map_reduce(1..4, engine, fn _, engine ->
        {engine, events} = step(engine, %Defend{})

        kind =
          events
          |> Enum.filter(&(&1["type"] in ["boss_telegraph", "monster_attacked", "attack_dodged", "attack_parried"]))
          |> Enum.map(fn
            %{"type" => "boss_telegraph"} -> "telegraph"
            %{"type" => "monster_attacked", "charged" => true} -> "charged"
            _ -> "normal"
          end)
          |> List.first("none")

        {kind, engine}
      end)

    assert sequence == ["normal", "normal", "telegraph", "charged"]
  end

  test "heal is capped and level three cleanses" do
    data = Helpers.data()
    engine = new_engine() |> fight() |> fixed_attack(1)

    engine =
      update_player(engine, fn player ->
        player = %{
          player
          | spell_uses: Map.put(player.spell_uses, "wound_cleansing", 50),
            statuses: [%ActiveStatus{status_id: "poison", turns: 5, per_turn: 1}]
        }

        %{player | hp: Character.build_sheet(player, data).max_hp - 3}
      end)

    {engine, events} = step(engine, %Cast{spell_id: "wound_cleansing"})
    healed = find(events, "spell_healed")
    assert healed["amount"] == 3
    assert healed["mana"] == 32
    assert %{"type" => "status_expired", "target" => "player", "status" => "poison"} in events
    assert engine.state.player.spell_uses["wound_cleansing"] == 51
  end

  test "a spell levels up after twenty uses" do
    engine =
      new_engine()
      |> fight()
      |> fixed_attack(1)
      |> update_player(&%{&1 | spell_uses: Map.put(&1.spell_uses, "brutal_strike", 19), mp: 1000})

    {_engine, events} = step(engine, %Cast{spell_id: "brutal_strike"})
    assert %{"type" => "spell_level_up", "spellId" => "brutal_strike", "level" => 2} in events
    assert find(events, "spell_cast")["mana"] == 20
  end

  test "magic level grows with mana spent" do
    base = Helpers.data().balance.magic_level_base
    engine = new_engine() |> fight() |> fixed_attack(1) |> update_player(&%{&1 | mana_spent: base - 1, mp: 1000})
    {_engine, events} = step(engine, %Cast{spell_id: "brutal_strike"})
    assert %{"type" => "magic_level_up", "magicLevel" => 2} in events
  end

  test "an immune monster takes no damage" do
    data = Helpers.data()
    assert MonsterDef.resistance(GameData.creature(data, "fire_elemental"), "fire") == 0

    engine =
      new_engine(vocation: "mage")
      |> fight()
      |> fixed_attack(1)
      |> update_monster(&%{&1 | creature_id: "fire_elemental"})
      |> update_player(&%{&1 | mp: 1000})

    {_engine, events} = step(engine, %Cast{spell_id: "flame_strike"})
    assert find(events, "spell_cast")["damage"] == 0
  end

  test "items add crit, dodge and leech" do
    data = Helpers.with_test_items(Helpers.data())
    ring = %ItemInstance{uid: 99, item_id: "test_ring", rarity: "common", tier: 0}
    engine = new_engine(data: data) |> update_player(&%{&1 | equipment: Map.put(&1.equipment, "ring", ring)})
    sheet = Character.build_sheet(engine.state.player, data)
    assert sheet.crit_chance == data.balance.caps.crit_chance
    assert sheet.dodge == data.balance.caps.dodge
    engine = engine |> fight() |> fixed_attack(5)

    {{crits, dodges}, _engine} =
      Enum.reduce(1..60, {{0, 0}, engine}, fn _, {{crits, dodges}, engine} ->
        {engine, events} = engine |> update_player(&%{&1 | hp: 100}) |> step(%Attack{})
        crits = crits + Enum.count(events, &(&1["type"] == "player_attacked" and &1["crit"]))
        dodges = dodges + Enum.count(events, &(&1["type"] == "attack_dodged"))
        {{crits, dodges}, engine}
      end)

    assert crits > 0
    assert dodges > 0
  end

  test "life and mana leech restore resources" do
    data = Helpers.data()
    item = %ItemInstance{uid: 98, item_id: "sword", rarity: "common", tier: 0, affixes: []}

    data =
      GameData.replace(data,
        items:
          Enum.map(
            data.items,
            &if(&1.id == "sword", do: %{&1 | stats: %{"attack" => 6, "lifeLeech" => 25, "manaLeech" => 25}}, else: &1)
          )
      )

    engine =
      new_engine(data: data)
      |> update_player(&%{&1 | equipment: %{"weapon" => item}})
      |> fight()
      |> fixed_attack(1)
      |> update_player(&%{&1 | hp: 10, mp: 0})

    {_engine, events} = step(engine, %Attack{})
    leeched = find(events, "leeched")
    assert leeched["hp"] > 0 or leeched["mp"] > 0
  end

  test "a potion restores and is consumed" do
    data = Helpers.data()
    engine = new_engine() |> fight() |> fixed_attack(1)
    before = Player.potion_count(engine.state.player, "mana_potion")
    engine = update_player(engine, &%{&1 | hp: 10, mp: 0})
    {engine, events} = step(engine, %UsePotion{potion_id: "mana_potion"})
    assert find(events, "potion_used")["resource"] == "mp"
    player = engine.state.player
    assert Player.potion_count(player, "mana_potion") == before - 1
    assert player.mp > 0 and player.mp <= Character.build_sheet(player, data).max_mp
  end

  test "a health potion restores hp" do
    engine = new_engine() |> fight() |> fixed_attack(1) |> update_player(&%{&1 | hp: 10})
    {_engine, events} = step(engine, %UsePotion{potion_id: "health_potion"})
    assert find(events, "potion_used")["resource"] == "hp"
    assert find(events, "potion_used")["amount"] > 0
  end

  for vocation <- ["warrior", "archer", "mage"] do
    test "every #{vocation} spell can be cast" do
      data = Helpers.data()
      engine = new_engine(vocation: unquote(vocation)) |> fight() |> fixed_attack(1)

      Enum.reduce(GameData.vocation(data, unquote(vocation)).spells, engine, fn spell_id, engine ->
        {engine, events} = engine |> update_player(&%{&1 | mp: 10_000, hp: 5}) |> step(%Cast{spell_id: spell_id})
        assert Enum.any?(events, &(&1["type"] in ["spell_cast", "spell_healed"] and &1["spellId"] == spell_id))
        engine
      end)
    end
  end

  test "level three attack spells can apply their status" do
    data = Helpers.data()

    spell =
      Enum.find(
        data.spells,
        &(&1.kind == :attack and &1.level3_bonus.status != nil and &1.id in GameData.vocation(data, "mage").spells)
      )

    engine =
      new_engine(vocation: "mage")
      |> fight()
      |> fixed_attack(1)
      |> update_monster(&%{&1 | creature_id: "rat"})

    applied =
      Enum.reduce_while(1..40, engine, fn _, engine ->
        engine = update_player(engine, &%{&1 | mp: 10_000, hp: 1000, spell_uses: Map.put(&1.spell_uses, spell.id, 60)})
        {engine, events} = step(engine, %Cast{spell_id: spell.id})

        if Enum.any?(events, &(&1["type"] == "status_applied" and &1["target"] == "monster")),
          do: {:halt, :applied},
          else: {:cont, update_monster(engine, fn m -> %{m | hp: 10_000, statuses: []} end)}
      end)

    assert applied == :applied
  end
end
