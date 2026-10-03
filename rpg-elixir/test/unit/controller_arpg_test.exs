defmodule Rpg.Unit.ControllerArpgTest do
  @moduledoc "M8 screens of the UI controller: settings, auto-equip step, auto-battle, victory and the equipment screen."
  use ExUnit.Case, async: true

  import Rpg.Test.ControllerHelpers

  alias Rpg.Application.Ports.ProfileRepository
  alias Rpg.Application.Profile.HallOfFameEntry
  alias Rpg.Domain.Character
  alias Rpg.Domain.Entities.{AffixRoll, ItemInstance}
  alias Rpg.Infrastructure.Repositories.{Settings, SettingsRepository}
  alias Rpg.Presentation.{Controller, Render}
  alias Rpg.Test.Helpers

  @moduletag :tmp_dir

  defp state(c), do: c.session.engine.state
  defp update_state(c, fun), do: %{c | session: %{c.session | engine: Helpers.update_state(c.session.engine, fun)}}
  defp update_player(c, fun), do: update_state(c, &%{&1 | player: fun.(&1.player)})
  defp update_monster(c, fun), do: update_state(c, &%{&1 | monster: fun.(&1.monster)})
  defp labels(c), do: Enum.map(Controller.options(c), & &1.label)
  defp keys(c), do: Enum.map(Controller.options(c), & &1.key)

  test "settings toggle and persist", %{tmp_dir: dir} do
    c = make_controller(dir) |> press("6")
    assert Enum.at(labels(c), 1) == "Auto-equip on new runs: Off"
    assert Enum.at(labels(c), 2) == "Auto-battle speed: 1x"
    c = press(c, ["2", "3"])
    assert c.settings == %Settings{locale: nil, auto_equip: true, battle_speed: 2}
    assert Controller.auto_battle_interval_ms(c) == div(600, 2)
    assert SettingsRepository.load(c.services.settings) == %Settings{locale: nil, auto_equip: true, battle_speed: 2}
    c = press(c, "3")
    assert c.settings.battle_speed == 1
    c = press(c, ["1", "1"])
    assert c.view == :settings
    assert SettingsRepository.load(c.services.settings) == %Settings{locale: "en", auto_equip: true, battle_speed: 1}
  end

  test "the new-run auto-equip step marks the default", %{tmp_dir: dir} do
    c = make_controller(dir)
    SettingsRepository.save(c.services.settings, %Settings{locale: "en", auto_equip: true})
    c = make_controller(dir) |> start_run("Zed", "1", "0")
    assert c.view == :vocation
    c = press(c, "1")
    assert c.view == :auto_equip
    assert Controller.title(c) == Controller.t(c, "new_run.auto_equip")
    [first, second | _] = labels(c)
    assert String.ends_with?(first, "(default)")
    refute String.ends_with?(second, "(default)")
    c = press(c, "1")
    assert c.view == :merchant
    assert state(c).config.auto_equip == true
  end

  defp battle_controller(dir) do
    c = make_controller(dir) |> start_run() |> press("0")
    assert c.view == :battle
    c
  end

  test "auto-battle menu and instant run", %{tmp_dir: dir} do
    c = battle_controller(dir) |> press("5")
    assert c.view == :auto_battle
    assert keys(c) == ["1", "2", "3", "0"]
    c = press(c, "0")
    assert c.view == :battle
    c = press(c, ["5", "3"])
    assert Controller.auto_battle_active?(c)
    assert List.last(c.log) == Controller.t(c, "auto_battle.started", mode: Controller.t(c, "auto_battle.balanced"))
    turn = state(c).turn
    c = press(c, "1")
    assert state(c).turn == turn
    c = Controller.run_auto_battle(c)
    refute Controller.auto_battle_active?(c)
    assert state(c).phase != :battle
    assert c.view in [:merchant, :game_over]
    assert {_c, false} = Controller.auto_battle_step(c)
  end

  test "auto-battle steps one turn at a time", %{tmp_dir: dir} do
    c =
      battle_controller(dir)
      |> update_monster(&%{&1 | hp: 1_000_000, max_hp: 1_000_000})
      |> press(["5", "1"])

    turn = state(c).turn
    c = update_player(c, &%{&1 | hp: 1_000_000})
    {c, running} = Controller.auto_battle_step(c)
    assert running == true
    assert state(c).turn == turn + 1
  end

  defp victory_controller(dir) do
    data = Helpers.data()
    c = make_controller(dir, "en", Helpers.calm(data)) |> start_run()
    c = c |> update_state(&%{&1 | round: data.balance.final_round - 1}) |> press("0")

    c =
      Enum.reduce_while(1..50, c, fn _, c ->
        if state(c).monster == nil,
          do: {:halt, c},
          else: {:cont, c |> update_monster(&%{&1 | hp: 1}) |> press("1")}
      end)

    assert c.view == :victory
    c
  end

  test "victory screen: end run", %{tmp_dir: dir} do
    c = victory_controller(dir)
    assert Controller.title(c) == Controller.t(c, "victory.title")
    assert hd(Controller.body_lines(c)) =~ "Ferumbras"
    c = press(c, "9")
    assert c.view == :victory
    c = press(c, "1")
    assert c.view == :game_over
    assert Controller.title(c) == Controller.t(c, "gameover.title_won")
    assert hd(Controller.body_lines(c)) =~ "won the run"
    c = press(c, ["2", "3"])
    assert hd(Controller.body_lines(c)) =~ "WON"
  end

  test "victory screen: continue", %{tmp_dir: dir} do
    c = dir |> victory_controller() |> press("2")
    assert c.view == :merchant
    assert state(c).won == true
  end

  test "continuing a saved victory returns to the victory screen", %{tmp_dir: dir} do
    c = victory_controller(dir)
    c = %{c | session: nil, view: :title} |> press("1")
    assert c.view == :victory
  end

  defp equipment_controller(dir) do
    c = make_controller(dir, "en", Helpers.with_test_items(Helpers.data())) |> start_run() |> press("3")
    assert c.view == :equipment
    c
  end

  test "the equipment screen lists every slot and the bag", %{tmp_dir: dir} do
    c = equipment_controller(dir)
    data = c.services.data

    c =
      update_player(c, fn p ->
        bag = [
          %ItemInstance{uid: 900, item_id: "test_axe", rarity: "rare", tier: 0},
          %ItemInstance{uid: 901, item_id: "test_rod", rarity: "common", tier: 0},
          %ItemInstance{uid: 902, item_id: "test_helmet", rarity: "legendary", tier: 9}
        ]

        %{p | bag: p.bag ++ bag}
      end)

    lines = Controller.body_lines(c)
    colors = Controller.body_colors(c)
    starter = state(c).player.equipment["weapon"]
    assert hd(lines) == "EQUIPPED · total score #{Character.item_score(starter, data)}"
    assert String.starts_with?(Enum.at(lines, 1), "Weapon: Sword [Common] · Lv 1")
    assert Enum.at(lines, 2) == "Shield: - empty -"
    assert Enum.at(colors, 2) == Render.style_warning()
    assert Enum.count(lines, &String.contains?(&1, "- empty -")) == 7
    assert List.last(lines) == "BAG (usable)"
    assert keys(c) == ["1", "2", "3", "0"]
    [axe, helmet, slot | _] = Controller.options(c)

    delta =
      Character.item_score(%ItemInstance{uid: 900, item_id: "test_axe", rarity: "rare", tier: 0}, data) -
        Character.item_score(starter, data)

    assert axe.detail == Render.format_delta(delta)
    assert axe.detail_color == Render.style_gain()
    assert axe.color == "rare"
    assert helmet.color == Render.style_dim()
    assert String.ends_with?(helmet.label, "requires Lv 37")
    assert slot.label == "Weapon: Sword [Common]"
  end

  test "the comparison shows stat and score deltas", %{tmp_dir: dir} do
    c =
      dir
      |> equipment_controller()
      |> update_player(fn p ->
        helmet = %ItemInstance{
          uid: 800,
          item_id: "test_helmet",
          rarity: "common",
          tier: 0,
          affixes: [%AffixRoll{stat: "dodge", value: 3}]
        }

        new = %ItemInstance{
          uid: 801,
          item_id: "test_helmet",
          rarity: "rare",
          tier: 0,
          affixes: [%AffixRoll{stat: "critChance", value: 2}]
        }

        %{p | equipment: Map.put(p.equipment, "helmet", helmet), bag: p.bag ++ [new]}
      end)
      |> press("1")

    assert c.view == :compare
    assert Controller.title(c) == "Helmet: Test Helmet → Test Helmet"
    lines = Controller.body_lines(c)
    colors = Map.new(Enum.zip(lines, Controller.body_colors(c)))
    assert colors["Armor: 10 → 15 (+5)"] == Render.style_gain()
    assert colors["Max HP: 50 → 75 (+25)"] == Render.style_gain()
    assert colors["Critical chance: 0 → 2 (+2)"] == Render.style_gain()
    assert colors["Dodge: 3 → 0 (-3)"] == Render.style_loss()
    assert colors["Affixes gained: +2 Critical chance"] == Render.style_gain()
    assert colors["Affixes lost: +3 Dodge"] == Render.style_loss()
    assert Enum.any?(lines, &String.starts_with?(&1, "Score: "))
    assert keys(c) == ["1", "0"]
    c = press(c, "1")
    assert c.view == :equipment
    assert state(c).player.equipment["helmet"].uid == 801
  end

  defp list_key_of(c, prefix), do: Enum.find(Controller.options(c), &String.starts_with?(&1.label, prefix)).key

  test "the comparison warns about the required level", %{tmp_dir: dir} do
    c =
      dir
      |> equipment_controller()
      |> update_player(
        &%{&1 | bag: &1.bag ++ [%ItemInstance{uid: 810, item_id: "test_axe", rarity: "common", tier: 3}]}
      )
      |> press("1")

    assert List.last(Controller.body_colors(c)) == Render.style_loss()
    assert List.last(Controller.body_lines(c)) == "Requires level 13 (you are level 1)."
    c = press(c, "1")
    assert c.message == Controller.t(c, "error.level_too_low")
    assert c.view == :equipment
    c = press(c, ["0", "3"])
    c = press(c, list_key_of(c, "Weapon"))
    assert c.view == :equipped_slot
    assert Controller.title(c) == "Weapon"
    assert Enum.at(Controller.body_lines(c), 1) == "Attack: 6"
    c = press(c, "1")
    assert c.view == :equipment
    refute Map.has_key?(state(c).player.equipment, "weapon")
  end

  test "compare and slot views survive missing items", %{tmp_dir: dir} do
    c = equipment_controller(dir)
    assert List.last(Controller.body_lines(c)) == Controller.t(c, "equipment.bag_empty")
    c = %{c | view: :compare}
    assert Controller.body_lines(c) == []
    assert Controller.title(c) == Controller.t(c, "merchant.equipment")
    c = %{c | view: :equipped_slot} |> press("0")
    assert c.view == :equipment
    c = %{c | view: :equipped_slot} |> update_player(&%{&1 | equipment: %{}})
    assert Controller.body_lines(c) == [Controller.t(c, "equipment.empty")]
  end

  test "monster view and Hall of Fame markers", %{tmp_dir: dir} do
    elite_data = Helpers.with_balance(Helpers.data(), elite_chance_pct: 100)
    c = make_controller(dir, "en", elite_data)
    repository = c.services.repositories.profile
    profile = ProfileRepository.load(repository)

    entry = %HallOfFameEntry{
      run_id: "r",
      name: "Ana",
      vocation: "mage",
      difficulty: "hard",
      round: 100,
      level: 50,
      ended_at: "2026-01-01T00:00:00Z",
      won: true
    }

    ProfileRepository.save(repository, %{profile | hall_of_fame: profile.hall_of_fame ++ [entry]})
    c = press(c, "3")
    assert hd(Controller.body_lines(c)) =~ "WON"
    c = c |> press("0") |> start_run() |> press("0")
    assert Controller.monster_view(c).enemy_class == "elite"
    assert Enum.any?(c.log, &String.contains?(&1, "ELITE"))
  end
end
