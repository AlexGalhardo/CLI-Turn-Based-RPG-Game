defmodule Rpg.Unit.ArpgRulesTest do
  @moduledoc """
  M8 rules (docs/game-design.md §3, §6, §8, §9): enemy classes, monster dodge/parry/heal/crit, parry reflects,
  class drop tables, potion drops, spell level effects and the victory phase.
  """
  use ExUnit.Case, async: true

  import Rpg.Test.Helpers,
    only: [calm: 1, new_engine: 1, types: 1, update_monster: 2, update_player: 2, with_balance: 2, with_enemy_class: 3]

  alias Rpg.Application.Commands.{Attack, BuyPotion, Cast, ContinueRun, Defend, EndRun, Equip, NextFight, SellItem}
  alias Rpg.Application.{GameEngine, Spawner}
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.{Balance, Caps, GameData, ItemDef, MonsterAttack, MonsterDef}
  alias Rpg.Domain.Entities.ItemInstance
  alias Rpg.Domain.{Formulas, Rng}
  alias Rpg.Test.Helpers

  @max_turns 300

  defp step(engine, command), do: GameEngine.step(engine, command)

  defp fight(engine, damage \\ 1, element \\ "physical") do
    {engine, _} = step(engine, %NextFight{})
    attack = %MonsterAttack{id: "test_hit", element: element, min: damage, max: damage, weight: 1, status: nil}
    update_monster(engine, &%{&1 | attacks: [attack], hp: 10_000, max_hp: 10_000})
  end

  # Finishes the current fight with melee hits (the monster is left with 1 HP before each hit).
  defp kill(engine), do: kill(engine, @max_turns)
  defp kill(_engine, 0), do: flunk("the fight did not end")

  defp kill(engine, turns) do
    engine = engine |> update_monster(&%{&1 | hp: 1}) |> update_player(&%{&1 | hp: 1_000_000})
    {engine, events} = step(engine, %Attack{})
    if engine.state.phase != :battle, do: {engine, events}, else: kill(engine, turns - 1)
  end

  defp physical_resistant_100(data) do
    Enum.find(data.monsters, &(MonsterDef.resistance(&1, "physical") == 100)).id
  end

  # ── spawn ──────────────────────────────────────────────────────────────────

  test "elite spawn scales stats and rewards" do
    data = Helpers.data()
    normal_data = calm(data)
    elite_data = with_balance(normal_data, elite_chance_pct: 100)
    difficulty = Balance.difficulty(data.balance, "normal")
    {normal, _, _} = Spawner.spawn_monster(normal_data, Rng.new(5), 3, difficulty)
    {elite, _, _} = Spawner.spawn_monster(elite_data, Rng.new(5), 3, difficulty)
    row = Balance.enemy_class(data.balance, "elite")
    assert {normal.enemy_class, elite.enemy_class} == {"normal", "elite"}
    assert elite.creature_id == normal.creature_id
    assert elite.max_hp == Formulas.pct(normal.max_hp, row.stat_pct)
    assert hd(elite.attacks).max == Formulas.pct(hd(normal.attacks).max, row.stat_pct)
    assert elite.xp == Formulas.pct(normal.xp, row.reward_pct)
    assert elite.gold_max == Formulas.pct(normal.gold_max, row.reward_pct)
  end

  test "the elite roll follows the monster pick" do
    data = Helpers.data()
    difficulty = Balance.difficulty(data.balance, "normal")

    {classes, _rng} =
      Enum.map_reduce(0..299, Rng.new(77), fn i, rng ->
        {monster, _info, rng} = Spawner.spawn_monster(data, rng, 1 + rem(i, 9), difficulty)
        {monster.enemy_class, rng}
      end)

    assert MapSet.new(classes) == MapSet.new(["normal", "elite"])
    assert Enum.count(classes, &(&1 == "elite")) in 30..90
    {boss, _, _} = Spawner.spawn_monster(data, Rng.new(1), 10, difficulty)
    assert boss.enemy_class == "boss"
  end

  test "round_started reports the enemy class" do
    engine = new_engine(data: with_balance(Helpers.data(), elite_chance_pct: 100))
    {_engine, [started | _]} = step(engine, %NextFight{})
    assert started["enemyClass"] == "elite"
    assert started["isBoss"] == false
  end

  # ── monster dodge, parry, heal and crit ────────────────────────────────────

  test "monster dodge stops melee and spells" do
    engine = new_engine(data: with_enemy_class(calm(Helpers.data()), "normal", dodge: 100)) |> fight()
    {engine, events} = step(engine, %Attack{})
    assert hd(events) == %{"type" => "monster_dodged"}
    assert "player_attacked" not in types(events)
    assert engine.state.monster.hp == engine.state.monster.max_hp
    engine = update_player(engine, &%{&1 | mp: 1000})
    {engine, events} = step(engine, %Cast{spell_id: "brutal_strike"})
    assert hd(events) == %{"type" => "monster_dodged"}
    assert engine.state.player.mp < 1000
    assert engine.state.player.spell_uses["brutal_strike"] == 1
  end

  test "monster parry reflects physical hits" do
    engine = new_engine(data: with_enemy_class(calm(Helpers.data()), "normal", parry: 100)) |> fight()
    {engine, events} = step(engine, %Defend{})
    assert "monster_parried" not in types(events)
    hp = engine.state.player.hp
    {engine, events} = step(engine, %Attack{})
    [parried | _] = events
    assert parried["type"] == "monster_parried"
    assert is_integer(parried["reflected"]) and parried["reflected"] >= 1
    assert "leeched" not in types(events)
    assert engine.state.monster.hp == engine.state.monster.max_hp
    hits = events |> Enum.filter(&(&1["type"] == "monster_attacked")) |> Enum.map(& &1["damage"]) |> Enum.sum()
    regen = events |> Enum.filter(&(&1["type"] == "regenerated")) |> Enum.map(& &1["hp"]) |> Enum.sum()
    assert engine.state.player.hp == hp - parried["reflected"] - hits + regen
  end

  test "a monster parry reflect can kill the player" do
    engine =
      new_engine(data: with_enemy_class(calm(Helpers.data()), "normal", parry: 100))
      |> fight()
      |> update_player(&%{&1 | hp: 1})

    {engine, events} = step(engine, %Attack{})
    assert types(events) == ["monster_parried", "player_died"]
    assert engine.state.phase == :game_over
    assert engine.state.monster.hp == engine.state.monster.max_hp
  end

  test "monster parry ignores non-physical spells" do
    data = with_enemy_class(calm(Helpers.data()), "normal", parry: 100)
    engine = new_engine(vocation: "mage", data: data) |> fight() |> update_player(&%{&1 | mp: 1000})
    {_engine, events} = step(engine, %Cast{spell_id: "flame_strike"})
    assert "monster_parried" not in types(events)
    assert "spell_cast" in types(events)
  end

  test "the monster heals instead of attacking" do
    data = Helpers.data()
    engine = new_engine(data: with_enemy_class(calm(data), "normal", heal: 100)) |> fight()
    {engine, events} = step(engine, %Defend{})
    assert "monster_healed" not in types(events)
    assert "monster_attacked" in types(events)
    engine = update_monster(engine, &%{&1 | hp: &1.max_hp - 5})
    {engine, events} = step(engine, %Defend{})
    assert %{"type" => "monster_healed", "amount" => 5} in events
    assert "monster_attacked" not in types(events)
    engine = update_monster(engine, &%{&1 | hp: 100})
    {engine, events} = step(engine, %Defend{})
    amount = Formulas.pct(engine.state.monster.max_hp, data.balance.monster_heal_pct)
    assert %{"type" => "monster_healed", "amount" => amount} in events
  end

  test "a healing boss does not advance its pattern" do
    engine =
      new_engine(data: with_enemy_class(calm(Helpers.data()), "boss", heal: 100))
      |> Helpers.update_state(&%{&1 | round: 9})

    {engine, _} = step(engine, %NextFight{})
    engine = engine |> update_monster(&%{&1 | hp: &1.max_hp - 1}) |> update_player(&%{&1 | hp: 1_000_000})
    {engine, _} = step(engine, %Defend{})
    assert engine.state.monster.boss_actions == 0
  end

  for {crit, expected} <- [{0, 100}, {100, 150}] do
    @crit crit
    @expected expected
    test "monster crit #{crit}% multiplies the raw damage" do
      engine =
        new_engine(data: with_enemy_class(calm(Helpers.data()), "normal", crit: @crit))
        |> fight(100, "fire")
        |> update_player(&%{&1 | hp: 1000})

      {_engine, events} = step(engine, %Attack{})
      hit = Enum.find(events, &(&1["type"] == "monster_attacked"))
      assert hit["crit"] == (@crit == 100)
      assert hit["damage"] == @expected
    end
  end

  defp parry_data(data) do
    shield = %ItemDef{
      id: "test_parry_shield",
      name: "Test Shield",
      slot: "shield",
      type: "shield",
      tier: 0,
      element: nil,
      stats: %{"parry" => 100},
      value: 10
    }

    caps = data.balance.caps
    game_data = GameData.replace(calm(data), items: data.items ++ [shield])
    caps = %Caps{crit_chance: caps.crit_chance, dodge: 0, parry: 100, leech: 25, protection: 75}
    with_balance(game_data, caps: caps)
  end

  test "the player's parry reflects damage to the monster" do
    game_data = parry_data(Helpers.data())

    engine =
      new_engine(data: game_data)
      |> update_player(fn player ->
        shield = %ItemInstance{uid: 90, item_id: "test_parry_shield", rarity: "common", tier: 0}
        %{player | equipment: Map.put(player.equipment, "shield", shield)}
      end)

    assert Character.build_sheet(engine.state.player, game_data).parry == 100
    engine = fight(engine, 50)
    {engine, events} = step(engine, %Defend{})
    assert %{"type" => "attack_parried", "attackId" => "test_hit", "reflected" => 10} in events
    assert engine.state.monster.hp == engine.state.monster.max_hp - 10
    assert engine.state.stats.parries == 1
    engine = update_monster(engine, &%{&1 | hp: 5})
    {engine, events} = step(engine, %Defend{})
    assert "monster_killed" in types(events)
    assert engine.state.phase == :merchant
  end

  # ── victory, drops and potions ─────────────────────────────────────────────

  test "normal drop table" do
    data = Helpers.data()
    engine = new_engine(data: with_enemy_class(calm(data), "normal", drop_chance_pct: 100)) |> fight()
    {_engine, events} = kill(engine)
    drops = Enum.filter(events, &(&1["type"] == "item_dropped"))
    assert length(drops) == 1
    assert hd(drops)["rarity"] in ["common", "rare"]
    assert "potion_dropped" not in types(events)
    engine = new_engine(data: with_enemy_class(calm(data), "normal", drop_chance_pct: 0)) |> fight()
    {_engine, events} = kill(engine)
    assert "item_dropped" not in types(events)
  end

  test "an elite drops a good item and maybe a potion" do
    data = Helpers.data()
    game_data = data |> calm() |> with_enemy_class("elite", potion_drop_pct: 100) |> with_balance(elite_chance_pct: 100)
    engine = new_engine(data: game_data) |> fight()
    before = engine.state.player.potions
    {engine, events} = kill(engine)
    drops = Enum.filter(events, &(&1["type"] == "item_dropped"))
    assert length(drops) == 1
    assert hd(drops)["rarity"] in ["rare", "legendary"]
    potion_id = Enum.find(events, &(&1["type"] == "potion_dropped"))["potionId"]
    assert GameData.potion(data, potion_id).unlock_round <= 1
    assert engine.state.player.potions[potion_id] == Map.get(before, potion_id, 0) + 1
    assert engine.state.stats.potions_dropped[potion_id] == 1
    assert engine.state.stats.elites_killed == 1
    assert Enum.find(events, &(&1["type"] == "monster_killed"))["enemyClass"] == "elite"
  end

  test "a potion drop without unlocked potions consumes nothing" do
    data = Helpers.data()
    locked = Enum.map(data.potions, &%{&1 | unlock_round: 99})
    game_data = calm(data) |> with_enemy_class("normal", drop_chance_pct: 0, potion_drop_pct: 100)
    engine = new_engine(data: GameData.replace(game_data, potions: locked)) |> fight()
    {_engine, events} = kill(engine)
    assert "potion_dropped" not in types(events)
  end

  test "a boss drops several top items" do
    data = Helpers.data()
    engine = new_engine(data: calm(data)) |> Helpers.update_state(&%{&1 | round: 9}) |> fight()
    {_engine, events} = kill(engine)
    drops = Enum.filter(events, &(&1["type"] == "item_dropped"))
    assert length(drops) == Balance.enemy_class(data.balance, "boss").drops
    assert Enum.all?(drops, &(&1["rarity"] in ["legendary", "mythic"]))
  end

  test "a full bag auto-sells drops" do
    data = Helpers.data()

    bag =
      for i <- 1..data.balance.bag_capacity,
          do: %ItemInstance{uid: 500 + i, item_id: "sword", rarity: "common", tier: 0}

    engine =
      new_engine(data: with_enemy_class(calm(data), "normal", drop_chance_pct: 100))
      |> update_player(&%{&1 | bag: &1.bag ++ bag})
      |> fight()

    {engine, events} = kill(engine)
    assert "item_auto_sold" in types(events)
    assert length(engine.state.player.bag) == data.balance.bag_capacity
  end

  defp final_victory(engine) do
    engine
    |> Helpers.update_state(&%{&1 | round: engine.data.balance.final_round - 1})
    |> fight()
    |> kill()
  end

  test "beating the final boss enters the victory phase" do
    data = Helpers.data()
    {engine, events} = new_engine(data: calm(data)) |> final_victory()
    assert List.last(events) == %{"type" => "run_won", "round" => data.balance.final_round}
    assert "merchant_entered" not in types(events)
    assert engine.state.phase == :victory
    assert engine.state.won == true
    rng_state = GameEngine.rng_state(engine)
    stock = engine.state.merchant_stock

    commands = [
      %Attack{},
      %Defend{},
      %NextFight{},
      %BuyPotion{potion_id: "health_potion", quantity: 1},
      %Equip{uid: 1},
      %SellItem{uid: 1}
    ]

    engine =
      Enum.reduce(commands, engine, fn command, engine ->
        {engine, events} = step(engine, command)
        assert events == [%{"type" => "error", "code" => "invalid_phase"}]
        engine
      end)

    assert GameEngine.rng_state(engine) == rng_state
    assert engine.state.merchant_stock == stock
    {engine, events} = step(engine, %EndRun{})
    assert events == [%{"type" => "run_ended", "won" => true}]
    assert engine.state.phase == :game_over
    assert engine.state.death_cause == nil
    assert {_, [%{"type" => "error", "code" => "invalid_phase"}]} = step(engine, %ContinueRun{})
  end

  test "continue_run enters the merchant" do
    data = Helpers.data()
    {engine, _} = new_engine(data: calm(data)) |> final_victory()
    {engine, events} = step(engine, %ContinueRun{})
    assert events == [%{"type" => "merchant_entered", "round" => data.balance.final_round}]
    assert engine.state.phase == :merchant
    assert length(engine.state.merchant_stock) == data.balance.merchant_stock_size
    {engine, _} = step(engine, %NextFight{})
    assert engine.state.round == data.balance.final_round + 1
    assert engine.state.won == true
  end

  test "victory commands are rejected outside the victory phase" do
    engine = new_engine([])
    assert {_, [%{"type" => "error", "code" => "invalid_phase"}]} = step(engine, %EndRun{})
    assert {_, [%{"type" => "error", "code" => "invalid_phase"}]} = step(engine, %ContinueRun{})
  end

  # ── spell levels ───────────────────────────────────────────────────────────

  for {uses, effect} <- [{0, 100}, {20, 150}, {50, 200}] do
    @uses uses
    @effect effect
    test "spell level effect #{effect}% scales damage" do
      data = Helpers.data()
      creature_id = physical_resistant_100(data)

      engine =
        new_engine(data: calm(data))
        |> fight()
        |> update_monster(&%{&1 | creature_id: creature_id})
        |> update_player(&%{&1 | spell_uses: Map.put(&1.spell_uses, "brutal_strike", @uses), mp: 1000})

      player = engine.state.player
      spell = GameData.spell(data, "brutal_strike")
      bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
      {base, _rng} = Rng.roll(Rng.new(GameEngine.rng_state(engine)), spell.min + bonus, spell.max + bonus)
      {_engine, events} = step(engine, %Cast{spell_id: "brutal_strike"})
      cast = Enum.find(events, &(&1["type"] == "spell_cast"))
      assert cast["damage"] == max(1, Formulas.pct(base, @effect))
    end
  end

  test "spell level effect scales healing" do
    data = Helpers.data()

    engine =
      new_engine(data: calm(data))
      |> fight()
      |> update_player(&%{&1 | spell_uses: Map.put(&1.spell_uses, "wound_cleansing", 20), mp: 1000, hp: 1})

    player = engine.state.player
    spell = GameData.spell(data, "wound_cleansing")
    bonus = player.level * spell.per_level + player.magic_level * spell.per_magic_level
    {raw, _rng} = Rng.roll(Rng.new(GameEngine.rng_state(engine)), spell.min + bonus, spell.max + bonus)
    expected = Formulas.pct(raw, 150)
    room = Character.build_sheet(player, data).max_hp - player.hp
    {_engine, events} = step(engine, %Cast{spell_id: "wound_cleansing"})
    healed = Enum.find(events, &(&1["type"] == "spell_healed"))
    assert healed["amount"] == min(expected, room)
  end
end
