defmodule Rpg.Unit.MerchantAndLootTest do
  use ExUnit.Case, async: true

  import Rpg.Test.Helpers, only: [new_engine: 0, new_engine: 1, update_player: 2, update_state: 2]

  alias Rpg.Application.Commands.{BuyPotion, BuyStockItem, Equip, NextFight, SellItem, Unequip}
  alias Rpg.Application.{GameEngine, Loot, Merchant}
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.{AffixDef, Balance, GameData}
  alias Rpg.Domain.Entities.{AffixRoll, ItemInstance}
  alias Rpg.Domain.Rng
  alias Rpg.Test.Helpers

  defp loot_data do
    affixes = [
      %AffixDef{id: "of_power", stat: "attack", min: 1, max: 3, per_tier: 2, slots: ["weapon"]},
      %AffixDef{id: "of_the_bear", stat: "maxHp", min: 5, max: 10, per_tier: 5, slots: ["weapon", "helmet"]},
      %AffixDef{id: "of_speed", stat: "dodge", min: 1, max: 2, per_tier: 0, slots: ["weapon", "ring"]},
      %AffixDef{id: "of_speed_2", stat: "dodge", min: 1, max: 2, per_tier: 0, slots: ["weapon"]}
    ]

    GameData.replace(Helpers.with_test_items(Helpers.data()), affixes: affixes)
  end

  defp step(engine, command), do: GameEngine.step(engine, command)
  defp item(uid, id, rarity \\ "common"), do: %ItemInstance{uid: uid, item_id: id, rarity: rarity, tier: 0}

  test "buy potion rules" do
    engine = new_engine()
    gold = engine.state.player.gold
    {engine, events} = step(engine, %BuyPotion{potion_id: "health_potion", quantity: 2})
    assert events == [%{"type" => "potion_bought", "potionId" => "health_potion", "quantity" => 2, "gold" => 100}]
    assert engine.state.player.gold == gold - 100

    for {command, code} <- [
          {%BuyPotion{potion_id: "health_potion", quantity: 1}, "not_enough_gold"},
          {%BuyPotion{potion_id: "health_potion", quantity: 0}, "invalid_quantity"},
          {%BuyPotion{potion_id: "great_health_potion", quantity: 1}, "potion_locked"},
          {%BuyPotion{potion_id: "elixir", quantity: 1}, "unknown_potion"}
        ] do
      assert {_, [%{"type" => "error", "code" => ^code}]} = step(engine, command)
    end

    assert engine.state.stats.potions_bought["health_potion"] == 2
    assert engine.state.stats.gold_spent == 100
  end

  test "available potions unlock by round" do
    data = Helpers.data()
    engine = new_engine()
    assert Merchant.available_potions(engine.state, data) == ["health_potion", "mana_potion"]
    engine = update_state(engine, &%{&1 | round: 80})
    assert length(Merchant.available_potions(engine.state, data)) == length(data.potions)
  end

  test "equip, swap, sell and unequip" do
    data = Helpers.with_test_items(Helpers.data())
    axe = item(50, "test_axe")
    rod = item(51, "test_rod", "rare")
    engine = new_engine(data: data) |> update_player(&%{&1 | bag: &1.bag ++ [axe, rod]})
    starter_uid = engine.state.player.equipment["weapon"].uid

    assert {_, [%{"type" => "error", "code" => "cannot_equip"}]} = step(engine, %Equip{uid: 51})
    {engine, events} = step(engine, %Equip{uid: 50})

    assert events == [
             %{"type" => "item_unequipped", "uid" => starter_uid, "itemId" => "sword", "slot" => "weapon"},
             %{"type" => "item_equipped", "uid" => 50, "itemId" => "test_axe", "slot" => "weapon"}
           ]

    assert Character.build_sheet(engine.state.player, data).melee_min == 8 + 20

    gold = engine.state.player.gold
    {engine, events} = step(engine, %SellItem{uid: 51})

    assert events == [
             %{"type" => "item_sold", "uid" => 51, "itemId" => "test_rod", "gold" => Character.item_value(rod, data)}
           ]

    assert engine.state.player.gold == gold + 250
    assert {_, [%{"code" => "invalid_item"}]} = step(engine, %SellItem{uid: 51})
    {engine, events} = step(engine, %Unequip{slot: "weapon"})
    assert events == [%{"type" => "item_unequipped", "uid" => 50, "itemId" => "test_axe", "slot" => "weapon"}]
    assert {_, [%{"code" => "invalid_item"}]} = step(engine, %Unequip{slot: "weapon"})
    assert {_, [%{"code" => "invalid_item"}]} = step(engine, %Equip{uid: 999})
  end

  test "unequip with a full bag, and hp clamp" do
    data = Helpers.with_test_items(Helpers.data())
    capacity = data.balance.bag_capacity

    engine =
      new_engine(data: data)
      |> update_player(fn player ->
        player = %{player | equipment: Map.put(player.equipment, "helmet", item(70, "test_helmet"))}
        rings = Enum.map(0..(capacity - 1), &item(100 + &1, "test_ring"))
        %{player | hp: Character.build_sheet(player, data).max_hp, bag: player.bag ++ rings}
      end)

    assert {_, [%{"code" => "bag_full"}]} = step(engine, %Unequip{slot: "helmet"})
    engine = update_player(engine, &%{&1 | bag: Enum.drop(&1.bag, -1)})
    {engine, _} = step(engine, %Unequip{slot: "helmet"})
    assert engine.state.player.hp == Character.build_sheet(engine.state.player, data).max_hp
  end

  test "merchant stock purchase" do
    data = loot_data()
    engine = new_engine(data: data)
    assert length(engine.state.merchant_stock) == data.balance.merchant_stock_size
    [stock_item | _] = engine.state.merchant_stock
    price = Merchant.stock_price(stock_item, data)
    engine = update_player(engine, &%{&1 | gold: price})
    {engine, events} = step(engine, %BuyStockItem{index: 0})

    assert events == [
             %{"type" => "item_bought", "uid" => stock_item.uid, "itemId" => stock_item.item_id, "gold" => price}
           ]

    assert stock_item in engine.state.player.bag
    assert {_, [%{"code" => "not_enough_gold"}]} = step(engine, %BuyStockItem{index: 0})
    assert {_, [%{"code" => "invalid_item"}]} = step(engine, %BuyStockItem{index: 9})
    engine = update_player(engine, &%{&1 | bag: &1.bag ++ Enum.map(0..29, fn i -> item(200 + i, "test_ring") end)})
    assert {_, [%{"code" => "bag_full"}]} = step(engine, %BuyStockItem{index: 0})
    {engine, _} = step(engine, %NextFight{})
    assert engine.state.merchant_stock == []
  end

  test "generate item is deterministic and affixes are unique" do
    data = loot_data()
    vocation = GameData.vocation(data, "warrior")
    weights = Balance.enemy_class(data.balance, "boss").rarity_weights
    opts = fn tier, uid -> [vocation: vocation, tier: tier, weights: weights, uid: uid] end
    first = Enum.map(0..19, &elem(Loot.generate_item(data, Rng.new(5), opts.(0, &1)), 0))
    second = Enum.map(0..19, &elem(Loot.generate_item(data, Rng.new(5), opts.(0, &1)), 0))
    assert first == second

    Enum.reduce(0..199, Rng.new(11), fn uid, rng ->
      {item, rng} = Loot.generate_item(data, rng, opts.(1, uid))
      assert item != nil
      assert item.rarity in ["legendary", "mythic"]
      stats = Enum.map(item.affixes, & &1.stat)
      assert length(stats) == length(Enum.uniq(stats))
      assert Loot.can_use(GameData.item(data, item.item_id), vocation)
      rng
    end)
  end

  test "generate item without candidates consumes nothing" do
    data = Helpers.data()
    empty = GameData.replace(data, items: [])
    rng = Rng.new(3)
    weights = Balance.enemy_class(data.balance, "normal").rarity_weights
    opts = [vocation: GameData.vocation(data, "mage"), tier: 9, weights: weights, uid: 1]
    assert Loot.generate_item(empty, rng, opts) == {nil, rng}
  end

  test "roll_rarity skips zero weights and single options" do
    data = Helpers.data()
    rng = Rng.new(1)
    assert {%{id: "rare"}, ^rng} = Loot.roll_rarity(data, rng, %{"rare" => 5})
    assert {%{id: "mythic"}, ^rng} = Loot.roll_rarity(data, rng, %{"common" => 0, "mythic" => 3})

    {rolled, rolled_rng} =
      Enum.map_reduce(1..40, rng, fn _, rng ->
        {rarity, rng} = Loot.roll_rarity(data, rng, %{"common" => 1, "legendary" => 1})
        {rarity.id, rng}
      end)

    assert MapSet.new(rolled) == MapSet.new(["common", "legendary"])
    assert Rng.state(rolled_rng) != 1
    assert_raise ArgumentError, ~r/positive/, fn -> Loot.roll_rarity(data, rng, %{"common" => 0}) end
  end

  for {rarity, attack, affixes} <- [{"common", 20, 0}, {"rare", 30, 1}, {"legendary", 40, 2}, {"mythic", 60, 2}] do
    @rarity rarity
    @attack attack
    @affixes affixes
    test "rarity #{rarity} scales base stats and affix counts" do
      data = Helpers.with_test_items(Helpers.data())
      assert Character.item_stats(item(1, "test_axe", @rarity), data) == %{"attack" => @attack}
      definition = Balance.rarity(data.balance, @rarity)
      assert {definition.affix_min, definition.affix_max} == {@affixes, @affixes}
    end
  end

  test "item stats apply rarity and affixes" do
    data = Helpers.with_test_items(Helpers.data())

    helmet = %ItemInstance{
      uid: 1,
      item_id: "test_helmet",
      rarity: "legendary",
      tier: 0,
      affixes: [%AffixRoll{stat: "maxHp", value: 7}]
    }

    assert Character.item_stats(helmet, data) == %{"armor" => 20, "maxHp" => 107}
    assert Character.item_value(helmet, data) == 600
  end

  test "item score weights the final stats" do
    data = Helpers.with_test_items(Helpers.data())
    weights = data.balance.item_score_weights
    common = item(1, "test_helmet")
    assert Character.item_score(common, data) == 10 * weights["armor"] + 50 * weights["maxHp"]
    scores = Enum.map(["common", "rare", "legendary"], &Character.item_score(item(1, "test_helmet", &1), data))
    assert scores == Enum.sort(scores)
    assert length(Enum.uniq(scores)) == 3
    with_affix = %{common | affixes: [%AffixRoll{stat: "dodge", value: 2}]}
    assert Character.item_score(with_affix, data) == Character.item_score(common, data) + 2 * weights["dodge"]
  end

  test "required level grows with the item tier" do
    data = Helpers.data()
    per_tier = data.balance.item_level_per_tier
    assert Character.required_level(%ItemInstance{uid: 1, item_id: "sword", rarity: "common", tier: 0}, data) == 1
    sword = %ItemInstance{uid: 1, item_id: "sword", rarity: "common", tier: 3}
    assert Character.required_level(sword, data) == 1 + 3 * per_tier
  end

  test "equip rejects items above the player level" do
    data = Helpers.with_test_items(Helpers.data())
    axe = %ItemInstance{uid: 60, item_id: "test_axe", rarity: "common", tier: 5}
    engine = new_engine(data: data) |> update_player(&%{&1 | bag: &1.bag ++ [axe]})
    rng_state = GameEngine.rng_state(engine)
    {engine, events} = step(engine, %Equip{uid: 60})
    assert events == [%{"type" => "error", "code" => "level_too_low"}]
    assert axe in engine.state.player.bag
    assert GameEngine.rng_state(engine) == rng_state
    engine = update_player(engine, &%{&1 | level: Character.required_level(axe, data)})
    {_engine, events} = step(engine, %Equip{uid: 60})
    assert List.last(events) == %{"type" => "item_equipped", "uid" => 60, "itemId" => "test_axe", "slot" => "weapon"}
  end

  test "selling an equipped uid is rejected" do
    engine = new_engine()
    weapon = engine.state.player.equipment["weapon"]
    {after_sell, events} = step(engine, %SellItem{uid: weapon.uid})
    assert events == [%{"type" => "error", "code" => "invalid_item"}]
    assert after_sell.state.player.equipment["weapon"] == weapon
    assert after_sell.state.player.gold == engine.state.player.gold
  end

  test "shields follow the vocation shield types" do
    data = Helpers.data()

    for vocation <- data.vocations, item <- data.items, item.slot == "shield" do
      assert Loot.can_use(item, vocation) == item.type in vocation.shield_types
    end
  end
end
