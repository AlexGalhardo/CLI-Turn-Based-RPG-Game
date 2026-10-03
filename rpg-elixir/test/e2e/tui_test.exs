defmodule Rpg.E2e.TuiTest do
  @moduledoc "End-to-end tests: the real renderer driven by key presses (no animation, fixed seed, temp data dir)."
  use ExUnit.Case, async: true

  alias Rpg.Application.Commands.{Attack, BuyPotion, BuyStockItem, Cast, Defend, Equip, NextFight, SellItem, UsePotion}
  alias Rpg.Application.{GameSession, GreedyBot, Loot, Merchant}
  alias Rpg.Domain.Definitions.GameData
  alias Rpg.Domain.Entities.Player
  alias Rpg.Infrastructure.Art
  alias Rpg.Presentation.{Controller, Render}
  alias Rpg.Presentation.Tui.App
  alias Rpg.Test.Helpers

  @moduletag :tmp_dir

  defp make_app(dir, lang \\ "en", animate \\ false) do
    services = Rpg.Main.build_services(Helpers.data(), dir)
    controller = Controller.new(services, seed: 42, locale_override: lang)
    App.new(controller, Art.library(:embedded), animate)
  end

  defp press(app, keys) when is_list(keys), do: Enum.reduce(keys, app, &press(&2, &1))

  defp press(app, key) do
    {app, _} = App.handle_key(app, key)
    # Render after every key, like the terminal loop does.
    _ = App.render(app)
    app
  end

  defp type_text(app, text), do: press(app, String.graphemes(text))
  defp view(app), do: app.controller.view
  defp screen(app), do: App.render_plain(app)
  defp state(app), do: GameSession.state(app.controller.session)

  test "first launch: language, then the new run flow", %{tmp_dir: dir} do
    app = make_app(dir, nil)
    assert view(app) == :language
    app = press(app, "2")
    assert view(app) == :title
    assert screen(app) =~ "Nova jornada"
    app = press(app, "2")
    assert view(app) == :difficulty
    app = app |> press("3") |> type_text("Ana") |> press("enter")
    assert view(app) == :vocation
    app = press(app, "3")
    assert view(app) == :merchant
    text = screen(app)
    assert text =~ "Ana"
    assert text =~ "Mago"
    assert Helpers.read_json(Path.join(dir, "settings.json"))["locale"] == "pt-BR"
    assert File.exists?(Path.join(dir, "save.json"))
  end

  test "battle, merchant, save & quit and continue", %{tmp_dir: dir} do
    app = make_app(dir) |> press(["2", "2"]) |> type_text("Bo") |> press(["enter", "1"])
    assert view(app) == :merchant
    app = press(app, "1")
    assert view(app) == :buy_potions
    app = press(app, "1")
    assert view(app) == :quantity
    assert screen(app) =~ "> _"
    app = press(app, ["1", "enter"])
    assert Player.potion_count(state(app).player, "health_potion") == 6
    app = press(app, ["0", "5"])
    assert screen(app) =~ "Equipment"
    app = press(app, ["0", "0"])
    assert view(app) == :battle
    assert screen(app) =~ "HP"
    app = press(app, "1") |> press("2")
    assert view(app) == :spells
    app = press(app, Render.list_key(0)) |> press("3")
    assert view(app) == :potions
    app = press(app, "escape")
    assert view(app) == :battle
    app = press(app, "q")
    assert view(app) == :title
    assert screen(app) =~ "Continue"
    app = press(app, "1")
    assert view(app) == :merchant
    assert app.controller.session.info.sessions == 2
  end

  test "a full run until game over through the keys", %{tmp_dir: dir} do
    app = make_app(dir) |> press(["2", "3"]) |> type_text("Hero") |> press(["enter", "1"])
    app = play(app, 5000)
    assert view(app) == :game_over
    assert screen(app) =~ "GAME OVER"
    app = press(app, ["2", "3"])
    assert screen(app) =~ "Hero"
    app = press(app, ["0", "5"])
    assert screen(app) =~ "[x] First Blood"
    app = press(app, ["0", "4"])
    assert view(app) == :bestiary
    app = press(app, ["n", "p", "0"])
    assert {_app, :quit} = App.handle_key(app, "0")
    refute File.exists?(Path.join(dir, "save.json"))
    assert length(Path.wildcard(Path.join([dir, "history", "*.json"]))) == 1
  end

  defp play(app, 0), do: app

  defp play(app, steps) do
    if state(app).phase == :game_over do
      app
    else
      command = GreedyBot.choose(Helpers.data(), state(app))
      app |> press(keys_for(command, app)) |> play(steps - 1)
    end
  end

  defp keys_for(command, app) do
    data = Helpers.data()
    state = state(app)

    case command do
      %Attack{} ->
        ["1"]

      %Defend{} ->
        ["4"]

      %Cast{spell_id: id} ->
        spells = GameData.vocation(data, state.player.vocation_id).spells
        ["2", Render.list_key(Enum.find_index(spells, &(&1 == id)))]

      %UsePotion{potion_id: id} ->
        owned = for p <- data.potions, Player.potion_count(state.player, p.id) > 0, do: p.id
        ["3", Render.list_key(Enum.find_index(owned, &(&1 == id)))]

      %NextFight{} ->
        ["0"]

      %BuyPotion{potion_id: id, quantity: quantity} ->
        index = state |> Merchant.available_potions(data) |> Enum.find_index(&(&1 == id))
        ["1", Render.list_key(index)] ++ String.graphemes(Integer.to_string(quantity)) ++ ["enter", "0"]

      %SellItem{uid: uid} ->
        ["2", Render.list_key(Enum.find_index(state.player.bag, &(&1.uid == uid))), "0"]

      %Equip{uid: uid} ->
        vocation = GameData.vocation(data, state.player.vocation_id)
        usable = Enum.filter(state.player.bag, &Loot.can_use(GameData.item(data, &1.item_id), vocation))
        ["3", Render.list_key(Enum.find_index(usable, &(&1.uid == uid))), "0"]

      %BuyStockItem{index: index} ->
        ["4", Render.list_key(index), "0"]
    end
  end

  test "the screen fills exactly 100 x 30 with the documented panels", %{tmp_dir: dir} do
    app = make_app(dir) |> press(["2", "2"]) |> type_text("Bo") |> press(["enter", "1", "0"])
    lines = String.split(screen(app), "\n")
    assert length(lines) == 30
    assert Enum.all?(lines, &(String.length(&1) == 100))
    assert hd(lines) =~ ~r/^╭─ Round 1 · Tier 1 · NORMAL · Seed 42 ─+╮$/u
    assert Enum.at(lines, 7) =~ ~r/^╰─+╯$/u
    assert Enum.at(lines, 10) =~ "HP █"
    assert Enum.at(lines, 11) =~ "MP █"
    assert Enum.any?(lines, &(&1 =~ "[1] Attack"))
    assert Enum.any?(lines, &(&1 =~ ~r/\[1\] Attack\s+\[2\] Spells/))
  end

  test "colours and styles are ANSI escape sequences", %{tmp_dir: dir} do
    app = make_app(dir) |> press(["2", "2"]) |> type_text("Bo") |> press(["enter", "1", "0"])
    ansi = App.render(app)
    assert ansi =~ "\e[38;2;255;166;43m╭"
    assert ansi =~ "\e[1;38;2;95;215;255m[1] "
    assert ansi =~ "\e[0m"
  end

  test "small terminals show the resize message", %{tmp_dir: dir} do
    app = make_app(dir) |> App.resize(80, 24)
    assert screen(app) == "Please resize your terminal to at least 100 x 30."
    assert App.render(app) =~ "\e[1;38;2;255;215;95m"
  end

  test "informative screens hide the combat log and grow the menu", %{tmp_dir: dir} do
    app = make_app(dir) |> press("4") |> App.resize(120, 40)
    lines = String.split(screen(app), "\n")
    assert length(lines) == 40
    assert Enum.at(lines, 0) =~ "An endless journey"
    assert Enum.any?(lines, &(&1 =~ "Page 1/"))
  end

  test "animation cues play once and the idle loop advances", %{tmp_dir: dir} do
    app = make_app(dir, "en", true) |> press(["2", "2"]) |> type_text("Bo") |> press(["enter", "1", "0", "1"])
    assert app.cues != [] or state(app).phase != :battle
    ticked = App.tick(app)
    assert ticked.tick == app.tick + 1
    assert length(ticked.cues) == max(0, length(app.cues) - 1)
    assert is_binary(App.render(ticked))
    assert {_app, :quit} = App.handle_key(app, "ctrl+c")
  end

  test "the boss is announced on the top panel", %{tmp_dir: dir} do
    app = make_app(dir) |> press(["2", "2"]) |> type_text("Bo") |> press(["enter", "1"])
    session = app.controller.session
    engine = Helpers.update_state(session.engine, &%{&1 | round: 9})
    controller = %{app.controller | session: %{session | engine: engine}}
    app = press(%{app | controller: controller}, "0")
    assert screen(app) =~ "BOSS"
  end
end
