defmodule Rpg.Infrastructure.DataLoader do
  @moduledoc "Loads shared/data/*.json into immutable domain definitions."

  alias Rpg.Domain.Definitions.{
    AchievementDef,
    AffixDef,
    Balance,
    Caps,
    DifficultyDef,
    GameData,
    ItemDef,
    Level3Bonus,
    MonsterDef,
    PotionDef,
    RarityDef,
    SpellDef,
    SpellLevelDef,
    StatusDef,
    VocationDef
  }

  alias Rpg.Domain.{Entities, Enums, JsonTypes}
  alias Rpg.Infrastructure.Assets

  defmodule DataError do
    @moduledoc "Raised when a data file is missing, is not JSON, or has a missing/invalid field."
    defexception [:message]
  end

  @spec load_game_data(Assets.source()) :: GameData.t()
  def load_game_data(source \\ :embedded) do
    read = &read(source, &1)
    optional = fn name -> if Assets.exists?(source, "data/#{name}.json"), do: read.(name), else: %{name => []} end

    try do
      GameData.new(
        balance: balance(read.("balance")),
        vocations: Enum.map(objects(read.("vocations"), "vocations"), &vocation/1),
        spells: Enum.map(objects(read.("spells"), "spells"), &spell/1),
        monsters: Enum.map(objects(read.("monsters"), "monsters"), &creature(&1, false)),
        bosses: Enum.map(objects(read.("bosses"), "bosses"), &creature(&1, true)),
        potions: Enum.map(objects(read.("potions"), "potions"), &potion/1),
        statuses: Enum.map(objects(read.("statuses"), "statuses"), &status/1),
        items: Enum.map(objects(read.("items"), "items"), &item/1),
        affixes: Enum.map(objects(optional.("affixes"), "affixes"), &affix/1),
        achievements: Enum.map(objects(optional.("achievements"), "achievements"), &achievement/1),
        families: Enum.map(JsonTypes.list(field(read.("families"), "families")), &JsonTypes.str/1)
      )
    rescue
      error in [JsonTypes.Error, ArgumentError, KeyError] ->
        reraise DataError,
                [message: "invalid game data in #{Assets.describe(source, "data")}: #{Exception.message(error)}"],
                __STACKTRACE__
    end
  end

  defp read(source, name) do
    relative = "data/#{name}.json"

    with {:ok, text} <- Assets.read(source, relative),
         {:ok, document} when is_map(document) <- JSON.decode(text) do
      document
    else
      {:ok, _other} -> raise DataError, message: "#{Assets.describe(source, relative)}: expected a JSON object"
      {:error, reason} -> raise DataError, message: "#{Assets.describe(source, relative)}: #{inspect(reason)}"
    end
  end

  defp field(map, key), do: JsonTypes.field(map, key)
  defp int(map, key), do: JsonTypes.int(field(map, key))
  defp str(map, key), do: JsonTypes.str(field(map, key))
  defp strs(value), do: Enum.map(JsonTypes.list(value), &JsonTypes.str/1)
  defp objects(document, key), do: Enum.map(JsonTypes.list(field(document, key)), &JsonTypes.obj/1)

  defp creature(raw, is_boss) do
    gold = field(raw, "gold")
    charge = Map.get(raw, "chargeAttack")

    %MonsterDef{
      id: str(raw, "id"),
      name: str(raw, "name"),
      tier: int(raw, "tier"),
      family: str(raw, "family"),
      hp: int(raw, "hp"),
      xp: int(raw, "xp"),
      gold_min: int(gold, "min"),
      gold_max: int(gold, "max"),
      attacks: Enum.map(JsonTypes.list(field(raw, "attacks")), &Entities.attack_from_map/1),
      resistances:
        Map.new(JsonTypes.obj(field(raw, "resistances")), fn {k, v} -> {Enums.element!(k), JsonTypes.int(v)} end),
      is_boss: is_boss,
      charge_attack: if(charge != nil, do: JsonTypes.str(charge))
    }
  end

  defp spell(raw) do
    bonus = JsonTypes.obj(field(raw, "level3Bonus"))
    status = Map.get(bonus, "status")

    %SpellDef{
      id: str(raw, "id"),
      name: str(raw, "name"),
      words: str(raw, "words"),
      kind: Enums.spell_kind!(str(raw, "kind")),
      element: Enums.element!(str(raw, "element")),
      mana: int(raw, "mana"),
      min: int(raw, "min"),
      max: int(raw, "max"),
      per_level: int(raw, "perLevel"),
      per_magic_level: int(raw, "perMagicLevel"),
      level3_bonus: %Level3Bonus{
        status: if(status != nil, do: JsonTypes.str(status)),
        chance: JsonTypes.int(Map.get(bonus, "chance", 0)),
        cleanse: JsonTypes.bool(Map.get(bonus, "cleanse", false))
      }
    }
  end

  defp vocation(raw) do
    %VocationDef{
      id: str(raw, "id"),
      name: str(raw, "name"),
      start_hp: int(raw, "startHp"),
      start_mp: int(raw, "startMp"),
      hp_per_level: int(raw, "hpPerLevel"),
      mp_per_level: int(raw, "mpPerLevel"),
      hp_regen: int(raw, "hpRegen"),
      mp_regen: int(raw, "mpRegen"),
      melee_min: int(raw, "meleeMin"),
      melee_max: int(raw, "meleeMax"),
      melee_per_level: int(raw, "meleePerLevel"),
      weapon_types: strs(field(raw, "weaponTypes")),
      shield_types: strs(field(raw, "shieldTypes")),
      starter_weapon: str(raw, "starterWeapon"),
      spells: strs(field(raw, "spells"))
    }
  end

  defp balance(raw) do
    caps = field(raw, "caps")
    magic = field(raw, "magicLevel")

    %Balance{
      rounds_per_tier: int(raw, "roundsPerTier"),
      cycle_stat_pct: int(raw, "cycleStatPct"),
      cycle_reward_pct: int(raw, "cycleRewardPct"),
      position_pct: int(raw, "positionPct"),
      difficulties:
        Enum.map(objects(raw, "difficulties"), fn d ->
          %DifficultyDef{
            id: str(d, "id"),
            hp_pct: int(d, "hpPct"),
            damage_pct: int(d, "damagePct"),
            gold_pct: int(d, "goldPct"),
            xp_pct: int(d, "xpPct"),
            non_common_weight_pct: int(d, "nonCommonWeightPct")
          }
        end),
      crit_multiplier_pct: int(raw, "critMultiplierPct"),
      defend_damage_pct: int(raw, "defendDamagePct"),
      boss_telegraph_every: int(raw, "bossTelegraphEvery"),
      boss_charge_damage_pct: int(raw, "bossChargeDamagePct"),
      caps: %Caps{
        crit_chance: int(caps, "critChance"),
        dodge: int(caps, "dodge"),
        parry: int(caps, "parry"),
        leech: int(caps, "leech"),
        protection: int(caps, "protection")
      },
      magic_level_base: int(magic, "base"),
      magic_level_growth_pct: int(magic, "growthPct"),
      spell_levels:
        Enum.map(objects(raw, "spellLevels"), fn s ->
          %SpellLevelDef{
            level: int(s, "level"),
            uses: int(s, "uses"),
            effect_pct: int(s, "effectPct"),
            mana_pct: int(s, "manaPct")
          }
        end),
      starting_gold: int(raw, "startingGold"),
      starting_potions: Enum.map(objects(raw, "startingPotions"), &{str(&1, "potionId"), int(&1, "quantity")}),
      bag_capacity: int(raw, "bagCapacity"),
      drop_chance_pct: int(raw, "dropChancePct"),
      boss_drops: int(raw, "bossDrops"),
      rarities:
        Enum.map(objects(raw, "rarities"), fn r ->
          %RarityDef{
            id: str(r, "id"),
            stat_pct: int(r, "statPct"),
            value_pct: int(r, "valuePct"),
            affix_min: int(r, "affixMin"),
            affix_max: int(r, "affixMax")
          }
        end),
      rarity_weights:
        Map.new(JsonTypes.obj(field(raw, "rarityWeights")), fn {table, weights} ->
          {table, Map.new(JsonTypes.obj(weights), fn {rarity, weight} -> {rarity, JsonTypes.int(weight)} end)}
        end),
      merchant_stock_size: int(raw, "merchantStockSize"),
      merchant_markup_pct: int(raw, "merchantMarkupPct"),
      spell_status_damage_pct: int(raw, "spellStatusDamagePct")
    }
  end

  defp potion(p) do
    %PotionDef{
      id: str(p, "id"),
      name: str(p, "name"),
      resource: Enums.resource!(str(p, "resource")),
      min: int(p, "min"),
      max: int(p, "max"),
      price: int(p, "price"),
      unlock_round: int(p, "unlockRound")
    }
  end

  defp status(s) do
    %StatusDef{
      id: str(s, "id"),
      kind: Enums.status_kind!(str(s, "kind")),
      element: Enums.element!(str(s, "element")),
      turns: int(s, "turns")
    }
  end

  defp item(raw) do
    element = Map.get(raw, "element")

    %ItemDef{
      id: str(raw, "id"),
      name: str(raw, "name"),
      slot: Enums.slot!(str(raw, "slot")),
      type: str(raw, "type"),
      tier: int(raw, "tier"),
      element: if(element != nil, do: Enums.element!(JsonTypes.str(element))),
      stats: Map.new(JsonTypes.obj(field(raw, "stats")), fn {k, v} -> {Enums.stat!(k), JsonTypes.int(v)} end),
      value: int(raw, "value")
    }
  end

  defp affix(a) do
    %AffixDef{
      id: str(a, "id"),
      stat: Enums.stat!(str(a, "stat")),
      min: int(a, "min"),
      max: int(a, "max"),
      per_tier: int(a, "perTier"),
      slots: Enum.map(strs(field(a, "slots")), &Enums.slot!/1)
    }
  end

  defp achievement(a), do: %AchievementDef{id: str(a, "id"), type: str(a, "type"), value: int(a, "value")}
end
