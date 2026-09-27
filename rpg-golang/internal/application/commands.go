package application

import (
	"encoding/json"
	"fmt"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// Command types (the `type` field of the JSON used by golden files).
const (
	CmdAttack       = "attack"
	CmdCast         = "cast"
	CmdPotion       = "potion"
	CmdDefend       = "defend"
	CmdNextFight    = "next_fight"
	CmdBuyPotion    = "buy_potion"
	CmdSellItem     = "sell_item"
	CmdEquip        = "equip"
	CmdUnequip      = "unequip"
	CmdBuyStockItem = "buy_stock_item"
)

// Command is a player command. Only the fields of its type are meaningful; JSON omits the others.
type Command struct {
	Type     string      `json:"type"`
	SpellID  string      `json:"spellId,omitempty"`
	PotionID string      `json:"potionId,omitempty"`
	Quantity int         `json:"quantity,omitempty"`
	UID      int         `json:"uid,omitempty"`
	Slot     domain.Slot `json:"slot,omitempty"`
	Index    int         `json:"-"`
}

// Attack is a melee attack.
func Attack() Command { return Command{Type: CmdAttack} }

// Cast casts a spell.
func Cast(spellID string) Command { return Command{Type: CmdCast, SpellID: spellID} }

// UsePotion drinks a potion.
func UsePotion(potionID string) Command { return Command{Type: CmdPotion, PotionID: potionID} }

// Defend halves incoming damage this turn.
func Defend() Command { return Command{Type: CmdDefend} }

// NextFight leaves the merchant.
func NextFight() Command { return Command{Type: CmdNextFight} }

// BuyPotion buys potions.
func BuyPotion(potionID string, quantity int) Command {
	return Command{Type: CmdBuyPotion, PotionID: potionID, Quantity: quantity}
}

// SellItem sells a bag item.
func SellItem(uid int) Command { return Command{Type: CmdSellItem, UID: uid} }

// Equip equips a bag item.
func Equip(uid int) Command { return Command{Type: CmdEquip, UID: uid} }

// Unequip moves an equipped item to the bag.
func Unequip(slot domain.Slot) Command { return Command{Type: CmdUnequip, Slot: slot} }

// BuyStockItem buys an item from the merchant stock.
func BuyStockItem(index int) Command { return Command{Type: CmdBuyStockItem, Index: index} }

// IsBattle reports whether the command belongs to the battle phase.
func (c Command) IsBattle() bool {
	switch c.Type {
	case CmdAttack, CmdCast, CmdPotion, CmdDefend:
		return true
	default:
		return false
	}
}

// ToMap renders the command exactly like the Python reference (`index` is always present for buy_stock_item).
func (c Command) ToMap() map[string]any {
	result := map[string]any{"type": c.Type}

	switch c.Type {
	case CmdCast:
		result["spellId"] = c.SpellID
	case CmdPotion:
		result["potionId"] = c.PotionID
	case CmdBuyPotion:
		result["potionId"] = c.PotionID
		result["quantity"] = c.Quantity
	case CmdSellItem, CmdEquip:
		result["uid"] = c.UID
	case CmdUnequip:
		result["slot"] = string(c.Slot)
	case CmdBuyStockItem:
		result["index"] = c.Index
	}

	return result
}

// CommandFromJSON parses a command object.
func CommandFromJSON(raw []byte) (Command, error) {
	var data struct {
		Type     string `json:"type"`
		SpellID  string `json:"spellId"`
		PotionID string `json:"potionId"`
		Quantity int    `json:"quantity"`
		UID      int    `json:"uid"`
		Slot     string `json:"slot"`
		Index    int    `json:"index"`
	}
	if err := json.Unmarshal(raw, &data); err != nil {
		return Command{}, fmt.Errorf("parse command: %w", err)
	}

	switch data.Type {
	case CmdAttack:
		return Attack(), nil
	case CmdCast:
		return Cast(data.SpellID), nil
	case CmdPotion:
		return UsePotion(data.PotionID), nil
	case CmdDefend:
		return Defend(), nil
	case CmdNextFight:
		return NextFight(), nil
	case CmdBuyPotion:
		return BuyPotion(data.PotionID, data.Quantity), nil
	case CmdSellItem:
		return SellItem(data.UID), nil
	case CmdEquip:
		return Equip(data.UID), nil
	case CmdUnequip:
		return Unequip(domain.Slot(data.Slot)), nil
	case CmdBuyStockItem:
		return BuyStockItem(data.Index), nil
	default:
		return Command{}, fmt.Errorf("unknown command type: %q", data.Type)
	}
}
