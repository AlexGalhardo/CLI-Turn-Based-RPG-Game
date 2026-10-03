package application

import (
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// The auto-battle policy (docs/game-design.md §13) picks the player's battle commands from the run state only. It
// lives in the application layer, not in the engine: the commands it returns are ordinary commands, so a fight played
// by the policy replays like any other. Every command it returns is valid (affordable spells, owned potions).

const offenseAttack = "attack"

// AutoBattleMode is an auto-battle mode id (a key of balance.autoBattle.modes).
type AutoBattleMode string

// Auto-battle modes in menu order.
const (
	AutoBattleMelee    AutoBattleMode = "melee"
	AutoBattleSpells   AutoBattleMode = "spells"
	AutoBattleBalanced AutoBattleMode = "balanced"
)

// AutoBattleModes lists the modes in menu order.
var AutoBattleModes = []AutoBattleMode{AutoBattleMelee, AutoBattleSpells, AutoBattleBalanced}

// strongest returns the option with the highest max; ties go to the lowest id.
func strongest[T any](options []T, key func(T) (int, string)) (T, bool) {
	var (
		best      T
		bestValue int
		bestID    string
		found     bool
	)

	for _, option := range options {
		value, id := key(option)
		if !found || value > bestValue || (value == bestValue && id < bestID) {
			best, bestValue, bestID, found = option, value, id, true
		}
	}

	return best, found
}

func spellStrength(spell *domain.SpellDef) (int, string)    { return spell.Max, spell.ID }
func potionStrength(potion *domain.PotionDef) (int, string) { return potion.Max, potion.ID }

// AutoBattlePolicy chooses battle commands for one auto-battle mode.
type AutoBattlePolicy struct {
	data   *domain.GameData
	mode   domain.AutoBattleModeDef
	config domain.AutoBattleDef
}

// NewAutoBattlePolicy creates the policy of a mode defined in balance.autoBattle.modes.
func NewAutoBattlePolicy(data *domain.GameData, mode AutoBattleMode) (*AutoBattlePolicy, error) {
	definition, err := data.Balance.AutoBattle.Mode(string(mode))
	if err != nil {
		return nil, err
	}

	return &AutoBattlePolicy{data: data, mode: definition, config: data.Balance.AutoBattle}, nil
}

// Choose returns the next battle command.
func (p *AutoBattlePolicy) Choose(state *RunState) Command {
	player := state.Player
	sheet := domain.BuildSheet(player, p.data)
	config := p.config

	if player.HP*100 < sheet.MaxHP*config.EmergencyHealBelowPct {
		if heal, ok := p.heal(state); ok {
			return heal
		}
	}

	every := p.mode.SupportEvery
	if state.Turn%every == every-1 {
		if command, ok := p.support(state, sheet); ok {
			return command
		}
	}

	return p.offense(state)
}

// support is the support turn: heal below healBelowPct, else a mana potion below manaBelowPct, else defend against a
// telegraphed boss charge.
func (p *AutoBattlePolicy) support(state *RunState, sheet domain.CharacterSheet) (Command, bool) {
	player := state.Player

	if player.HP*100 < sheet.MaxHP*p.config.HealBelowPct {
		if heal, ok := p.heal(state); ok {
			return heal, true
		}
	}

	if player.MP*100 < sheet.MaxMP*p.config.ManaBelowPct {
		if potion, ok := p.bestPotion(state, "mp"); ok {
			return UsePotion(potion.ID), true
		}
	}

	if p.telegraphPending(state) {
		return Defend(), true
	}

	return Command{}, false
}

func (p *AutoBattlePolicy) offense(state *RunState) Command {
	if p.mode.Offense == offenseAttack {
		return Attack()
	}

	if spell, ok := strongest(p.affordable(state, "attack"), spellStrength); ok {
		return Cast(spell.ID)
	}

	return Attack()
}

func (p *AutoBattlePolicy) heal(state *RunState) (Command, bool) {
	if spell, ok := strongest(p.affordable(state, "heal"), spellStrength); ok {
		return Cast(spell.ID), true
	}

	if potion, ok := p.bestPotion(state, "hp"); ok {
		return UsePotion(potion.ID), true
	}

	return Command{}, false
}

func (p *AutoBattlePolicy) affordable(state *RunState, kind string) []*domain.SpellDef {
	player := state.Player
	levels := p.data.Balance.SpellLevels
	result := []*domain.SpellDef{}

	for _, id := range p.data.Vocation(player.VocationID).Spells {
		spell := p.data.Spell(id)
		cost := domain.Pct(spell.Mana, domain.SpellLevelForUses(player.SpellUses[spell.ID], levels).ManaPct)

		if spell.Kind == kind && cost <= player.MP {
			result = append(result, spell)
		}
	}

	return result
}

func (p *AutoBattlePolicy) bestPotion(state *RunState, resource string) (*domain.PotionDef, bool) {
	owned := []*domain.PotionDef{}

	for i := range p.data.Potions {
		potion := &p.data.Potions[i]
		if potion.Resource == resource && state.Player.PotionCount(potion.ID) > 0 {
			owned = append(owned, potion)
		}
	}

	return strongest(owned, potionStrength)
}

// telegraphPending reports that the boss announced its charged attack: its next action is the charge.
func (p *AutoBattlePolicy) telegraphPending(state *RunState) bool {
	monster := state.Monster
	if monster == nil || !monster.IsBoss {
		return false
	}

	every := p.data.Balance.BossTelegraphEvery

	return monster.BossActions%(every+1) == every
}
