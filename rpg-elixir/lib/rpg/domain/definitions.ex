defmodule Rpg.Domain.Definitions do
  @moduledoc """
  Immutable game definitions loaded from shared/data (see docs/data-format.md).

  Each definition is a struct. Lists keep the JSON file order; maps are only used for lookups by id, never iterated to
  make a game decision (Elixir maps with more than 32 keys have no defined order).
  """

  defmodule UnknownIdError do
    @moduledoc "Raised when a definition id is not present in the loaded data."
    defexception [:id]

    @impl true
    def message(%{id: id}), do: "unknown id: #{inspect(id)}"
  end

  defmodule StatusOnHit do
    @moduledoc false
    @enforce_keys [:status, :chance, :damage_pct]
    defstruct [:status, :chance, :damage_pct]
    @type t :: %__MODULE__{status: String.t(), chance: integer(), damage_pct: integer()}
  end

  defmodule MonsterAttack do
    @moduledoc false
    @enforce_keys [:id, :element, :min, :max, :weight]
    defstruct [:id, :element, :min, :max, :weight, status: nil]

    @type t :: %__MODULE__{
            id: String.t(),
            element: String.t(),
            min: integer(),
            max: integer(),
            weight: integer(),
            status: StatusOnHit.t() | nil
          }
  end

  defmodule MonsterDef do
    @moduledoc "A monster or a boss (bosses have `is_boss` and a `charge_attack`)."
    @enforce_keys [:id, :name, :tier, :family, :hp, :xp, :gold_min, :gold_max, :attacks, :resistances]
    defstruct [
      :id,
      :name,
      :tier,
      :family,
      :hp,
      :xp,
      :gold_min,
      :gold_max,
      :attacks,
      :resistances,
      is_boss: false,
      charge_attack: nil
    ]

    @type t :: %__MODULE__{
            id: String.t(),
            name: String.t(),
            tier: integer(),
            family: String.t(),
            hp: integer(),
            xp: integer(),
            gold_min: integer(),
            gold_max: integer(),
            attacks: [MonsterAttack.t()],
            resistances: %{String.t() => integer()},
            is_boss: boolean(),
            charge_attack: String.t() | nil
          }

    @doc "Damage taken % for an element (default 100)."
    @spec resistance(t(), String.t()) :: integer()
    def resistance(%__MODULE__{resistances: resistances}, element), do: Map.get(resistances, element, 100)

    @spec attack(t(), String.t()) :: MonsterAttack.t()
    def attack(%__MODULE__{attacks: attacks}, attack_id) do
      Enum.find(attacks, &(&1.id == attack_id)) || raise UnknownIdError, id: attack_id
    end
  end

  defmodule Level3Bonus do
    @moduledoc false
    defstruct status: nil, chance: 0, cleanse: false
    @type t :: %__MODULE__{status: String.t() | nil, chance: integer(), cleanse: boolean()}
  end

  defmodule SpellDef do
    @moduledoc false
    @enforce_keys [:id, :name, :words, :kind, :element, :mana, :min, :max, :per_level, :per_magic_level, :level3_bonus]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            id: String.t(),
            name: String.t(),
            words: String.t(),
            kind: :attack | :heal,
            element: String.t(),
            mana: integer(),
            min: integer(),
            max: integer(),
            per_level: integer(),
            per_magic_level: integer(),
            level3_bonus: Level3Bonus.t()
          }
  end

  defmodule VocationDef do
    @moduledoc false
    @enforce_keys [
      :id,
      :name,
      :start_hp,
      :start_mp,
      :hp_per_level,
      :mp_per_level,
      :hp_regen,
      :mp_regen,
      :melee_min,
      :melee_max,
      :melee_per_level,
      :weapon_types,
      :shield_types,
      :starter_weapon,
      :spells
    ]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            id: String.t(),
            name: String.t(),
            start_hp: integer(),
            start_mp: integer(),
            hp_per_level: integer(),
            mp_per_level: integer(),
            hp_regen: integer(),
            mp_regen: integer(),
            melee_min: integer(),
            melee_max: integer(),
            melee_per_level: integer(),
            weapon_types: [String.t()],
            shield_types: [String.t()],
            starter_weapon: String.t(),
            spells: [String.t()]
          }
  end

  defmodule PotionDef do
    @moduledoc false
    @enforce_keys [:id, :name, :resource, :min, :max, :price, :unlock_round]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            id: String.t(),
            name: String.t(),
            resource: :hp | :mp,
            min: integer(),
            max: integer(),
            price: integer(),
            unlock_round: integer()
          }
  end

  defmodule StatusDef do
    @moduledoc false
    @enforce_keys [:id, :kind, :element, :turns]
    defstruct @enforce_keys
    @type t :: %__MODULE__{id: String.t(), kind: :dot | :stun, element: String.t(), turns: integer()}
  end

  defmodule ItemDef do
    @moduledoc false
    @enforce_keys [:id, :name, :slot, :type, :tier, :element, :stats, :value]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            id: String.t(),
            name: String.t(),
            slot: String.t(),
            type: String.t(),
            tier: integer(),
            element: String.t() | nil,
            stats: %{String.t() => integer()},
            value: integer()
          }
  end

  defmodule AffixDef do
    @moduledoc false
    @enforce_keys [:id, :stat, :min, :max, :per_tier, :slots]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            id: String.t(),
            stat: String.t(),
            min: integer(),
            max: integer(),
            per_tier: integer(),
            slots: [String.t()]
          }
  end

  defmodule AchievementDef do
    @moduledoc false
    @enforce_keys [:id, :type, :value]
    defstruct @enforce_keys
    @type t :: %__MODULE__{id: String.t(), type: String.t(), value: integer()}
  end

  defmodule DifficultyDef do
    @moduledoc false
    @enforce_keys [:id, :hp_pct, :damage_pct, :gold_pct, :xp_pct]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            id: String.t(),
            hp_pct: integer(),
            damage_pct: integer(),
            gold_pct: integer(),
            xp_pct: integer()
          }
  end

  defmodule RarityDef do
    @moduledoc false
    @enforce_keys [:id, :stat_pct, :value_pct, :affix_min, :affix_max]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            id: String.t(),
            stat_pct: integer(),
            value_pct: integer(),
            affix_min: integer(),
            affix_max: integer()
          }
  end

  defmodule EnemyClassDef do
    @moduledoc "A row of `balance.enemyClasses`: multipliers, combat chances and drop table (docs/game-design.md §3)."
    @enforce_keys [
      :id,
      :stat_pct,
      :reward_pct,
      :dodge,
      :parry,
      :crit,
      :heal,
      :drop_chance_pct,
      :drops,
      :potion_drop_pct,
      :rarity_weights
    ]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            id: String.t(),
            stat_pct: integer(),
            reward_pct: integer(),
            dodge: integer(),
            parry: integer(),
            crit: integer(),
            heal: integer(),
            drop_chance_pct: integer(),
            drops: integer(),
            potion_drop_pct: integer(),
            rarity_weights: %{String.t() => integer()}
          }
  end

  defmodule AutoBattleModeDef do
    @moduledoc false
    @enforce_keys [:id, :offense, :support_every]
    defstruct @enforce_keys
    @type t :: %__MODULE__{id: String.t(), offense: String.t(), support_every: pos_integer()}
  end

  defmodule AutoBattleDef do
    @moduledoc false
    @enforce_keys [:heal_below_pct, :mana_below_pct, :emergency_heal_below_pct, :modes]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            heal_below_pct: integer(),
            mana_below_pct: integer(),
            emergency_heal_below_pct: integer(),
            modes: [AutoBattleModeDef.t()]
          }

    @spec mode(t(), String.t()) :: AutoBattleModeDef.t()
    def mode(%__MODULE__{modes: modes}, id) do
      Enum.find(modes, &(&1.id == id)) || raise UnknownIdError, id: id
    end
  end

  defmodule SpellLevelDef do
    @moduledoc false
    @enforce_keys [:level, :uses, :effect_pct, :mana_pct]
    defstruct @enforce_keys
    @type t :: %__MODULE__{level: integer(), uses: integer(), effect_pct: integer(), mana_pct: integer()}
  end

  defmodule Caps do
    @moduledoc false
    @enforce_keys [:crit_chance, :dodge, :parry, :leech, :protection]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            crit_chance: integer(),
            dodge: integer(),
            parry: integer(),
            leech: integer(),
            protection: integer()
          }
  end

  defmodule Balance do
    @moduledoc "Global knobs from balance.json."
    @enforce_keys [
      :rounds_per_tier,
      :cycle_stat_pct,
      :cycle_reward_pct,
      :position_pct,
      :final_round,
      :elite_chance_pct,
      :difficulties,
      :enemy_classes,
      :crit_multiplier_pct,
      :defend_damage_pct,
      :parry_reflect_pct,
      :monster_heal_pct,
      :boss_telegraph_every,
      :boss_charge_damage_pct,
      :caps,
      :magic_level_base,
      :magic_level_growth_pct,
      :spell_levels,
      :starting_gold,
      :starting_potions,
      :bag_capacity,
      :item_level_per_tier,
      :item_score_weights,
      :rarities,
      :rarity_weights,
      :merchant_stock_size,
      :merchant_markup_pct,
      :spell_status_damage_pct,
      :auto_battle
    ]
    defstruct @enforce_keys

    @type t :: %__MODULE__{
            rounds_per_tier: integer(),
            cycle_stat_pct: integer(),
            cycle_reward_pct: integer(),
            position_pct: integer(),
            final_round: integer(),
            elite_chance_pct: integer(),
            difficulties: [DifficultyDef.t()],
            enemy_classes: [EnemyClassDef.t()],
            crit_multiplier_pct: integer(),
            defend_damage_pct: integer(),
            parry_reflect_pct: integer(),
            monster_heal_pct: integer(),
            boss_telegraph_every: integer(),
            boss_charge_damage_pct: integer(),
            caps: Caps.t(),
            magic_level_base: integer(),
            magic_level_growth_pct: integer(),
            spell_levels: [SpellLevelDef.t()],
            starting_gold: integer(),
            starting_potions: [{String.t(), integer()}],
            bag_capacity: integer(),
            item_level_per_tier: integer(),
            item_score_weights: %{String.t() => integer()},
            rarities: [RarityDef.t()],
            rarity_weights: %{String.t() => %{String.t() => integer()}},
            merchant_stock_size: integer(),
            merchant_markup_pct: integer(),
            spell_status_damage_pct: integer(),
            auto_battle: AutoBattleDef.t()
          }

    @spec difficulty(t(), String.t()) :: DifficultyDef.t()
    def difficulty(%__MODULE__{difficulties: difficulties}, id) do
      Enum.find(difficulties, &(&1.id == id)) || raise UnknownIdError, id: id
    end

    @spec enemy_class(t(), String.t()) :: EnemyClassDef.t()
    def enemy_class(%__MODULE__{enemy_classes: classes}, id) do
      Enum.find(classes, &(&1.id == id)) || raise UnknownIdError, id: id
    end

    @spec rarity(t(), String.t()) :: RarityDef.t()
    def rarity(%__MODULE__{rarities: rarities}, id) do
      Enum.find(rarities, &(&1.id == id)) || raise UnknownIdError, id: id
    end
  end

  defmodule GameData do
    @moduledoc """
    Every definition of the game. Build it with `new/1`, which also precomputes the lookup indexes (the Python port
    uses `cached_property`; here the indexes are plain fields of the immutable struct).
    """

    @enforce_keys [:balance, :vocations, :spells, :monsters, :bosses, :potions, :statuses, :items]
    defstruct [
      :balance,
      :vocations,
      :spells,
      :monsters,
      :bosses,
      :potions,
      :statuses,
      :items,
      affixes: [],
      achievements: [],
      families: [],
      index: %{}
    ]

    @type t :: %__MODULE__{
            balance: Balance.t(),
            vocations: [VocationDef.t()],
            spells: [SpellDef.t()],
            monsters: [MonsterDef.t()],
            bosses: [MonsterDef.t()],
            potions: [PotionDef.t()],
            statuses: [StatusDef.t()],
            items: [ItemDef.t()],
            affixes: [AffixDef.t()],
            achievements: [AchievementDef.t()],
            families: [String.t()],
            index: map()
          }

    @doc "Builds the data (or rebuilds it after changing a list) with fresh indexes."
    @spec new(keyword() | map()) :: t()
    def new(fields) do
      data = struct!(__MODULE__, Map.delete(Map.new(fields), :index))

      monsters_by_tier =
        data.monsters
        |> Enum.group_by(& &1.tier)
        |> Map.new(fn {tier, group} -> {tier, Enum.sort_by(group, & &1.id)} end)

      index = %{
        vocations: by_id(data.vocations),
        spells: by_id(data.spells),
        creatures: by_id(data.monsters ++ data.bosses),
        potions: by_id(data.potions),
        statuses: by_id(data.statuses),
        items: by_id(data.items),
        monsters_by_tier: monsters_by_tier,
        bosses_by_tier: Map.new(data.bosses, &{&1.tier, &1})
      }

      %{data | index: index}
    end

    @doc "Returns a copy with some lists replaced (the indexes are rebuilt)."
    @spec replace(t(), keyword()) :: t()
    def replace(%__MODULE__{} = data, changes), do: new(Map.merge(Map.from_struct(data), Map.new(changes)))

    defp by_id(items), do: Map.new(items, &{&1.id, &1})

    @spec tier_count(t()) :: non_neg_integer()
    def tier_count(%__MODULE__{bosses: bosses}), do: length(bosses)

    @spec vocation(t(), String.t()) :: VocationDef.t()
    def vocation(data, id), do: lookup(data, :vocations, id)

    @spec spell(t(), String.t()) :: SpellDef.t()
    def spell(data, id), do: lookup(data, :spells, id)

    @spec creature(t(), String.t()) :: MonsterDef.t()
    def creature(data, id), do: lookup(data, :creatures, id)

    @spec potion(t(), String.t()) :: PotionDef.t()
    def potion(data, id), do: lookup(data, :potions, id)

    @spec status(t(), String.t()) :: StatusDef.t()
    def status(data, id), do: lookup(data, :statuses, id)

    @spec item(t(), String.t()) :: ItemDef.t()
    def item(data, id), do: lookup(data, :items, id)

    @doc "Monsters of a tier, sorted by id."
    @spec monsters_in_tier(t(), integer()) :: [MonsterDef.t()]
    def monsters_in_tier(%__MODULE__{index: index}, tier), do: Map.get(index.monsters_by_tier, tier, [])

    @spec boss_of_tier(t(), integer()) :: MonsterDef.t()
    def boss_of_tier(%__MODULE__{index: index}, tier), do: Map.fetch!(index.bosses_by_tier, tier)

    defp lookup(%__MODULE__{index: index}, kind, id) do
      case Map.fetch(Map.fetch!(index, kind), id) do
        {:ok, value} -> value
        :error -> raise UnknownIdError, id: id
      end
    end
  end
end
