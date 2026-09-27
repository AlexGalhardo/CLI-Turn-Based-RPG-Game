package application

import "github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"

// Progression handles experience, levels, magic levels and spell levels (docs/game-design.md §4 and §9).
type Progression struct {
	data *domain.GameData
}

// NewProgression creates the progression service.
func NewProgression(data *domain.GameData) Progression {
	return Progression{data: data}
}

// GainExperience adds XP and applies every level up it reaches.
func (p Progression) GainExperience(player *domain.Player, amount int) []Event {
	player.XP += amount
	events := []Event{NewEvent("xp_gained", map[string]any{"amount": amount, "total": player.XP})}
	vocation := p.data.Vocation(player.VocationID)

	for player.XP >= domain.XPForLevel(player.Level+1) {
		player.Level++
		sheet := domain.BuildSheet(player, p.data)
		player.HP = min(sheet.MaxHP, player.HP+vocation.HPPerLevel)
		player.MP = min(sheet.MaxMP, player.MP+vocation.MPPerLevel)
		events = append(events, NewEvent("level_up", map[string]any{"level": player.Level, "maxHp": sheet.MaxHP, "maxMp": sheet.MaxMP}))
	}

	return events
}

// AfterCast counts a spell use and grows spell level and magic level.
func (p Progression) AfterCast(player *domain.Player, spell *domain.SpellDef, manaCost int) []Event {
	levels := p.data.Balance.SpellLevels
	events := []Event{}
	usesBefore := player.SpellUses[spell.ID]
	player.SpellUses[spell.ID] = usesBefore + 1
	before := domain.SpellLevelForUses(usesBefore, levels)

	after := domain.SpellLevelForUses(usesBefore+1, levels)
	if after.Level != before.Level {
		events = append(events, NewEvent("spell_level_up", map[string]any{"spellId": spell.ID, "level": after.Level}))
	}

	player.ManaSpent += manaCost
	for player.ManaSpent >= domain.ManaForMagicLevel(player.MagicLevel, &p.data.Balance) {
		player.MagicLevel++
		events = append(events, NewEvent("magic_level_up", map[string]any{"magicLevel": player.MagicLevel}))
	}

	return events
}
