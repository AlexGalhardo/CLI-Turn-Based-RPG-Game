defmodule Rpg.Application.Commands do
  @moduledoc """
  Player commands. Each maps 1:1 to a JSON object used by golden files (`%{"type" => "cast", "spellId" => ...}`).

  Every command is its own struct, so the engine dispatches with pattern matching on the struct name.
  """

  alias Rpg.Domain.{Enums, JsonTypes}

  defmodule Attack, do: defstruct([])
  defmodule Defend, do: defstruct([])
  defmodule NextFight, do: defstruct([])

  defmodule Cast do
    @enforce_keys [:spell_id]
    defstruct [:spell_id]
  end

  defmodule UsePotion do
    @enforce_keys [:potion_id]
    defstruct [:potion_id]
  end

  defmodule BuyPotion do
    @enforce_keys [:potion_id, :quantity]
    defstruct [:potion_id, :quantity]
  end

  defmodule SellItem do
    @enforce_keys [:uid]
    defstruct [:uid]
  end

  defmodule Equip do
    @enforce_keys [:uid]
    defstruct [:uid]
  end

  defmodule Unequip do
    @enforce_keys [:slot]
    defstruct [:slot]
  end

  defmodule BuyStockItem do
    @enforce_keys [:index]
    defstruct [:index]
  end

  @type t ::
          %Attack{}
          | %Cast{}
          | %UsePotion{}
          | %Defend{}
          | %NextFight{}
          | %BuyPotion{}
          | %SellItem{}
          | %Equip{}
          | %Unequip{}
          | %BuyStockItem{}

  @spec to_map(t()) :: map()
  def to_map(%Attack{}), do: %{"type" => "attack"}
  def to_map(%Cast{spell_id: id}), do: %{"type" => "cast", "spellId" => id}
  def to_map(%UsePotion{potion_id: id}), do: %{"type" => "potion", "potionId" => id}
  def to_map(%Defend{}), do: %{"type" => "defend"}
  def to_map(%NextFight{}), do: %{"type" => "next_fight"}
  def to_map(%BuyPotion{potion_id: id, quantity: q}), do: %{"type" => "buy_potion", "potionId" => id, "quantity" => q}
  def to_map(%SellItem{uid: uid}), do: %{"type" => "sell_item", "uid" => uid}
  def to_map(%Equip{uid: uid}), do: %{"type" => "equip", "uid" => uid}
  def to_map(%Unequip{slot: slot}), do: %{"type" => "unequip", "slot" => slot}
  def to_map(%BuyStockItem{index: index}), do: %{"type" => "buy_stock_item", "index" => index}

  @spec from_map(term()) :: t()
  def from_map(raw) do
    field = &JsonTypes.field(raw, &1)

    case JsonTypes.str(field.("type")) do
      "attack" ->
        %Attack{}

      "cast" ->
        %Cast{spell_id: JsonTypes.str(field.("spellId"))}

      "potion" ->
        %UsePotion{potion_id: JsonTypes.str(field.("potionId"))}

      "defend" ->
        %Defend{}

      "next_fight" ->
        %NextFight{}

      "buy_potion" ->
        %BuyPotion{potion_id: JsonTypes.str(field.("potionId")), quantity: JsonTypes.int(field.("quantity"))}

      "sell_item" ->
        %SellItem{uid: JsonTypes.int(field.("uid"))}

      "equip" ->
        %Equip{uid: JsonTypes.int(field.("uid"))}

      "unequip" ->
        %Unequip{slot: Enums.slot!(JsonTypes.str(field.("slot")))}

      "buy_stock_item" ->
        %BuyStockItem{index: JsonTypes.int(field.("index"))}

      other ->
        raise ArgumentError, "unknown command type: #{other}"
    end
  end
end
