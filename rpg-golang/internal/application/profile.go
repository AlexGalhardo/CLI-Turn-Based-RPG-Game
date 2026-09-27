package application

import (
	"slices"
	"strings"

	"github.com/AlexGalhardo/CLI-Turn-Based-RPG-Game/rpg-golang/internal/domain"
)

// Profile constants (docs/game-design.md §11).
const (
	HallOfFameSize      = 10
	BestiaryRevealKills = 5
)

// BestiaryEntry counts kills of one creature across runs.
type BestiaryEntry struct {
	Kills         int    `json:"kills"`
	FirstKilledAt string `json:"firstKilledAt"`
}

// Unlock records when an achievement was unlocked.
type Unlock struct {
	UnlockedAt string `json:"unlockedAt"`
	RunID      string `json:"runId"`
}

// HallOfFameEntry is one ranked finished run.
type HallOfFameEntry struct {
	RunID      string `json:"runId"`
	Name       string `json:"name"`
	Vocation   string `json:"vocation"`
	Difficulty string `json:"difficulty"`
	Round      int    `json:"round"`
	Level      int    `json:"level"`
	EndedAt    string `json:"endedAt"`
}

// Profile is the cross-run profile (profile.json). Maps marshal with sorted keys, like the reference.
type Profile struct {
	SchemaVersion int                       `json:"schemaVersion"`
	Bestiary      map[string]*BestiaryEntry `json:"bestiary"`
	Achievements  map[string]Unlock         `json:"achievements"`
	HallOfFame    []HallOfFameEntry         `json:"hallOfFame"`
}

// NewProfile returns an empty profile.
func NewProfile() *Profile {
	return &Profile{
		SchemaVersion: SchemaVersion,
		Bestiary:      map[string]*BestiaryEntry{},
		Achievements:  map[string]Unlock{},
		HallOfFame:    []HallOfFameEntry{},
	}
}

// ProfileService feeds the profile from engine events and returns newly unlocked achievements.
type ProfileService struct {
	data    *domain.GameData
	Profile *Profile
}

// NewProfileService wraps a profile.
func NewProfileService(data *domain.GameData, profile *Profile) *ProfileService {
	return &ProfileService{data: data, Profile: profile}
}

// Observe updates the bestiary and returns the achievements unlocked by this step.
func (s *ProfileService) Observe(events []Event, state *RunState, now, runID string) []domain.AchievementDef {
	for _, evt := range events {
		if evt.Type() != "monster_killed" {
			continue
		}

		monsterID := evt.Str("monsterId")
		if entry, ok := s.Profile.Bestiary[monsterID]; ok {
			entry.Kills++
		} else {
			s.Profile.Bestiary[monsterID] = &BestiaryEntry{Kills: 1, FirstKilledAt: now}
		}
	}

	unlocked := []domain.AchievementDef{}

	for _, achievement := range s.data.Achievements {
		if _, done := s.Profile.Achievements[achievement.ID]; done {
			continue
		}

		if s.progress(achievement, state) >= achievement.Value {
			s.Profile.Achievements[achievement.ID] = Unlock{UnlockedAt: now, RunID: runID}
			unlocked = append(unlocked, achievement)
		}
	}

	return unlocked
}

//nolint:gocyclo // one case per achievement type, mirroring the reference implementation.
func (s *ProfileService) progress(achievement domain.AchievementDef, state *RunState) int {
	player := state.Player

	switch achievement.Type {
	case "kills_total":
		total := 0
		for _, entry := range s.Profile.Bestiary {
			total += entry.Kills
		}

		return total
	case "bosses_total":
		total := 0

		for _, boss := range s.data.Bosses {
			if entry, ok := s.Profile.Bestiary[boss.ID]; ok {
				total += entry.Kills
			}
		}

		return total
	case "round_reached":
		return state.Round
	case "level_reached":
		return player.Level
	case "legendary_found":
		return state.Stats.ItemsDropped["legendary"]
	case "spell_level_3":
		threshold := s.data.Balance.SpellLevels[len(s.data.Balance.SpellLevels)-1].Uses
		count := 0

		for _, uses := range player.SpellUses {
			if uses >= threshold {
				count++
			}
		}

		return count
	case "gold_held":
		return player.Gold
	case "distinct_monsters":
		return len(s.Profile.Bestiary)
	case "hard_round_reached":
		if state.Config.DifficultyID == "hard" {
			return state.Round
		}

		return 0
	default:
		return 0
	}
}

// RecordFinishedRun inserts a run into the Hall of Fame (round desc, level desc, earliest end first).
func (s *ProfileService) RecordFinishedRun(entry HallOfFameEntry) {
	ranking := append(slices.Clone(s.Profile.HallOfFame), entry)
	slices.SortStableFunc(ranking, func(a, b HallOfFameEntry) int {
		if a.Round != b.Round {
			return b.Round - a.Round
		}

		if a.Level != b.Level {
			return b.Level - a.Level
		}

		return strings.Compare(a.EndedAt, b.EndedAt)
	})
	s.Profile.HallOfFame = ranking[:min(HallOfFameSize, len(ranking))]
}

// Revealed reports whether a creature's weaknesses are shown in the bestiary.
func (s *ProfileService) Revealed(monsterID string) bool {
	entry, ok := s.Profile.Bestiary[monsterID]

	return ok && entry.Kills >= BestiaryRevealKills
}
