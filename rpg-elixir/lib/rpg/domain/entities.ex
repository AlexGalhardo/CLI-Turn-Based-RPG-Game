defmodule Rpg.Domain.Entities do
  @moduledoc """
  Run entities. Serialised with camelCase keys: the save format is shared with Python, TypeScript and Go.

  `to_map/1` returns plain maps with string keys (ready for JSON) and `from_map/1` reads them back.
  """

  alias Rpg.Domain.Definitions.{MonsterAttack, StatusOnHit}
  alias Rpg.Domain.{Enums, JsonTypes}

  defmodule ActiveStatus do
    @moduledoc false
    @enforce_keys [:status_id, :turns, :per_turn]
    defstruct @enforce_keys
    @type t :: %__MODULE__{status_id: String.t(), turns: integer(), per_turn: integer()}

    @spec to_map(t()) :: map()
    def to_map(%__MODULE__{} = s), do: %{"statusId" => s.status_id, "turns" => s.turns, "perTurn" => s.per_turn}

    @spec from_map(term()) :: t()
    def from_map(raw) do
      %__MODULE__{
        status_id: JsonTypes.str(JsonTypes.field(raw, "statusId")),
        turns: JsonTypes.int(JsonTypes.field(raw, "turns")),
        per_turn: JsonTypes.int(JsonTypes.field(raw, "perTurn"))
      }
    end
  end

  defmodule AffixRoll do
    @moduledoc false
    @enforce_keys [:stat, :value]
    defstruct @enforce_keys
    @type t :: %__MODULE__{stat: String.t(), value: integer()}

    @spec to_map(t()) :: map()
    def to_map(%__MODULE__{stat: stat, value: value}), do: %{"stat" => stat, "value" => value}

    @spec from_map(term()) :: t()
    def from_map(raw) do
      %__MODULE__{
        stat: Enums.stat!(JsonTypes.str(JsonTypes.field(raw, "stat"))),
        value: JsonTypes.int(JsonTypes.field(raw, "value"))
      }
    end
  end

  defmodule ItemInstance do
    @moduledoc false
    @enforce_keys [:uid, :item_id, :rarity, :tier]
    defstruct [:uid, :item_id, :rarity, :tier, affixes: []]

    @type t :: %__MODULE__{
            uid: integer(),
            item_id: String.t(),
            rarity: String.t(),
            tier: integer(),
            affixes: [AffixRoll.t()]
          }

    @spec to_map(t()) :: map()
    def to_map(%__MODULE__{} = item) do
      %{
        "uid" => item.uid,
        "itemId" => item.item_id,
        "rarity" => item.rarity,
        "tier" => item.tier,
        "affixes" => Enum.map(item.affixes, &AffixRoll.to_map/1)
      }
    end

    @spec from_map(term()) :: t()
    def from_map(raw) do
      %__MODULE__{
        uid: JsonTypes.int(JsonTypes.field(raw, "uid")),
        item_id: JsonTypes.str(JsonTypes.field(raw, "itemId")),
        rarity: JsonTypes.str(JsonTypes.field(raw, "rarity")),
        tier: JsonTypes.int(JsonTypes.field(raw, "tier")),
        affixes: Enum.map(JsonTypes.list(JsonTypes.field(raw, "affixes")), &AffixRoll.from_map/1)
      }
    end
  end

  defmodule Player do
    @moduledoc false
    @enforce_keys [:name, :vocation_id, :hp, :mp, :gold]
    defstruct [
      :name,
      :vocation_id,
      :hp,
      :mp,
      :gold,
      level: 1,
      xp: 0,
      magic_level: 1,
      mana_spent: 0,
      potions: %{},
      equipment: %{},
      bag: [],
      spell_uses: %{},
      statuses: [],
      stun_cooldown: 0,
      defending: false
    ]

    @type t :: %__MODULE__{
            name: String.t(),
            vocation_id: String.t(),
            hp: integer(),
            mp: integer(),
            gold: integer(),
            level: integer(),
            xp: integer(),
            magic_level: integer(),
            mana_spent: integer(),
            potions: %{String.t() => integer()},
            equipment: %{String.t() => ItemInstance.t()},
            bag: [ItemInstance.t()],
            spell_uses: %{String.t() => integer()},
            statuses: [ActiveStatus.t()],
            stun_cooldown: integer(),
            defending: boolean()
          }

    @spec potion_count(t(), String.t()) :: integer()
    def potion_count(%__MODULE__{potions: potions}, potion_id), do: Map.get(potions, potion_id, 0)

    @spec to_map(t()) :: map()
    def to_map(%__MODULE__{} = p) do
      %{
        "name" => p.name,
        "vocationId" => p.vocation_id,
        "hp" => p.hp,
        "mp" => p.mp,
        "gold" => p.gold,
        "level" => p.level,
        "xp" => p.xp,
        "magicLevel" => p.magic_level,
        "manaSpent" => p.mana_spent,
        "potions" => p.potions,
        "equipment" => Map.new(p.equipment, fn {slot, item} -> {slot, ItemInstance.to_map(item)} end),
        "bag" => Enum.map(p.bag, &ItemInstance.to_map/1),
        "spellUses" => p.spell_uses,
        "statuses" => Enum.map(p.statuses, &ActiveStatus.to_map/1),
        "stunCooldown" => p.stun_cooldown,
        "defending" => p.defending
      }
    end

    @spec from_map(term()) :: t()
    def from_map(raw) do
      f = &JsonTypes.field(raw, &1)

      %__MODULE__{
        name: JsonTypes.str(f.("name")),
        vocation_id: JsonTypes.str(f.("vocationId")),
        hp: JsonTypes.int(f.("hp")),
        mp: JsonTypes.int(f.("mp")),
        gold: JsonTypes.int(f.("gold")),
        level: JsonTypes.int(f.("level")),
        xp: JsonTypes.int(f.("xp")),
        magic_level: JsonTypes.int(f.("magicLevel")),
        mana_spent: JsonTypes.int(f.("manaSpent")),
        potions: Map.new(JsonTypes.obj(f.("potions")), fn {k, v} -> {k, JsonTypes.int(v)} end),
        equipment: Map.new(JsonTypes.obj(f.("equipment")), fn {k, v} -> {Enums.slot!(k), ItemInstance.from_map(v)} end),
        bag: Enum.map(JsonTypes.list(f.("bag")), &ItemInstance.from_map/1),
        spell_uses: Map.new(JsonTypes.obj(f.("spellUses")), fn {k, v} -> {k, JsonTypes.int(v)} end),
        statuses: Enum.map(JsonTypes.list(f.("statuses")), &ActiveStatus.from_map/1),
        stun_cooldown: JsonTypes.int(f.("stunCooldown")),
        defending: JsonTypes.bool(f.("defending"))
      }
    end
  end

  defmodule MonsterInstance do
    @moduledoc "A spawned monster: definition id plus stats already scaled for the round and difficulty."
    @enforce_keys [:creature_id, :is_boss, :hp, :max_hp, :xp, :gold_min, :gold_max, :attacks]
    defstruct [
      :creature_id,
      :is_boss,
      :hp,
      :max_hp,
      :xp,
      :gold_min,
      :gold_max,
      :attacks,
      statuses: [],
      stun_cooldown: 0,
      boss_actions: 0
    ]

    @type t :: %__MODULE__{
            creature_id: String.t(),
            is_boss: boolean(),
            hp: integer(),
            max_hp: integer(),
            xp: integer(),
            gold_min: integer(),
            gold_max: integer(),
            attacks: [MonsterAttack.t()],
            statuses: [ActiveStatus.t()],
            stun_cooldown: integer(),
            boss_actions: integer()
          }

    @spec attack(t(), String.t()) :: MonsterAttack.t()
    def attack(%__MODULE__{attacks: attacks}, attack_id) do
      Enum.find(attacks, &(&1.id == attack_id)) || raise KeyError, key: attack_id
    end

    @spec to_map(t()) :: map()
    def to_map(%__MODULE__{} = m) do
      %{
        "creatureId" => m.creature_id,
        "isBoss" => m.is_boss,
        "hp" => m.hp,
        "maxHp" => m.max_hp,
        "xp" => m.xp,
        "goldMin" => m.gold_min,
        "goldMax" => m.gold_max,
        "attacks" => Enum.map(m.attacks, &Rpg.Domain.Entities.attack_to_map/1),
        "statuses" => Enum.map(m.statuses, &ActiveStatus.to_map/1),
        "stunCooldown" => m.stun_cooldown,
        "bossActions" => m.boss_actions
      }
    end

    @spec from_map(term()) :: t()
    def from_map(raw) do
      f = &JsonTypes.field(raw, &1)

      %__MODULE__{
        creature_id: JsonTypes.str(f.("creatureId")),
        is_boss: JsonTypes.bool(f.("isBoss")),
        hp: JsonTypes.int(f.("hp")),
        max_hp: JsonTypes.int(f.("maxHp")),
        xp: JsonTypes.int(f.("xp")),
        gold_min: JsonTypes.int(f.("goldMin")),
        gold_max: JsonTypes.int(f.("goldMax")),
        attacks: Enum.map(JsonTypes.list(f.("attacks")), &Rpg.Domain.Entities.attack_from_map/1),
        statuses: Enum.map(JsonTypes.list(f.("statuses")), &ActiveStatus.from_map/1),
        stun_cooldown: JsonTypes.int(f.("stunCooldown")),
        boss_actions: JsonTypes.int(f.("bossActions"))
      }
    end
  end

  @doc "A monster attack as JSON; the `status` key only exists when the attack applies one."
  @spec attack_to_map(MonsterAttack.t()) :: map()
  def attack_to_map(%MonsterAttack{} = attack) do
    base = %{
      "id" => attack.id,
      "element" => attack.element,
      "min" => attack.min,
      "max" => attack.max,
      "weight" => attack.weight
    }

    case attack.status do
      nil ->
        base

      %StatusOnHit{} = status ->
        Map.put(base, "status", %{"id" => status.status, "chance" => status.chance, "damagePct" => status.damage_pct})
    end
  end

  @spec attack_from_map(term()) :: MonsterAttack.t()
  def attack_from_map(raw) do
    status =
      case Map.get(JsonTypes.obj(raw), "status") do
        nil ->
          nil

        status_raw ->
          %StatusOnHit{
            status: JsonTypes.str(JsonTypes.field(status_raw, "id")),
            chance: JsonTypes.int(JsonTypes.field(status_raw, "chance")),
            damage_pct: JsonTypes.int(JsonTypes.field(status_raw, "damagePct"))
          }
      end

    %MonsterAttack{
      id: JsonTypes.str(JsonTypes.field(raw, "id")),
      element: Enums.element!(JsonTypes.str(JsonTypes.field(raw, "element"))),
      min: JsonTypes.int(JsonTypes.field(raw, "min")),
      max: JsonTypes.int(JsonTypes.field(raw, "max")),
      weight: JsonTypes.int(JsonTypes.field(raw, "weight")),
      status: status
    }
  end
end
