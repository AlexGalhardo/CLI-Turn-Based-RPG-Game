package application

import (
	"encoding/json"
	"errors"
	"fmt"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// RunConfig is what the player chooses for a new run.
type RunConfig struct {
	Name         string `json:"name"`
	VocationID   string `json:"vocation"`
	DifficultyID string `json:"difficulty"`
	AutoEquip    bool   `json:"autoEquip"`
}

// RunState is everything needed to continue a run, except the PRNG state (kept by the engine).
// Its JSON is exactly the reference `RunState.to_dict()` (the shared save format).
type RunState struct {
	Seed          uint64                  `json:"seed"`
	Config        RunConfig               `json:"config"`
	Player        *domain.Player          `json:"player"`
	Phase         domain.Phase            `json:"phase"`
	Round         int                     `json:"round"`
	Turn          int                     `json:"turn"`
	Monster       *domain.MonsterInstance `json:"monster"`
	MerchantStock []domain.ItemInstance   `json:"merchantStock"`
	NextItemUID   int                     `json:"nextItemUid"`
	DeathCause    *string                 `json:"deathCause"`
	Won           bool                    `json:"won"`
	Stats         *RunStatistics          `json:"stats"`
}

// NewRunState creates the state of a fresh run.
func NewRunState(seed uint64, config RunConfig, player *domain.Player) *RunState {
	return &RunState{
		Seed:          seed,
		Config:        config,
		Player:        player,
		Phase:         domain.PhaseMerchant,
		MerchantStock: []domain.ItemInstance{},
		NextItemUID:   1,
		Stats:         NewRunStatistics(),
	}
}

// TakeItemUID returns the next item uid and advances the counter.
func (s *RunState) TakeItemUID() int {
	uid := s.NextItemUID
	s.NextItemUID++

	return uid
}

// DeathCauseOr returns the death cause or a fallback.
func (s *RunState) DeathCauseOr(fallback string) string {
	if s.DeathCause == nil {
		return fallback
	}

	return *s.DeathCause
}

// Clone deep-copies the state through the save format (used for merchant snapshots).
func (s *RunState) Clone() *RunState {
	raw, err := json.Marshal(s)
	if err != nil {
		panic(fmt.Sprintf("marshal run state: %v", err))
	}

	clone, err := RunStateFromJSON(raw)
	if err != nil {
		panic(fmt.Sprintf("unmarshal run state: %v", err))
	}

	return clone
}

// RunStateFromJSON parses the shared save format, normalising empty collections.
func RunStateFromJSON(raw []byte) (*RunState, error) {
	var state RunState
	if err := json.Unmarshal(raw, &state); err != nil {
		return nil, fmt.Errorf("parse run state: %w", err)
	}

	if state.Player == nil || state.Stats == nil {
		return nil, errors.New("parse run state: missing player or stats")
	}

	normalisePlayer(state.Player)
	normaliseStats(state.Stats)

	if state.MerchantStock == nil {
		state.MerchantStock = []domain.ItemInstance{}
	}

	if state.Monster != nil && state.Monster.Statuses == nil {
		state.Monster.Statuses = []domain.ActiveStatus{}
	}

	return &state, nil
}

func normalisePlayer(player *domain.Player) {
	if player.Potions == nil {
		player.Potions = map[string]int{}
	}

	if player.Equipment == nil {
		player.Equipment = map[domain.Slot]domain.ItemInstance{}
	}

	if player.Bag == nil {
		player.Bag = []domain.ItemInstance{}
	}

	if player.SpellUses == nil {
		player.SpellUses = map[string]int{}
	}

	if player.Statuses == nil {
		player.Statuses = []domain.ActiveStatus{}
	}

	for slot, item := range player.Equipment {
		if item.Affixes == nil {
			item.Affixes = []domain.AffixRoll{}
			player.Equipment[slot] = item
		}
	}

	for i := range player.Bag {
		if player.Bag[i].Affixes == nil {
			player.Bag[i].Affixes = []domain.AffixRoll{}
		}
	}
}

func normaliseStats(stats *RunStatistics) {
	empty := NewRunStatistics()
	if stats.SpellsCast == nil {
		stats.SpellsCast = empty.SpellsCast
	}

	if stats.PotionsUsed == nil {
		stats.PotionsUsed = empty.PotionsUsed
	}

	if stats.PotionsBought == nil {
		stats.PotionsBought = empty.PotionsBought
	}

	if stats.PotionsDropped == nil {
		stats.PotionsDropped = empty.PotionsDropped
	}

	if stats.ItemsDropped == nil {
		stats.ItemsDropped = empty.ItemsDropped
	}

	if stats.Kills == nil {
		stats.Kills = empty.Kills
	}

	if stats.StatusesApplied == nil {
		stats.StatusesApplied = empty.StatusesApplied
	}

	if stats.DroppedItems == nil {
		stats.DroppedItems = empty.DroppedItems
	}
}
