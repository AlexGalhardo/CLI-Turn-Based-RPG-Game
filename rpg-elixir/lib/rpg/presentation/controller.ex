defmodule Rpg.Presentation.Controller do
  @moduledoc """
  Framework-independent UI state machine: which screen is shown, its options and what each key does.

  The terminal renderer only draws this controller. The other five ports implement the same controller, which is what
  keeps the six interfaces practically identical (docs/tui.md).

  The controller is an immutable struct: `press/2` returns a new controller, and the query functions (`title/1`,
  `options/1`, `body_lines/1`, ...) never change it. Menu entries pair a `MenuOption` with an action, a function
  from controller to controller.
  """

  alias Rpg.Application.Commands.{
    Attack,
    BuyPotion,
    BuyStockItem,
    Cast,
    ContinueRun,
    Defend,
    EndRun,
    Equip,
    NextFight,
    SellItem,
    Unequip,
    UsePotion
  }

  alias Rpg.Application.GameSession.{Repositories, StepResult}
  alias Rpg.Application.{AutoBattle, GameSession, Loot, Merchant, ProfileService, RunConfig}
  alias Rpg.Application.Ports.{ProfileRepository, SaveRepository}
  alias Rpg.Domain.Character.CharacterSheet
  alias Rpg.Domain.Definitions.{GameData, MonsterDef}
  alias Rpg.Domain.Entities.{ItemInstance, Player}
  alias Rpg.Domain.{Character, Enums, Formulas}
  alias Rpg.Infrastructure.I18n
  alias Rpg.Infrastructure.Repositories
  alias Rpg.Infrastructure.Repositories.{Settings, SettingsRepository}
  alias Rpg.Presentation.{EventText, Render}

  @max_log_lines 50
  @max_name_length 16
  @max_quantity_digits 2
  @page_size 10
  # Auto-battle pace (docs/tui.md): one turn every 600 ms at 1x, 300 ms at 2x; instant with --no-anim.
  @auto_battle_base_ms 600
  # Safety net: a fight that somehow never ends hands control back to the player.
  @max_auto_battle_turns 10_000

  @battle_views [:battle, :spells, :potions, :auto_battle]
  @text_input_views [:name, :quantity]
  @paged_views [:hall_of_fame, :bestiary, :achievements, :character]
  @styled_views [:equipment, :compare, :equipped_slot]

  @type view ::
          :language
          | :title
          | :settings
          | :difficulty
          | :name
          | :vocation
          | :auto_equip
          | :merchant
          | :buy_potions
          | :quantity
          | :sell
          | :equipment
          | :compare
          | :equipped_slot
          | :stock
          | :character
          | :battle
          | :spells
          | :potions
          | :auto_battle
          | :victory
          | :game_over
          | :hall_of_fame
          | :bestiary
          | :achievements

  defmodule MenuOption do
    @moduledoc false
    @enforce_keys [:key, :label]
    defstruct [:key, :label, color: nil, detail: "", detail_color: nil]

    @type t :: %__MODULE__{
            key: String.t(),
            label: String.t(),
            color: String.t() | nil,
            detail: String.t(),
            detail_color: String.t() | nil
          }
  end

  defmodule BodyLine do
    @moduledoc false
    @enforce_keys [:text]
    defstruct [:text, color: nil]
    @type t :: %__MODULE__{text: String.t(), color: String.t() | nil}
  end

  defmodule Services do
    @moduledoc "Everything the controller needs from the outside world (the adapters are injected here)."
    @enforce_keys [:data, :shared, :settings, :repositories, :clock, :version]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            data: GameData.t(),
            shared: Rpg.Infrastructure.Assets.source(),
            settings: SettingsRepository.t(),
            repositories: Repositories.t(),
            clock: struct(),
            version: String.t()
          }
  end

  defmodule MonsterView do
    @moduledoc false
    @enforce_keys [:name, :creature_id, :hp, :max_hp, :is_boss, :enemy_class, :element, :details]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            name: String.t(),
            creature_id: String.t(),
            hp: integer(),
            max_hp: integer(),
            is_boss: boolean(),
            enemy_class: String.t(),
            element: String.t(),
            details: String.t()
          }
  end

  defmodule PlayerView do
    @moduledoc false
    @enforce_keys [:summary, :gold, :hp, :max_hp, :mp, :max_mp, :xp, :statuses]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            summary: String.t(),
            gold: String.t(),
            hp: integer(),
            max_hp: integer(),
            mp: integer(),
            max_mp: integer(),
            xp: String.t(),
            statuses: String.t()
          }
  end

  @enforce_keys [:services, :seed, :seed_source, :locale, :translator, :formatter, :view, :settings]
  defstruct [
    :services,
    :settings,
    :seed,
    :seed_source,
    :locale,
    :translator,
    :formatter,
    :view,
    language_return: :title,
    session: nil,
    log: [],
    message: "",
    input_buffer: "",
    exit_requested: false,
    animation_cues: [],
    page: 0,
    difficulty: "normal",
    name: "",
    vocation: "",
    potion_id: "",
    compare_uid: 0,
    slot: "weapon",
    auto_battle: nil,
    auto_mode: "melee",
    auto_turns: 0
  ]

  @type t :: %__MODULE__{}
  @type action :: (t() -> t())

  @spec page_size() :: pos_integer()
  def page_size, do: @page_size

  @spec paged?(view()) :: boolean()
  def paged?(view), do: view in @paged_views

  @spec random_seed() :: non_neg_integer()
  def random_seed, do: :rand.uniform(2 ** 32) - 1

  @doc "Options: `:seed` (nil = random per run), `:locale_override`, `:seed_source` (a 0-arity function)."
  @spec new(Services.t(), keyword()) :: t()
  def new(%Services{} = services, opts \\ []) do
    settings = SettingsRepository.load(services.settings)
    override = Keyword.get(opts, :locale_override)
    chosen = override || settings.locale

    controller = %__MODULE__{
      services: services,
      seed: Keyword.get(opts, :seed),
      seed_source: Keyword.get(opts, :seed_source, &random_seed/0),
      locale: nil,
      translator: nil,
      formatter: nil,
      settings: settings,
      view: if(chosen, do: :title, else: :language)
    }

    set_locale(controller, chosen || I18n.default_locale())
  end

  # ── i18n ────────────────────────────────────────────────────────────────────

  defp set_locale(controller, locale) do
    translator = I18n.new(controller.services.shared, locale)
    formatter = EventText.formatter(controller.services.data, translator)
    %{controller | locale: locale, translator: translator, formatter: formatter}
  end

  @spec t(t(), String.t(), map() | keyword()) :: String.t()
  def t(%__MODULE__{translator: translator}, key, params \\ %{}), do: I18n.t(translator, key, params)

  defp data(controller), do: controller.services.data

  # ── queries used by renderers ───────────────────────────────────────────────

  @spec title(t()) :: String.t()
  def title(%__MODULE__{view: view} = c) do
    case view do
      :language -> t(c, "language.title")
      :title -> t(c, "app.title")
      :settings -> t(c, "settings.title")
      :difficulty -> t(c, "new_run.difficulty")
      :name -> t(c, "new_run.name")
      :vocation -> t(c, "new_run.vocation")
      :auto_equip -> t(c, "new_run.auto_equip")
      :merchant -> merchant_title(c)
      :buy_potions -> t(c, "merchant.buy_potions")
      :quantity -> t(c, "merchant.quantity", name: GameData.potion(data(c), c.potion_id).name)
      :sell -> t(c, "merchant.sell_items")
      :equipment -> t(c, "merchant.equipment")
      :compare -> compare_title(c)
      :equipped_slot -> t(c, "slot.#{c.slot}")
      :stock -> t(c, "merchant.stock")
      :character -> t(c, "merchant.character")
      :battle -> t(c, "battle.title")
      :spells -> t(c, "battle.spells")
      :potions -> t(c, "battle.potions")
      :auto_battle -> t(c, "auto_battle.title")
      :victory -> t(c, "victory.title")
      :game_over -> if(won?(c), do: t(c, "gameover.title_won"), else: t(c, "gameover.title"))
      :hall_of_fame -> t(c, "menu.hall_of_fame")
      :bestiary -> t(c, "menu.bestiary")
      :achievements -> t(c, "menu.achievements")
    end
  end

  defp won?(%__MODULE__{session: nil}), do: false
  defp won?(%__MODULE__{session: session}), do: GameSession.state(session).won

  defp merchant_title(c) do
    case if(c.session, do: GameSession.state(c.session).round, else: 0) do
      0 -> t(c, "merchant.title_start")
      round -> t(c, "merchant.title", round: round)
    end
  end

  @spec options(t()) :: [MenuOption.t()]
  def options(%__MODULE__{} = c), do: Enum.map(menu(c), fn {option, _action} -> option end)

  @doc "Informative lines shown above the options (paged for long lists)."
  @spec body_lines(t()) :: [String.t()]
  def body_lines(%__MODULE__{} = c) do
    lines = if c.view in @styled_views, do: Enum.map(styled_body(c), & &1.text), else: body(c)

    if c.view not in @paged_views or length(lines) <= @page_size do
      lines
    else
      pages = page_count(lines)
      page = min(c.page, pages - 1)
      Enum.slice(lines, page * @page_size, @page_size) ++ ["", t(c, "menu.page", page: page + 1, pages: pages)]
    end
  end

  @doc "Colour of each line of `body_lines/1` (semantic styles from `Render` or rarity ids)."
  @spec body_colors(t()) :: [String.t() | nil]
  def body_colors(%__MODULE__{} = c) do
    if c.view in @styled_views,
      do: Enum.map(styled_body(c), & &1.color),
      else: List.duplicate(nil, length(body_lines(c)))
  end

  defp page_count(lines), do: max(1, div(length(lines) + @page_size - 1, @page_size))

  @spec input_prompt(t()) :: String.t() | nil
  def input_prompt(%__MODULE__{view: view, input_buffer: buffer}) when view in @text_input_views, do: "> #{buffer}_"
  def input_prompt(%__MODULE__{}), do: nil

  @spec header(t()) :: String.t()
  def header(%__MODULE__{session: nil} = c), do: t(c, "app.subtitle")

  def header(%__MODULE__{session: session} = c) do
    state = GameSession.state(session)
    data = data(c)
    tier = Formulas.round_info(max(1, state.round), data.balance, GameData.tier_count(data)).tier + 1
    difficulty = t(c, "difficulty.#{state.config.difficulty_id}")
    round_text = t(c, "hud.round", round: state.round, tier: tier, difficulty: difficulty)
    "#{round_text} · #{t(c, "hud.seed", seed: state.seed)}"
  end

  @spec monster_view(t()) :: MonsterView.t() | nil
  def monster_view(%__MODULE__{session: nil}), do: nil

  def monster_view(%__MODULE__{session: session} = c) do
    case GameSession.state(session).monster do
      nil ->
        nil

      monster ->
        creature = GameData.creature(data(c), monster.creature_id)
        main_element = Enum.max_by(monster.attacks, &{&1.weight, &1.id}).element
        weak = for e <- Enums.elements(), MonsterDef.resistance(creature, e) > 100, do: t(c, "element.#{e}")
        details = monster.attacks |> Enum.map(&t(c, "element.#{&1.element}")) |> Enum.uniq() |> Enum.join(" · ")

        details =
          if weak != [], do: details <> " · " <> t(c, "hud.weak", elements: Enum.join(weak, ", ")), else: details

        statuses = status_text(c, monster.statuses)
        details = if statuses != "", do: details <> " · " <> statuses, else: details

        %MonsterView{
          name: creature.name,
          creature_id: creature.id,
          hp: monster.hp,
          max_hp: monster.max_hp,
          is_boss: monster.is_boss,
          enemy_class: monster.enemy_class,
          element: main_element,
          details: details
        }
    end
  end

  defp status_text(c, statuses), do: Enum.map_join(statuses, " ", &"#{t(c, "status.#{&1.status_id}")}(#{&1.turns})")

  @spec player_view(t()) :: PlayerView.t() | nil
  def player_view(%__MODULE__{session: nil}), do: nil

  def player_view(%__MODULE__{session: session} = c) do
    player = GameSession.state(session).player
    sheet = Character.build_sheet(player, data(c))

    summary =
      t(c, "hud.player",
        name: player.name,
        vocation: t(c, "vocation.#{player.vocation_id}"),
        level: player.level,
        magicLevel: player.magic_level
      )

    %PlayerView{
      summary: summary,
      gold: t(c, "hud.gold", gold: player.gold),
      hp: player.hp,
      max_hp: sheet.max_hp,
      mp: player.mp,
      max_mp: sheet.max_mp,
      xp: t(c, "hud.xp", xp: player.xp, next: Formulas.xp_for_level(player.level + 1)),
      statuses: status_text(c, player.statuses)
    }
  end

  # ── input ───────────────────────────────────────────────────────────────────

  @doc "Handles one key: a character (`\"1\"`, `\"a\"`) or a name (`\"enter\"`, `\"escape\"`, `\"backspace\"`)."
  @spec press(t(), String.t()) :: t()
  def press(%__MODULE__{auto_battle: policy} = c, _key) when policy != nil, do: c

  def press(%__MODULE__{} = c, key) do
    c = %{c | message: ""}

    cond do
      c.view in @text_input_views ->
        text_input(c, key)

      c.view in @paged_views and key in ["n", "p"] ->
        pages = c |> body() |> page_count()
        %{c | page: (c.page + if(key == "n", do: 1, else: -1)) |> max(0) |> min(pages - 1)}

      true ->
        key = if key == "escape", do: "0", else: key

        case Enum.find(menu(c), fn {option, _action} -> option.key == key end) do
          nil -> c
          {_option, action} -> action.(c)
        end
    end
  end

  # ── auto-battle (docs/game-design.md §13, docs/tui.md) ─────────────────────

  @spec auto_battle_active?(t()) :: boolean()
  def auto_battle_active?(%__MODULE__{auto_battle: policy}), do: policy != nil

  @spec auto_battle_interval_ms(t()) :: pos_integer()
  def auto_battle_interval_ms(%__MODULE__{settings: settings}), do: div(@auto_battle_base_ms, settings.battle_speed)

  @doc "Plays one auto-battle turn. Returns the controller and whether the fight goes on (the timer keeps ticking)."
  @spec auto_battle_step(t()) :: {t(), boolean()}
  def auto_battle_step(%__MODULE__{auto_battle: policy, session: session} = c) do
    if policy == nil or session == nil or GameSession.state(session).phase != :battle do
      {%{c | auto_battle: nil}, false}
    else
      c = %{c | auto_turns: c.auto_turns + 1}
      c = step(c, AutoBattle.choose(policy, GameSession.state(c.session)))

      c =
        if GameSession.state(c.session).phase != :battle or c.auto_turns >= @max_auto_battle_turns,
          do: %{c | auto_battle: nil},
          else: c

      {c, auto_battle_active?(c)}
    end
  end

  @doc "Instant mode (--no-anim): plays the whole fight at once."
  @spec run_auto_battle(t()) :: t()
  def run_auto_battle(%__MODULE__{} = c) do
    case auto_battle_step(c) do
      {c, true} -> run_auto_battle(c)
      {c, false} -> c
    end
  end

  defp start_auto_battle(c, mode) do
    line = t(c, "auto_battle.started", mode: t(c, "auto_battle.#{mode}"))

    push_log(
      %{c | auto_mode: mode, auto_turns: 0, auto_battle: AutoBattle.policy(data(c), mode), view: :battle},
      line
    )
  end

  defp text_input(c, "escape"),
    do: %{c | input_buffer: "", view: if(c.view == :name, do: :difficulty, else: :buy_potions)}

  defp text_input(c, "backspace"), do: %{c | input_buffer: String.slice(c.input_buffer, 0..-2//1)}
  defp text_input(c, "enter"), do: submit_text(c)

  defp text_input(%{view: :name} = c, key) do
    if printable_char?(key) and String.length(c.input_buffer) < @max_name_length,
      do: %{c | input_buffer: c.input_buffer <> key},
      else: c
  end

  defp text_input(%{view: :quantity} = c, key) do
    if key =~ ~r/^[0-9]$/ and String.length(c.input_buffer) < @max_quantity_digits,
      do: %{c | input_buffer: c.input_buffer <> key},
      else: c
  end

  defp printable_char?(key) do
    case String.to_charlist(key) do
      [codepoint] -> codepoint >= 32 and codepoint != 127 and String.printable?(key)
      _ -> false
    end
  end

  defp submit_text(c) do
    text = String.trim(c.input_buffer)
    c = %{c | input_buffer: ""}

    case c.view do
      :name ->
        if String.length(text) in 1..@max_name_length,
          do: %{c | name: text, view: :vocation},
          else: %{c | message: t(c, "new_run.name_invalid")}

      :quantity ->
        c = %{c | view: :buy_potions}

        case Integer.parse(text) do
          {quantity, ""} when quantity > 0 -> step(c, %BuyPotion{potion_id: c.potion_id, quantity: quantity})
          _ -> c
        end
    end
  end

  # ── menus ───────────────────────────────────────────────────────────────────

  @spec menu(t()) :: [{MenuOption.t(), action()}]
  defp menu(%__MODULE__{view: view} = c) do
    case view do
      :language ->
        I18n.supported_locales()
        |> Enum.with_index(1)
        |> Enum.map(fn {locale, i} -> {option("#{i}", t(c, "language.#{locale}")), &choose_language(&1, locale)} end)

      :title ->
        title_menu(c)

      :settings ->
        settings_menu(c)

      :difficulty ->
        items =
          data(c).balance.difficulties
          |> Enum.with_index(1)
          |> Enum.map(fn {d, i} ->
            label = "#{t(c, "difficulty.#{d.id}")} — #{t(c, "difficulty.#{d.id}.description")}"
            {option("#{i}", label), &choose_difficulty(&1, d.id)}
          end)

        items ++ [back(c, :title)]

      :vocation ->
        items =
          data(c).vocations
          |> Enum.with_index(1)
          |> Enum.map(fn {v, i} ->
            label = "#{t(c, "vocation.#{v.id}")} — #{t(c, "vocation.#{v.id}.description")}"
            {option("#{i}", label), &choose_vocation(&1, v.id)}
          end)

        items ++ [back(c, :difficulty)]

      :auto_equip ->
        auto_equip_menu(c)

      :merchant ->
        merchant_menu(c)

      :buy_potions ->
        potion_shop(c) ++ [back(c, :merchant)]

      :sell ->
        sell_menu(c) ++ [back(c, :merchant)]

      :equipment ->
        equipment_menu(c) ++ [back(c, :merchant)]

      :compare ->
        [{option("1", t(c, "equipment.equip")), &equip_compared/1}, back(c, :equipment)]

      :equipped_slot ->
        [{option("1", t(c, "equipment.unequip")), &unequip_slot/1}, back(c, :equipment)]

      :stock ->
        stock_menu(c) ++ [back(c, :merchant)]

      :character ->
        [back(c, :merchant)]

      :battle ->
        [
          {option("1", t(c, "battle.attack")), &step(&1, %Attack{})},
          {option("2", t(c, "battle.spells")), go(:spells)},
          {option("3", t(c, "battle.potions")), go(:potions)},
          {option("4", t(c, "battle.defend")), &step(&1, %Defend{})},
          {option("5", t(c, "battle.auto")), go(:auto_battle)},
          {option("q", t(c, "battle.save_quit")), &save_and_quit/1}
        ]

      :auto_battle ->
        modes =
          AutoBattle.modes()
          |> Enum.with_index(1)
          |> Enum.map(fn {mode, i} -> {option("#{i}", t(c, "auto_battle.#{mode}")), &start_auto_battle(&1, mode)} end)

        modes ++ [back(c, :battle)]

      :victory ->
        [
          {option("1", t(c, "victory.end_run")), &step(&1, %EndRun{})},
          {option("2", t(c, "victory.continue")), &step(&1, %ContinueRun{})}
        ]

      :spells ->
        spell_menu(c) ++ [back(c, :battle)]

      :potions ->
        battle_potions(c) ++ [back(c, :battle)]

      :game_over ->
        [
          {option("1", t(c, "gameover.new_run")), &new_run/1},
          {option("2", t(c, "gameover.title_screen")), go(:title)}
        ]

      paged when paged in [:hall_of_fame, :bestiary, :achievements] ->
        [back(c, :title)]

      text when text in @text_input_views ->
        []
    end
  end

  defp option(key, label, color \\ nil), do: %MenuOption{key: key, label: label, color: color}

  defp back(c, target), do: {option("0", t(c, "menu.back")), go(target)}

  defp go(target), do: fn c -> %{c | view: target, page: 0} end

  defp title_menu(c) do
    continue =
      if SaveRepository.load(c.services.repositories.saves) != nil,
        do: [{option("1", t(c, "menu.continue")), &continue_run/1}],
        else: []

    continue ++
      [
        {option("2", t(c, "menu.new_run")), &new_run/1},
        {option("3", t(c, "menu.hall_of_fame")), go(:hall_of_fame)},
        {option("4", t(c, "menu.bestiary")), go(:bestiary)},
        {option("5", t(c, "menu.achievements")), go(:achievements)},
        {option("6", t(c, "menu.settings")), go(:settings)},
        {option("0", t(c, "menu.quit")), &%{&1 | exit_requested: true}}
      ]
  end

  defp on_off(c, enabled), do: t(c, if(enabled, do: "settings.on", else: "settings.off"))

  defp settings_menu(c) do
    settings = c.settings

    [
      {option("1", t(c, "settings.language", language: t(c, "language.#{c.locale}"))), &open_language/1},
      {option("2", t(c, "settings.auto_equip", state: on_off(c, settings.auto_equip))),
       &save_settings(&1, %{&1.settings | auto_equip: not &1.settings.auto_equip})},
      {option("3", t(c, "settings.battle_speed", speed: settings.battle_speed)), &cycle_battle_speed/1},
      back(c, :title)
    ]
  end

  defp save_settings(c, %Settings{} = settings) do
    SettingsRepository.save(c.services.settings, settings)
    %{c | settings: settings}
  end

  defp cycle_battle_speed(c) do
    speeds = Repositories.battle_speeds()
    index = Enum.find_index(speeds, &(&1 == c.settings.battle_speed)) || 0
    save_settings(c, %{c.settings | battle_speed: Enum.at(speeds, rem(index + 1, length(speeds)))})
  end

  defp auto_equip_menu(c) do
    default = t(c, "new_run.default")

    menu =
      Enum.map([{"1", true}, {"2", false}], fn {key, enabled} ->
        label = t(c, if(enabled, do: "new_run.auto_equip_on", else: "new_run.auto_equip_off"))
        label = if enabled == c.settings.auto_equip, do: "#{label} #{default}", else: label
        {option(key, label), &start_run(&1, enabled)}
      end)

    menu ++ [back(c, :vocation)]
  end

  defp merchant_menu(c) do
    [
      {option("1", t(c, "merchant.buy_potions")), go(:buy_potions)},
      {option("2", t(c, "merchant.sell_items")), go(:sell)},
      {option("3", t(c, "merchant.equipment")), go(:equipment)},
      {option("4", t(c, "merchant.stock")), go(:stock)},
      {option("5", t(c, "merchant.character")), go(:character)},
      {option("0", t(c, "merchant.next_fight")), &step(&1, %NextFight{})},
      {option("q", t(c, "battle.save_quit")), &save_and_quit/1}
    ]
  end

  defp item_label(c, key, template, %ItemInstance{} = item, params) do
    definition = GameData.item(data(c), item.item_id)

    params =
      Map.merge(
        %{name: definition.name, rarity: t(c, "rarity.#{item.rarity}"), slot: t(c, "slot.#{definition.slot}")},
        Map.new(params)
      )

    option(key, t(c, template, params), item.rarity)
  end

  defp run_state(c), do: GameSession.state(require_session(c))

  defp potion_shop(c) do
    state = run_state(c)

    state
    |> Merchant.available_potions(data(c))
    |> Enum.with_index()
    |> Enum.map(fn {potion_id, index} ->
      potion = GameData.potion(data(c), potion_id)
      count = Player.potion_count(state.player, potion_id)
      label = t(c, "merchant.potion_option", name: potion.name, price: potion.price, count: count)
      {option(Render.list_key(index), label), &ask_quantity(&1, potion_id)}
    end)
  end

  defp sell_menu(c) do
    run_state(c).player.bag
    |> Enum.with_index()
    |> Enum.map(fn {item, index} ->
      gold = Character.item_value(item, data(c))

      {item_label(c, Render.list_key(index), "merchant.sell_option", item, gold: gold),
       command(%SellItem{uid: item.uid})}
    end)
  end

  defp usable_bag(c) do
    player = run_state(c).player
    vocation = GameData.vocation(data(c), player.vocation_id)
    Enum.filter(player.bag, &Loot.can_use(GameData.item(data(c), &1.item_id), vocation))
  end

  # Score of `item` minus the score of what is equipped in its slot (0 for an empty slot).
  defp score_delta(c, item) do
    equipped = Map.get(run_state(c).player.equipment, GameData.item(data(c), item.item_id).slot)
    current = if equipped == nil, do: 0, else: Character.item_score(equipped, data(c))
    Character.item_score(item, data(c)) - current
  end

  # Usable bag items first (keys 1..n, as in docs/tui.md), then the equipped slots.
  defp equipment_menu(c) do
    player = run_state(c).player
    bag = Enum.map(usable_bag(c), &bag_entry(c, player, &1))

    slots =
      for slot <- Enums.equipment_slot_order(), Map.has_key?(player.equipment, slot) do
        equipped = player.equipment[slot]

        label =
          t(c, "equipment.slot_option",
            slot: t(c, "slot.#{slot}"),
            name: GameData.item(data(c), equipped.item_id).name,
            rarity: t(c, "rarity.#{equipped.rarity}")
          )

        {option("", label, equipped.rarity), &open_slot(&1, slot)}
      end

    (bag ++ slots)
    |> Enum.with_index()
    |> Enum.map(fn {{option, action}, index} -> {%{option | key: Render.list_key(index)}, action} end)
  end

  defp bag_entry(c, player, item) do
    definition = GameData.item(data(c), item.item_id)
    level = Character.required_level(item, data(c))

    label =
      t(c, "equipment.bag_option",
        name: definition.name,
        rarity: t(c, "rarity.#{item.rarity}"),
        slot: t(c, "slot.#{definition.slot}"),
        level: level,
        score: Character.item_score(item, data(c))
      )

    too_high = level > player.level
    label = if too_high, do: "#{label} · #{t(c, "equipment.requires_level", level: level)}", else: label
    delta = score_delta(c, item)

    option = %MenuOption{
      key: "",
      label: label,
      color: if(too_high, do: Render.style_dim(), else: item.rarity),
      detail: Render.format_delta(delta),
      detail_color: Render.delta_style(delta)
    }

    {option, &open_compare(&1, item.uid)}
  end

  defp open_compare(c, uid), do: %{c | compare_uid: uid, view: :compare}
  defp open_slot(c, slot), do: %{c | slot: slot, view: :equipped_slot}

  defp compared_item(c), do: Enum.find(run_state(c).player.bag, &(&1.uid == c.compare_uid))

  defp equip_compared(c), do: %{step(c, %Equip{uid: c.compare_uid}) | view: :equipment}
  defp unequip_slot(c), do: %{step(c, %Unequip{slot: c.slot}) | view: :equipment}

  defp stock_menu(c) do
    run_state(c).merchant_stock
    |> Enum.with_index()
    |> Enum.map(fn {item, index} ->
      gold = Merchant.stock_price(item, data(c))
      label = item_label(c, Render.list_key(index), "merchant.stock_option", item, gold: gold)
      {label, command(%BuyStockItem{index: index})}
    end)
  end

  defp spell_menu(c) do
    player = run_state(c).player
    levels = data(c).balance.spell_levels

    GameData.vocation(data(c), player.vocation_id).spells
    |> Enum.with_index()
    |> Enum.map(fn {spell_id, index} ->
      spell = GameData.spell(data(c), spell_id)
      uses = Map.get(player.spell_uses, spell_id, 0)
      level = Formulas.spell_level_for_uses(uses, levels)

      label =
        t(c, "battle.spell_option",
          name: spell.name,
          words: spell.words,
          mana: Formulas.pct(spell.mana, level.mana_pct),
          level: level.level,
          uses: uses
        )

      color = if spell.kind == :heal, do: "green", else: spell.element
      {option(Render.list_key(index), label, color), command(%Cast{spell_id: spell_id})}
    end)
  end

  defp battle_potions(c) do
    player = run_state(c).player

    data(c).potions
    |> Enum.filter(&(Player.potion_count(player, &1.id) > 0))
    |> Enum.with_index()
    |> Enum.map(fn {p, index} ->
      label = t(c, "battle.potion_option", name: p.name, count: Player.potion_count(player, p.id))
      {option(Render.list_key(index), label), command(%UsePotion{potion_id: p.id})}
    end)
  end

  # ── actions ─────────────────────────────────────────────────────────────────

  defp choose_language(c, locale) do
    c = save_settings(c, %{c.settings | locale: locale})
    %{set_locale(c, locale) | view: c.language_return}
  end

  defp open_language(c), do: %{c | language_return: :settings, view: :language}

  defp new_run(c), do: %{c | session: nil, view: :difficulty}

  defp choose_difficulty(c, difficulty_id), do: %{c | difficulty: difficulty_id, input_buffer: "", view: :name}

  defp choose_vocation(c, vocation_id), do: %{c | vocation: vocation_id, view: :auto_equip}

  defp start_run(c, auto_equip) do
    seed = if c.seed != nil, do: c.seed, else: c.seed_source.()
    services = c.services
    config = %RunConfig{name: c.name, vocation_id: c.vocation, difficulty_id: c.difficulty, auto_equip: auto_equip}

    {session, events} =
      GameSession.start(services.data, config, seed,
        repositories: services.repositories,
        clock: services.clock,
        game_version: services.version
      )

    c = %{c | session: session, log: []}
    %{record(c, %StepResult{events: events, achievements: []}) | view: :merchant}
  end

  defp continue_run(c) do
    services = c.services

    case GameSession.resume(services.data, services.repositories, services.clock, services.version) do
      nil ->
        c

      session ->
        player = GameSession.state(session).player
        line = t(c, "menu.welcome_back", name: player.name, round: GameSession.state(session).round)
        view = if GameSession.state(session).phase == :victory, do: :victory, else: :merchant
        %{c | session: session, log: [line], view: view}
    end
  end

  defp ask_quantity(c, potion_id), do: %{c | potion_id: potion_id, input_buffer: "", view: :quantity}

  defp command(cmd), do: &step(&1, cmd)

  defp save_and_quit(c) do
    if c.session, do: GameSession.save_and_quit(c.session)
    %{c | session: nil, view: :title}
  end

  defp require_session(%__MODULE__{session: nil, view: view}),
    do: raise(RuntimeError, "view #{view} needs an active run")

  defp require_session(%__MODULE__{session: session}), do: session

  defp step(c, cmd) do
    {session, result} = GameSession.step(require_session(c), cmd)
    c = record(%{c | session: session}, result)

    case GameSession.state(session).phase do
      :battle -> %{c | view: :battle}
      :game_over -> %{c | view: :game_over}
      :victory -> %{c | view: :victory}
      _ -> if c.view in @battle_views or c.view == :victory, do: %{c | view: :merchant}, else: c
    end
  end

  defp record(c, %StepResult{} = result) do
    state = GameSession.state(require_session(c))

    {c, cues} =
      Enum.reduce(result.events, {c, []}, fn event, {c, cues} ->
        text = EventText.format(c.formatter, event, state)

        cond do
          event["type"] == "error" ->
            {%{c | message: text}, cues}

          event["type"] in ["player_attacked", "spell_cast"] and Map.get(event, "damage", 0) != 0 ->
            {push_log(c, text), cues ++ ["hurt"]}

          event["type"] == "monster_attacked" ->
            {push_log(c, text), cues ++ ["attack"]}

          true ->
            {push_log(c, text), cues}
        end
      end)

    c =
      Enum.reduce(result.achievements, c, fn achievement, c ->
        push_log(c, t(c, "achievement.unlocked", name: t(c, "achievement.#{achievement.id}.name")))
      end)

    %{c | animation_cues: cues}
  end

  defp push_log(c, line), do: %{c | log: Enum.take(c.log ++ [line], -@max_log_lines)}

  # ── informative bodies ──────────────────────────────────────────────────────

  defp profile(c) do
    if c.session,
      do: c.session.profile,
      else: ProfileService.new(data(c), ProfileRepository.load(c.services.repositories.profile))
  end

  defp body(%__MODULE__{view: view} = c) do
    case view do
      :character ->
        character_sheet(c)

      :game_over ->
        game_over_summary(c)

      :victory ->
        victory_summary(c)

      :hall_of_fame ->
        hall_of_fame(c)

      :bestiary ->
        bestiary(c)

      :achievements ->
        achievements(c)

      :merchant ->
        merchant_welcome(c)

      :sell ->
        if run_state(c).player.bag == [], do: [t(c, "merchant.empty_bag")], else: []

      :stock ->
        if run_state(c).merchant_stock == [], do: [t(c, "merchant.empty_stock")], else: []

      :potions ->
        if Enum.all?(Map.values(run_state(c).player.potions), &(&1 == 0)), do: [t(c, "battle.no_potions")], else: []

      _ ->
        []
    end
  end

  defp merchant_welcome(c) do
    player = run_state(c).player
    [t(c, "merchant.welcome", name: player.name, gold: player.gold)]
  end

  defp character_sheet(c) do
    player = run_state(c).player
    sheet = Character.build_sheet(player, data(c))
    balance = data(c).balance

    header = [
      t(c, "character.level", level: player.level, xp: player.xp, next: Formulas.xp_for_level(player.level + 1)),
      t(c, "character.magic_level",
        magicLevel: player.magic_level,
        spent: player.mana_spent,
        next: Formulas.mana_for_magic_level(player.magic_level, balance)
      ),
      t(c, "character.hp_mp", hp: player.hp, maxHp: sheet.max_hp, mp: player.mp, maxMp: sheet.max_mp),
      t(c, "character.melee",
        min: sheet.melee_min,
        max: sheet.melee_max,
        element: t(c, "element.#{sheet.weapon_element}")
      )
    ]

    stats =
      for {key, value} <- sheet_stats(sheet), value != 0 do
        t(c, "character.stat_line", stat: t(c, key), value: value)
      end

    protections =
      for {element, value} <- sheet.protections, value != 0 do
        t(c, "character.stat_line", stat: t(c, "element.#{element}"), value: "#{value}%")
      end

    slots =
      Enum.map(Enums.slots(), fn slot ->
        case Map.get(player.equipment, slot) do
          nil ->
            t(c, "character.empty_slot", slot: t(c, "slot.#{slot}"))

          item ->
            t(c, "character.slot",
              slot: t(c, "slot.#{slot}"),
              item: GameData.item(data(c), item.item_id).name,
              rarity: t(c, "rarity.#{item.rarity}")
            )
        end
      end)

    header ++
      stats ++
      protections ++
      ["", t(c, "character.equipment")] ++
      slots ++ [t(c, "character.bag", count: length(player.bag), capacity: balance.bag_capacity)]
  end

  defp sheet_stats(%CharacterSheet{} = sheet) do
    [
      {"stat.armor", sheet.armor},
      {"stat.hpRegen", sheet.hp_regen},
      {"stat.mpRegen", sheet.mp_regen},
      {"stat.critChance", sheet.crit_chance},
      {"stat.critDamage", sheet.crit_damage},
      {"stat.spellPower", sheet.spell_power},
      {"stat.physicalDamage", sheet.physical_damage},
      {"stat.dodge", sheet.dodge},
      {"stat.parry", sheet.parry},
      {"stat.lifeLeech", sheet.life_leech},
      {"stat.manaLeech", sheet.mana_leech}
    ]
  end

  defp run_stats_line(c) do
    state = run_state(c)

    t(c, "gameover.stats",
      level: state.player.level,
      damage: state.stats.damage_dealt,
      kills: state.stats.kills |> Map.values() |> Enum.sum(),
      elites: state.stats.elites_killed,
      bosses: state.stats.bosses_killed
    )
  end

  defp game_over_summary(c) do
    state = run_state(c)
    params = [name: state.player.name, vocation: t(c, "vocation.#{state.player.vocation_id}"), round: state.round]

    summary =
      cond do
        state.death_cause not in [nil, ""] ->
          t(c, "gameover.summary", [monster: GameData.creature(data(c), state.death_cause).name] ++ params)

        state.won ->
          t(c, "gameover.won_summary", params)

        true ->
          t(c, "gameover.summary", [monster: "?"] ++ params)
      end

    [summary, run_stats_line(c)]
  end

  defp victory_summary(c) do
    state = run_state(c)
    data = data(c)
    boss = GameData.boss_of_tier(data, Formulas.round_info(state.round, data.balance, GameData.tier_count(data)).tier)

    [
      t(c, "victory.summary",
        name: state.player.name,
        vocation: t(c, "vocation.#{state.player.vocation_id}"),
        monster: boss.name,
        round: state.round
      ),
      run_stats_line(c),
      "",
      t(c, "victory.choice")
    ]
  end

  # ── equipment screens (docs/tui.md "Equipment screen") ─────────────────────

  defp styled_body(%__MODULE__{view: :equipment} = c), do: equipment_body(c)
  defp styled_body(%__MODULE__{view: :compare} = c), do: compare_body(c)
  defp styled_body(c), do: slot_body(c)

  defp equipment_body(c) do
    player = run_state(c).player
    header = %BodyLine{text: t(c, "equipment.equipped_header", score: Character.equipment_score(player, data(c)))}
    slots = Enum.map(Enums.equipment_slot_order(), &slot_line(c, player, &1))
    empty = if usable_bag(c) == [], do: [%BodyLine{text: t(c, "equipment.bag_empty")}], else: []
    [header | slots] ++ [%BodyLine{text: ""}, %BodyLine{text: t(c, "equipment.bag_header")}] ++ empty
  end

  defp slot_line(c, player, slot) do
    slot_name = t(c, "slot.#{slot}")

    case Map.get(player.equipment, slot) do
      nil ->
        %BodyLine{text: t(c, "equipment.slot_empty", slot: slot_name), color: Render.style_warning()}

      item ->
        text =
          t(c, "equipment.slot_line",
            slot: slot_name,
            name: GameData.item(data(c), item.item_id).name,
            rarity: t(c, "rarity.#{item.rarity}"),
            level: Character.required_level(item, data(c)),
            score: Character.item_score(item, data(c))
          )

        %BodyLine{text: text, color: item.rarity}
    end
  end

  defp compare_title(c) do
    case compared_item(c) do
      nil ->
        t(c, "merchant.equipment")

      item ->
        slot = GameData.item(data(c), item.item_id).slot
        current = Map.get(run_state(c).player.equipment, slot)

        current_name =
          if current == nil, do: t(c, "equipment.empty"), else: GameData.item(data(c), current.item_id).name

        t(c, "equipment.compare_title",
          slot: t(c, "slot.#{slot}"),
          current: current_name,
          new: GameData.item(data(c), item.item_id).name
        )
    end
  end

  defp affix_list(_c, nil), do: ""

  defp affix_list(c, %ItemInstance{affixes: affixes}) do
    Enum.map_join(affixes, ", ", &t(c, "equipment.affix", value: &1.value, stat: t(c, "stat.#{&1.stat}")))
  end

  defp compare_body(c) do
    case compared_item(c) do
      nil -> []
      item -> compare_lines(c, item)
    end
  end

  defp compare_lines(c, item) do
    data = data(c)
    player = run_state(c).player
    current = Map.get(player.equipment, GameData.item(data, item.item_id).slot)
    new_stats = Character.item_stats(item, data)
    old_stats = if current == nil, do: %{}, else: Character.item_stats(current, data)

    stats =
      for stat <- Enums.stats(), Map.has_key?(new_stats, stat) or Map.has_key?(old_stats, stat) do
        old = Map.get(old_stats, stat, 0)
        new = Map.get(new_stats, stat, 0)
        delta = Render.format_delta(new - old)
        text = t(c, "equipment.stat_delta", stat: t(c, "stat.#{stat}"), current: old, new: new, delta: delta)
        %BodyLine{text: text, color: Render.delta_style(new - old)}
      end

    gained = affix_list(c, item)
    lost = affix_list(c, current)
    gained_line = %BodyLine{text: t(c, "equipment.affixes_gained", affixes: gained), color: Render.style_gain()}
    lost_line = %BodyLine{text: t(c, "equipment.affixes_lost", affixes: lost), color: Render.style_loss()}
    affixes = if(gained != "", do: [gained_line], else: []) ++ if(lost != "", do: [lost_line], else: [])

    old_score = if current == nil, do: 0, else: Character.item_score(current, data)
    new_score = Character.item_score(item, data)
    delta = Render.format_delta(new_score - old_score)
    score = t(c, "equipment.score_delta", current: old_score, new: new_score, delta: delta)
    level = Character.required_level(item, data)
    needed_text = t(c, "equipment.level_needed", level: level, current: player.level)
    needed = if level > player.level, do: [%BodyLine{text: needed_text, color: Render.style_loss()}], else: []

    stats ++ affixes ++ [%BodyLine{text: score, color: Render.delta_style(new_score - old_score)}] ++ needed
  end

  defp slot_body(c) do
    case Map.get(run_state(c).player.equipment, c.slot) do
      nil ->
        [%BodyLine{text: t(c, "equipment.empty"), color: Render.style_warning()}]

      item ->
        title =
          t(c, "equipment.item_title",
            name: GameData.item(data(c), item.item_id).name,
            rarity: t(c, "rarity.#{item.rarity}"),
            level: Character.required_level(item, data(c)),
            score: Character.item_score(item, data(c))
          )

        stats = Character.item_stats(item, data(c))

        lines =
          for stat <- Enums.stats(), Map.has_key?(stats, stat) do
            %BodyLine{text: t(c, "character.stat_line", stat: t(c, "stat.#{stat}"), value: stats[stat])}
          end

        [%BodyLine{text: title, color: item.rarity} | lines]
    end
  end

  defp hall_of_fame(c) do
    case profile(c).profile.hall_of_fame do
      [] ->
        [t(c, "hall.empty")]

      hall ->
        hall
        |> Enum.with_index(1)
        |> Enum.map(fn {entry, position} ->
          t(c, if(entry.won, do: "hall.entry_won", else: "hall.entry"),
            position: position,
            name: entry.name,
            vocation: t(c, "vocation.#{entry.vocation}"),
            difficulty: t(c, "difficulty.#{entry.difficulty}"),
            round: entry.round,
            level: entry.level,
            date: String.slice(entry.ended_at, 0, 10)
          )
        end)
    end
  end

  defp bestiary(c) do
    service = profile(c)

    (data(c).monsters ++ data(c).bosses)
    |> Enum.sort_by(&{&1.tier, &1.is_boss, &1.name})
    |> Enum.map(fn creature ->
      case Map.get(service.profile.bestiary, creature.id) do
        nil ->
          t(c, "bestiary.unknown", tier: creature.tier + 1)

        entry ->
          if ProfileService.revealed?(service, creature.id) do
            weak = for e <- Enums.elements(), MonsterDef.resistance(creature, e) > 100, do: t(c, "element.#{e}")
            strong = for e <- Enums.elements(), MonsterDef.resistance(creature, e) < 100, do: t(c, "element.#{e}")

            t(c, "bestiary.entry_revealed",
              name: creature.name,
              tier: creature.tier + 1,
              kills: entry.kills,
              weak: join_or_dash(weak),
              strong: join_or_dash(strong)
            )
          else
            t(c, "bestiary.entry", name: creature.name, tier: creature.tier + 1, kills: entry.kills)
          end
      end
    end)
  end

  defp join_or_dash([]), do: "—"
  defp join_or_dash(list), do: Enum.join(list, ", ")

  defp achievements(c) do
    unlocked = profile(c).profile.achievements

    Enum.map(data(c).achievements, fn achievement ->
      name = t(c, "achievement.#{achievement.id}.name")
      description = t(c, "achievement.#{achievement.id}.description", value: achievement.value)

      case Map.get(unlocked, achievement.id) do
        nil ->
          t(c, "achievements.locked", name: name, description: description)

        unlock ->
          t(c, "achievements.unlocked",
            name: name,
            description: description,
            date: String.slice(unlock.unlocked_at, 0, 10)
          )
      end
    end)
  end
end
