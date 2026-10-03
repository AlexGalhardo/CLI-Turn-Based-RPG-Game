package application

// DroppedItem records a drop for the run history.
type DroppedItem struct {
	ItemID string `json:"itemId"`
	Rarity string `json:"rarity"`
	Round  int    `json:"round"`
}

// RunStatistics are deterministic run counters derived only from engine events (docs/game-design.md §11).
// Maps marshal with sorted keys, matching the Python reference.
type RunStatistics struct {
	DamageDealt       int            `json:"damageDealt"`
	DamageTaken       int            `json:"damageTaken"`
	HealingDone       int            `json:"healingDone"`
	HighestHit        int            `json:"highestHit"`
	NormalAttacks     int            `json:"normalAttacks"`
	Crits             int            `json:"crits"`
	Dodges            int            `json:"dodges"`
	Parries           int            `json:"parries"`
	Defends           int            `json:"defends"`
	GoldLooted        int            `json:"goldLooted"`
	GoldSpent         int            `json:"goldSpent"`
	GoldEarned        int            `json:"goldEarned"`
	ItemsSold         int            `json:"itemsSold"`
	ItemsAutoEquipped int            `json:"itemsAutoEquipped"`
	BossesKilled      int            `json:"bossesKilled"`
	ElitesKilled      int            `json:"elitesKilled"`
	SpellsCast        map[string]int `json:"spellsCast"`
	PotionsUsed       map[string]int `json:"potionsUsed"`
	PotionsBought     map[string]int `json:"potionsBought"`
	PotionsDropped    map[string]int `json:"potionsDropped"`
	ItemsDropped      map[string]int `json:"itemsDropped"`
	Kills             map[string]int `json:"kills"`
	StatusesApplied   map[string]int `json:"statusesApplied"`
	DroppedItems      []DroppedItem  `json:"droppedItems"`
}

// NewRunStatistics returns empty counters.
func NewRunStatistics() *RunStatistics {
	return &RunStatistics{
		SpellsCast:      map[string]int{},
		PotionsUsed:     map[string]int{},
		PotionsBought:   map[string]int{},
		PotionsDropped:  map[string]int{},
		ItemsDropped:    map[string]int{},
		Kills:           map[string]int{},
		StatusesApplied: map[string]int{},
		DroppedItems:    []DroppedItem{},
	}
}

// TotalKills sums the kills of every monster.
func (s *RunStatistics) TotalKills() int {
	total := 0
	for _, count := range s.Kills {
		total += count
	}

	return total
}

// Record updates the counters from a step's events.
func (s *RunStatistics) Record(events []Event, currentRound int) {
	for _, evt := range events {
		s.recordOne(evt, currentRound)
	}
}

//nolint:gocyclo // one flat switch over event types mirrors the reference implementation.
func (s *RunStatistics) recordOne(evt Event, currentRound int) {
	switch evt.Type() {
	case "player_attacked":
		s.NormalAttacks++
		s.dealt(evt)
	case "spell_cast":
		s.SpellsCast[evt.Str("spellId")]++
		s.dealt(evt)
	case "spell_healed":
		s.SpellsCast[evt.Str("spellId")]++
		s.HealingDone += evt.Int("amount")
	case "potion_used":
		s.PotionsUsed[evt.Str("potionId")]++
		if evt.Str("resource") == "hp" {
			s.HealingDone += evt.Int("amount")
		}
	case "player_defended":
		s.Defends++
	case "monster_attacked":
		s.DamageTaken += evt.Int("damage")
	case "attack_dodged":
		s.Dodges++
	case "attack_parried":
		s.Parries++
		s.DamageDealt += evt.Int("reflected")
	case "monster_parried":
		s.DamageTaken += evt.Int("reflected")
	case "status_ticked":
		if evt.Str("target") == "player" {
			s.DamageTaken += evt.Int("damage")
		} else {
			s.DamageDealt += evt.Int("damage")
		}
	case "status_applied":
		if evt.Str("target") == "monster" {
			s.StatusesApplied[evt.Str("status")]++
		}
	case "monster_killed":
		s.Kills[evt.Str("monsterId")]++
		if evt.Bool("isBoss") {
			s.BossesKilled++
		}

		if evt.Str("enemyClass") == "elite" {
			s.ElitesKilled++
		}
	case "gold_looted":
		s.GoldLooted += evt.Int("amount")
	case "item_dropped":
		s.ItemsDropped[evt.Str("rarity")]++
		s.DroppedItems = append(s.DroppedItems, DroppedItem{ItemID: evt.Str("itemId"), Rarity: evt.Str("rarity"), Round: currentRound})
	case "potion_bought":
		s.PotionsBought[evt.Str("potionId")] += evt.Int("quantity")
		s.GoldSpent += evt.Int("gold")
	case "item_bought":
		s.GoldSpent += evt.Int("gold")
	case "potion_dropped":
		s.PotionsDropped[evt.Str("potionId")]++
	case "item_auto_equipped":
		s.ItemsAutoEquipped++
	case "item_sold", "item_auto_sold":
		s.ItemsSold++
		s.GoldEarned += evt.Int("gold")
	}
}

func (s *RunStatistics) dealt(evt Event) {
	damage := evt.Int("damage")
	s.DamageDealt += damage
	s.HighestHit = max(s.HighestHit, damage)

	if evt.Bool("crit") {
		s.Crits++
	}
}
