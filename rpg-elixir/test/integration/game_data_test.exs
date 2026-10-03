defmodule Rpg.Integration.GameDataTest do
  use ExUnit.Case, async: false

  alias Rpg.Domain.Definitions.{Balance, GameData, MonsterDef, UnknownIdError}
  alias Rpg.Infrastructure.DataLoader
  alias Rpg.Infrastructure.DataLoader.DataError
  alias Rpg.Infrastructure.Paths
  alias Rpg.Test.Helpers

  test "content requirements" do
    data = Helpers.data()
    assert length(data.monsters) >= 100
    assert GameData.tier_count(data) == 10
    assert data.vocations |> Enum.map(& &1.id) |> Enum.sort() == ["archer", "mage", "warrior"]

    for tier <- 0..(GameData.tier_count(data) - 1) do
      assert length(GameData.monsters_in_tier(data, tier)) >= 9
      assert GameData.boss_of_tier(data, tier).is_boss
    end
  end

  test "the embedded copy equals the shared folder" do
    assert DataLoader.load_game_data(Helpers.shared_dir()) == Helpers.data()
  end

  test "cross references are valid" do
    data = Helpers.data()

    for vocation <- data.vocations do
      GameData.item(data, vocation.starter_weapon)
      Enum.each(vocation.spells, &GameData.spell(data, &1))
    end

    for creature <- data.monsters ++ data.bosses do
      assert creature.family in data.families

      for attack <- creature.attacks do
        assert attack.min <= attack.max
        if attack.status, do: GameData.status(data, attack.status.status)
      end

      if creature.is_boss do
        assert creature.charge_attack != nil
        MonsterDef.attack(creature, creature.charge_attack)
      end
    end

    for spell <- data.spells, spell.level3_bonus.status != nil do
      assert GameData.status(data, spell.level3_bonus.status).kind in [:dot, :stun]
    end

    for {potion_id, _} <- data.balance.starting_potions, do: GameData.potion(data, potion_id)
    ids = Enum.map(data.monsters ++ data.bosses, & &1.id)
    assert length(ids) == length(Enum.uniq(ids))
  end

  test "lookup errors" do
    data = Helpers.data()
    assert_raise UnknownIdError, fn -> GameData.spell(data, "avada_kedavra") end
    assert_raise UnknownIdError, fn -> Balance.difficulty(data.balance, "nightmare") end
    assert_raise UnknownIdError, fn -> Balance.rarity(data.balance, "mythic") end
    assert_raise UnknownIdError, ~r/laser/, fn -> MonsterDef.attack(GameData.creature(data, "rat"), "laser") end
  end

  @tag :tmp_dir
  test "invalid data raises DataError", %{tmp_dir: dir} do
    copy = Path.join(dir, "shared")
    File.mkdir_p!(copy)
    File.cp_r!(Path.join(Helpers.shared_dir(), "data"), Path.join(copy, "data"))
    vocations = Path.join([copy, "data", "vocations.json"])
    document = Helpers.read_json(vocations)
    [first | rest] = document["vocations"]
    File.write!(vocations, JSON.encode!(%{document | "vocations" => [Map.delete(first, "startHp") | rest]}))
    assert_raise DataError, ~r/startHp/, fn -> DataLoader.load_game_data(copy) end
    File.write!(vocations, "{ not json")
    assert_raise DataError, ~r/vocations\.json/, fn -> DataLoader.load_game_data(copy) end
    File.write!(vocations, "[]")
    assert_raise DataError, ~r/vocations\.json/, fn -> DataLoader.load_game_data(copy) end
    File.rm!(Path.join([copy, "data", "affixes.json"]))
    File.rm!(Path.join([copy, "data", "achievements.json"]))
    File.cp!(Path.join([Helpers.shared_dir(), "data", "vocations.json"]), vocations)
    data = DataLoader.load_game_data(copy)
    assert data.affixes == [] and data.achievements == []
  end

  @tag :tmp_dir
  test "paths", %{tmp_dir: dir} do
    System.put_env("RPG_SHARED_DIR", dir)
    assert Paths.find_shared_dir() == dir
    assert Paths.shared_source() == dir
    System.delete_env("RPG_SHARED_DIR")
    assert Paths.shared_source() == :embedded
    # ExUnit's tmp_dir lives inside the repository, so search from the system temp folder instead.
    outside = Path.join(System.tmp_dir!(), "rpg-elixir-no-shared-#{System.unique_integer([:positive])}")
    File.mkdir_p!(outside)
    assert_raise File.Error, fn -> Paths.find_shared_dir(outside) end
    File.rm_rf!(outside)
    assert Paths.find_shared_dir(Path.join(Helpers.shared_dir(), "data")) == Helpers.shared_dir()
    assert Paths.resolve_data_dir("custom") == "custom"
    previous = System.get_env("RPG_DATA_DIR")
    System.put_env("RPG_DATA_DIR", Path.join(dir, "x"))
    assert Paths.resolve_data_dir() == Path.join(dir, "x")
    System.delete_env("RPG_DATA_DIR")
    assert Path.basename(Paths.resolve_data_dir()) == ".cli-turn-based-rpg"
    System.put_env("RPG_DATA_DIR", previous)
  end
end
