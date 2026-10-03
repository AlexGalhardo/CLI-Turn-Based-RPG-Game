defmodule Rpg.Application.AutoBattle do
  @moduledoc """
  Auto-battle policy (docs/game-design.md §13): picks the player's battle commands from the run state only.

  It lives in the application layer, not in the engine: the commands it returns are ordinary commands, so a fight
  played by the policy replays like any other. Every command it returns is valid (affordable spells, owned potions).
  """

  alias Rpg.Application.Commands.{Attack, Cast, Defend, UsePotion}
  alias Rpg.Application.RunState
  alias Rpg.Domain.Character
  alias Rpg.Domain.Definitions.{AutoBattleDef, GameData}
  alias Rpg.Domain.Entities.Player
  alias Rpg.Domain.Formulas

  @offense_attack "attack"
  @modes ["melee", "spells", "balanced"]

  defmodule AutoBattlePolicy do
    @moduledoc false
    @enforce_keys [:data, :mode]
    defstruct @enforce_keys
    @type t :: %__MODULE__{data: GameData.t(), mode: Rpg.Domain.Definitions.AutoBattleModeDef.t()}
  end

  @doc "Modes in menu order: `melee` (weapon focus), `spells` (spell focus), `balanced`."
  @spec modes() :: [String.t()]
  def modes, do: @modes

  @spec policy(GameData.t(), String.t()) :: AutoBattlePolicy.t()
  def policy(%GameData{} = data, mode) when mode in @modes do
    %AutoBattlePolicy{data: data, mode: AutoBattleDef.mode(data.balance.auto_battle, mode)}
  end

  @spec choose(AutoBattlePolicy.t(), RunState.t()) :: struct()
  def choose(%AutoBattlePolicy{data: data, mode: mode} = policy, %RunState{player: player} = state) do
    sheet = Character.build_sheet(player, data)
    config = data.balance.auto_battle
    every = mode.support_every
    support_turn = rem(state.turn, every) == every - 1

    emergency = if player.hp * 100 < sheet.max_hp * config.emergency_heal_below_pct, do: heal(policy, state)

    cond do
      emergency != nil ->
        emergency

      support_turn ->
        support(policy, state, sheet, config) || offense(policy, state)

      true ->
        offense(policy, state)
    end
  end

  defp support(policy, state, sheet, config) do
    player = state.player
    heal = if player.hp * 100 < sheet.max_hp * config.heal_below_pct, do: heal(policy, state)
    mana = if player.mp * 100 < sheet.max_mp * config.mana_below_pct, do: best_potion(policy, state, :mp)

    cond do
      heal != nil -> heal
      mana != nil -> %UsePotion{potion_id: mana.id}
      telegraph_pending?(policy, state) -> %Defend{}
      true -> nil
    end
  end

  defp offense(%AutoBattlePolicy{mode: %{offense: @offense_attack}}, _state), do: %Attack{}

  defp offense(policy, state) do
    case strongest(affordable(policy, state, :attack)) do
      nil -> %Attack{}
      spell -> %Cast{spell_id: spell.id}
    end
  end

  defp heal(policy, state) do
    case strongest(affordable(policy, state, :heal)) do
      nil ->
        case best_potion(policy, state, :hp) do
          nil -> nil
          potion -> %UsePotion{potion_id: potion.id}
        end

      spell ->
        %Cast{spell_id: spell.id}
    end
  end

  # Highest `max`; ties go to the lowest id.
  defp strongest([]), do: nil
  defp strongest(options), do: Enum.min_by(options, &{-&1.max, &1.id})

  defp affordable(%AutoBattlePolicy{data: data}, %RunState{player: player}, kind) do
    levels = data.balance.spell_levels

    GameData.vocation(data, player.vocation_id).spells
    |> Enum.map(&GameData.spell(data, &1))
    |> Enum.filter(fn spell ->
      level = Formulas.spell_level_for_uses(Map.get(player.spell_uses, spell.id, 0), levels)
      spell.kind == kind and Formulas.pct(spell.mana, level.mana_pct) <= player.mp
    end)
  end

  defp best_potion(%AutoBattlePolicy{data: data}, %RunState{player: player}, resource) do
    data.potions
    |> Enum.filter(&(&1.resource == resource and Player.potion_count(player, &1.id) > 0))
    |> strongest()
  end

  # The boss announced its charged attack: its next action is the charge.
  defp telegraph_pending?(%AutoBattlePolicy{data: data}, %RunState{monster: monster}) do
    case monster do
      %{is_boss: true, boss_actions: actions} ->
        every = data.balance.boss_telegraph_every
        rem(actions, every + 1) == every

      _ ->
        false
    end
  end
end
