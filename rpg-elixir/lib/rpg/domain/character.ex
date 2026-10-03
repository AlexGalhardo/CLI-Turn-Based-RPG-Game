defmodule Rpg.Domain.Character do
  @moduledoc "Derived character stats: vocation base + equipment (docs/game-design.md §4 and §8)."

  alias Rpg.Domain.Definitions.{Balance, GameData}
  alias Rpg.Domain.Entities.{ItemInstance, Player}
  alias Rpg.Domain.{Enums, Formulas}

  defmodule CharacterSheet do
    @moduledoc false
    @enforce_keys [
      :max_hp,
      :max_mp,
      :hp_regen,
      :mp_regen,
      :melee_min,
      :melee_max,
      :weapon_element,
      :armor,
      :crit_chance,
      :crit_damage,
      :spell_power,
      :physical_damage,
      :dodge,
      :parry,
      :life_leech,
      :mana_leech,
      :protections
    ]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            max_hp: integer(),
            max_mp: integer(),
            hp_regen: integer(),
            mp_regen: integer(),
            melee_min: integer(),
            melee_max: integer(),
            weapon_element: String.t(),
            armor: integer(),
            crit_chance: integer(),
            crit_damage: integer(),
            spell_power: integer(),
            physical_damage: integer(),
            dodge: integer(),
            parry: integer(),
            life_leech: integer(),
            mana_leech: integer(),
            protections: [{String.t(), integer()}]
          }

    @doc "Protection % for an element. `protections` is a list in element order (the UI lists it in that order)."
    @spec protection(t(), String.t()) :: integer()
    def protection(%__MODULE__{protections: protections}, element) do
      case List.keyfind(protections, element, 0) do
        {_, value} -> value
        nil -> 0
      end
    end
  end

  @spec item_stats(ItemInstance.t(), GameData.t()) :: %{String.t() => integer()}
  def item_stats(%ItemInstance{} = item, %GameData{} = data) do
    definition = GameData.item(data, item.item_id)
    rarity = Balance.rarity(data.balance, item.rarity)
    base = Map.new(definition.stats, fn {stat, value} -> {stat, Formulas.pct(value, rarity.stat_pct)} end)
    Enum.reduce(item.affixes, base, fn affix, acc -> Map.update(acc, affix.stat, affix.value, &(&1 + affix.value)) end)
  end

  @spec item_value(ItemInstance.t(), GameData.t()) :: integer()
  def item_value(%ItemInstance{} = item, %GameData{} = data) do
    Formulas.pct(GameData.item(data, item.item_id).value, Balance.rarity(data.balance, item.rarity).value_pct)
  end

  @doc "Sum of the item's final stats weighted by `balance.itemScoreWeights` (like Diablo's item power)."
  @spec item_score(ItemInstance.t(), GameData.t()) :: integer()
  def item_score(%ItemInstance{} = item, %GameData{} = data) do
    weights = data.balance.item_score_weights
    item |> item_stats(data) |> Enum.reduce(0, fn {stat, value}, acc -> acc + value * Map.get(weights, stat, 0) end)
  end

  @doc "Uses the instance tier: the round tier the item was generated for (docs/game-design.md §8)."
  @spec required_level(ItemInstance.t(), GameData.t()) :: integer()
  def required_level(%ItemInstance{tier: tier}, %GameData{} = data), do: 1 + tier * data.balance.item_level_per_tier

  @spec equipment_score(Player.t(), GameData.t()) :: integer()
  def equipment_score(%Player{} = player, %GameData{} = data) do
    player.equipment |> Map.values() |> Enum.reduce(0, &(item_score(&1, data) + &2))
  end

  @spec build_sheet(Player.t(), GameData.t()) :: CharacterSheet.t()
  def build_sheet(%Player{} = player, %GameData{} = data) do
    vocation = GameData.vocation(data, player.vocation_id)
    caps = data.balance.caps

    totals =
      Enum.reduce(Map.values(player.equipment), %{}, fn item, acc ->
        Map.merge(acc, item_stats(item, data), fn _stat, a, b -> a + b end)
      end)

    total = &Map.get(totals, &1, 0)

    weapon_element =
      case Map.get(player.equipment, "weapon") do
        nil -> "physical"
        weapon -> GameData.item(data, weapon.item_id).element || "physical"
      end

    level_bonus = (player.level - 1) * vocation.melee_per_level
    attack = total.("attack")

    %CharacterSheet{
      max_hp: vocation.start_hp + (player.level - 1) * vocation.hp_per_level + total.("maxHp"),
      max_mp: vocation.start_mp + (player.level - 1) * vocation.mp_per_level + total.("maxMp"),
      hp_regen: vocation.hp_regen + total.("hpRegen"),
      mp_regen: vocation.mp_regen + total.("mpRegen"),
      melee_min: vocation.melee_min + level_bonus + attack,
      melee_max: vocation.melee_max + level_bonus + attack,
      weapon_element: weapon_element,
      armor: total.("armor"),
      crit_chance: min(total.("critChance"), caps.crit_chance),
      crit_damage: total.("critDamage"),
      spell_power: total.("spellPower"),
      physical_damage: total.("physicalDamage"),
      dodge: min(total.("dodge"), caps.dodge),
      parry: min(total.("parry"), caps.parry),
      life_leech: min(total.("lifeLeech"), caps.leech),
      mana_leech: min(total.("manaLeech"), caps.leech),
      protections:
        Enum.map(Enums.elements(), fn element ->
          {element, min(total.(Enums.protection_stat(element)), caps.protection)}
        end)
    }
  end
end
