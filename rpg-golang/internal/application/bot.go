package application

import (
	"slices"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

const (
	healThresholdPct        = 45
	manaPotionThresholdPct  = 25
	maxPotionStock          = 20
	potionStockBase         = 5
	potionStockRoundDivisor = 5
)

// maxBy mirrors Python's max(items, key=(number, id)): the highest number wins, ties broken by the higher id.
func maxBy[T any](items []T, key func(T) (int, string)) (T, bool) {
	var (
		best      T
		bestValue int
		bestID    string
		found     bool
	)

	for _, item := range items {
		value, id := key(item)
		if !found || value > bestValue || (value == bestValue && id > bestID) {
			best, bestValue, bestID, found = item, value, id, true
		}
	}

	return best, found
}

// GreedyBot is a deterministic heuristic player used by the simulator and the end-to-end parity tests.
// Its decisions are part of the golden "bot full run" files: it mirrors the Python GreedyBot exactly.
type GreedyBot struct {
	data *domain.GameData
}

// NewGreedyBot creates the bot.
func NewGreedyBot(data *domain.GameData) *GreedyBot {
	return &GreedyBot{data: data}
}

// Choose returns the next command for the current state.
func (b *GreedyBot) Choose(state *RunState) Command {
	if state.Phase == domain.PhaseBattle {
		return b.battle(state)
	}

	if state.Phase == domain.PhaseVictory {
		return EndRun()
	}

	return b.merchant(state)
}

func (b *GreedyBot) battle(state *RunState) Command {
	player := state.Player
	monster := state.Monster
	sheet := domain.BuildSheet(player, b.data)

	if b.chargeIncoming(monster) {
		return Defend()
	}

	if player.HP*100 < sheet.MaxHP*healThresholdPct {
		if heal, ok := b.heal(state); ok {
			return heal
		}
	}

	if player.MP*100 < sheet.MaxMP*manaPotionThresholdPct {
		if potion, ok := b.bestOwnedPotion(state, "mp"); ok {
			return UsePotion(potion.ID)
		}
	}

	if spell, ok := b.bestAttackSpell(state, monster); ok {
		return Cast(spell.ID)
	}

	return Attack()
}

func (b *GreedyBot) chargeIncoming(monster *domain.MonsterInstance) bool {
	if !monster.IsBoss {
		return false
	}

	every := b.data.Balance.BossTelegraphEvery

	return monster.BossActions%(every+1) == every
}

func (b *GreedyBot) cost(state *RunState, spell *domain.SpellDef) int {
	level := domain.SpellLevelForUses(state.Player.SpellUses[spell.ID], b.data.Balance.SpellLevels)

	return domain.Pct(spell.Mana, level.ManaPct)
}

func (b *GreedyBot) spells(state *RunState, kind string) []*domain.SpellDef {
	result := []*domain.SpellDef{}

	for _, id := range b.data.Vocation(state.Player.VocationID).Spells {
		spell := b.data.Spell(id)
		if spell.Kind == kind && b.cost(state, spell) <= state.Player.MP {
			result = append(result, spell)
		}
	}

	return result
}

func (b *GreedyBot) heal(state *RunState) (Command, bool) {
	if spell, ok := maxBy(b.spells(state, "heal"), func(s *domain.SpellDef) (int, string) { return s.Max, s.ID }); ok {
		return Cast(spell.ID), true
	}

	if potion, ok := b.bestOwnedPotion(state, "hp"); ok {
		return UsePotion(potion.ID), true
	}

	return Command{}, false
}

func (b *GreedyBot) bestOwnedPotion(state *RunState, resource string) (*domain.PotionDef, bool) {
	owned := []*domain.PotionDef{}

	for i := range b.data.Potions {
		potion := &b.data.Potions[i]
		if potion.Resource == resource && state.Player.PotionCount(potion.ID) > 0 {
			owned = append(owned, potion)
		}
	}

	return maxBy(owned, func(p *domain.PotionDef) (int, string) { return p.Max, p.ID })
}

func (b *GreedyBot) bestAttackSpell(state *RunState, monster *domain.MonsterInstance) (*domain.SpellDef, bool) {
	creature := b.data.Creature(monster.CreatureID)
	candidates := []*domain.SpellDef{}

	for _, spell := range b.spells(state, "attack") {
		if creature.Resistance(spell.Element) > 0 {
			candidates = append(candidates, spell)
		}
	}

	return maxBy(candidates, func(s *domain.SpellDef) (int, string) {
		return (s.Min + s.Max) * creature.Resistance(s.Element), s.ID
	})
}

func (b *GreedyBot) merchant(state *RunState) Command {
	player := state.Player
	vocation := b.data.Vocation(player.VocationID)
	bag := slices.Clone(player.Bag)
	slices.SortFunc(bag, func(x, y domain.ItemInstance) int { return x.UID - y.UID })

	for _, item := range bag {
		definition := b.data.Item(item.ItemID)
		if !CanUse(definition, vocation) || domain.RequiredLevel(item, b.data) > player.Level {
			continue
		}

		current, equipped := player.Equipment[definition.Slot]
		if !equipped || domain.ItemScore(item, b.data) > domain.ItemScore(current, b.data) {
			return Equip(item.UID)
		}
	}

	if len(bag) > 0 {
		return SellItem(bag[0].UID)
	}

	if purchase, ok := b.potionPurchase(state, "hp"); ok {
		return purchase
	}

	if purchase, ok := b.potionPurchase(state, "mp"); ok {
		return purchase
	}

	return NextFight()
}

func (b *GreedyBot) potionPurchase(state *RunState, resource string) (Command, bool) {
	unlocked := AvailablePotions(state, b.data)
	options := []*domain.PotionDef{}

	for i := range b.data.Potions {
		potion := &b.data.Potions[i]
		if potion.Resource == resource && slices.Contains(unlocked, potion.ID) {
			options = append(options, potion)
		}
	}

	best, ok := maxBy(options, func(p *domain.PotionDef) (int, string) { return p.Max, p.ID })
	if !ok {
		return Command{}, false
	}

	owned := 0
	for _, potion := range options {
		owned += state.Player.PotionCount(potion.ID)
	}

	target := min(maxPotionStock, potionStockBase+state.Round/potionStockRoundDivisor)

	budget := state.Player.Gold
	if resource == "mp" {
		budget = state.Player.Gold / 2
	}

	quantity := min(target-owned, budget/best.Price)
	if quantity <= 0 {
		return Command{}, false
	}

	return BuyPotion(best.ID, quantity), true
}
