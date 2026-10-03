package application

import (
	"slices"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

const maxPotionsPerPurchase = 99

// StockPrice is the merchant's selling price for a stock item.
func StockPrice(item domain.ItemInstance, data *domain.GameData) int {
	return domain.Pct(domain.ItemValue(item, data), data.Balance.MerchantMarkupPct)
}

// AvailablePotions lists the potions unlocked for the next round, in data file order.
func AvailablePotions(state *RunState, data *domain.GameData) []string {
	nextRound := state.Round + 1
	result := []string{}

	for _, potion := range data.Potions {
		if potion.UnlockRound <= nextRound {
			result = append(result, potion.ID)
		}
	}

	return result
}

// Merchant handles the merchant phase: potions, bag, equipment and rotating stock (docs/game-design.md §10).
type Merchant struct {
	data  *domain.GameData
	rng   *domain.Rng
	state *RunState
}

// NewMerchant creates the merchant over the current state.
func NewMerchant(data *domain.GameData, rng *domain.Rng, state *RunState) *Merchant {
	return &Merchant{data: data, rng: rng, state: state}
}

// Enter generates the rotating stock for the tier of the next round.
func (m *Merchant) Enter() []Event {
	state := m.state
	vocation := m.data.Vocation(state.Player.VocationID)
	tier := domain.RoundInfoFor(state.Round+1, &m.data.Balance, m.data.TierCount()).Tier
	state.MerchantStock = []domain.ItemInstance{}

	for range m.data.Balance.MerchantStockSize {
		item, ok := GenerateItem(m.data, m.rng, ItemRequest{
			Vocation: vocation, Tier: tier, Weights: m.data.Balance.RarityWeights["merchant"], UID: state.NextItemUID,
		})
		if ok {
			state.TakeItemUID()
			state.MerchantStock = append(state.MerchantStock, item)
		}
	}

	return []Event{NewEvent("merchant_entered", map[string]any{"round": state.Round})}
}

// Handle executes a merchant command.
func (m *Merchant) Handle(command Command) []Event {
	switch command.Type {
	case CmdBuyPotion:
		return m.buyPotion(command.PotionID, command.Quantity)
	case CmdSellItem:
		return m.sell(command.UID)
	case CmdEquip:
		return m.equip(command.UID)
	case CmdUnequip:
		return m.unequip(command.Slot)
	case CmdBuyStockItem:
		return m.buyStock(command.Index)
	default:
		return []Event{ErrorEvent(ErrInvalidPhase)}
	}
}

func (m *Merchant) buyPotion(potionID string, quantity int) []Event {
	player := m.state.Player
	if !m.data.HasPotion(potionID) {
		return []Event{ErrorEvent(ErrUnknownPotion)}
	}

	if !slices.Contains(AvailablePotions(m.state, m.data), potionID) {
		return []Event{ErrorEvent(ErrPotionLocked)}
	}

	if quantity < 1 || quantity > maxPotionsPerPurchase {
		return []Event{ErrorEvent(ErrInvalidQuantity)}
	}

	cost := m.data.Potion(potionID).Price * quantity
	if player.Gold < cost {
		return []Event{ErrorEvent(ErrNotEnoughGold)}
	}

	player.Gold -= cost
	player.Potions[potionID] += quantity

	return []Event{NewEvent("potion_bought", map[string]any{"potionId": potionID, "quantity": quantity, "gold": cost})}
}

func (m *Merchant) findInBag(uid int) int {
	return slices.IndexFunc(m.state.Player.Bag, func(item domain.ItemInstance) bool { return item.UID == uid })
}

func (m *Merchant) sell(uid int) []Event {
	index := m.findInBag(uid)
	if index < 0 {
		return []Event{ErrorEvent(ErrInvalidItem)}
	}

	player := m.state.Player
	item := player.Bag[index]
	value := domain.ItemValue(item, m.data)
	player.Bag = slices.Delete(player.Bag, index, index+1)
	player.Gold += value

	return []Event{NewEvent("item_sold", map[string]any{"uid": uid, "itemId": item.ItemID, "gold": value})}
}

func (m *Merchant) equip(uid int) []Event {
	player := m.state.Player

	index := m.findInBag(uid)
	if index < 0 {
		return []Event{ErrorEvent(ErrInvalidItem)}
	}

	item := player.Bag[index]

	definition := m.data.Item(item.ItemID)
	if !CanUse(definition, m.data.Vocation(player.VocationID)) {
		return []Event{ErrorEvent(ErrCannotEquip)}
	}

	if domain.RequiredLevel(item, m.data) > player.Level {
		return []Event{ErrorEvent(ErrLevelTooLow)}
	}

	events := []Event{}
	player.Bag = slices.Delete(player.Bag, index, index+1)

	if previous, ok := player.Equipment[definition.Slot]; ok {
		delete(player.Equipment, definition.Slot)
		player.Bag = append(player.Bag, previous)
		events = append(events, NewEvent("item_unequipped", map[string]any{"uid": previous.UID, "itemId": previous.ItemID, "slot": string(definition.Slot)}))
	}

	player.Equipment[definition.Slot] = item
	events = append(events, NewEvent("item_equipped", map[string]any{"uid": item.UID, "itemId": item.ItemID, "slot": string(definition.Slot)}))

	m.clampResources()

	return events
}

func (m *Merchant) unequip(slot domain.Slot) []Event {
	player := m.state.Player

	item, ok := player.Equipment[slot]
	if !ok {
		return []Event{ErrorEvent(ErrInvalidItem)}
	}

	if len(player.Bag) >= m.data.Balance.BagCapacity {
		return []Event{ErrorEvent(ErrBagFull)}
	}

	delete(player.Equipment, slot)
	player.Bag = append(player.Bag, item)

	m.clampResources()

	return []Event{NewEvent("item_unequipped", map[string]any{"uid": item.UID, "itemId": item.ItemID, "slot": string(slot)})}
}

func (m *Merchant) buyStock(index int) []Event {
	state := m.state
	if index < 0 || index >= len(state.MerchantStock) {
		return []Event{ErrorEvent(ErrInvalidItem)}
	}

	if len(state.Player.Bag) >= m.data.Balance.BagCapacity {
		return []Event{ErrorEvent(ErrBagFull)}
	}

	item := state.MerchantStock[index]

	price := StockPrice(item, m.data)
	if state.Player.Gold < price {
		return []Event{ErrorEvent(ErrNotEnoughGold)}
	}

	state.Player.Gold -= price
	state.MerchantStock = slices.Delete(state.MerchantStock, index, index+1)
	state.Player.Bag = append(state.Player.Bag, item)

	events := []Event{NewEvent("item_bought", map[string]any{"uid": item.UID, "itemId": item.ItemID, "gold": price})}
	if state.Config.AutoEquip {
		events = append(events, AutoEquip(state, m.data)...)
	}

	return events
}

func (m *Merchant) clampResources() {
	player := m.state.Player
	sheet := domain.BuildSheet(player, m.data)
	player.HP = min(player.HP, sheet.MaxHP)
	player.MP = min(player.MP, sheet.MaxMP)
}
