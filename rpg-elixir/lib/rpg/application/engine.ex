defmodule Rpg.Application.GameEngine do
  @moduledoc """
  The game engine: a pure state machine `step(engine, command) -> {engine, events}` (docs/architecture.md).

  The Python, TypeScript and Go engines mutate their state in place; here the engine is an immutable value and every
  step returns a new one, which is why saves and the merchant snapshot need no deep copies.
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

  alias Rpg.Application.{
    AutoEquip,
    Battle,
    Commands,
    Events,
    Loot,
    Merchant,
    Progression,
    RunConfig,
    RunState,
    RunStatistics,
    Spawner
  }

  alias Rpg.Domain.Definitions.{Balance, GameData, UnknownIdError}
  alias Rpg.Domain.Entities.{ItemInstance, Player}
  alias Rpg.Domain.{Character, Formulas, Rng}

  @enforce_keys [:data, :state, :rng]
  defstruct @enforce_keys

  @type t :: %__MODULE__{data: GameData.t(), state: RunState.t(), rng: Rng.t()}

  @spec rng_state(t()) :: non_neg_integer()
  def rng_state(%__MODULE__{rng: rng}), do: Rng.state(rng)

  @doc "Creates a run. Raises `ArgumentError` (\"invalid run config: ...\") for an unknown vocation or difficulty."
  @spec new_run(GameData.t(), RunConfig.t(), integer()) :: {t(), [Events.t()]}
  def new_run(%GameData{} = data, %RunConfig{} = config, seed) do
    vocation =
      try do
        vocation = GameData.vocation(data, config.vocation_id)
        _ = Balance.difficulty(data.balance, config.difficulty_id)
        vocation
      rescue
        error in UnknownIdError -> reraise ArgumentError, "invalid run config: '#{error.id}'", __STACKTRACE__
      end

    player = %Player{
      name: config.name,
      vocation_id: vocation.id,
      hp: vocation.start_hp,
      mp: vocation.start_mp,
      gold: data.balance.starting_gold,
      potions: Map.new(data.balance.starting_potions)
    }

    state = %RunState{seed: seed, config: config, player: player}
    starter = GameData.item(data, vocation.starter_weapon)
    {uid, state} = RunState.take_item_uid(state)
    weapon = %ItemInstance{uid: uid, item_id: starter.id, rarity: "common", tier: starter.tier}
    state = put_in(state.player.equipment, %{starter.slot => weapon})

    {merchant_events, state, rng} = Merchant.enter(data, Rng.new(seed), state)
    started = Events.event("run_started", seed: seed, vocation: vocation.id, difficulty: config.difficulty_id)
    {%__MODULE__{data: data, state: state, rng: rng}, [started | merchant_events]}
  end

  @spec restore(GameData.t(), RunState.t(), non_neg_integer()) :: t()
  def restore(%GameData{} = data, %RunState{} = state, rng_state) do
    %__MODULE__{data: data, state: state, rng: Rng.new(rng_state)}
  end

  @spec step(t(), Commands.t()) :: {t(), [Events.t()]}
  def step(%__MODULE__{} = engine, command) do
    {engine, events} = dispatch(engine, command)
    stats = RunStatistics.record(engine.state.stats, events, engine.state.round)
    {put_in(engine.state.stats, stats), events}
  end

  defp dispatch(engine, %module{} = command) when module in [Attack, Cast, UsePotion, Defend] do
    if engine.state.phase == :battle, do: battle_turn(engine, command), else: invalid_phase(engine)
  end

  defp dispatch(engine, %NextFight{}) do
    if engine.state.phase == :merchant, do: next_fight(engine), else: invalid_phase(engine)
  end

  defp dispatch(engine, %module{} = command) when module in [BuyPotion, SellItem, Equip, Unequip, BuyStockItem] do
    if engine.state.phase == :merchant do
      {events, state} = Merchant.handle(engine.data, engine.state, command)
      {%{engine | state: state}, events}
    else
      invalid_phase(engine)
    end
  end

  defp dispatch(engine, %module{}) when module in [EndRun, ContinueRun] do
    cond do
      engine.state.phase != :victory -> invalid_phase(engine)
      module == EndRun -> end_run(engine)
      true -> continue_run(engine)
    end
  end

  defp invalid_phase(engine), do: {engine, [Events.error("invalid_phase")]}

  defp next_fight(%__MODULE__{data: data, state: state} = engine) do
    state = %{state | round: state.round + 1}
    difficulty = Balance.difficulty(data.balance, state.config.difficulty_id)
    {monster, info, rng} = Spawner.spawn_monster(data, engine.rng, state.round, difficulty)
    state = %{state | monster: monster, phase: :battle, turn: 1, merchant_stock: []}

    event =
      Events.event("round_started",
        round: state.round,
        tier: info.tier,
        cycle: info.cycle,
        monsterId: monster.creature_id,
        isBoss: monster.is_boss,
        enemyClass: monster.enemy_class,
        hp: monster.hp
      )

    {%{engine | state: state, rng: rng}, [event]}
  end

  defp battle_turn(engine, command) do
    case Battle.validate(engine.data, engine.state, command) do
      nil ->
        {events, outcome, state, rng} = Battle.play_turn(engine.data, engine.rng, engine.state, command)
        engine = %{engine | state: state, rng: rng}

        case outcome do
          :victory ->
            {engine, more} = victory(engine)
            {engine, events ++ more}

          :defeat ->
            {engine, more} = defeat(engine)
            {engine, events ++ more}

          :ongoing ->
            {engine, events}
        end

      invalid ->
        {engine, [invalid]}
    end
  end

  defp victory(%__MODULE__{data: data, state: %RunState{monster: monster} = state} = engine) do
    if monster == nil, do: raise(RuntimeError, "victory without a monster")

    killed =
      Events.event("monster_killed",
        monsterId: monster.creature_id,
        isBoss: monster.is_boss,
        enemyClass: monster.enemy_class
      )

    {player, xp_events} = Progression.gain_experience(data, state.player, monster.xp)

    {gold, rng} = Rng.roll(engine.rng, monster.gold_min, monster.gold_max)
    state = %{state | player: %{player | gold: player.gold + gold}}
    looted = Events.event("gold_looted", amount: gold)
    {drop_events, state, rng} = drops(data, rng, state, monster)

    {equip_events, state} =
      if state.config.auto_equip, do: AutoEquip.auto_equip(state, data), else: {[], state}

    player = %{state.player | statuses: [], stun_cooldown: 0, defending: false}
    sheet = Character.build_sheet(player, data)
    player = %{player | hp: min(player.hp, sheet.max_hp), mp: min(player.mp, sheet.max_mp)}
    state = %{state | player: player, monster: nil, turn: 0}
    events = [killed] ++ xp_events ++ [looted] ++ drop_events ++ equip_events

    if state.round == data.balance.final_round do
      state = %{state | won: true, phase: :victory}
      {%{engine | state: state, rng: rng}, events ++ [Events.event("run_won", round: state.round)]}
    else
      {merchant_events, state, rng} = Merchant.enter(data, rng, %{state | phase: :merchant})
      {%{engine | state: state, rng: rng}, events ++ merchant_events}
    end
  end

  # One rule for the three classes: chance(100) and chance(0) consume nothing (docs/game-design.md §8).
  defp drops(data, rng, state, monster) do
    row = Balance.enemy_class(data.balance, monster.enemy_class)

    {item_events, state, rng} =
      case Rng.chance(rng, row.drop_chance_pct) do
        {true, rng} -> generate_drops(data, rng, state, row)
        {false, rng} -> {[], state, rng}
      end

    case Rng.chance(rng, row.potion_drop_pct) do
      {true, rng} ->
        {potion_events, state, rng} = drop_potion(data, rng, state)
        {item_events ++ potion_events, state, rng}

      {false, rng} ->
        {item_events, state, rng}
    end
  end

  defp generate_drops(data, rng, state, row) do
    tier = Formulas.round_info(state.round, data.balance, GameData.tier_count(data)).tier
    vocation = GameData.vocation(data, state.player.vocation_id)

    {events, state, rng} =
      Enum.reduce(1..row.drops//1, {[], state, rng}, fn _, {events, state, rng} ->
        opts = [vocation: vocation, tier: tier, weights: row.rarity_weights, uid: state.next_item_uid]

        case Loot.generate_item(data, rng, opts) do
          {nil, rng} ->
            {events, state, rng}

          {item, rng} ->
            {_uid, state} = RunState.take_item_uid(state)
            dropped = Events.event("item_dropped", uid: item.uid, itemId: item.item_id, rarity: item.rarity)
            {more, state} = store_drop(data, state, item)
            {Enum.reverse(more, [dropped | events]), state, rng}
        end
      end)

    {Enum.reverse(events), state, rng}
  end

  defp drop_potion(data, rng, state) do
    case Enum.filter(data.potions, &(&1.unlock_round <= state.round)) do
      [] ->
        {[], state, rng}

      unlocked ->
        {potion, rng} = Rng.pick(rng, unlocked)
        player = state.player
        potions = Map.put(player.potions, potion.id, Player.potion_count(player, potion.id) + 1)
        state = %{state | player: %{player | potions: potions}}
        {[Events.event("potion_dropped", potionId: potion.id)], state, rng}
    end
  end

  defp end_run(%__MODULE__{state: state} = engine) do
    state = %{state | phase: :game_over, death_cause: nil}
    {%{engine | state: state}, [Events.event("run_ended", won: state.won)]}
  end

  defp continue_run(%__MODULE__{data: data, state: state} = engine) do
    {events, state, rng} = Merchant.enter(data, engine.rng, %{state | phase: :merchant})
    {%{engine | state: state, rng: rng}, events}
  end

  # A full bag auto-sells the new item.
  defp store_drop(data, state, item) do
    player = state.player

    if length(player.bag) >= data.balance.bag_capacity do
      value = Character.item_value(item, data)
      sold = Events.event("item_auto_sold", uid: item.uid, itemId: item.item_id, gold: value)
      {[sold], %{state | player: %{player | gold: player.gold + value}}}
    else
      {[], %{state | player: %{player | bag: player.bag ++ [item]}}}
    end
  end

  defp defeat(%__MODULE__{state: state} = engine) do
    monster_id = if state.monster == nil, do: "", else: state.monster.creature_id
    state = %{state | phase: :game_over, death_cause: monster_id}
    {%{engine | state: state}, [Events.event("player_died", monsterId: monster_id, round: state.round)]}
  end
end
