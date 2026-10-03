defmodule Rpg.Application.RunStatistics do
  @moduledoc "Deterministic run counters, derived only from engine events (docs/game-design.md §11)."

  alias Rpg.Domain.JsonTypes

  defmodule DroppedItem do
    @moduledoc false
    @enforce_keys [:item_id, :rarity, :round]
    defstruct @enforce_keys
    @type t :: %__MODULE__{item_id: String.t(), rarity: String.t(), round: integer()}
  end

  @int_fields [
    damage_dealt: "damageDealt",
    damage_taken: "damageTaken",
    healing_done: "healingDone",
    highest_hit: "highestHit",
    normal_attacks: "normalAttacks",
    crits: "crits",
    dodges: "dodges",
    parries: "parries",
    defends: "defends",
    gold_looted: "goldLooted",
    gold_spent: "goldSpent",
    gold_earned: "goldEarned",
    items_sold: "itemsSold",
    items_auto_equipped: "itemsAutoEquipped",
    bosses_killed: "bossesKilled",
    elites_killed: "elitesKilled"
  ]

  @counter_fields [
    spells_cast: "spellsCast",
    potions_used: "potionsUsed",
    potions_bought: "potionsBought",
    potions_dropped: "potionsDropped",
    items_dropped: "itemsDropped",
    kills: "kills",
    statuses_applied: "statusesApplied"
  ]

  defstruct Enum.map(@int_fields, fn {field, _} -> {field, 0} end) ++
              Enum.map(@counter_fields, fn {field, _} -> {field, %{}} end) ++ [dropped_items: []]

  @type counter :: %{String.t() => integer()}
  @type t :: %__MODULE__{}

  @spec record(t(), [map()], integer()) :: t()
  def record(%__MODULE__{} = stats, events, current_round) do
    Enum.reduce(events, stats, &record_one(&2, &1, current_round))
  end

  defp record_one(stats, %{"type" => type} = event, current_round) do
    case type do
      "player_attacked" ->
        stats |> bump(:normal_attacks, 1) |> dealt(event)

      "spell_cast" ->
        stats |> count(:spells_cast, event["spellId"], 1) |> dealt(event)

      "spell_healed" ->
        stats |> count(:spells_cast, event["spellId"], 1) |> bump(:healing_done, event["amount"])

      "potion_used" ->
        stats = count(stats, :potions_used, event["potionId"], 1)
        if event["resource"] == "hp", do: bump(stats, :healing_done, event["amount"]), else: stats

      "player_defended" ->
        bump(stats, :defends, 1)

      "monster_attacked" ->
        bump(stats, :damage_taken, event["damage"])

      "attack_dodged" ->
        bump(stats, :dodges, 1)

      "attack_parried" ->
        stats |> bump(:parries, 1) |> bump(:damage_dealt, event["reflected"])

      "monster_parried" ->
        bump(stats, :damage_taken, event["reflected"])

      "status_ticked" ->
        field = if event["target"] == "player", do: :damage_taken, else: :damage_dealt
        bump(stats, field, event["damage"])

      "status_applied" ->
        if event["target"] == "monster", do: count(stats, :statuses_applied, event["status"], 1), else: stats

      "monster_killed" ->
        stats = count(stats, :kills, event["monsterId"], 1)
        stats = if event["isBoss"] === true, do: bump(stats, :bosses_killed, 1), else: stats
        if event["enemyClass"] == "elite", do: bump(stats, :elites_killed, 1), else: stats

      "gold_looted" ->
        bump(stats, :gold_looted, event["amount"])

      "item_dropped" ->
        dropped = %DroppedItem{item_id: event["itemId"], rarity: event["rarity"], round: current_round}

        stats
        |> count(:items_dropped, event["rarity"], 1)
        |> Map.update!(:dropped_items, &(&1 ++ [dropped]))

      "potion_bought" ->
        stats |> count(:potions_bought, event["potionId"], event["quantity"]) |> bump(:gold_spent, event["gold"])

      "item_bought" ->
        bump(stats, :gold_spent, event["gold"])

      "potion_dropped" ->
        count(stats, :potions_dropped, event["potionId"], 1)

      "item_auto_equipped" ->
        bump(stats, :items_auto_equipped, 1)

      sold when sold in ["item_sold", "item_auto_sold"] ->
        stats |> bump(:items_sold, 1) |> bump(:gold_earned, event["gold"])

      _ ->
        stats
    end
  end

  defp bump(stats, field, amount) when is_integer(amount), do: Map.update!(stats, field, &(&1 + amount))

  defp count(stats, field, key, amount) when is_binary(key) and is_integer(amount) do
    Map.update!(stats, field, fn counter -> Map.update(counter, key, amount, &(&1 + amount)) end)
  end

  defp dealt(stats, event) do
    damage = event["damage"]
    stats = %{bump(stats, :damage_dealt, damage) | highest_hit: max(stats.highest_hit, damage)}
    if event["crit"] === true, do: bump(stats, :crits, 1), else: stats
  end

  @spec to_map(t()) :: map()
  def to_map(%__MODULE__{} = stats) do
    ints = Map.new(@int_fields, fn {field, key} -> {key, Map.fetch!(stats, field)} end)
    counters = Map.new(@counter_fields, fn {field, key} -> {key, Map.fetch!(stats, field)} end)

    dropped =
      Enum.map(stats.dropped_items, fn item ->
        %{"itemId" => item.item_id, "rarity" => item.rarity, "round" => item.round}
      end)

    ints |> Map.merge(counters) |> Map.put("droppedItems", dropped)
  end

  @spec from_map(term()) :: t()
  def from_map(raw) do
    ints = Enum.map(@int_fields, fn {field, key} -> {field, JsonTypes.int(JsonTypes.field(raw, key))} end)

    counters =
      Enum.map(@counter_fields, fn {field, key} ->
        {field, Map.new(JsonTypes.obj(JsonTypes.field(raw, key)), fn {k, v} -> {k, JsonTypes.int(v)} end)}
      end)

    dropped =
      raw
      |> JsonTypes.field("droppedItems")
      |> JsonTypes.list()
      |> Enum.map(fn entry ->
        %DroppedItem{
          item_id: JsonTypes.str(JsonTypes.field(entry, "itemId")),
          rarity: JsonTypes.str(JsonTypes.field(entry, "rarity")),
          round: JsonTypes.int(JsonTypes.field(entry, "round"))
        }
      end)

    struct!(__MODULE__, ints ++ counters ++ [dropped_items: dropped])
  end
end
