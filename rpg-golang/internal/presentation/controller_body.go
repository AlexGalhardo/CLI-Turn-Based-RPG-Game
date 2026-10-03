package presentation

import (
	"fmt"
	"slices"
	"strings"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/application"
	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// Informative screen bodies of the controller (character sheet, game over, Hall of Fame, bestiary, achievements).

func (c *Controller) profile() *application.ProfileService {
	if c.Session != nil {
		return c.Session.Profile
	}

	profile, err := c.Services.Repositories.Profile.Load()
	if err != nil {
		c.Err = err
		profile = application.NewProfile()
	}

	return application.NewProfileService(c.Services.Data, profile)
}

//nolint:gocyclo // one case per informative screen, mirroring the reference controller.
func (c *Controller) body() []string {
	switch c.View {
	case ViewCharacter:
		return c.characterSheet()
	case ViewGameOver:
		return c.gameOverSummary()
	case ViewVictory:
		return c.victorySummary()
	case ViewHallOfFame:
		return c.hallOfFame()
	case ViewBestiary:
		return c.bestiary()
	case ViewAchievements:
		return c.achievements()
	case ViewMerchant:
		player := c.Session.State().Player

		return []string{c.T("merchant.welcome", map[string]any{"name": player.Name, "gold": player.Gold})}
	case ViewSell:
		if len(c.Session.State().Player.Bag) == 0 {
			return []string{c.T("merchant.empty_bag", nil)}
		}
	case ViewStock:
		if len(c.Session.State().MerchantStock) == 0 {
			return []string{c.T("merchant.empty_stock", nil)}
		}
	case ViewPotions:
		for _, count := range c.Session.State().Player.Potions {
			if count > 0 {
				return nil
			}
		}

		return []string{c.T("battle.no_potions", nil)}
	default:
		return nil
	}

	return nil
}

func (c *Controller) characterSheet() []string {
	data := c.Services.Data
	player := c.Session.State().Player
	sheet := domain.BuildSheet(player, data)
	lines := []string{
		c.T("character.level", map[string]any{"level": player.Level, "xp": player.XP, "next": domain.XPForLevel(player.Level + 1)}),
		c.T("character.magic_level", map[string]any{
			"magicLevel": player.MagicLevel, "spent": player.ManaSpent, "next": domain.ManaForMagicLevel(player.MagicLevel, &data.Balance),
		}),
		c.T("character.hp_mp", map[string]any{"hp": player.HP, "maxHp": sheet.MaxHP, "mp": player.MP, "maxMp": sheet.MaxMP}),
		c.T("character.melee", map[string]any{"min": sheet.MeleeMin, "max": sheet.MeleeMax, "element": c.T("element."+string(sheet.WeaponElement), nil)}),
	}
	stats := []struct {
		key   string
		value int
	}{
		{"stat.armor", sheet.Armor},
		{"stat.hpRegen", sheet.HPRegen},
		{"stat.mpRegen", sheet.MPRegen},
		{"stat.critChance", sheet.CritChance},
		{"stat.critDamage", sheet.CritDamage},
		{"stat.spellPower", sheet.SpellPower},
		{"stat.physicalDamage", sheet.PhysicalDamage},
		{"stat.dodge", sheet.Dodge},
		{"stat.parry", sheet.Parry},
		{"stat.lifeLeech", sheet.LifeLeech},
		{"stat.manaLeech", sheet.ManaLeech},
	}

	for _, stat := range stats {
		if stat.value != 0 {
			lines = append(lines, c.T("character.stat_line", map[string]any{paramStat: c.T(stat.key, nil), paramValue: stat.value}))
		}
	}

	for _, element := range domain.Elements {
		if value := sheet.Protection(element); value != 0 {
			lines = append(lines, c.T("character.stat_line", map[string]any{paramStat: c.T("element."+string(element), nil), paramValue: fmt.Sprintf("%d%%", value)}))
		}
	}

	lines = append(lines, "", c.T("character.equipment", nil))

	for _, slot := range domain.Slots {
		slotName := c.T("slot."+string(slot), nil)
		if item, ok := player.Equipment[slot]; ok {
			lines = append(lines, c.T("character.slot", map[string]any{
				"slot": slotName, "item": data.Item(item.ItemID).Name, paramRarity: c.T("rarity."+item.Rarity, nil),
			}))
		} else {
			lines = append(lines, c.T("character.empty_slot", map[string]any{"slot": slotName}))
		}
	}

	return append(lines, c.T("character.bag", map[string]any{"count": len(player.Bag), "capacity": data.Balance.BagCapacity}))
}

func (c *Controller) runStatsLine() string {
	state := c.Session.State()

	return c.T("gameover.stats", map[string]any{
		"level": state.Player.Level, "damage": state.Stats.DamageDealt, "kills": state.Stats.TotalKills(),
		"elites": state.Stats.ElitesKilled, "bosses": state.Stats.BossesKilled,
	})
}

func (c *Controller) gameOverSummary() []string {
	state := c.Session.State()
	params := map[string]any{
		"name": state.Player.Name, "vocation": c.T("vocation."+state.Player.VocationID, nil), "round": state.Round,
	}

	var summary string

	switch cause := state.DeathCauseOr(""); {
	case cause != "":
		params["monster"] = c.Services.Data.Creature(cause).Name
		summary = c.T("gameover.summary", params)
	case state.Won:
		summary = c.T("gameover.won_summary", params)
	default:
		params["monster"] = "?"
		summary = c.T("gameover.summary", params)
	}

	return []string{summary, c.runStatsLine()}
}

func (c *Controller) victorySummary() []string {
	state := c.Session.State()
	data := c.Services.Data
	boss := data.BossOfTier(domain.RoundInfoFor(state.Round, &data.Balance, data.TierCount()).Tier)

	return []string{
		c.T("victory.summary", map[string]any{
			"name": state.Player.Name, "vocation": c.T("vocation."+state.Player.VocationID, nil),
			"monster": boss.Name, "round": state.Round,
		}),
		c.runStatsLine(),
		"",
		c.T("victory.choice", nil),
	}
}

func (c *Controller) hallOfFame() []string {
	hall := c.profile().Profile.HallOfFame
	if len(hall) == 0 {
		return []string{c.T("hall.empty", nil)}
	}

	lines := make([]string, len(hall))
	for index, entry := range hall {
		key := "hall.entry"
		if entry.Won {
			key = "hall.entry_won"
		}

		lines[index] = c.T(key, map[string]any{
			"position": index + 1, "name": entry.Name, "vocation": c.T("vocation."+entry.Vocation, nil),
			"difficulty": c.T("difficulty."+entry.Difficulty, nil), "round": entry.Round, "level": entry.Level,
			"date": entry.EndedAt[:min(10, len(entry.EndedAt))],
		})
	}

	return lines
}

func (c *Controller) bestiary() []string {
	data := c.Services.Data
	profile := c.profile()
	creatures := make([]*domain.MonsterDef, 0, len(data.Monsters)+len(data.Bosses))

	for i := range data.Monsters {
		creatures = append(creatures, &data.Monsters[i])
	}

	for i := range data.Bosses {
		creatures = append(creatures, &data.Bosses[i])
	}

	slices.SortStableFunc(creatures, func(a, b *domain.MonsterDef) int {
		if a.Tier != b.Tier {
			return a.Tier - b.Tier
		}

		if a.IsBoss != b.IsBoss {
			if a.IsBoss {
				return 1
			}

			return -1
		}

		return strings.Compare(a.Name, b.Name)
	})

	lines := make([]string, 0, len(creatures))

	for _, creature := range creatures {
		entry, known := profile.Profile.Bestiary[creature.ID]

		switch {
		case !known:
			lines = append(lines, c.T("bestiary.unknown", map[string]any{"tier": creature.Tier + 1}))
		case profile.Revealed(creature.ID):
			lines = append(lines, c.T("bestiary.entry_revealed", map[string]any{
				"name": creature.Name, "tier": creature.Tier + 1, "kills": entry.Kills,
				"weak": joinOrDash(c.weakElements(creature, true)), "strong": joinOrDash(c.weakElements(creature, false)),
			}))
		default:
			lines = append(lines, c.T("bestiary.entry", map[string]any{"name": creature.Name, "tier": creature.Tier + 1, "kills": entry.Kills}))
		}
	}

	return lines
}

func joinOrDash(values []string) string {
	if len(values) == 0 {
		return "—"
	}

	return strings.Join(values, ", ")
}

func (c *Controller) achievements() []string {
	unlocked := c.profile().Profile.Achievements
	lines := []string{}

	for _, achievement := range c.Services.Data.Achievements {
		name := c.T("achievement."+achievement.ID+".name", nil)
		description := c.T("achievement."+achievement.ID+".description", map[string]any{paramValue: achievement.Value})

		if unlock, ok := unlocked[achievement.ID]; ok {
			lines = append(lines, c.T("achievements.unlocked", map[string]any{
				"name": name, "description": description, "date": unlock.UnlockedAt[:min(10, len(unlock.UnlockedAt))],
			}))
		} else {
			lines = append(lines, c.T("achievements.locked", map[string]any{"name": name, "description": description}))
		}
	}

	return lines
}
