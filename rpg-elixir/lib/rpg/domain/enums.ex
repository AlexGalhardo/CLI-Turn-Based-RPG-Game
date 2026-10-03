defmodule Rpg.Domain.Enums do
  @moduledoc """
  Enumerations of the game.

  Values that are data ids (elements, slots, stats) stay strings, exactly as they appear in `shared/data` and in the
  saves. Internal states with a closed set of values (phase, spell kind, status kind, resource, target) are atoms,
  converted to strings only at the JSON boundary.
  """

  @elements ~w(physical fire ice energy earth holy death)
  @slots ~w(helmet armor legs boots amulet ring weapon shield)
  @stats ~w(attack armor maxHp maxMp hpRegen mpRegen critChance critDamage spellPower physicalDamage dodge parry
            lifeLeech manaLeech protPhysical protFire protIce protEnergy protEarth protHoly protDeath)
  @phases [:merchant, :battle, :victory, :game_over]
  @enemy_classes ~w(normal elite boss)
  @equipment_slot_order ~w(weapon shield helmet armor legs boots ring amulet)

  @protection_by_element %{
    "physical" => "protPhysical",
    "fire" => "protFire",
    "ice" => "protIce",
    "energy" => "protEnergy",
    "earth" => "protEarth",
    "holy" => "protHoly",
    "death" => "protDeath"
  }

  @type element :: String.t()
  @type slot :: String.t()
  @type stat :: String.t()
  @type phase :: :merchant | :battle | :victory | :game_over
  @type enemy_class :: String.t()
  @type resource :: :hp | :mp
  @type spell_kind :: :attack | :heal
  @type status_kind :: :dot | :stun
  @type target :: :player | :monster

  @doc "Elements in declaration order (used whenever the UI lists elements)."
  @spec elements() :: [element()]
  def elements, do: @elements

  @doc "Equipment slots in declaration order."
  @spec slots() :: [slot()]
  def slots, do: @slots

  @doc "Order in which auto-equip and the equipment screen walk the slots (docs/game-design.md §8.1)."
  @spec equipment_slot_order() :: [slot()]
  def equipment_slot_order, do: @equipment_slot_order

  @doc "Enemy classes in declaration order: `normal`, `elite`, `boss`."
  @spec enemy_classes() :: [enemy_class()]
  def enemy_classes, do: @enemy_classes

  @spec stats() :: [stat()]
  def stats, do: @stats

  @spec protection_stat(element()) :: stat()
  def protection_stat(element), do: Map.fetch!(@protection_by_element, element)

  @spec element!(String.t()) :: element()
  def element!(value), do: member!(value, @elements, "element")

  @spec slot!(String.t()) :: slot()
  def slot!(value), do: member!(value, @slots, "slot")

  @spec stat!(String.t()) :: stat()
  def stat!(value), do: member!(value, @stats, "stat")

  @spec phase!(String.t()) :: phase()
  def phase!(value), do: atom!(value, @phases, "phase")

  @spec resource!(String.t()) :: resource()
  def resource!(value), do: atom!(value, [:hp, :mp], "resource")

  @spec spell_kind!(String.t()) :: spell_kind()
  def spell_kind!(value), do: atom!(value, [:attack, :heal], "spell kind")

  @spec status_kind!(String.t()) :: status_kind()
  def status_kind!(value), do: atom!(value, [:dot, :stun], "status kind")

  defp member!(value, allowed, what) do
    if value in allowed, do: value, else: raise(ArgumentError, "invalid #{what}: #{inspect(value)}")
  end

  # Only known atoms are produced: untrusted JSON never creates new atoms.
  defp atom!(value, allowed, what) do
    Enum.find(allowed, &(Atom.to_string(&1) == value)) || raise(ArgumentError, "invalid #{what}: #{inspect(value)}")
  end
end
