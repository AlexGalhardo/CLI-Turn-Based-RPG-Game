defmodule Rpg.Unit.ControllerTest do
  use ExUnit.Case, async: true

  alias Rpg.Application.Ports.ProfileRepository
  alias Rpg.Application.Profile.BestiaryEntry
  alias Rpg.Domain.Entities.ItemInstance
  alias Rpg.Infrastructure.Art
  alias Rpg.Presentation.{Controller, Render}
  alias Rpg.Test.Helpers

  @moduletag :tmp_dir

  def make_controller(dir, lang \\ "en") do
    services = Rpg.Main.build_services(Helpers.data(), dir)
    Controller.new(services, seed: 7, locale_override: lang)
  end

  def press(controller, keys) when is_list(keys), do: Enum.reduce(keys, controller, &Controller.press(&2, &1))
  def press(controller, key), do: Controller.press(controller, key)

  def start_run(controller, name \\ "Zed", vocation_key \\ "1") do
    controller |> press(["2", "2"]) |> press(String.graphemes(name)) |> press(["enter", vocation_key])
  end

  defp update_player(controller, fun) do
    session = controller.session
    engine = Helpers.update_player(session.engine, fun)
    %{controller | session: %{session | engine: engine}}
  end

  defp player(controller), do: controller.session.engine.state.player

  test "name validation and editing", %{tmp_dir: dir} do
    c = make_controller(dir) |> press(["2", "1"])
    assert c.view == :name
    c = press(c, "enter")
    assert c.message == Controller.t(c, "new_run.name_invalid")
    c = press(c, String.graphemes("Abcdefghijklmnopqrstuvwxyz"))
    assert c.input_buffer == "Abcdefghijklmnop"
    c = press(c, "backspace")
    assert Controller.input_prompt(c) == "> Abcdefghijklmno_"
    c = press(c, "\t")
    assert c.input_buffer == "Abcdefghijklmno"
    c = press(c, "escape")
    assert c.view == :difficulty
    assert press(c, "0").view == :title
  end

  test "title without a save has no continue", %{tmp_dir: dir} do
    c = make_controller(dir)
    refute "1" in Enum.map(Controller.options(c), & &1.key)
    c = press(c, "1")
    assert c.view == :title
    assert press(c, "0").exit_requested
  end

  test "first launch asks for the language", %{tmp_dir: dir} do
    c = make_controller(dir, nil)
    assert c.view == :language
    assert Controller.title(c) == Controller.t(c, "language.title")
  end

  test "language switch from the title", %{tmp_dir: dir} do
    c = make_controller(dir) |> press("6")
    assert c.view == :language
    c = press(c, "2")
    assert c.view == :title
    assert c.locale == "pt-BR"
    assert Controller.title(c) == "CLI Turn-Based RPG"
    assert Enum.any?(Controller.options(c), &(&1.label == "Sair"))
    assert make_controller(dir, nil).locale == "pt-BR"
  end

  test "merchant menus", %{tmp_dir: dir} do
    c = make_controller(dir) |> start_run()
    assert c.session != nil
    assert Controller.title(c) == Controller.t(c, "merchant.title_start")
    assert Controller.body_lines(c) == [Controller.t(c, "merchant.welcome", name: "Zed", gold: player(c).gold)]
    c = press(c, "2")
    assert Controller.body_lines(c) == [Controller.t(c, "merchant.empty_bag")]
    c = press(c, "0")

    c =
      update_player(c, fn p ->
        bag = [
          %ItemInstance{uid: 900, item_id: "hand_axe", rarity: "rare", tier: 0},
          %ItemInstance{uid: 901, item_id: "bow", rarity: "common", tier: 0}
        ]

        %{p | bag: p.bag ++ bag}
      end)

    c = press(c, "3")
    labels = Enum.map(Controller.options(c), & &1.label)
    assert Enum.any?(labels, &String.contains?(&1, "Hand Axe"))
    refute Enum.any?(labels, &String.contains?(&1, "Bow"))
    c = press(c, "1")
    assert player(c).equipment["weapon"].uid in [900, 1]
    c = press(c, ["0", "2"])
    sell_keys = Enum.map(Controller.options(c), & &1.key)
    assert List.last(sell_keys) == "0"
    c = c |> press(hd(sell_keys)) |> press(["0", "4"])
    assert Controller.title(c) == Controller.t(c, "merchant.stock")
    assert length(Controller.options(c)) == length(c.session.engine.state.merchant_stock) + 1
    c = update_player(c, &%{&1 | gold: 0}) |> press("1")
    assert c.message == Controller.t(c, "error.not_enough_gold")
    c = press(c, ["0", "1"])
    assert Controller.title(c) == Controller.t(c, "merchant.buy_potions")
    c = press(c, "1")
    assert c.view == :quantity
    assert Controller.title(c) =~ "Health Potion"
    c = press(c, ["x", "1", "2", "3"])
    assert c.input_buffer == "12"
    c = press(c, "escape")
    assert c.view == :buy_potions
    c = press(c, ["1", "enter"])
    assert c.view == :buy_potions
    c = press(c, ["0", "5"])
    assert c.view == :character
    assert Controller.title(c) == Controller.t(c, "merchant.character")
    assert Enum.any?(Controller.body_lines(c), &String.contains?(&1, "Equipment"))
    assert press(c, "0").view == :merchant
  end

  test "an empty stock shows a message", %{tmp_dir: dir} do
    c = make_controller(dir) |> start_run()
    session = c.session
    engine = Helpers.update_state(session.engine, &%{&1 | merchant_stock: []})
    c = %{c | session: %{session | engine: engine}} |> press("4")
    assert Controller.body_lines(c) == [Controller.t(c, "merchant.empty_stock")]
  end

  test "buying potions through the quantity prompt", %{tmp_dir: dir} do
    c = make_controller(dir) |> start_run()
    before = player(c).potions["health_potion"]
    c = press(c, ["1", "1", "1", "enter"])
    assert c.view == :buy_potions
    assert player(c).potions["health_potion"] == before + 1
    assert Enum.any?(c.log, &String.contains?(&1, "Health Potion"))
  end

  test "battle submenus and messages", %{tmp_dir: dir} do
    c = make_controller(dir) |> start_run("Zed", "3") |> press("0")
    assert c.view == :battle
    assert Controller.title(c) == Controller.t(c, "battle.title")
    assert Controller.monster_view(c) != nil
    assert Controller.player_view(c) != nil
    assert Controller.header(c) =~ "Seed 7"
    c = update_player(c, &%{&1 | potions: %{}}) |> press("3")
    assert Controller.title(c) == Controller.t(c, "battle.potions")
    assert Controller.body_lines(c) == [Controller.t(c, "battle.no_potions")]
    c = press(c, "0") |> update_player(&%{&1 | mp: 0}) |> press("2")
    assert Controller.title(c) == Controller.t(c, "battle.spells")
    c = press(c, Render.list_key(0))
    assert c.message == Controller.t(c, "error.not_enough_mana")
    c = press(c, "escape") |> press("4")
    assert c.animation_cues in [["attack"], []]
    c = press(c, "q")
    assert c.view == :title
    assert c.session == nil
  end

  test "battle actions update the log and the views", %{tmp_dir: dir} do
    c = make_controller(dir) |> start_run("Zed", "3") |> press("0") |> press("1")
    assert c.view in [:battle, :merchant, :game_over]
    assert length(c.log) > 0
    c = if c.view == :battle, do: press(c, ["3", "1"]), else: c
    assert c.view in [:battle, :merchant, :game_over]
  end

  test "bestiary paging and reveal", %{tmp_dir: dir} do
    c = make_controller(dir)
    repository = c.services.repositories.profile
    profile = ProfileRepository.load(repository)

    bestiary =
      profile.bestiary
      |> Map.put("rat", %BestiaryEntry{kills: 9, first_killed_at: "2026-01-01T00:00:00Z"})
      |> Map.put("bat", %BestiaryEntry{kills: 1, first_killed_at: "2026-01-01T00:00:00Z"})

    ProfileRepository.save(repository, %{profile | bestiary: bestiary})
    c = press(c, "4")
    lines = Controller.body_lines(c)
    assert length(lines) == Controller.page_size() + 2
    assert Enum.any?(lines, &(String.starts_with?(&1, "Rat") and String.contains?(&1, "weak")))
    assert Enum.any?(lines, &(String.starts_with?(&1, "Bat") and not String.contains?(&1, "weak")))
    c = press(c, "n")
    assert Controller.body_lines(c) != lines
    c = press(c, List.duplicate("n", 50))
    assert c |> Controller.body_lines() |> List.last() |> String.starts_with?("Page 12/12")
    c = press(c, ["p", "0", "3"])
    assert Controller.body_lines(c) == [Controller.t(c, "hall.empty")]
    c = press(c, ["0", "5"])

    assert c
           |> Controller.body_lines()
           |> Enum.take(Controller.page_size())
           |> Enum.all?(&String.starts_with?(&1, "[ ]"))
  end

  test "render helpers" do
    assert Render.bar(0, 100, 10) == String.duplicate("░", 10)
    assert Render.bar(1, 100, 10) == "█" <> String.duplicate("░", 9)
    assert Render.bar(100, 100, 10) == String.duplicate("█", 10)
    assert Render.bar(5, 0, 4) == String.duplicate("░", 4)
    assert Render.hp_color(60, 100) == "green"
    assert Render.hp_color(30, 100) == "yellow"
    assert Render.hp_color(10, 100) == "red"
    assert Render.list_key(0) == "1"
    assert Render.list_key(9) == "a"
    assert Render.list_index("a") == 9
    assert Render.list_index("!") == nil
    assert Render.list_index("ab") == nil
    assert Render.color_hex("rare") == "#1e90ff"
    assert Render.color_hex(nil) == nil
  end

  test "art parsing" do
    animations = Art.parse_art("@idle\n a\n%%\n b\n@hurt\n x\n")
    assert animations == %{"idle" => [[" a"], [" b"]], "hurt" => [[" x"]]}
    assert Art.frame_for(animations, "idle", 3) == [" b"]
    assert Art.frame_for(animations, "attack", 0) == [" a"]
    assert Art.frame_for(%{}, "idle", 0) == []
    assert_raise ArgumentError, ~r/before/, fn -> Art.parse_art("oops") end
    assert_raise ArgumentError, ~r/separator/, fn -> Art.parse_art("%%") end
    library = Art.library(:embedded)
    data = Helpers.data()

    for creature <- data.monsters ++ data.bosses do
      assert Art.frame_for(Art.for_creature(library, creature), "idle", 0) != []
    end
  end
end
