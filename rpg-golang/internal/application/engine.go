package application

import (
	"errors"
	"fmt"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// ErrInvalidRunConfig is returned when a run is created with an unknown vocation or difficulty.
var ErrInvalidRunConfig = errors.New("invalid run config")

// GameEngine is a pure state machine: Step(command) -> events (docs/architecture.md).
type GameEngine struct {
	Data  *domain.GameData
	State *RunState
	rng   *domain.Rng
}

// NewRun starts a run in the merchant phase (round 0) with the vocation's starter weapon.
func NewRun(data *domain.GameData, config RunConfig, seed uint64) (*GameEngine, []Event, error) {
	if !data.HasVocation(config.VocationID) {
		return nil, nil, fmt.Errorf("%w: unknown vocation %q", ErrInvalidRunConfig, config.VocationID)
	}

	if _, err := data.Balance.Difficulty(config.DifficultyID); err != nil {
		return nil, nil, fmt.Errorf("%w: %w", ErrInvalidRunConfig, err)
	}

	vocation := data.Vocation(config.VocationID)
	player := domain.NewPlayer(config.Name, vocation.ID, vocation.StartHP, vocation.StartMP, data.Balance.StartingGold)

	for _, stack := range data.Balance.StartingPotions {
		player.Potions[stack.PotionID] = stack.Quantity
	}

	state := NewRunState(seed, config, player)
	starter := data.Item(vocation.StarterWeapon)
	player.Equipment[starter.Slot] = domain.ItemInstance{
		UID: state.TakeItemUID(), ItemID: starter.ID, Rarity: "common", Tier: starter.Tier, Affixes: []domain.AffixRoll{},
	}
	engine := &GameEngine{Data: data, State: state, rng: domain.NewRng(seed)}
	events := []Event{NewEvent("run_started", map[string]any{
		"seed":       int(seed), //nolint:gosec // JSON seeds are non-negative ints below 2^63 in every implementation.
		"vocation":   vocation.ID,
		"difficulty": config.DifficultyID,
	})}
	events = append(events, NewMerchant(data, engine.rng, state).Enter()...)

	return engine, events, nil
}

// Restore continues a run from a saved state and PRNG state.
func Restore(data *domain.GameData, state *RunState, rngState uint32) *GameEngine {
	return &GameEngine{Data: data, State: state, rng: domain.NewRng(uint64(rngState))}
}

// RngState returns the PRNG state (stored in saves).
func (e *GameEngine) RngState() uint32 {
	return e.rng.State()
}

// Step applies a command and returns its events.
func (e *GameEngine) Step(command Command) []Event {
	events := e.dispatch(command)
	e.State.Stats.Record(events, e.State.Round)

	return events
}

func (e *GameEngine) dispatch(command Command) []Event {
	phase := e.State.Phase

	switch {
	case command.IsBattle():
		if phase != domain.PhaseBattle {
			return []Event{ErrorEvent(ErrInvalidPhase)}
		}

		return e.battleTurn(command)
	case command.Type == CmdNextFight:
		if phase != domain.PhaseMerchant {
			return []Event{ErrorEvent(ErrInvalidPhase)}
		}

		return e.nextFight()
	default:
		if phase != domain.PhaseMerchant {
			return []Event{ErrorEvent(ErrInvalidPhase)}
		}

		return NewMerchant(e.Data, e.rng, e.State).Handle(command)
	}
}

func (e *GameEngine) nextFight() []Event {
	state := e.State
	state.Round++
	difficulty := e.Data.Balance.MustDifficulty(state.Config.DifficultyID)
	monster, info := SpawnMonster(e.Data, e.rng, state.Round, difficulty)
	state.Monster = monster
	state.Phase = domain.PhaseBattle
	state.Turn = 1
	state.MerchantStock = []domain.ItemInstance{}

	return []Event{NewEvent("round_started", map[string]any{
		"round": state.Round, "tier": info.Tier, "cycle": info.Cycle, "monsterId": monster.CreatureID,
		"isBoss": monster.IsBoss, "hp": monster.HP,
	})}
}

func (e *GameEngine) battleTurn(command Command) []Event {
	battle := NewBattle(e.Data, e.rng, e.State)
	if invalid := battle.Validate(command); invalid != nil {
		return []Event{invalid}
	}

	events, outcome := battle.PlayTurn(command)

	switch outcome {
	case OutcomeVictory:
		events = append(events, e.victory()...)
	case OutcomeDefeat:
		events = append(events, e.defeat()...)
	case OutcomeOngoing:
	}

	return events
}

func (e *GameEngine) victory() []Event {
	state := e.State
	player := state.Player
	monster := state.Monster
	events := []Event{NewEvent("monster_killed", map[string]any{"monsterId": monster.CreatureID, "isBoss": monster.IsBoss})}
	events = append(events, NewProgression(e.Data).GainExperience(player, monster.XP)...)

	gold := e.rng.Roll(monster.GoldMin, monster.GoldMax)
	player.Gold += gold
	events = append(events, NewEvent("gold_looted", map[string]any{"amount": gold}))
	events = append(events, e.drops(monster.IsBoss)...)

	player.Statuses = []domain.ActiveStatus{}
	player.StunCooldown = 0
	player.Defending = false
	sheet := domain.BuildSheet(player, e.Data)
	player.HP = min(player.HP, sheet.MaxHP)
	player.MP = min(player.MP, sheet.MaxMP)
	state.Monster = nil
	state.Phase = domain.PhaseMerchant
	state.Turn = 0

	return append(events, NewMerchant(e.Data, e.rng, state).Enter()...)
}

func (e *GameEngine) drops(isBoss bool) []Event {
	state := e.State
	balance := &e.Data.Balance

	var (
		count int
		table string
	)

	switch {
	case isBoss:
		count, table = balance.BossDrops, "boss"
	case e.rng.Chance(balance.DropChancePct):
		count, table = 1, "monster"
	default:
		return nil
	}

	request := ItemRequest{
		Vocation:   e.Data.Vocation(state.Player.VocationID),
		Tier:       domain.RoundInfoFor(state.Round, balance, e.Data.TierCount()).Tier,
		Table:      table,
		Difficulty: balance.MustDifficulty(state.Config.DifficultyID),
	}
	events := []Event{}

	for range count {
		request.UID = state.NextItemUID

		item, ok := GenerateItem(e.Data, e.rng, request)
		if !ok {
			continue
		}

		state.TakeItemUID()

		events = append(events, NewEvent("item_dropped", map[string]any{"uid": item.UID, "itemId": item.ItemID, "rarity": item.Rarity}))

		if len(state.Player.Bag) >= balance.BagCapacity {
			value := domain.ItemValue(item, e.Data)
			state.Player.Gold += value
			events = append(events, NewEvent("item_auto_sold", map[string]any{"uid": item.UID, "itemId": item.ItemID, "gold": value}))
		} else {
			state.Player.Bag = append(state.Player.Bag, item)
		}
	}

	return events
}

func (e *GameEngine) defeat() []Event {
	state := e.State
	monsterID := ""

	if state.Monster != nil {
		monsterID = state.Monster.CreatureID
	}

	state.Phase = domain.PhaseGameOver
	state.DeathCause = &monsterID

	return []Event{NewEvent("player_died", map[string]any{"monsterId": monsterID, "round": state.Round})}
}
